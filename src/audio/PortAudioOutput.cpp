#include "rood/PortAudioOutput.hpp"

#include "rood/AudioTimeline.hpp"
#include "rood/ClockRecovery.hpp"
#include "rood/MediaReceiver.hpp"

#include <portaudio.h>
#ifdef _WIN32
#include <pa_win_wasapi.h>
#endif

extern "C" {
#include <libavutil/channel_layout.h>
#include <libavutil/error.h>
#include <libavutil/frame.h>
#include <libswresample/swresample.h>
}

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <limits>
#include <map>
#include <memory>
#include <set>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace rood {
namespace {

using Clock = std::chrono::steady_clock;

std::string ffmpegError(int code) {
    char buffer[AV_ERROR_MAX_STRING_SIZE] = {};
    av_strerror(code, buffer, sizeof(buffer));
    return buffer;
}

void checkPa(PaError error, const char* operation) {
    if (error != paNoError)
        throw std::runtime_error(std::string(operation) + ": " + Pa_GetErrorText(error));
}

class PaRuntime {
public:
    PaRuntime() { checkPa(Pa_Initialize(), "Pa_Initialize"); }
    ~PaRuntime() { Pa_Terminate(); }
};

class PaStreamHandle {
public:
    ~PaStreamHandle() {
        if (stream_) {
            if (Pa_IsStreamStopped(stream_) == 0) Pa_StopStream(stream_);
            Pa_CloseStream(stream_);
        }
    }
    PaStream** address() { return &stream_; }
    PaStream* get() const { return stream_; }
private:
    PaStream* stream_ = nullptr;
};

class Resampler {
public:
    ~Resampler() { swr_free(&context_); av_channel_layout_uninit(&inputLayout_); }
    Resampler() = default;
    Resampler(const Resampler&) = delete;
    Resampler& operator=(const Resampler&) = delete;

    void configure(const AVFrame& frame, int outputRate) {
        if (frame.ch_layout.nb_channels <= 0 || frame.sample_rate <= 0)
            throw std::runtime_error("audio frame has no channel layout or sample rate");
        const auto inputFormat = static_cast<AVSampleFormat>(frame.format);
        if (context_ && frame.sample_rate == inputRate_ && inputFormat == inputFormat_ &&
            av_channel_layout_compare(&frame.ch_layout, &inputLayout_) == 0) return;

        swr_free(&context_);
        av_channel_layout_uninit(&inputLayout_);
        int result = av_channel_layout_copy(&inputLayout_, &frame.ch_layout);
        if (result < 0) throw std::runtime_error("av_channel_layout_copy: " + ffmpegError(result));
        result = swr_alloc_set_opts2(&context_, &inputLayout_, AV_SAMPLE_FMT_FLT,
                                     outputRate, &inputLayout_, inputFormat,
                                     frame.sample_rate, 0, nullptr);
        if (result < 0) throw std::runtime_error("swr_alloc_set_opts2: " + ffmpegError(result));
        result = swr_init(context_);
        if (result < 0) throw std::runtime_error("swr_init: " + ffmpegError(result));
        inputRate_ = frame.sample_rate;
        inputFormat_ = inputFormat;
        hasNextFrame_ = false;
    }

    int convert(const AVFrame& frame, std::vector<float>& output) {
        const int capacity = swr_get_out_samples(context_, frame.nb_samples);
        if (capacity < 0 || capacity > 65536)
            throw std::runtime_error("unexpected resampler output size");
        output.resize(static_cast<std::size_t>(capacity) * inputLayout_.nb_channels);
        std::uint8_t* destination[] = {reinterpret_cast<std::uint8_t*>(output.data())};
        const int count = swr_convert(context_, destination, capacity,
                                      reinterpret_cast<const std::uint8_t* const*>(frame.extended_data),
                                      frame.nb_samples);
        if (count < 0) throw std::runtime_error("swr_convert: " + ffmpegError(count));
        output.resize(static_cast<std::size_t>(count) * inputLayout_.nb_channels);
        return count;
    }

    void setCompensation(double ppm, int outputRate) {
        const int distance = outputRate * 10;
        const int delta = static_cast<int>(std::lround(ppm * distance / 1000000.0));
        const int result = swr_set_compensation(context_, delta, distance);
        if (result < 0)
            throw std::runtime_error("swr_set_compensation: " + ffmpegError(result));
    }

    std::int64_t nextFrame() const { return nextFrame_; }
    bool hasNextFrame() const { return hasNextFrame_; }
    void setNextFrame(std::int64_t position) { nextFrame_ = position; hasNextFrame_ = true; }
private:
    SwrContext* context_ = nullptr;
    AVChannelLayout inputLayout_ = {};
    AVSampleFormat inputFormat_ = AV_SAMPLE_FMT_NONE;
    int inputRate_ = 0;
    std::int64_t nextFrame_ = 0;
    bool hasNextFrame_ = false;
};

} // namespace

namespace {

std::size_t checkedCapacity(const AudioOutputConfig& config) {
    if (config.sampleRate < 8000 || config.sampleRate > 384000 ||
        config.channels == 0 || config.channels > 256 ||
        config.outputDelayMs < 0 || config.outputDelayMs > 3000)
        throw std::invalid_argument("invalid audio output format");
    const auto frames = std::min<std::size_t>(
        static_cast<std::size_t>(config.sampleRate) * 5,
        25000000 / config.channels);
    const auto required = static_cast<std::size_t>(config.sampleRate) *
                          static_cast<std::size_t>(config.outputDelayMs + 500) / 1000;
    if (frames < required)
        throw std::invalid_argument("audio output format exceeds bounded timeline capacity");
    return frames;
}

} // namespace

class PortAudioOutput::Impl {
public:
    explicit Impl(AudioOutputConfig config)
        : config_(std::move(config)),
          timeline_(config_.channels, checkedCapacity(config_), config_.routes) {
        if (config_.deviceIndex < 0 ||
            config_.routes.empty())
            throw std::invalid_argument("invalid audio output configuration");
        delayFrames_ = static_cast<std::int64_t>(config_.outputDelayMs) * config_.sampleRate / 1000;
        for (const auto& route : config_.routes) mappedTracks_.insert(route.track_id);
        const PaDeviceInfo* device = Pa_GetDeviceInfo(config_.deviceIndex);
        if (!device || config_.channels > static_cast<std::uint32_t>(device->maxOutputChannels))
            throw std::invalid_argument("audio device missing or output channel count unsupported");
        const PaHostApiInfo* api = Pa_GetHostApiInfo(device->hostApi);
        if (!api) throw std::runtime_error("audio host API unavailable");

        PaStreamParameters parameters{};
        parameters.device = config_.deviceIndex;
        parameters.channelCount = static_cast<int>(config_.channels);
        parameters.sampleFormat = paFloat32;
        parameters.suggestedLatency = device->defaultLowOutputLatency;
#ifdef _WIN32
        PaWasapiStreamInfo wasapi{};
        if (api->type == paWASAPI) {
            wasapi.size = sizeof(wasapi);
            wasapi.hostApiType = paWASAPI;
            wasapi.version = 1;
            wasapi.flags = config_.wasapiExclusive ? paWinWasapiExclusive : 0;
            parameters.hostApiSpecificStreamInfo = &wasapi;
        } else if (config_.wasapiExclusive) {
            throw std::invalid_argument("WASAPI exclusive mode requires a WASAPI device");
        }
#else
        if (config_.wasapiExclusive)
            throw std::invalid_argument("WASAPI exclusive mode requires Windows");
#endif
        checkPa(Pa_IsFormatSupported(nullptr, &parameters, config_.sampleRate),
                "Pa_IsFormatSupported");
        checkPa(Pa_OpenStream(stream_.address(), nullptr, &parameters, config_.sampleRate,
                              paFramesPerBufferUnspecified, paNoFlag, &callback, this),
                "Pa_OpenStream");
    }

    void pushFrame(const FrameInfo& info, const AVFrame& frame) {
        if (info.kind != "audio" || info.streamId < 0 ||
            mappedTracks_.count(static_cast<std::uint32_t>(info.streamId)) == 0) return;
        auto& resampler = resamplers_[info.streamId];
        resampler.configure(frame, config_.sampleRate);
        std::int64_t position = delayFrames_;
        if (info.hasPts && info.timeBaseDen > 0) {
            const double mediaSeconds = static_cast<double>(info.pts) * info.timeBaseNum /
                                        info.timeBaseDen;
            if (!std::isfinite(mediaSeconds))
                throw std::runtime_error("audio PTS is not finite");
            if (!originSet_.load()) {
                originSeconds_.store(mediaSeconds -
                    static_cast<double>(resampler.hasNextFrame()
                                            ? resampler.nextFrame() - delayFrames_ : 0) /
                    config_.sampleRate);
                mediaPosition_.store(originSeconds_.load() -
                    static_cast<double>(delayFrames_) / config_.sampleRate);
                originSet_.store(true);
            }
            const double relativeFrames = (mediaSeconds - originSeconds_.load()) * config_.sampleRate;
            if (!std::isfinite(relativeFrames) ||
                relativeFrames < -static_cast<double>(config_.sampleRate) * 60 ||
                relativeFrames > static_cast<double>(std::numeric_limits<std::int64_t>::max() / 2))
                throw std::runtime_error("audio PTS is outside supported timeline range");
            position += static_cast<std::int64_t>(std::llround(relativeFrames));
        } else if (resampler.hasNextFrame()) {
            position = resampler.nextFrame();
        }
        if (resampler.hasNextFrame()) {
            const auto difference = std::fabs(
                static_cast<long double>(position) - resampler.nextFrame());
            if (difference <= config_.sampleRate / 10) {
                position = resampler.nextFrame();
            } else {
                clockRecovery_.reset();
                driftPpm_.store(0.0);
                driftErrorMs_.store(0.0);
                driftLocked_.store(false);
                lastClockObservation_ = {};
            }
        }
        if (resampler.hasNextFrame() && started_.load() &&
            static_cast<std::uint32_t>(info.streamId) == *mappedTracks_.begin()) {
            const auto now = std::chrono::steady_clock::now();
            const double elapsed = lastClockObservation_.time_since_epoch().count() == 0
                ? 0.0 : std::chrono::duration<double>(now - lastClockObservation_).count();
            lastClockObservation_ = now;
            const double leadSeconds = static_cast<double>(
                resampler.nextFrame() - timeline_.playhead()) / config_.sampleRate;
            driftPpm_.store(clockRecovery_.observe(leadSeconds, elapsed));
            driftErrorMs_.store(clockRecovery_.errorMs());
            driftLocked_.store(clockRecovery_.locked());
        }
        resampler.setCompensation(driftPpm_.load(), config_.sampleRate);
        const int converted = resampler.convert(frame, scratch_);
        if (converted == 0) return;
        if (position < 0) {
            const auto skip = std::min<std::int64_t>(-position, converted);
            position += skip;
            const auto channels = static_cast<std::size_t>(frame.ch_layout.nb_channels);
            scratch_.erase(scratch_.begin(), scratch_.begin() + skip * channels);
        }
        const auto available = scratch_.size() / static_cast<std::size_t>(frame.ch_layout.nb_channels);
        std::size_t accepted = 0;
        if (available > 0)
            accepted = timeline_.push(static_cast<std::uint32_t>(info.streamId), position,
                                      frame.ch_layout.nb_channels, scratch_.data(), available);
        resampler.setNextFrame(position + static_cast<std::int64_t>(available));
        if (!started_.load() && accepted > 0) {
            checkPa(Pa_StartStream(stream_.get()), "Pa_StartStream");
            streamStartTicks_.store(Clock::now().time_since_epoch().count());
            started_.store(true);
        }
    }

    void reset() {
        if (started_.load()) {
            checkPa(Pa_StopStream(stream_.get()), "Pa_StopStream");
            started_.store(false);
            streamStartTicks_.store(0);
        }
        resamplers_.clear();
        timeline_.reset();
        originSet_.store(false);
        originSeconds_.store(0);
        mediaPosition_.store(0);
        clockRecovery_.reset();
        driftPpm_.store(0);
        driftErrorMs_.store(0);
        driftLocked_.store(false);
        lastClockObservation_ = {};
        lastDacTime_ = 0;
    }

    AudioOutputStats stats() const {
        AudioOutputStats result;
        result.callbackCount = callbacks_.load();
        result.deviceUnderflows = deviceUnderflows_.load();
        result.timestampRegressions = timestampRegressions_.load();
        result.silentFrames = timeline_.silentFrames();
        result.renderedFrames = renderedFrames_.load();
        result.rejectedFrames = timeline_.rejectedFrames();
        result.playheadFrames = timeline_.playhead();
        result.streamStarted = started_.load();
        result.streamActive = started_.load() && Pa_IsStreamActive(stream_.get()) == 1;
        result.driftCorrectionPpm = driftPpm_.load();
        result.driftErrorMs = driftErrorMs_.load();
        result.driftLocked = driftLocked_.load();
        const auto startTicks = streamStartTicks_.load();
        if (startTicks != 0) {
            const auto started = Clock::time_point(Clock::duration(startTicks));
            const double elapsed = std::chrono::duration<double>(Clock::now() - started).count();
            if (elapsed >= 5.0) {
                result.observedSampleRate = result.playheadFrames / elapsed;
                result.sampleClockMismatch =
                    std::fabs(result.observedSampleRate / config_.sampleRate - 1.0) > 0.05;
            }
        }
        return result;
    }

    std::optional<double> playbackMediaSeconds() const noexcept {
        if (!started_.load() || !originSet_.load()) return std::nullopt;
        return mediaPosition_.load();
    }

private:
    static int callback(const void*, void* output, unsigned long frames,
                        const PaStreamCallbackTimeInfo* time, PaStreamCallbackFlags flags,
                        void* user) {
        auto& self = *static_cast<Impl*>(user);
        const auto rendered = self.timeline_.pull(frames, static_cast<float*>(output));
        self.renderedFrames_.fetch_add(rendered);
        self.callbacks_.fetch_add(1);
        const double ratio = 1.0 + self.driftPpm_.load() / 1000000.0;
        self.mediaPosition_.store(self.mediaPosition_.load() +
            static_cast<double>(frames) / (self.config_.sampleRate * ratio));
        if (flags & paOutputUnderflow) self.deviceUnderflows_.fetch_add(1);
        if (time && std::isfinite(time->outputBufferDacTime)) {
            const double current = time->outputBufferDacTime;
            if (self.lastDacTime_ != 0 && current < self.lastDacTime_)
                self.timestampRegressions_.fetch_add(1);
            self.lastDacTime_ = current;
        }
        return paContinue;
    }

    PaRuntime runtime_;
    AudioOutputConfig config_;
    AudioTimeline timeline_;
    PaStreamHandle stream_;
    std::map<int, Resampler> resamplers_;
    std::set<std::uint32_t> mappedTracks_;
    std::vector<float> scratch_;
    std::int64_t delayFrames_ = 0;
    std::atomic_bool originSet_{false};
    std::atomic<double> originSeconds_{0};
    std::atomic<double> mediaPosition_{0};
    std::atomic_bool started_{false};
    std::atomic<Clock::duration::rep> streamStartTicks_{0};
    ClockRecovery clockRecovery_;
    std::chrono::steady_clock::time_point lastClockObservation_;
    std::atomic<double> driftPpm_{0};
    std::atomic<double> driftErrorMs_{0};
    std::atomic_bool driftLocked_{false};
    double lastDacTime_ = 0;
    std::atomic<std::uint64_t> callbacks_{0};
    std::atomic<std::uint64_t> renderedFrames_{0};
    std::atomic<std::uint64_t> deviceUnderflows_{0};
    std::atomic<std::uint64_t> timestampRegressions_{0};
};

PortAudioOutput::PortAudioOutput(AudioOutputConfig config)
    : impl_(std::make_unique<Impl>(std::move(config))) {}
PortAudioOutput::~PortAudioOutput() = default;
void PortAudioOutput::pushFrame(const FrameInfo& info, const AVFrame& frame) {
    impl_->pushFrame(info, frame);
}
void PortAudioOutput::reset() { impl_->reset(); }
AudioOutputStats PortAudioOutput::stats() const { return impl_->stats(); }
std::optional<double> PortAudioOutput::playbackMediaSeconds() const noexcept {
    return impl_->playbackMediaSeconds();
}

std::vector<AudioDeviceInfo> listAudioOutputDevices() {
    PaRuntime runtime;
    const int count = Pa_GetDeviceCount();
    if (count < 0) checkPa(count, "Pa_GetDeviceCount");
    std::vector<AudioDeviceInfo> devices;
    for (int index = 0; index < count; ++index) {
        const auto* device = Pa_GetDeviceInfo(index);
        if (!device || device->maxOutputChannels <= 0) continue;
        const auto* api = Pa_GetHostApiInfo(device->hostApi);
        AudioDeviceInfo entry;
        entry.index = index;
        entry.name = device->name ? device->name : "unknown";
        entry.hostApi = api && api->name ? api->name : "unknown";
        entry.maxOutputChannels = device->maxOutputChannels;
        entry.defaultSampleRate = device->defaultSampleRate;
        devices.push_back(std::move(entry));
    }
    return devices;
}

} // namespace rood
