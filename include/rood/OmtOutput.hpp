#pragma once

#include "rood/ChannelRouter.hpp"

#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

struct AVFrame;

namespace rood {

struct FrameInfo;

struct OmtOutputConfig {
    std::string name = "ROOD";
    // Relative to input arrival in host-clock mode. With an audio device,
    // the difference from its configured delay offsets OMT's schedule.
    int outputDelayMs = 250;
    int audioSampleRate = 48000;
    std::uint32_t audioChannels = 2;
    int videoFrameRateNumerator = 25;
    int videoFrameRateDenominator = 1;
    std::vector<ChannelRoute> audioRoutes;
};

struct OmtOutputStats {
    std::uint64_t videoFrames = 0;
    std::uint64_t audioPackets = 0;
    std::uint64_t droppedVideoFrames = 0;
    std::uint64_t rejectedAudioFrames = 0;
    std::uint64_t audioClockVideoFrames = 0;
    std::uint64_t audioClockAudioPackets = 0;
    // Software error at omt_send, relative to the PortAudio media clock.
    // These values do not measure a receiver display or physical DAC.
    double maxAudioClockVideoErrorMs = 0.0;
    double maxAudioClockAudioErrorMs = 0.0;
    int connections = 0;
};

// Preserves decoded media PTS in OMT's 100 ns timestamp unit. Video and
// routed 20 ms planar audio packets share a presentation schedule. With a
// PortAudio media clock, OMT follows that clock; otherwise it uses host time.
class OmtOutput {
public:
    using AudioMediaClock = std::function<std::optional<double>()>;
    explicit OmtOutput(OmtOutputConfig config);
    OmtOutput(OmtOutputConfig config, AudioMediaClock audioMediaClock,
              int audioOutputDelayMs);
    ~OmtOutput();
    OmtOutput(const OmtOutput&) = delete;
    OmtOutput& operator=(const OmtOutput&) = delete;

    void pushFrame(const FrameInfo& info, const AVFrame& frame);
    void setVideoFrameRate(int numerator, int denominator);
    void reset();
    OmtOutputStats stats() const;
    std::string address() const;

private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace rood
