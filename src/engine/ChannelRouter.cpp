#include "srt_rx/ChannelRouter.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <utility>

namespace srt_rx {

ChannelRouter::ChannelRouter(std::uint32_t device_channels,
                             std::vector<ChannelRoute> routes)
    : device_channels_(device_channels), routes_(std::move(routes)) {
    if (device_channels_ == 0) {
        throw std::invalid_argument("device_channels must be positive");
    }
    for (const auto& route : routes_) {
        if (route.device_channel >= device_channels_ || !std::isfinite(route.gain)) {
            throw std::invalid_argument("invalid channel route");
        }
    }
}

bool ChannelRouter::render(const std::vector<AudioBlockView>& blocks,
                           std::size_t frames,
                           float* output) const noexcept {
    if (frames == 0) {
        return true;
    }
    if (output == nullptr || frames > std::numeric_limits<std::size_t>::max() / device_channels_) {
        return false;
    }
    for (const auto& block : blocks) {
        if (block.channels == 0 || block.samples == nullptr || block.frames < frames ||
            frames > std::numeric_limits<std::size_t>::max() / block.channels) {
            return false;
        }
    }
    for (const auto& route : routes_) {
        const auto block = std::find_if(blocks.begin(), blocks.end(),
                                        [&route](const AudioBlockView& candidate) {
                                            return candidate.track_id == route.track_id;
                                        });
        if (block != blocks.end() && route.source_channel >= block->channels) {
            return false;
        }
    }

    const auto output_samples = frames * device_channels_;
    std::fill_n(output, output_samples, 0.0f);

    for (const auto& route : routes_) {
        const auto block = std::find_if(blocks.begin(), blocks.end(),
                                        [&route](const AudioBlockView& candidate) {
                                            return candidate.track_id == route.track_id;
                                        });
        if (block == blocks.end()) {
            continue;
        }
        for (std::size_t frame = 0; frame < frames; ++frame) {
            output[frame * device_channels_ + route.device_channel] +=
                block->samples[frame * block->channels + route.source_channel] * route.gain;
        }
    }

    // The output contract is normalized float32 PCM.
    for (std::size_t i = 0; i < output_samples; ++i) {
        output[i] = std::clamp(output[i], -1.0f, 1.0f);
    }
    return true;
}

} // namespace srt_rx
