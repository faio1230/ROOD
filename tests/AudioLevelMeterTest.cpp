#include "rood/AudioLevelMeter.hpp"

extern "C" {
#include <libavutil/frame.h>
}

#include <cmath>
#include <cstdint>
#include <iostream>
#include <memory>

namespace {

struct FrameDeleter {
    void operator()(AVFrame* frame) const { av_frame_free(&frame); }
};

using Frame = std::unique_ptr<AVFrame, FrameDeleter>;

Frame makeFrame(int channels, AVSampleFormat format, int samples) {
    Frame frame(av_frame_alloc());
    if (!frame) return {};
    av_channel_layout_default(&frame->ch_layout, channels);
    frame->format = format;
    frame->nb_samples = samples;
    if (av_frame_get_buffer(frame.get(), 0) < 0) return {};
    return frame;
}

bool near(float value, float expected) {
    return std::abs(value - expected) < 0.1f;
}

} // namespace

int main() {
    auto packed = makeFrame(2, AV_SAMPLE_FMT_S16, 4);
    if (!packed) return 1;
    auto* packedSamples = reinterpret_cast<std::int16_t*>(packed->extended_data[0]);
    for (int i = 0; i < 4; ++i) {
        packedSamples[i * 2] = 16384;
        packedSamples[i * 2 + 1] = 8192;
    }
    const auto stereo = rood::measureAudioLevels(*packed);
    if (stereo.size() != 2 ||
        !near(stereo[0].peakDbfs, -6.02f) || !near(stereo[0].rmsDbfs, -6.02f) ||
        !near(stereo[1].peakDbfs, -12.04f) || !near(stereo[1].rmsDbfs, -12.04f)) {
        std::cerr << "Packed stereo levels differ from expected dBFS\n";
        return 2;
    }

    auto planar = makeFrame(8, AV_SAMPLE_FMT_FLTP, 4);
    if (!planar) return 3;
    for (int channel = 0; channel < 8; ++channel) {
        auto* samples = reinterpret_cast<float*>(planar->extended_data[channel]);
        for (int i = 0; i < 4; ++i)
            samples[i] = channel == 7 ? 0.0f : std::pow(0.5f, channel + 1);
    }
    const auto eight = rood::measureAudioLevels(*planar);
    if (eight.size() != 8 || !near(eight[0].peakDbfs, -6.02f) ||
        !near(eight[6].rmsDbfs, -42.14f) ||
        eight[7].peakDbfs != -90.0f || eight[7].rmsDbfs != -90.0f) {
        std::cerr << "Planar multichannel or silence levels differ from expected dBFS\n";
        return 4;
    }
    std::cout << "Decoded input levels: packed stereo, planar 8ch and silence passed\n";
    return 0;
}
