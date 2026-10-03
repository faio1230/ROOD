#include <libomt.h>

#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <sstream>
#include <string>
#include <vector>

namespace {

int parseInt(const char* text, int minimum, int maximum) {
    char* end = nullptr;
    const long value = std::strtol(text, &end, 10);
    return end != text && *end == '\0' && value >= minimum && value <= maximum
        ? static_cast<int>(value) : -1;
}

std::vector<int> parseChannels(const char* text, int channelCount) {
    std::vector<int> channels;
    std::stringstream input(text);
    std::string item;
    while (std::getline(input, item, ',')) {
        const int channel = parseInt(item.c_str(), 0, channelCount - 1);
        if (channel < 0) return {};
        channels.push_back(channel);
    }
    return channels;
}

} // namespace

int main(int argc, char** argv) {
    if (argc < 2 || argc > 5) {
        std::cerr << "usage: rood_omt_probe ADDRESS [SECONDS] [CHANNELS] [SIGNAL_CHANNELS_CSV]\n";
        return 2;
    }
    const int seconds = argc >= 3 ? parseInt(argv[2], 1, 120) : 15;
    const int expectedChannels = argc >= 4 ? parseInt(argv[3], 1, 32) : 2;
    if (seconds < 0 || expectedChannels < 0) return 2;
    const auto signalChannels = argc >= 5
        ? parseChannels(argv[4], expectedChannels)
        : (expectedChannels == 1 ? std::vector<int>{0} : std::vector<int>{0, 1});
    if (signalChannels.empty()) return 2;
    for (const int channel : signalChannels)
        if (channel >= expectedChannels) return 2;
    const auto both = static_cast<OMTFrameType>(OMTFrameType_Video | OMTFrameType_Audio);
    omt_receive_t* receiver = omt_receive_create(argv[1], both,
        OMTPreferredVideoFormat_BGRA, OMTReceiveFlags_None);
    if (!receiver) {
        std::cerr << "OMT receiver initialization failed\n";
        return 1;
    }
    int videoFrames = 0;
    int audioFrames = 0;
    int width = 0;
    int height = 0;
    int channels = 0;
    std::uint64_t pixelSampleSum = 0;
    double audioSampleSum = 0.0;
    double secondChannelSampleSum = 0.0;
    std::vector<double> signalChannelSums(signalChannels.size(), 0.0);
    std::int64_t firstVideoTimestamp = -1;
    std::int64_t firstAudioTimestamp = -1;
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(seconds);
    while (std::chrono::steady_clock::now() < deadline) {
        const OMTMediaFrame* frame = omt_receive(receiver, both, 200);
        if (!frame || !frame->Data) continue;
        if (frame->Type == OMTFrameType_Video && frame->Width > 0 && frame->Height > 0 &&
            frame->DataLength >= frame->Width * frame->Height * 4) {
            ++videoFrames;
            width = frame->Width;
            height = frame->Height;
            if (firstVideoTimestamp < 0) firstVideoTimestamp = frame->Timestamp;
            const auto* pixels = static_cast<const std::uint8_t*>(frame->Data);
            for (int i = 0; i < frame->DataLength; i += 256)
                pixelSampleSum += pixels[i];
        } else if (frame->Type == OMTFrameType_Audio && frame->Channels == expectedChannels &&
                   frame->SamplesPerChannel > 0 && frame->SampleRate > 0 &&
                   frame->DataLength >= static_cast<std::int64_t>(frame->Channels) *
                       frame->SamplesPerChannel * static_cast<std::int64_t>(sizeof(float))) {
            ++audioFrames;
            channels = frame->Channels;
            if (firstAudioTimestamp < 0) firstAudioTimestamp = frame->Timestamp;
            const auto* samples = static_cast<const float*>(frame->Data);
            for (int i = 0; i < frame->SamplesPerChannel; i += 16) {
                audioSampleSum += std::fabs(samples[i]);
                if (frame->Channels > 1)
                    secondChannelSampleSum += std::fabs(samples[frame->SamplesPerChannel + i]);
                for (std::size_t channel = 0; channel < signalChannels.size(); ++channel)
                    signalChannelSums[channel] += std::fabs(
                        samples[signalChannels[channel] * frame->SamplesPerChannel + i]);
            }
        }
    }
    omt_receive_destroy(receiver);
    std::cout << "omtProbe video=" << videoFrames << " audio=" << audioFrames
              << " size=" << width << 'x' << height << " channels=" << channels
              << " pixelSampleSum=" << pixelSampleSum
              << " audioSampleSum=" << audioSampleSum
              << " secondChannelSampleSum=" << secondChannelSampleSum
              << " videoTimestamp=" << firstVideoTimestamp
              << " audioTimestamp=" << firstAudioTimestamp << std::endl;
    std::cout << "omtProbe signalChannelSums=";
    bool allSignalsPresent = true;
    for (std::size_t index = 0; index < signalChannels.size(); ++index) {
        if (index) std::cout << ',';
        std::cout << signalChannels[index] << ':' << signalChannelSums[index];
        allSignalsPresent &= signalChannelSums[index] > 0.0;
    }
    std::cout << std::endl;
    return videoFrames > 0 && audioFrames > 0 && channels == expectedChannels &&
           pixelSampleSum > 0 && allSignalsPresent &&
           firstVideoTimestamp > 0 && firstAudioTimestamp > 0 ? 0 : 1;
}
