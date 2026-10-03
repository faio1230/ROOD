#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

namespace srt_rx {

// One already decoded, timestamp-aligned audio block. Samples are interleaved
// float32, with frames * channels elements. The caller owns the storage.
struct AudioBlockView {
    std::uint32_t track_id;
    std::uint32_t channels;
    std::size_t frames;
    const float* samples;
};

struct ChannelRoute {
    std::uint32_t track_id;
    std::uint32_t source_channel;
    std::uint32_t device_channel;
    float gain = 1.0f;
};

// This class does channel routing only. Synchronization and sample-rate
// conversion must happen before render(), outside the audio callback.
class ChannelRouter {
public:
    ChannelRouter(std::uint32_t device_channels, std::vector<ChannelRoute> routes);

    std::uint32_t device_channels() const noexcept { return device_channels_; }

    // output must hold frames * device_channels() samples. Valid calls do not
    // allocate or throw. False means invalid input and output is untouched.
    // A missing track produces silence for that route. Overlapping routes mix.
    bool render(const std::vector<AudioBlockView>& blocks,
                std::size_t frames,
                float* output) const noexcept;

private:
    std::uint32_t device_channels_;
    std::vector<ChannelRoute> routes_;
};

} // namespace srt_rx
