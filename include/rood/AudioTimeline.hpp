#pragma once

#include "rood/ChannelRouter.hpp"

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <vector>

namespace rood {

// A bounded, device-rate PCM timeline shared by the decode thread and one
// audio callback. Frame positions use the output sample rate as their clock.
// Different tracks may contribute to the same position before it is played.
class AudioTimeline {
public:
    AudioTimeline(std::uint32_t outputChannels, std::size_t capacityFrames,
                  std::vector<ChannelRoute> routes);

    // Producer thread: mix an interleaved float32 block at its absolute
    // output-frame position. Late and over-capacity samples are discarded.
    // Returns the number of source frames accepted by at least one route.
    std::size_t push(std::uint32_t trackId, std::int64_t startFrame,
                     std::uint32_t sourceChannels, const float* samples,
                     std::size_t frames);

    // Audio callback: fills all requested output frames. Never allocates or
    // waits for the producer; contention and missing positions yield silence.
    // Returns the number of positions that contained media.
    std::size_t pull(std::size_t frames, float* output) noexcept;

    // Reset when the source timebase or device changes. The owner should stop
    // the audio stream first so no callback is still consuming old material.
    void reset();

    std::int64_t playhead() const noexcept { return playhead_.load(); }
    std::uint64_t silentFrames() const noexcept { return silentFrames_.load(); }
    std::uint64_t rejectedFrames() const noexcept { return rejectedFrames_.load(); }
    std::uint32_t outputChannels() const noexcept { return outputChannels_; }
    std::size_t capacityFrames() const noexcept { return capacityFrames_; }

private:
    const std::uint32_t outputChannels_;
    const std::size_t capacityFrames_;
    const std::vector<ChannelRoute> routes_;
    std::vector<float> samples_;
    std::vector<std::int64_t> tags_;
    mutable std::mutex mutex_;
    std::atomic<std::int64_t> playhead_{0};
    std::atomic<std::uint64_t> silentFrames_{0};
    std::atomic<std::uint64_t> rejectedFrames_{0};
};

} // namespace rood
