#include "rood/SpoutVideoOutput.hpp"

#include "rood/MediaReceiver.hpp"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <SpoutLibrary/SpoutLibrary.h>

extern "C" {
#include <libavutil/frame.h>
#include <libswscale/swscale.h>
}

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <limits>
#include <mutex>
#include <stdexcept>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace rood {
namespace {

using Clock = std::chrono::steady_clock;

struct QueuedVideoFrame {
    std::vector<std::uint8_t> rgba;
    unsigned width = 0;
    unsigned height = 0;
    double ptsSeconds = 0.0;
    bool hasPts = false;
    std::uint64_t generation = 0;
};

constexpr std::size_t maxQueuedBytes = 256ULL * 1024 * 1024;
constexpr std::size_t maxQueuedFrames = 64;

} // namespace

class SpoutVideoOutput::Impl {
public:
    Impl(SpoutVideoConfig config, AudioMediaClock audioMediaClock)
        : config_(std::move(config)), audioMediaClock_(std::move(audioMediaClock)) {
        if (config_.senderName.empty() || config_.senderName.size() > 240 ||
            config_.outputDelayMs < 0 || config_.outputDelayMs > 5000 ||
            config_.videoOffsetMs < -5000 || config_.videoOffsetMs > 5000 ||
            config_.lateDropMs < 0 || config_.lateDropMs > 2000)
            throw std::invalid_argument("invalid Spout video configuration");
        worker_ = std::thread(&Impl::run, this);
        std::unique_lock<std::mutex> lock(mutex_);
        readyCv_.wait(lock, [this] { return startupDone_; });
        if (!startupError_.empty()) {
            lock.unlock();
            stopping_.store(true);
            queueCv_.notify_all();
            worker_.join();
            throw std::runtime_error(startupError_);
        }
    }

    ~Impl() {
        stopping_.store(true);
        queueCv_.notify_all();
        if (worker_.joinable()) worker_.join();
        if (scale_) sws_freeContext(scale_);
    }

    void pushFrame(const FrameInfo& info, const AVFrame& source) {
        if (info.kind != "video") return;
        if (source.width <= 0 || source.height <= 0 ||
            source.width > 8192 || source.height > 8192 ||
            static_cast<std::int64_t>(source.width) * source.height > 16777216)
            throw std::invalid_argument("unsupported video frame dimensions");
        const double ptsSeconds = info.hasPts && info.timeBaseDen > 0
            ? static_cast<double>(info.pts) * info.timeBaseNum / info.timeBaseDen : 0.0;
        if (!std::isfinite(ptsSeconds)) throw std::invalid_argument("invalid video PTS");
        scale_ = sws_getCachedContext(scale_, source.width, source.height,
                                      static_cast<AVPixelFormat>(source.format),
                                      source.width, source.height, AV_PIX_FMT_RGBA,
                                      SWS_BILINEAR, nullptr, nullptr, nullptr);
        if (!scale_) throw std::runtime_error("sws_getCachedContext failed for video frame");
        QueuedVideoFrame converted;
        converted.width = static_cast<unsigned>(source.width);
        converted.height = static_cast<unsigned>(source.height);
        converted.ptsSeconds = ptsSeconds;
        converted.hasPts = info.hasPts;
        converted.rgba.resize(static_cast<std::size_t>(source.width) * source.height * 4);
        std::uint8_t* destination[] = {converted.rgba.data(), nullptr, nullptr, nullptr};
        int stride[] = {source.width * 4, 0, 0, 0};
        const int rows = sws_scale(scale_, source.data, source.linesize, 0, source.height,
                                   destination, stride);
        if (rows != source.height) throw std::runtime_error("sws_scale did not convert the full frame");

        {
            std::lock_guard<std::mutex> lock(mutex_);
            converted.generation = generation_;
            const auto frameBytes = converted.rgba.size();
            const auto maxFramesForSize = std::clamp(maxQueuedBytes / frameBytes,
                                                      std::size_t{2}, maxQueuedFrames);
            if (queue_.size() >= maxFramesForSize) {
                queue_.pop_front();
                droppedFrames_.fetch_add(1);
                rebaseRequested_ = true;
            }
            queue_.push_back(std::move(converted));
            receivedFrames_.fetch_add(1);
        }
        queueCv_.notify_one();
    }

    void reset() {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            droppedFrames_.fetch_add(queue_.size());
            queue_.clear();
            ++generation_;
            rebaseRequested_ = false;
        }
        queueCv_.notify_all();
    }

    SpoutVideoStats stats() const {
        SpoutVideoStats result;
        result.receivedFrames = receivedFrames_.load();
        result.sentFrames = sentFrames_.load();
        result.droppedFrames = droppedFrames_.load();
        result.failedFrames = failedFrames_.load();
        std::lock_guard<std::mutex> lock(mutex_);
        result.lastAudioSyncErrorMs = lastAudioSyncErrorMs_;
        result.hasAudioSyncError = hasAudioSyncError_;
        result.senderReady = senderReady_;
        result.lastError = lastError_;
        return result;
    }

private:
    void fail(const std::string& message) {
        std::lock_guard<std::mutex> lock(mutex_);
        lastError_ = message;
        failedFrames_.fetch_add(1);
    }

    void run() {
        SPOUTLIBRARY* spout = GetSpout();
        if (!spout) {
            std::lock_guard<std::mutex> lock(mutex_);
            startupError_ = "GetSpout failed";
            startupDone_ = true;
            readyCv_.notify_one();
            return;
        }
        const bool glReady = spout->CreateOpenGL();
        {
            std::lock_guard<std::mutex> lock(mutex_);
            startupDone_ = true;
            senderReady_ = glReady;
            if (!glReady) startupError_ = "Spout CreateOpenGL failed";
        }
        readyCv_.notify_one();
        if (!glReady) {
            spout->Release();
            return;
        }
        spout->SetSenderName(config_.senderName.c_str());
        std::uint64_t observedGeneration = 0;
        bool hostAnchorSet = false;
        double hostAnchorPts = 0.0;
        Clock::time_point hostAnchorTime;
        while (!stopping_.load()) {
            QueuedVideoFrame frame;
            {
                std::unique_lock<std::mutex> lock(mutex_);
                queueCv_.wait(lock, [this, &observedGeneration] {
                    return stopping_.load() || !queue_.empty() || generation_ != observedGeneration;
                });
                if (stopping_.load()) break;
                if (generation_ != observedGeneration) {
                    observedGeneration = generation_;
                    hostAnchorSet = false;
                    lock.unlock();
                    spout->ReleaseSender();
                    spout->SetSenderName(config_.senderName.c_str());
                    continue;
                }
                frame = std::move(queue_.front());
                queue_.pop_front();
            }

            const auto clockWaitStart = Clock::now();
            bool discard = false;
            while (!stopping_.load()) {
                {
                    std::lock_guard<std::mutex> lock(mutex_);
                    if (generation_ != frame.generation) { discard = true; break; }
                }
                std::optional<double> audioPosition;
                if (audioMediaClock_) audioPosition = audioMediaClock_();
                if (!audioPosition) {
                    std::lock_guard<std::mutex> lock(mutex_);
                    if (rebaseRequested_ && !queue_.empty()) {
                        droppedFrames_.fetch_add(queue_.size());
                        frame = std::move(queue_.back());
                        queue_.clear();
                        rebaseRequested_ = false;
                        hostAnchorSet = false;
                    }
                }
                if (audioMediaClock_ && !audioPosition &&
                    Clock::now() - clockWaitStart < std::chrono::seconds(1)) {
                    std::unique_lock<std::mutex> lock(mutex_);
                    queueCv_.wait_for(lock, std::chrono::milliseconds(5));
                    continue;
                }

                double waitSeconds = 0.0;
                if (frame.hasPts && audioPosition) {
                    waitSeconds = frame.ptsSeconds + config_.videoOffsetMs / 1000.0 - *audioPosition;
                } else if (frame.hasPts) {
                    if (!hostAnchorSet) {
                        hostAnchorSet = true;
                        hostAnchorPts = frame.ptsSeconds;
                        hostAnchorTime = Clock::now() +
                            std::chrono::milliseconds(config_.outputDelayMs);
                    }
                    const auto target = hostAnchorTime + std::chrono::duration_cast<Clock::duration>(
                        std::chrono::duration<double>(frame.ptsSeconds - hostAnchorPts +
                                                       config_.videoOffsetMs / 1000.0));
                    waitSeconds = std::chrono::duration<double>(target - Clock::now()).count();
                }
                if (waitSeconds < -config_.lateDropMs / 1000.0) {
                    droppedFrames_.fetch_add(1);
                    discard = true;
                    break;
                }
                if (waitSeconds <= 0.002) {
                    if (frame.hasPts && audioPosition) {
                        std::lock_guard<std::mutex> lock(mutex_);
                        lastAudioSyncErrorMs_ = -waitSeconds * 1000.0;
                        hasAudioSyncError_ = true;
                    }
                    break;
                }
                std::unique_lock<std::mutex> lock(mutex_);
                queueCv_.wait_for(lock, std::chrono::duration<double>(
                    std::min(waitSeconds, 0.010)));
            }
            if (discard || stopping_.load()) continue;
            if (spout->SendImage(frame.rgba.data(), frame.width, frame.height, GL_RGBA, false))
                sentFrames_.fetch_add(1);
            else
                fail("Spout SendImage failed");
        }
        spout->ReleaseSender();
        spout->CloseOpenGL();
        spout->Release();
    }

    SpoutVideoConfig config_;
    AudioMediaClock audioMediaClock_;
    SwsContext* scale_ = nullptr;
    mutable std::mutex mutex_;
    std::condition_variable queueCv_;
    std::condition_variable readyCv_;
    std::deque<QueuedVideoFrame> queue_;
    bool rebaseRequested_ = false;
    std::uint64_t generation_ = 0;
    bool startupDone_ = false;
    bool senderReady_ = false;
    bool hasAudioSyncError_ = false;
    double lastAudioSyncErrorMs_ = 0.0;
    std::string startupError_;
    std::string lastError_;
    std::thread worker_;
    std::atomic_bool stopping_{false};
    std::atomic<std::uint64_t> receivedFrames_{0};
    std::atomic<std::uint64_t> sentFrames_{0};
    std::atomic<std::uint64_t> droppedFrames_{0};
    std::atomic<std::uint64_t> failedFrames_{0};
};

SpoutVideoOutput::SpoutVideoOutput(SpoutVideoConfig config, AudioMediaClock audioMediaClock)
    : impl_(std::make_unique<Impl>(std::move(config), std::move(audioMediaClock))) {}
SpoutVideoOutput::~SpoutVideoOutput() = default;
void SpoutVideoOutput::pushFrame(const FrameInfo& info, const AVFrame& frame) {
    impl_->pushFrame(info, frame);
}
void SpoutVideoOutput::reset() { impl_->reset(); }
SpoutVideoStats SpoutVideoOutput::stats() const { return impl_->stats(); }

} // namespace rood
