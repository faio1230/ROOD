#include "rood/AudioLevelMeter.hpp"

extern "C" {
#include <libavutil/frame.h>
#include <libavutil/samplefmt.h>
}

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>

namespace rood {
namespace {

double sampleValue(const std::uint8_t* data, AVSampleFormat format, int index) {
    switch (format) {
    case AV_SAMPLE_FMT_U8:
        return (static_cast<double>(data[index]) - 128.0) / 128.0;
    case AV_SAMPLE_FMT_S16:
        return static_cast<double>(reinterpret_cast<const std::int16_t*>(data)[index]) / 32768.0;
    case AV_SAMPLE_FMT_S32:
        return static_cast<double>(reinterpret_cast<const std::int32_t*>(data)[index]) / 2147483648.0;
    case AV_SAMPLE_FMT_S64:
        return static_cast<double>(reinterpret_cast<const std::int64_t*>(data)[index]) / 9223372036854775808.0;
    case AV_SAMPLE_FMT_FLT:
        return static_cast<double>(reinterpret_cast<const float*>(data)[index]);
    case AV_SAMPLE_FMT_DBL:
        return reinterpret_cast<const double*>(data)[index];
    default:
        return 0.0;
    }
}

float dbfs(double amplitude) {
    if (!(amplitude > 0.0) || !std::isfinite(amplitude)) return -90.0f;
    return static_cast<float>(std::max(-90.0, 20.0 * std::log10(amplitude)));
}

} // namespace

std::vector<AudioChannelLevel> measureAudioLevels(const AVFrame& frame) {
    const int channels = frame.ch_layout.nb_channels;
    const int samples = frame.nb_samples;
    const auto format = static_cast<AVSampleFormat>(frame.format);
    const auto packed = av_get_packed_sample_fmt(format);
    if (channels <= 0 || channels > 256 || samples <= 0 ||
        !frame.extended_data || av_get_bytes_per_sample(format) <= 0 ||
        (packed != AV_SAMPLE_FMT_U8 && packed != AV_SAMPLE_FMT_S16 &&
         packed != AV_SAMPLE_FMT_S32 && packed != AV_SAMPLE_FMT_S64 &&
         packed != AV_SAMPLE_FMT_FLT && packed != AV_SAMPLE_FMT_DBL))
        return {};

    const bool planar = av_sample_fmt_is_planar(format) != 0;
    std::vector<AudioChannelLevel> levels(static_cast<std::size_t>(channels));
    for (int channel = 0; channel < channels; ++channel) {
        const auto* data = frame.extended_data[planar ? channel : 0];
        if (!data) return {};
        double peak = 0.0;
        double sumSquares = 0.0;
        for (int sample = 0; sample < samples; ++sample) {
            const int index = planar ? sample : sample * channels + channel;
            const double value = sampleValue(data, packed, index);
            if (!std::isfinite(value)) continue;
            peak = std::max(peak, std::abs(value));
            sumSquares += value * value;
        }
        levels[static_cast<std::size_t>(channel)].peakDbfs = dbfs(peak);
        levels[static_cast<std::size_t>(channel)].rmsDbfs =
            dbfs(std::sqrt(sumSquares / samples));
    }
    return levels;
}

} // namespace rood
