#pragma once

#include "rood/ChannelRouter.hpp"

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

struct AVFrame;

namespace rood {

struct FrameInfo;

struct AudioOutputConfig {
    int deviceIndex = -1;
    int sampleRate = 48000;
    std::uint32_t channels = 2;
    int outputDelayMs = 250;
    bool wasapiExclusive = false;
    std::vector<ChannelRoute> routes;
};

struct AudioOutputStats {
    std::uint64_t callbackCount = 0;
    std::uint64_t deviceUnderflows = 0;
    std::uint64_t timestampRegressions = 0;
    std::uint64_t silentFrames = 0;
    std::uint64_t renderedFrames = 0;
    std::uint64_t rejectedFrames = 0;
    std::int64_t playheadFrames = 0;
    bool streamStarted = false;
    bool streamActive = false;
    bool callbackStalled = false;
    double driftCorrectionPpm = 0.0;
    double driftErrorMs = 0.0;
    bool driftLocked = false;
    double observedSampleRate = 0.0;
    bool sampleClockMismatch = false;
};

struct AudioDeviceInfo {
    int index = -1;
    std::string name;
    std::string hostApi;
    int maxOutputChannels = 0;
    double defaultSampleRate = 0.0;
};

std::vector<AudioDeviceInfo> listAudioOutputDevices();

// Decodes on the receiver thread; PortAudio only pulls ready interleaved PCM.
// All PortAudio API calls and resampling happen outside the audio callback.
class PortAudioOutput {
public:
    explicit PortAudioOutput(AudioOutputConfig config);
    ~PortAudioOutput();
    PortAudioOutput(const PortAudioOutput&) = delete;
    PortAudioOutput& operator=(const PortAudioOutput&) = delete;

    void pushFrame(const FrameInfo& info, const AVFrame& frame);
    void reset();
    AudioOutputStats stats() const;
    // Estimated source PTS currently consumed by the callback. This uses the
    // callback sample count and is not a measured physical DAC presentation time.
    std::optional<double> playbackMediaSeconds() const noexcept;

private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace rood
