#pragma once

#include <atomic>
#include <cstdint>
#include <string>

struct AVFrame;

namespace rood {

struct ReceiveConfig {
    std::uint16_t port = 9000;
    int srtLatencyMs = 120;
};

struct TrackInfo {
    int streamIndex = -1;
    int streamId = -1;
    std::string kind;
    std::string codec;
    std::string channelLayout;
    int channels = 0;
    int sampleRate = 0;
    int width = 0;
    int height = 0;
    int frameRateNum = 0;
    int frameRateDen = 1;
    int timeBaseNum = 0;
    int timeBaseDen = 1;
};

struct FrameInfo {
    int streamIndex = -1;
    int streamId = -1;
    std::string kind;
    std::int64_t pts = 0;
    bool hasPts = false;
    int timeBaseNum = 0;
    int timeBaseDen = 1;
    int channels = 0;
    int sampleRate = 0;
    int width = 0;
    int height = 0;
};

struct ConnectionStats {
    double receiveMbps = 0.0;
    double rttMs = 0.0;
    std::int64_t receivedBytes = 0;
    int lostPackets = 0;
    int retransmittedPacketsInInterval = 0;
    int receiveBufferBytes = 0;
    int receiveBufferMs = 0;
};

// Callbacks run on the receiver thread and must not throw. The frame is valid only during onFrame;
// clients that retain it must make their own av_frame_ref/clone.
class MediaReceiverObserver {
public:
    virtual ~MediaReceiverObserver() = default;
    virtual void onState(const std::string& state) = 0;
    virtual void onTrack(const TrackInfo& track) = 0;
    virtual void onFrame(const FrameInfo& info, const AVFrame& frame) = 0;
    virtual void onStats(const ConnectionStats& stats) = 0;
    virtual void onError(const std::string& error) = 0;
};

// Receives one MPEG-TS source at a time. After a disconnect, listens again.
// stop is checked during accept and receive; no SRT transmission is provided.
void runMediaReceiver(const ReceiveConfig& config, std::atomic_bool& stop,
                      MediaReceiverObserver& observer);

} // namespace rood
