#include "rood/MediaReceiver.hpp"
#ifdef ROOD_HAS_AUDIO_OUTPUT
#include "rood/RecoveringAudioOutput.hpp"
#endif
#ifdef ROOD_HAS_SPOUT_OUTPUT
#include "rood/SpoutVideoOutput.hpp"
#endif
#ifdef ROOD_HAS_OMT_OUTPUT
#include "rood/OmtOutput.hpp"
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

#if defined(ROOD_HAS_AUDIO_OUTPUT) || defined(ROOD_HAS_OMT_OUTPUT)
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
    void onState(const std::string& state) override {
        if (state == "connected") frameCounts.clear();
#ifdef ROOD_HAS_OMT_OUTPUT
        if (state == "connected") omtFailed = false;
        if (omtOutput && state == "disconnected") {
            const auto summary = omtOutput->stats();
            std::cout << "omt video=" << summary.videoFrames
                      << " audioPackets=" << summary.audioPackets
                      << " droppedVideo=" << summary.droppedVideoFrames
                      << " rejectedAudio=" << summary.rejectedAudioFrames
                      << " connections=" << summary.connections << std::endl;
            omtOutput->reset();
        }
#endif
#ifdef ROOD_HAS_SPOUT_OUTPUT
        if (state == "connected") spoutFailed = false;
        if (spoutOutput && state == "disconnected") {
            const auto video = spoutOutput->stats();
            std::cout << "spout received=" << video.receivedFrames
                      << " sent=" << video.sentFrames
                      << " dropped=" << video.droppedFrames
                      << " failed=" << video.failedFrames << std::endl;
            spoutOutput->reset();
        }
#endif
#ifdef ROOD_HAS_AUDIO_OUTPUT
        if (audioOutput && state == "disconnected") {
            const auto summary = audioOutput->stats();
            std::cout << "audio callbacks=" << summary.callbackCount
                      << " deviceUnderflows=" << summary.deviceUnderflows
                      << " timestampRegressions=" << summary.timestampRegressions
                      << " silentFrames=" << summary.silentFrames
                      << " renderedFrames=" << summary.renderedFrames
                      << " rejectedFrames=" << summary.rejectedFrames
                      << " recoveries=" << summary.recoveries << std::endl;
            audioOutput->reset();
        }
#endif
        std::cout << "state " << state << std::endl;
    }
    void onTrack(const rood::TrackInfo& track) override {
#ifdef ROOD_HAS_OMT_OUTPUT
        if (omtOutput && track.kind == "video")
            omtOutput->setVideoFrameRate(track.frameRateNum, track.frameRateDen);
#endif
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
#ifdef ROOD_HAS_OMT_OUTPUT
        if (omtOutput && !omtFailed) {
            try {
                omtOutput->pushFrame(info, frame);
            } catch (const std::exception& error) {
                onError(std::string("OMT output: ") + error.what());
                omtFailed = true;
                omtOutput->reset();
            }
        }
#endif
#ifdef ROOD_HAS_SPOUT_OUTPUT
        if (spoutOutput && !spoutFailed && info.kind == "video") {
            try {
                spoutOutput->pushFrame(info, frame);
            } catch (const std::exception& error) {
                onError(std::string("Spout output: ") + error.what());
                spoutFailed = true;
                spoutOutput->reset();
            }
        }
#endif
#ifdef ROOD_HAS_AUDIO_OUTPUT
        if (audioOutput && info.kind == "audio") {
            try {
                audioOutput->pushFrame(info, frame);
            } catch (const std::exception& error) {
                onError(std::string("audio output: ") + error.what());
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
#ifdef ROOD_HAS_OMT_OUTPUT
        if (omtOutput) {
            const auto omt = omtOutput->stats();
            std::cout << "omt video=" << omt.videoFrames
                      << " audioPackets=" << omt.audioPackets
                      << " droppedVideo=" << omt.droppedVideoFrames
                      << " rejectedAudio=" << omt.rejectedAudioFrames
                      << " connections=" << omt.connections << std::endl;
        }
#endif
#ifdef ROOD_HAS_SPOUT_OUTPUT
        if (spoutOutput) {
            const auto video = spoutOutput->stats();
            std::cout << "spout received=" << video.receivedFrames
                      << " sent=" << video.sentFrames
                      << " dropped=" << video.droppedFrames
                      << " failed=" << video.failedFrames
                      << " audioSyncErrorMs=";
            if (video.hasAudioSyncError) std::cout << video.lastAudioSyncErrorMs;
            else std::cout << "unknown";
            std::cout << " ready=" << video.senderReady << std::endl;
            if (!video.lastError.empty()) onError(video.lastError);
        }
#endif
#ifdef ROOD_HAS_AUDIO_OUTPUT
        if (audioOutput) {
            const auto audio = audioOutput->stats();
            std::cout << "audio callbacks=" << audio.callbackCount
                      << " deviceUnderflows=" << audio.deviceUnderflows
                      << " timestampRegressions=" << audio.timestampRegressions
                      << " silentFrames=" << audio.silentFrames
                      << " renderedFrames=" << audio.renderedFrames
                      << " rejectedFrames=" << audio.rejectedFrames
                      << " streamActive=" << audio.streamActive
                      << " driftLocked=" << audio.driftLocked
                      << " driftPpm=" << audio.driftCorrectionPpm
                      << " driftErrorMs=" << audio.driftErrorMs
                      << " deviceAvailable=" << audio.deviceAvailable
                      << " reopenAttempts=" << audio.reopenAttempts
                      << " recoveries=" << audio.recoveries << std::endl;
            if (!audio.lastError.empty())
                std::cout << "audioRecoveryError=" << audio.lastError << std::endl;
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
    std::unique_ptr<rood::RecoveringAudioOutput> audioOutput;
#endif
#ifdef ROOD_HAS_SPOUT_OUTPUT
    std::unique_ptr<rood::SpoutVideoOutput> spoutOutput;
    bool spoutFailed = false;
#endif
#ifdef ROOD_HAS_OMT_OUTPUT
    std::unique_ptr<rood::OmtOutput> omtOutput;
    bool omtFailed = false;
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
#ifdef ROOD_HAS_SPOUT_OUTPUT
        rood::SpoutVideoConfig spoutConfig;
        bool spoutRequested = false;
        bool videoOptionsSpecified = false;
#endif
#ifdef ROOD_HAS_OMT_OUTPUT
        rood::OmtOutputConfig omtConfig;
        bool omtRequested = false;
        bool omtOptionsSpecified = false;
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
#ifdef ROOD_HAS_SPOUT_OUTPUT
            else if (argument == "--spout" && i + 1 < argc) {
                spoutConfig.senderName = argv[++i];
                spoutRequested = true;
            } else if (argument == "--video-delay" && i + 1 < argc) {
                spoutConfig.outputDelayMs = readNumber(argv[++i], 0, 5000);
                videoOptionsSpecified = true;
            } else if (argument == "--video-offset" && i + 1 < argc) {
                spoutConfig.videoOffsetMs = readNumber(argv[++i], -5000, 5000);
                videoOptionsSpecified = true;
            } else if (argument == "--video-late-drop" && i + 1 < argc) {
                spoutConfig.lateDropMs = readNumber(argv[++i], 0, 2000);
                videoOptionsSpecified = true;
            }
#endif
#ifdef ROOD_HAS_OMT_OUTPUT
            else if (argument == "--omt" && i + 1 < argc) {
                omtConfig.name = argv[++i];
                omtRequested = true;
            } else if (argument == "--omt-channels" && i + 1 < argc) {
                omtConfig.audioChannels = readNumber(argv[++i], 1, 32);
                omtOptionsSpecified = true;
            } else if (argument == "--omt-rate" && i + 1 < argc) {
                omtConfig.audioSampleRate = readNumber(argv[++i], 8000, 192000);
                omtOptionsSpecified = true;
            } else if (argument == "--omt-delay" && i + 1 < argc) {
                omtConfig.outputDelayMs = readNumber(argv[++i], 0, 5000);
                omtOptionsSpecified = true;
            } else if (argument == "--omt-route" && i + 1 < argc) {
                omtConfig.audioRoutes.push_back(readRoute(argv[++i]));
                omtOptionsSpecified = true;
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
#ifdef ROOD_HAS_SPOUT_OUTPUT
                             " [--spout NAME --video-delay MS --video-offset MS"
                             " --video-late-drop MS]"
#endif
#ifdef ROOD_HAS_OMT_OUTPUT
                             " [--omt NAME --omt-channels N --omt-rate HZ --omt-delay MS"
                             " --omt-route TRACK:SOURCE:OUTPUT[:GAIN] [--omt-route ...]]"
#endif
                             "\n";
                return 2;
            }
        }
        std::signal(SIGINT, onSignal);
#ifdef ROOD_HAS_SPOUT_OUTPUT
        if (videoOptionsSpecified && !spoutRequested)
            throw std::invalid_argument("video options require --spout");
#endif
#ifdef ROOD_HAS_OMT_OUTPUT
        if (omtOptionsSpecified && !omtRequested)
            throw std::invalid_argument("OMT options require --omt");
        if (omtRequested && omtConfig.audioRoutes.empty())
            throw std::invalid_argument("--omt requires at least one --omt-route");
#endif
#ifdef ROOD_HAS_AUDIO_OUTPUT
        if (audioOptionsSpecified && !audioRequested)
            throw std::invalid_argument("audio options require --audio-device");
        std::unique_ptr<rood::RecoveringAudioOutput> audioOutput;
        if (audioRequested) audioOutput = std::make_unique<rood::RecoveringAudioOutput>(std::move(audioConfig));
#endif
        ConsoleObserver observer;
#ifdef ROOD_HAS_AUDIO_OUTPUT
        observer.audioOutput = std::move(audioOutput);
#endif
#ifdef ROOD_HAS_SPOUT_OUTPUT
        if (spoutRequested) {
            rood::SpoutVideoOutput::AudioMediaClock mediaClock;
#ifdef ROOD_HAS_AUDIO_OUTPUT
            if (observer.audioOutput) {
                auto* output = observer.audioOutput.get();
                mediaClock = [output] { return output->playbackMediaSeconds(); };
            }
#endif
            observer.spoutOutput = std::make_unique<rood::SpoutVideoOutput>(
                std::move(spoutConfig), std::move(mediaClock));
        }
#endif
#ifdef ROOD_HAS_OMT_OUTPUT
        if (omtRequested) {
            observer.omtOutput = std::make_unique<rood::OmtOutput>(std::move(omtConfig));
            std::cout << "omt address=" << observer.omtOutput->address() << std::endl;
        }
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
