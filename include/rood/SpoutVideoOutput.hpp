#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>

struct AVFrame;

namespace rood {

struct FrameInfo;

struct SpoutVideoConfig {
    std::string senderName = "ROOD";
    int outputDelayMs = 250;
    int videoOffsetMs = 0;
    int lateDropMs = 120;
};

struct SpoutVideoStats {
    std::uint64_t receivedFrames = 0;
    std::uint64_t sentFrames = 0;
    std::uint64_t droppedFrames = 0;
    std::uint64_t failedFrames = 0;
    double lastAudioSyncErrorMs = 0.0;
    bool hasAudioSyncError = false;
    bool senderReady = false;
    std::string lastError;
};

// FFmpeg conversion runs on the decode thread. The bounded sender queue and
// OpenGL context live on a separate worker so Spout cannot block SRT receive.
// audioMediaClock should return the source PTS currently consumed by the audio
// callback; nullopt selects a monotonic host-clock schedule.
class SpoutVideoOutput {
public:
    using AudioMediaClock = std::function<std::optional<double>()>;
    SpoutVideoOutput(SpoutVideoConfig config, AudioMediaClock audioMediaClock = {});
    ~SpoutVideoOutput();
    SpoutVideoOutput(const SpoutVideoOutput&) = delete;
    SpoutVideoOutput& operator=(const SpoutVideoOutput&) = delete;

    void pushFrame(const FrameInfo& info, const AVFrame& frame);
    void reset();
    SpoutVideoStats stats() const;

private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace rood
