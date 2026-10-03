#include "rood/RecoveringAudioOutput.hpp"

#include "rood/MediaReceiver.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <memory>
#include <mutex>
#include <set>
#include <stdexcept>
#include <string>
#include <utility>

namespace rood {
namespace {

using Clock = std::chrono::steady_clock;

} // namespace

class RecoveringAudioOutput::Impl {
public:
    explicit Impl(AudioOutputConfig config) : config_(std::move(config)) {
        if (config_.deviceIndex < 0 || config_.routes.empty())
            throw std::invalid_argument("invalid recovering audio output configuration");
        for (const auto& route : config_.routes) mappedTracks_.insert(route.track_id);
        const auto devices = listAudioOutputDevices();
        const auto selected = std::find_if(devices.begin(), devices.end(),
            [this](const AudioDeviceInfo& device) { return device.index == config_.deviceIndex; });
        if (selected == devices.end())
            throw std::invalid_argument("selected audio device was not found");
        if (config_.channels == 0 ||
            config_.channels > static_cast<std::uint32_t>(selected->maxOutputChannels) ||
            config_.sampleRate < 8000 || config_.sampleRate > 384000 ||
            config_.outputDelayMs < 0 || config_.outputDelayMs > 3000)
            throw std::invalid_argument("audio output format is not supported by the selected device");
        for (const auto& route : config_.routes) {
            if (route.device_channel >= config_.channels || !std::isfinite(route.gain))
                throw std::invalid_argument("invalid audio route");
        }
        deviceName_ = selected->name;
        hostApi_ = selected->hostApi;
        tryOpen();
    }

    void pushFrame(const FrameInfo& info, const AVFrame& frame) {
        if (info.kind != "audio" || info.streamId < 0 ||
            mappedTracks_.count(static_cast<std::uint32_t>(info.streamId)) == 0) return;
        poll();
        auto output = std::atomic_load(&output_);
        if (!output) return;
        try {
            output->pushFrame(info, frame);
        } catch (const std::exception& error) {
            retire(output, error.what());
        }
    }

    void poll() {
        const auto now = Clock::now();
        auto output = std::atomic_load(&output_);
        if (!output) {
            if (now >= nextAttempt_) tryOpen();
            return;
        }
        if (now < nextHealthCheck_) return;
        nextHealthCheck_ = now + std::chrono::milliseconds(500);
        const auto current = output->stats();
        if (current.streamStarted && !current.streamActive)
            retire(output, "audio output stream stopped unexpectedly");
    }

    void reset() {
        auto output = std::atomic_load(&output_);
        if (output) {
            try { output->reset(); }
            catch (const std::exception& error) { retire(output, error.what()); }
        }
        nextAttempt_ = Clock::now();
    }

    RecoveringAudioOutputStats stats() const {
        RecoveringAudioOutputStats result;
        auto output = std::atomic_load(&output_);
        if (output) {
            static_cast<AudioOutputStats&>(result) = output->stats();
            result.deviceAvailable = true;
        }
        std::lock_guard<std::mutex> lock(mutex_);
        if (!output) static_cast<AudioOutputStats&>(result) = lastStats_;
        result.reopenAttempts = reopenAttempts_;
        result.recoveries = recoveries_;
        result.lastError = lastError_;
        return result;
    }

    std::optional<double> playbackMediaSeconds() const noexcept {
        auto output = std::atomic_load(&output_);
        return output ? output->playbackMediaSeconds() : std::nullopt;
    }

private:
    void tryOpen() {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            ++reopenAttempts_;
        }
        nextAttempt_ = Clock::now() + std::chrono::seconds(1);
        try {
            const auto devices = listAudioOutputDevices();
            const auto found = std::find_if(devices.begin(), devices.end(),
                [this](const AudioDeviceInfo& device) {
                    return device.name == deviceName_ && device.hostApi == hostApi_ &&
                           device.maxOutputChannels >= static_cast<int>(config_.channels);
                });
            if (found == devices.end())
                throw std::runtime_error("selected audio device is unavailable");
            AudioOutputConfig current = config_;
            current.deviceIndex = found->index;
            auto opened = std::make_shared<PortAudioOutput>(std::move(current));
            std::atomic_store(&output_, std::move(opened));
            nextHealthCheck_ = Clock::now() + std::chrono::milliseconds(500);
            std::lock_guard<std::mutex> lock(mutex_);
            if (reopenAttempts_ > 1) ++recoveries_;
            lastError_.clear();
        } catch (const std::exception& error) {
            std::lock_guard<std::mutex> lock(mutex_);
            lastError_ = error.what();
        }
    }

    void retire(const std::shared_ptr<PortAudioOutput>& output, const std::string& error) {
        AudioOutputStats oldStats = output->stats();
        oldStats.streamActive = false;
        std::atomic_store(&output_, std::shared_ptr<PortAudioOutput>{});
        {
            std::lock_guard<std::mutex> lock(mutex_);
            lastStats_ = oldStats;
            lastError_ = error;
        }
        nextAttempt_ = Clock::now() + std::chrono::seconds(1);
    }

    AudioOutputConfig config_;
    std::string deviceName_;
    std::string hostApi_;
    std::set<std::uint32_t> mappedTracks_;
    std::shared_ptr<PortAudioOutput> output_;
    Clock::time_point nextAttempt_ = Clock::now();
    Clock::time_point nextHealthCheck_ = Clock::now();
    mutable std::mutex mutex_;
    AudioOutputStats lastStats_;
    std::uint64_t reopenAttempts_ = 0;
    std::uint64_t recoveries_ = 0;
    std::string lastError_;
};

RecoveringAudioOutput::RecoveringAudioOutput(AudioOutputConfig config)
    : impl_(std::make_unique<Impl>(std::move(config))) {}
RecoveringAudioOutput::~RecoveringAudioOutput() = default;
void RecoveringAudioOutput::pushFrame(const FrameInfo& info, const AVFrame& frame) {
    impl_->pushFrame(info, frame);
}
void RecoveringAudioOutput::poll() { impl_->poll(); }
void RecoveringAudioOutput::reset() { impl_->reset(); }
RecoveringAudioOutputStats RecoveringAudioOutput::stats() const { return impl_->stats(); }
std::optional<double> RecoveringAudioOutput::playbackMediaSeconds() const noexcept {
    return impl_->playbackMediaSeconds();
}

} // namespace rood
