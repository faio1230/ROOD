#pragma once

#include "rood/PortAudioOutput.hpp"

#include <cstdint>
#include <memory>
#include <optional>
#include <string>

struct AVFrame;

namespace rood {

struct FrameInfo;

struct RecoveringAudioOutputStats : AudioOutputStats {
    bool deviceAvailable = false;
    std::uint64_t reopenAttempts = 0;
    std::uint64_t recoveries = 0;
    std::uint64_t streamFailures = 0;
    std::string lastError;
};

// Keeps one selected output device by host API and name. When a stream fails,
// decoded input continues while this adapter retries device enumeration and
// stream creation. The object itself remains stable for video clock readers.
class RecoveringAudioOutput {
public:
    explicit RecoveringAudioOutput(AudioOutputConfig config);
    ~RecoveringAudioOutput();
    RecoveringAudioOutput(const RecoveringAudioOutput&) = delete;
    RecoveringAudioOutput& operator=(const RecoveringAudioOutput&) = delete;

    void pushFrame(const FrameInfo& info, const AVFrame& frame);
    // Call periodically even when no mapped audio frames arrive. Checks the
    // active stream and retries a missing device without waiting for audio.
    void poll();
    void reset();
    RecoveringAudioOutputStats stats() const;
    std::optional<double> playbackMediaSeconds() const noexcept;

private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace rood
