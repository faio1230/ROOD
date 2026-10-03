#include <libomt.h>

#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <string>

int main(int argc, char** argv) {
    if (argc < 2 || argc > 3) {
        std::cerr << "usage: rood_omt_probe ADDRESS [SECONDS]\n";
        return 2;
    }
    const int seconds = argc == 3 ? std::atoi(argv[2]) : 15;
    if (seconds < 1 || seconds > 120) return 2;
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
        } else if (frame->Type == OMTFrameType_Audio && frame->Channels > 0 &&
                   frame->SamplesPerChannel > 0 && frame->SampleRate > 0 &&
                   frame->DataLength >= frame->Channels * frame->SamplesPerChannel * 4) {
            ++audioFrames;
            channels = frame->Channels;
            if (firstAudioTimestamp < 0) firstAudioTimestamp = frame->Timestamp;
            const auto* samples = static_cast<const float*>(frame->Data);
            for (int i = 0; i < frame->SamplesPerChannel; i += 16) {
                audioSampleSum += std::fabs(samples[i]);
                if (frame->Channels > 1)
                    secondChannelSampleSum += std::fabs(samples[frame->SamplesPerChannel + i]);
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
    return videoFrames > 0 && audioFrames > 0 && channels == 2 &&
           pixelSampleSum > 0 && audioSampleSum > 0 && secondChannelSampleSum > 0 &&
           firstVideoTimestamp > 0 && firstAudioTimestamp > 0 ? 0 : 1;
}
