#pragma once

#include <vector>

struct AVFrame;

namespace rood {

struct AudioChannelLevel {
    float peakDbfs = -90.0f;
    float rmsDbfs = -90.0f;
};

// Measures decoded input audio before any routing or output gain. An empty result
// means that the frame format cannot be measured.
std::vector<AudioChannelLevel> measureAudioLevels(const AVFrame& frame);

} // namespace rood
