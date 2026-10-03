#pragma once

#include "rood/ChannelRouter.hpp"

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

struct AVFrame;

namespace rood {

struct FrameInfo;

struct OmtOutputConfig {
    std::string name = "ROOD";
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
    int connections = 0;
};

// Preserves decoded media PTS in OMT's 100 ns timestamp unit. Video and
// routed 20 ms planar audio packets share a host-clock presentation schedule.
class OmtOutput {
public:
    explicit OmtOutput(OmtOutputConfig config);
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
