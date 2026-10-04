#include "rood/AudioPtsContinuity.hpp"

#include <cmath>
#include <iostream>
#include <optional>

int main() {
    rood::AudioPtsContinuity continuity;
    constexpr int sampleRate = 48000;
    constexpr int samplesPerFrame = 480;
    constexpr double frameSeconds = 0.01;
    double pts = 0.0;
    double correctedOutputFrames = 0.0;
    for (int frame = 0; frame < 360000; ++frame) {
        const auto gap = continuity.observe(pts, samplesPerFrame, sampleRate);
        if ((frame == 0 && gap) ||
            (frame > 0 && (!gap || std::fabs(*gap) > 1e-8))) {
            std::cerr << "continuous source PTS was classified as a discontinuity\n";
            return 1;
        }
        pts += frameSeconds;
        correctedOutputFrames += samplesPerFrame * 1.0003;
    }
    if (correctedOutputFrames - pts * sampleRate < sampleRate) {
        std::cerr << "test did not accumulate a meaningful output clock correction\n";
        return 1;
    }

    pts += 0.25;
    const auto forwardGap = continuity.observe(pts, samplesPerFrame, sampleRate);
    if (!forwardGap || std::fabs(*forwardGap - 0.25) > 1e-8) return 1;
    pts += frameSeconds;
    const auto nextGap = continuity.observe(pts, samplesPerFrame, sampleRate);
    if (!nextGap || std::fabs(*nextGap) > 1e-8) return 1;

    pts -= 0.19;
    const auto backwardGap = continuity.observe(pts, samplesPerFrame, sampleRate);
    if (!backwardGap || std::fabs(*backwardGap + 0.2) > 1e-8) return 1;

    continuity.observe(std::nullopt, samplesPerFrame, sampleRate);
    if (continuity.observe(pts + frameSeconds, samplesPerFrame, sampleRate)) return 1;
    continuity.reset();
    if (continuity.observe(0.0, samplesPerFrame, sampleRate)) return 1;
    return 0;
}
