#include "rood/MediaReceiver.hpp"
#ifdef ROOD_HAS_AUDIO_OUTPUT
#include "rood/PortAudioOutput.hpp"
#endif

#include <atomic>
#include <chrono>
#include <csignal>
#include <cstdlib>
#include <exception>
#include <iostream>
#include <map>
#include <memory>
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

#ifdef ROOD_HAS_AUDIO_OUTPUT
rood::ChannelRoute readRoute(const std::string& text) {
    std::string parts[4];
    std::size_t begin = 0;
    int count = 0;
    bool extraPart = false;
    while (count < 4) {
        const auto separator = text.find(':', begin);
        parts[count++] = text.substr(begin, separator - begin);
        if (separator == std::string::npos) break;
        if (count == 4) extraPart = true;
        begin = separator + 1;
    }
    if (count < 3 || extraPart ||
        (count == 4 && parts[3].empty()))
        throw std::invalid_argument("route format must be TRACK:SOURCE:OUTPUT[:GAIN]");
    rood::ChannelRoute route{};
    route.track_id = readNumber(parts[0].c_str(), 0, 1000000);
    route.source_channel = readNumber(parts[1].c_str(), 0, 255);
    route.device_channel = readNumber(parts[2].c_str(), 0, 255);
    if (count == 4) {
        std::size_t consumed = 0;
        route.gain = std::stof(parts[3], &consumed);
        if (consumed != parts[3].size()) throw std::invalid_argument("invalid route gain");
    }
    return route;
}
#endif

class ConsoleObserver final : public rood::MediaReceiverObserver {
public:
#ifdef ROOD_HAS_AUDIO_OUTPUT
    explicit ConsoleObserver(std::unique_ptr<rood::PortAudioOutput> output)
        : audioOutput(std::move(output)) {}
#endif
    void onState(const std::string& state) override {
        if (state == "connected") frameCounts.clear();
#ifdef ROOD_HAS_AUDIO_OUTPUT
        if (state == "connected") audioFailed = false;
        if (audioOutput && state == "disconnected") {
            const auto summary = audioOutput->stats();
            std::cout << "audio callbacks=" << summary.callbackCount
                      << " deviceUnderflows=" << summary.deviceUnderflows
                      << " timestampRegressions=" << summary.timestampRegressions
                      << " silentFrames=" << summary.silentFrames
                      << " renderedFrames=" << summary.renderedFrames
                      << " rejectedFrames=" << summary.rejectedFrames << std::endl;
            audioOutput->reset();
        }
#endif
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
    void onFrame(const rood::FrameInfo& info, const AVFrame& frame) override {
#ifdef ROOD_HAS_AUDIO_OUTPUT
        if (audioOutput && !audioFailed && info.kind == "audio") {
            try {
                audioOutput->pushFrame(info, frame);
            } catch (const std::exception& error) {
                onError(std::string("audio output: ") + error.what());
                audioFailed = true;
                audioOutput->reset();
            }
        }
#endif
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
#ifdef ROOD_HAS_AUDIO_OUTPUT
        if (audioOutput) {
            const auto audio = audioOutput->stats();
            std::cout << "audio callbacks=" << audio.callbackCount
                      << " deviceUnderflows=" << audio.deviceUnderflows
                      << " timestampRegressions=" << audio.timestampRegressions
                      << " silentFrames=" << audio.silentFrames
                      << " renderedFrames=" << audio.renderedFrames
                      << " rejectedFrames=" << audio.rejectedFrames
                      << " streamActive=" << audio.streamActive << std::endl;
        }
#endif
    }
    void onError(const std::string& error) override {
        std::cerr << "error " << error << std::endl;
    }
    int audioTracks = 0;
    int videoTracks = 0;
    int audioFrames = 0;
    int videoFrames = 0;
    std::map<int, int> frameCounts;
#ifdef ROOD_HAS_AUDIO_OUTPUT
    std::unique_ptr<rood::PortAudioOutput> audioOutput;
    bool audioFailed = false;
#endif
};

} // namespace

int main(int argc, char** argv) {
    try {
        rood::ReceiveConfig config;
        int seconds = 0;
        bool requireMedia = false;
#ifdef ROOD_HAS_AUDIO_OUTPUT
        rood::AudioOutputConfig audioConfig;
        bool audioRequested = false;
        bool audioOptionsSpecified = false;
#endif
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
#ifdef ROOD_HAS_AUDIO_OUTPUT
            else if (argument == "--audio-device" && i + 1 < argc) {
                audioConfig.deviceIndex = readNumber(argv[++i], 0, 10000);
                audioRequested = true;
            } else if (argument == "--audio-channels" && i + 1 < argc) {
                audioConfig.channels = readNumber(argv[++i], 1, 256);
                audioOptionsSpecified = true;
            } else if (argument == "--audio-rate" && i + 1 < argc) {
                audioConfig.sampleRate = readNumber(argv[++i], 8000, 384000);
                audioOptionsSpecified = true;
            } else if (argument == "--audio-delay" && i + 1 < argc) {
                audioConfig.outputDelayMs = readNumber(argv[++i], 0, 3000);
                audioOptionsSpecified = true;
            } else if (argument == "--wasapi-exclusive") {
                audioConfig.wasapiExclusive = true;
                audioOptionsSpecified = true;
            } else if (argument == "--route" && i + 1 < argc) {
                audioConfig.routes.push_back(readRoute(argv[++i]));
                audioOptionsSpecified = true;
            }
#endif
            else {
                std::cerr << "usage: rood_ingest [--port N] [--latency MS]"
                             " [--seconds N] [--require-media]"
#ifdef ROOD_HAS_AUDIO_OUTPUT
                             " [--audio-device INDEX --audio-channels N --audio-rate HZ"
                             " --audio-delay MS --route TRACK:SOURCE:OUTPUT[:GAIN]"
                             " [--route ...] [--wasapi-exclusive]]"
#endif
                             "\n";
                return 2;
            }
        }
        std::signal(SIGINT, onSignal);
#ifdef ROOD_HAS_AUDIO_OUTPUT
        if (audioOptionsSpecified && !audioRequested)
            throw std::invalid_argument("audio options require --audio-device");
        std::unique_ptr<rood::PortAudioOutput> audioOutput;
        if (audioRequested) audioOutput = std::make_unique<rood::PortAudioOutput>(std::move(audioConfig));
        ConsoleObserver observer(std::move(audioOutput));
#else
        ConsoleObserver observer;
#endif
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
