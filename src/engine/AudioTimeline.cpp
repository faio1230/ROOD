#include "rood/AudioTimeline.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <utility>

namespace rood {
namespace {

std::size_t checkedSamples(std::uint32_t channels, std::size_t frames) {
    if (channels == 0 || frames == 0 ||
        frames > std::numeric_limits<std::size_t>::max() / channels ||
        frames > static_cast<std::size_t>(std::numeric_limits<std::int64_t>::max()))
        throw std::invalid_argument("invalid audio timeline dimensions");
    return frames * channels;
}

} // namespace

AudioTimeline::AudioTimeline(std::uint32_t outputChannels, std::size_t capacityFrames,
                             std::vector<ChannelRoute> routes)
    : outputChannels_(outputChannels), capacityFrames_(capacityFrames),
      routes_(std::move(routes)),
      samples_(checkedSamples(outputChannels, capacityFrames), 0.0f),
      tags_(capacityFrames, -1) {
    for (const auto& route : routes_) {
        if (route.device_channel >= outputChannels || !std::isfinite(route.gain))
            throw std::invalid_argument("invalid audio timeline route");
    }
}

std::size_t AudioTimeline::push(std::uint32_t trackId, std::int64_t startFrame,
                                std::uint32_t sourceChannels, const float* samples,
                                std::size_t frames) {
    if (frames == 0) return 0;
    if (!samples || sourceChannels == 0 ||
        frames > std::numeric_limits<std::size_t>::max() / sourceChannels ||
        startFrame < 0 || frames > static_cast<std::size_t>(
            std::numeric_limits<std::int64_t>::max() - startFrame))
        throw std::invalid_argument("invalid audio timeline block");
    bool routed = false;
    for (const auto& route : routes_) {
        if (route.track_id != trackId) continue;
        routed = true;
        if (route.source_channel >= sourceChannels)
            throw std::invalid_argument("route source channel exceeds block channels");
    }
    if (!routed) return 0;

    std::lock_guard<std::mutex> lock(mutex_);
    const auto cursor = playhead_.load();
    std::size_t accepted = 0;
    for (std::size_t frame = 0; frame < frames; ++frame) {
        const auto position = startFrame + static_cast<std::int64_t>(frame);
        if (position < cursor || position - cursor >= static_cast<std::int64_t>(capacityFrames_)) {
            rejectedFrames_.fetch_add(1);
            continue;
        }
        const auto slot = static_cast<std::size_t>(position % static_cast<std::int64_t>(capacityFrames_));
        float* destination = samples_.data() + slot * outputChannels_;
        if (tags_[slot] != position) {
            std::fill_n(destination, outputChannels_, 0.0f);
            tags_[slot] = position;
        }
        for (const auto& route : routes_) {
            if (route.track_id == trackId)
                destination[route.device_channel] +=
                    samples[frame * sourceChannels + route.source_channel] * route.gain;
        }
        ++accepted;
    }
    return accepted;
}

std::size_t AudioTimeline::pull(std::size_t frames, float* output) noexcept {
    if (frames == 0 || !output) return 0;
    if (frames > std::numeric_limits<std::size_t>::max() / outputChannels_ ||
        frames > static_cast<std::size_t>(std::numeric_limits<std::int64_t>::max() - playhead_.load()))
        return 0;
    std::fill_n(output, frames * outputChannels_, 0.0f);
    if (!mutex_.try_lock()) {
        playhead_.fetch_add(static_cast<std::int64_t>(frames));
        silentFrames_.fetch_add(frames);
        return 0;
    }
    const auto cursor = playhead_.load();
    std::size_t populated = 0;
    for (std::size_t frame = 0; frame < frames; ++frame) {
        const auto position = cursor + static_cast<std::int64_t>(frame);
        const auto slot = static_cast<std::size_t>(position % static_cast<std::int64_t>(capacityFrames_));
        if (tags_[slot] != position) continue;
        const float* source = samples_.data() + slot * outputChannels_;
        float* destination = output + frame * outputChannels_;
        for (std::uint32_t channel = 0; channel < outputChannels_; ++channel)
            destination[channel] = std::clamp(source[channel], -1.0f, 1.0f);
        tags_[slot] = -1;
        ++populated;
    }
    playhead_.store(cursor + static_cast<std::int64_t>(frames));
    mutex_.unlock();
    silentFrames_.fetch_add(frames - populated);
    return populated;
}

void AudioTimeline::reset() {
    std::lock_guard<std::mutex> lock(mutex_);
    std::fill(tags_.begin(), tags_.end(), -1);
    playhead_.store(0);
    silentFrames_.store(0);
    rejectedFrames_.store(0);
}

} // namespace rood
