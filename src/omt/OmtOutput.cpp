#include "rood/OmtOutput.hpp"

#include "rood/AudioTimeline.hpp"
#include "rood/MediaReceiver.hpp"

#include <libomt.h>

extern "C" {
#include <libavutil/channel_layout.h>
#include <libavutil/error.h>
#include <libavutil/frame.h>
#include <libswresample/swresample.h>
#include <libswscale/swscale.h>
}

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <cstdint>
#include <cstdlib>
#include <deque>
#include <limits>
#include <map>
#include <mutex>
#include <set>
#include <stdexcept>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace rood {
namespace {

using Clock = std::chrono::steady_clock;

std::string ffmpegError(int code) {
    char message[AV_ERROR_MAX_STRING_SIZE] = {};
    av_strerror(code, message, sizeof(message));
    return message;
}

class AudioResampler {
public:
    ~AudioResampler() {
        swr_free(&context_);
        av_channel_layout_uninit(&layout_);
    }
    AudioResampler() = default;
    AudioResampler(const AudioResampler&) = delete;
    AudioResampler& operator=(const AudioResampler&) = delete;

    int convert(const AVFrame& frame, int outputRate, std::vector<float>& samples) {
        if (frame.ch_layout.nb_channels <= 0 || frame.sample_rate <= 0)
            throw std::runtime_error("OMT audio frame has no channel layout or sample rate");
        const auto inputFormat = static_cast<AVSampleFormat>(frame.format);
        if (!context_ || inputRate_ != frame.sample_rate || inputFormat_ != inputFormat ||
            av_channel_layout_compare(&layout_, &frame.ch_layout) != 0) {
            swr_free(&context_);
            av_channel_layout_uninit(&layout_);
            int result = av_channel_layout_copy(&layout_, &frame.ch_layout);
            if (result < 0) throw std::runtime_error("OMT channel layout: " + ffmpegError(result));
            result = swr_alloc_set_opts2(&context_, &layout_, AV_SAMPLE_FMT_FLT,
                                         outputRate, &layout_, inputFormat,
                                         frame.sample_rate, 0, nullptr);
            if (result < 0) throw std::runtime_error("OMT resampler allocation: " + ffmpegError(result));
            result = swr_init(context_);
            if (result < 0) throw std::runtime_error("OMT resampler initialization: " + ffmpegError(result));
            inputRate_ = frame.sample_rate;
            inputFormat_ = inputFormat;
            hasNextFrame_ = false;
        }
        const int capacity = swr_get_out_samples(context_, frame.nb_samples);
        if (capacity < 0 || capacity > 65536)
            throw std::runtime_error("OMT resampler output size is invalid");
        samples.resize(static_cast<std::size_t>(capacity) * layout_.nb_channels);
        std::uint8_t* destination[] = {reinterpret_cast<std::uint8_t*>(samples.data())};
        const int converted = swr_convert(context_, destination, capacity,
                                          reinterpret_cast<const std::uint8_t* const*>(frame.extended_data),
                                          frame.nb_samples);
        if (converted < 0) throw std::runtime_error("OMT audio conversion: " + ffmpegError(converted));
        samples.resize(static_cast<std::size_t>(converted) * layout_.nb_channels);
        return converted;
    }

    bool hasNextFrame() const { return hasNextFrame_; }
    std::int64_t nextFrame() const { return nextFrame_; }
    void setNextFrame(std::int64_t frame) { nextFrame_ = frame; hasNextFrame_ = true; }

private:
    SwrContext* context_ = nullptr;
    AVChannelLayout layout_ = {};
    AVSampleFormat inputFormat_ = AV_SAMPLE_FMT_NONE;
    int inputRate_ = 0;
    bool hasNextFrame_ = false;
    std::int64_t nextFrame_ = 0;
};

struct VideoFrame {
    std::vector<std::uint8_t> bgra;
    int width = 0;
    int height = 0;
    double ptsSeconds = 0.0;
    std::uint64_t generation = 0;
};

constexpr std::size_t maxVideoQueueBytes = 256ULL * 1024 * 1024;
constexpr std::size_t maxVideoQueueFrames = 64;

std::size_t checkedTimelineCapacity(const OmtOutputConfig& config) {
    if (config.audioSampleRate < 8000 || config.audioSampleRate > 192000 ||
        config.audioChannels == 0 || config.audioChannels > 32)
        throw std::invalid_argument("invalid OMT audio format");
    return static_cast<std::size_t>(config.audioSampleRate) * 5;
}

Clock::time_point scheduled(Clock::time_point anchorTime, double seconds) {
    return anchorTime + std::chrono::duration_cast<Clock::duration>(
        std::chrono::duration<double>(seconds));
}

std::int64_t omtTimestamp(double seconds) {
    const double units = seconds * 10000000.0;
    if (!std::isfinite(units) || units < 0 ||
        units > static_cast<double>(std::numeric_limits<std::int64_t>::max()))
        throw std::runtime_error("OMT media timestamp is out of range");
    return static_cast<std::int64_t>(std::llround(units));
}

} // namespace

class OmtOutput::Impl {
public:
    explicit Impl(OmtOutputConfig config, AudioMediaClock audioMediaClock,
                  int audioOutputDelayMs)
        : config_(std::move(config)),
          timeline_(config_.audioChannels,
                    checkedTimelineCapacity(config_),
                    config_.audioRoutes),
          audioMediaClock_(std::move(audioMediaClock)),
          audioClockOffsetSeconds_((config_.outputDelayMs - audioOutputDelayMs) / 1000.0),
          frameRateN_(config_.videoFrameRateNumerator),
          frameRateD_(config_.videoFrameRateDenominator) {
        if (config_.name.empty() || config_.name.size() > 240 ||
            config_.outputDelayMs < 0 || config_.outputDelayMs > 5000 ||
            config_.audioSampleRate < 8000 || config_.audioSampleRate > 192000 ||
            config_.audioChannels == 0 || config_.audioChannels > 32 ||
            (audioMediaClock_ && (audioOutputDelayMs < 0 || audioOutputDelayMs > 3000)) ||
            config_.videoFrameRateNumerator <= 0 ||
            config_.videoFrameRateDenominator <= 0)
            throw std::invalid_argument("invalid OMT output configuration");
        for (const auto& route : config_.audioRoutes) mappedTracks_.insert(route.track_id);
        sender_ = omt_send_create(config_.name.c_str(), OMTQuality_Default);
        if (!sender_) throw std::runtime_error("omt_send_create failed");
        try {
            videoThread_ = std::thread(&Impl::runVideo, this);
            audioThread_ = std::thread(&Impl::runAudio, this);
        } catch (...) {
            stopping_.store(true);
            cv_.notify_all();
            if (videoThread_.joinable()) videoThread_.join();
            if (audioThread_.joinable()) audioThread_.join();
            omt_send_destroy(sender_);
            throw;
        }
    }

    ~Impl() {
        stopping_.store(true);
        cv_.notify_all();
        if (videoThread_.joinable()) videoThread_.join();
        if (audioThread_.joinable()) audioThread_.join();
        omt_send_destroy(sender_);
        if (scale_) sws_freeContext(scale_);
    }

    void pushFrame(const FrameInfo& info, const AVFrame& frame) {
        if (!info.hasPts || info.timeBaseDen <= 0) return;
        const double pts = static_cast<double>(info.pts) * info.timeBaseNum / info.timeBaseDen;
        omtTimestamp(pts);
        if (info.kind == "video") pushVideo(info, frame, pts);
        else if (info.kind == "audio") pushAudio(info, frame, pts);
    }

    void setVideoFrameRate(int numerator, int denominator) {
        if (numerator <= 0 || denominator <= 0 || numerator > 1000000 || denominator > 1000000)
            return;
        frameRateN_.store(numerator);
        frameRateD_.store(denominator);
    }

    void reset() {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            droppedVideoFrames_.fetch_add(videoQueue_.size());
            videoQueue_.clear();
            anchorSet_ = false;
            audioStarted_ = false;
            hostClockFallback_.store(false);
            lastAudioClockValid_ = false;
            ++generation_;
            timeline_.reset();
        }
        resamplers_.clear();
        cv_.notify_all();
    }

    OmtOutputStats stats() const {
        OmtOutputStats result;
        result.videoFrames = videoFrames_.load();
        result.audioPackets = audioPackets_.load();
        result.droppedVideoFrames = droppedVideoFrames_.load();
        result.rejectedAudioFrames = timeline_.rejectedFrames();
        {
            std::lock_guard<std::mutex> lock(mutex_);
            result.audioClockVideoFrames = audioClockVideoFrames_;
            result.audioClockAudioPackets = audioClockAudioPackets_;
            result.maxAudioClockVideoErrorMs = maxAudioClockVideoErrorMs_;
            result.maxAudioClockAudioErrorMs = maxAudioClockAudioErrorMs_;
        }
        std::lock_guard<std::mutex> lock(sendMutex_);
        result.connections = omt_send_connections(sender_);
        return result;
    }

    std::string address() const {
        char buffer[1024] = {};
        std::lock_guard<std::mutex> lock(sendMutex_);
        return omt_send_getaddress(sender_, buffer, sizeof(buffer)) > 0
            ? std::string(buffer) : std::string();
    }

private:
    enum class WaitResult { Ready, Late, Reset, Stop };

    void establishAnchor(double pts) {
        if (anchorSet_) return;
        anchorSet_ = true;
        anchorPts_ = pts;
        anchorTime_ = Clock::now() + std::chrono::milliseconds(config_.outputDelayMs);
    }

    WaitResult waitForTarget(std::uint64_t generation, double pts,
                             Clock::time_point hostTarget, double lateSeconds) {
        const auto startedWaiting = Clock::now();
        const auto clockGrace = std::chrono::milliseconds(
            std::min(config_.outputDelayMs, 250));
        while (true) {
            const auto now = Clock::now();
            std::optional<double> clockPosition;
            {
                std::lock_guard<std::mutex> lock(mutex_);
                if (stopping_.load()) return WaitResult::Stop;
                if (generation != generation_) return WaitResult::Reset;
                if (lastAudioClockValid_) {
                    clockPosition = lastAudioPosition_ +
                        std::chrono::duration<double>(now - lastAudioClockTime_).count();
                }
            }
            std::optional<double> devicePosition;
            if (audioMediaClock_) devicePosition = audioMediaClock_();
            if (devicePosition && !std::isfinite(*devicePosition)) devicePosition.reset();
            if (devicePosition) {
                std::lock_guard<std::mutex> lock(mutex_);
                if (generation != generation_) return WaitResult::Reset;
                lastAudioPosition_ = *devicePosition;
                lastAudioClockTime_ = Clock::now();
                lastAudioClockValid_ = true;
                clockPosition = devicePosition;
            }
            if (audioMediaClock_ && !clockPosition && !hostClockFallback_.load() &&
                now - startedWaiting < clockGrace) {
                std::unique_lock<std::mutex> lock(mutex_);
                cv_.wait_for(lock, std::chrono::milliseconds(5));
                continue;
            }
            if (devicePosition) hostClockFallback_.store(false);
            else if (audioMediaClock_) hostClockFallback_.store(true);
            const double waitSeconds = clockPosition
                ? pts + audioClockOffsetSeconds_ - *clockPosition
                : std::chrono::duration<double>(hostTarget - Clock::now()).count();
            if (waitSeconds < -lateSeconds) return WaitResult::Late;
            if (waitSeconds <= 0.002) return WaitResult::Ready;
            std::unique_lock<std::mutex> lock(mutex_);
            cv_.wait_for(lock, std::chrono::duration<double>(
                std::min(waitSeconds, 0.010)));
        }
    }

    void recordClockError(bool video, double pts,
                          std::optional<double> devicePosition) {
        if (!devicePosition || !std::isfinite(*devicePosition)) return;
        const double errorMs = (*devicePosition - pts - audioClockOffsetSeconds_) * 1000.0;
        std::lock_guard<std::mutex> lock(mutex_);
        if (video) {
            ++audioClockVideoFrames_;
            maxAudioClockVideoErrorMs_ = std::max(maxAudioClockVideoErrorMs_, std::fabs(errorMs));
        } else {
            ++audioClockAudioPackets_;
            maxAudioClockAudioErrorMs_ = std::max(maxAudioClockAudioErrorMs_, std::fabs(errorMs));
        }
    }

    void pushVideo(const FrameInfo&, const AVFrame& frame, double pts) {
        if (frame.width <= 0 || frame.height <= 0 ||
            frame.width > 8192 || frame.height > 8192 ||
            static_cast<std::int64_t>(frame.width) * frame.height > 16777216)
            throw std::invalid_argument("unsupported OMT video dimensions");
        scale_ = sws_getCachedContext(scale_, frame.width, frame.height,
                                      static_cast<AVPixelFormat>(frame.format),
                                      frame.width, frame.height, AV_PIX_FMT_BGRA,
                                      SWS_BILINEAR, nullptr, nullptr, nullptr);
        if (!scale_) throw std::runtime_error("OMT video swscale initialization failed");
        VideoFrame converted;
        converted.width = frame.width;
        converted.height = frame.height;
        converted.ptsSeconds = pts;
        converted.bgra.resize(static_cast<std::size_t>(frame.width) * frame.height * 4);
        std::uint8_t* destination[] = {converted.bgra.data(), nullptr, nullptr, nullptr};
        int stride[] = {frame.width * 4, 0, 0, 0};
        if (sws_scale(scale_, frame.data, frame.linesize, 0, frame.height,
                      destination, stride) != frame.height)
            throw std::runtime_error("OMT video conversion failed");
        {
            std::lock_guard<std::mutex> lock(mutex_);
            establishAnchor(pts);
            converted.generation = generation_;
            const std::size_t maxFrames = std::clamp(maxVideoQueueBytes / converted.bgra.size(),
                                                     std::size_t{2}, maxVideoQueueFrames);
            if (videoQueue_.size() >= maxFrames) {
                videoQueue_.pop_front();
                droppedVideoFrames_.fetch_add(1);
            }
            videoQueue_.push_back(std::move(converted));
        }
        cv_.notify_all();
    }

    void pushAudio(const FrameInfo& info, const AVFrame& frame, double pts) {
        if (mappedTracks_.count(static_cast<std::uint32_t>(info.streamId)) == 0) return;
        auto& resampler = resamplers_[info.streamId];
        const int count = resampler.convert(frame, config_.audioSampleRate, audioScratch_);
        if (count <= 0) return;
        double anchorPts;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            establishAnchor(pts);
            anchorPts = anchorPts_;
        }
        std::int64_t position = static_cast<std::int64_t>(
            std::llround((pts - anchorPts) * config_.audioSampleRate));
        if (resampler.hasNextFrame() &&
            std::llabs(position - resampler.nextFrame()) <= 3)
            position = resampler.nextFrame();
        std::size_t skip = 0;
        if (position < 0) {
            skip = static_cast<std::size_t>(std::min<std::int64_t>(-position, count));
            position += static_cast<std::int64_t>(skip);
        }
        const std::size_t remaining = static_cast<std::size_t>(count) - skip;
        if (remaining > 0) {
            timeline_.push(static_cast<std::uint32_t>(info.streamId), position,
                           frame.ch_layout.nb_channels,
                           audioScratch_.data() + skip * frame.ch_layout.nb_channels,
                           remaining);
            {
                std::lock_guard<std::mutex> lock(mutex_);
                audioStarted_ = true;
            }
            cv_.notify_all();
        }
        resampler.setNextFrame(position + static_cast<std::int64_t>(remaining));
    }

    void runVideo() {
        while (!stopping_.load()) {
            VideoFrame frame;
            Clock::time_point anchorTime;
            double anchorPts = 0.0;
            {
                std::unique_lock<std::mutex> lock(mutex_);
                cv_.wait(lock, [this] { return stopping_.load() || !videoQueue_.empty(); });
                if (stopping_.load()) return;
                frame = std::move(videoQueue_.front());
                videoQueue_.pop_front();
                anchorTime = anchorTime_;
                anchorPts = anchorPts_;
            }
            const auto target = scheduled(anchorTime, frame.ptsSeconds - anchorPts);
            const auto wait = waitForTarget(frame.generation, frame.ptsSeconds,
                                            target, 0.120);
            if (wait == WaitResult::Stop) return;
            if (wait == WaitResult::Reset) continue;
            if (wait == WaitResult::Late) {
                droppedVideoFrames_.fetch_add(1);
                continue;
            }
            {
                std::lock_guard<std::mutex> lock(mutex_);
                if (frame.generation != generation_) continue;
            }
            OMTMediaFrame output = {};
            output.Type = OMTFrameType_Video;
            output.Codec = OMTCodec_BGRA;
            output.Timestamp = omtTimestamp(frame.ptsSeconds);
            output.Width = frame.width;
            output.Height = frame.height;
            output.Stride = frame.width * 4;
            output.FrameRateN = frameRateN_.load();
            output.FrameRateD = frameRateD_.load();
            output.AspectRatio = static_cast<float>(frame.width) / frame.height;
            output.Data = frame.bgra.data();
            output.DataLength = static_cast<int>(frame.bgra.size());
            {
                std::lock_guard<std::mutex> lock(sendMutex_);
                const auto devicePosition = audioMediaClock_
                    ? audioMediaClock_() : std::optional<double>{};
                if (omt_send(sender_, &output) >= 0) {
                    videoFrames_.fetch_add(1);
                    recordClockError(true, frame.ptsSeconds, devicePosition);
                }
                else droppedVideoFrames_.fetch_add(1);
            }
        }
    }

    void runAudio() {
        const std::size_t packetFrames = static_cast<std::size_t>(config_.audioSampleRate / 50);
        std::vector<float> interleaved(packetFrames * config_.audioChannels);
        std::vector<float> planar(packetFrames * config_.audioChannels);
        while (!stopping_.load()) {
            Clock::time_point anchorTime;
            double anchorPts = 0.0;
            std::uint64_t generation = 0;
            std::int64_t playhead = 0;
            {
                std::unique_lock<std::mutex> lock(mutex_);
                cv_.wait(lock, [this] { return stopping_.load() || audioStarted_; });
                if (stopping_.load()) return;
                generation = generation_;
                anchorTime = anchorTime_;
                anchorPts = anchorPts_;
                playhead = timeline_.playhead();
            }
            const double packetPts = anchorPts +
                static_cast<double>(playhead) / config_.audioSampleRate;
            const auto target = scheduled(anchorTime,
                static_cast<double>(playhead) / config_.audioSampleRate);
            const auto wait = waitForTarget(generation, packetPts, target, 0.200);
            if (wait == WaitResult::Stop) return;
            if (wait == WaitResult::Reset) continue;
            if (wait == WaitResult::Late) {
                std::lock_guard<std::mutex> lock(mutex_);
                if (generation == generation_) timeline_.pull(packetFrames, interleaved.data());
                continue;
            }
            {
                std::lock_guard<std::mutex> lock(mutex_);
                if (generation != generation_) continue;
                timeline_.pull(packetFrames, interleaved.data());
            }
            for (std::size_t sample = 0; sample < packetFrames; ++sample)
                for (std::uint32_t channel = 0; channel < config_.audioChannels; ++channel)
                    planar[channel * packetFrames + sample] =
                        interleaved[sample * config_.audioChannels + channel];
            OMTMediaFrame output = {};
            output.Type = OMTFrameType_Audio;
            output.Codec = OMTCodec_FPA1;
            output.Timestamp = omtTimestamp(packetPts);
            output.SampleRate = config_.audioSampleRate;
            output.Channels = static_cast<int>(config_.audioChannels);
            output.SamplesPerChannel = static_cast<int>(packetFrames);
            output.Data = planar.data();
            output.DataLength = static_cast<int>(planar.size() * sizeof(float));
            {
                std::lock_guard<std::mutex> lock(mutex_);
                if (generation != generation_) continue;
            }
            std::lock_guard<std::mutex> lock(sendMutex_);
            const auto devicePosition = audioMediaClock_
                ? audioMediaClock_() : std::optional<double>{};
            if (omt_send(sender_, &output) >= 0) {
                audioPackets_.fetch_add(1);
                recordClockError(false, packetPts, devicePosition);
            }
        }
    }

    OmtOutputConfig config_;
    AudioTimeline timeline_;
    AudioMediaClock audioMediaClock_;
    double audioClockOffsetSeconds_ = 0.0;
    omt_send_t* sender_ = nullptr;
    SwsContext* scale_ = nullptr;
    std::map<int, AudioResampler> resamplers_;
    std::set<std::uint32_t> mappedTracks_;
    std::vector<float> audioScratch_;
    mutable std::mutex mutex_;
    mutable std::mutex sendMutex_;
    std::condition_variable cv_;
    std::deque<VideoFrame> videoQueue_;
    bool anchorSet_ = false;
    bool audioStarted_ = false;
    std::atomic_bool hostClockFallback_{false};
    bool lastAudioClockValid_ = false;
    double lastAudioPosition_ = 0.0;
    Clock::time_point lastAudioClockTime_;
    double anchorPts_ = 0.0;
    Clock::time_point anchorTime_;
    std::uint64_t generation_ = 0;
    std::thread videoThread_;
    std::thread audioThread_;
    std::atomic_bool stopping_{false};
    std::atomic<std::uint64_t> videoFrames_{0};
    std::atomic<std::uint64_t> audioPackets_{0};
    std::atomic<std::uint64_t> droppedVideoFrames_{0};
    std::uint64_t audioClockVideoFrames_ = 0;
    std::uint64_t audioClockAudioPackets_ = 0;
    double maxAudioClockVideoErrorMs_ = 0.0;
    double maxAudioClockAudioErrorMs_ = 0.0;
    std::atomic<int> frameRateN_;
    std::atomic<int> frameRateD_;
};

OmtOutput::OmtOutput(OmtOutputConfig config)
    : impl_(std::make_unique<Impl>(std::move(config), AudioMediaClock{}, 0)) {}
OmtOutput::OmtOutput(OmtOutputConfig config, AudioMediaClock audioMediaClock,
                     int audioOutputDelayMs)
    : impl_(std::make_unique<Impl>(std::move(config), std::move(audioMediaClock),
                                   audioOutputDelayMs)) {}
OmtOutput::~OmtOutput() = default;
void OmtOutput::pushFrame(const FrameInfo& info, const AVFrame& frame) {
    impl_->pushFrame(info, frame);
}
void OmtOutput::setVideoFrameRate(int numerator, int denominator) {
    impl_->setVideoFrameRate(numerator, denominator);
}
void OmtOutput::reset() { impl_->reset(); }
OmtOutputStats OmtOutput::stats() const { return impl_->stats(); }
std::string OmtOutput::address() const { return impl_->address(); }

} // namespace rood
