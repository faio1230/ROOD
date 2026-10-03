#include "rood/MediaReceiver.hpp"

#include <atomic>
#include <chrono>
#include <csignal>
#include <cstdlib>
#include <exception>
#include <iostream>
#include <map>
#include <stdexcept>
#include <string>
#include <thread>

namespace {

std::atomic_bool stopped{false};

void onSignal(int) { stopped.store(true); }

int readNumber(const char* text, int min, int max) {
    const std::string value(text);
    std::size_t parsed = 0;
    const int result = std::stoi(value, &parsed);
    if (parsed != value.size() || result < min || result > max)
        throw std::invalid_argument("invalid number: " + value);
    return result;
}

class ConsoleObserver final : public rood::MediaReceiverObserver {
public:
    void onState(const std::string& state) override {
        if (state == "connected") frameCounts.clear();
        std::cout << "state " << state << std::endl;
    }
    void onTrack(const rood::TrackInfo& track) override {
        std::cout << "track index=" << track.streamIndex << " id=" << track.streamId
                  << " kind=" << track.kind << " codec=" << track.codec;
        if (track.kind == "audio") {
            ++audioTracks;
            std::cout << " channels=" << track.channels << " layout=" << track.channelLayout
                      << " rate=" << track.sampleRate;
        }
        if (track.kind == "video") {
            ++videoTracks;
            std::cout << " size=" << track.width << 'x' << track.height;
        }
        std::cout << " timebase=" << track.timeBaseNum << '/' << track.timeBaseDen << std::endl;
    }
    void onFrame(const rood::FrameInfo& info, const AVFrame&) override {
        auto& count = frameCounts[info.streamIndex];
        ++count;
        if (info.kind == "audio") ++audioFrames;
        if (info.kind == "video") ++videoFrames;
        if (count <= 2) {
            std::cout << "frame index=" << info.streamIndex << " id=" << info.streamId
                      << " kind=" << info.kind << " pts=";
            if (info.hasPts) std::cout << info.pts << " (" << info.timeBaseNum << '/'
                                       << info.timeBaseDen << ')';
            else std::cout << "unknown";
            if (info.kind == "audio")
                std::cout << " channels=" << info.channels << " rate=" << info.sampleRate;
            std::cout << std::endl;
        }
    }
    void onStats(const rood::ConnectionStats& stats) override {
        std::cout << "stats Mbps=" << stats.receiveMbps << " RTTms=" << stats.rttMs
                  << " bytes=" << stats.receivedBytes << " loss=" << stats.lostPackets
                  << " retransInterval=" << stats.retransmittedPacketsInInterval
                  << " receiveBufferBytes=" << stats.receiveBufferBytes
                  << " receiveBufferMs=" << stats.receiveBufferMs << std::endl;
    }
    void onError(const std::string& error) override {
        std::cerr << "error " << error << std::endl;
    }
    int audioTracks = 0;
    int videoTracks = 0;
    int audioFrames = 0;
    int videoFrames = 0;
    std::map<int, int> frameCounts;
};

} // namespace

int main(int argc, char** argv) {
    try {
        rood::ReceiveConfig config;
        int seconds = 0;
        bool requireMedia = false;
        for (int i = 1; i < argc; ++i) {
            const std::string argument(argv[i]);
            if (argument == "--port" && i + 1 < argc)
                config.port = static_cast<std::uint16_t>(readNumber(argv[++i], 1, 65535));
            else if (argument == "--latency" && i + 1 < argc)
                config.srtLatencyMs = readNumber(argv[++i], 20, 8000);
            else if (argument == "--seconds" && i + 1 < argc)
                seconds = readNumber(argv[++i], 1, 86400);
            else if (argument == "--require-media")
                requireMedia = true;
            else {
                std::cerr << "usage: rood_ingest [--port N] [--latency MS]"
                             " [--seconds N] [--require-media]\n";
                return 2;
            }
        }
        std::signal(SIGINT, onSignal);
        ConsoleObserver observer;
        std::thread timer;
        if (seconds > 0) {
            timer = std::thread([seconds] {
                const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(seconds);
                while (!stopped.load() && std::chrono::steady_clock::now() < deadline)
                    std::this_thread::sleep_for(std::chrono::milliseconds(50));
                stopped.store(true);
            });
        }
        try {
            rood::runMediaReceiver(config, stopped, observer);
        } catch (...) {
            stopped.store(true);
            if (timer.joinable()) timer.join();
            throw;
        }
        if (timer.joinable()) timer.join();
        std::cout << "summary audioTracks=" << observer.audioTracks
                  << " videoTracks=" << observer.videoTracks
                  << " audioFrames=" << observer.audioFrames
                  << " videoFrames=" << observer.videoFrames << std::endl;
        if (requireMedia && (observer.audioFrames == 0 || observer.videoFrames == 0)) return 1;
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "fatal " << error.what() << std::endl;
        return 1;
    }
}
