#include "rood/MediaReceiver.hpp"

#include <srt/srt.h>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/channel_layout.h>
#include <libavutil/error.h>
#include <libavutil/mem.h>
}

#ifdef _WIN32
#include <winsock2.h>
#else
#include <arpa/inet.h>
#include <netinet/in.h>
#endif

#include <chrono>
#include <cerrno>
#include <memory>
#include <new>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

namespace rood {
namespace {

using Clock = std::chrono::steady_clock;

std::string ffmpegError(int code) {
    char buffer[AV_ERROR_MAX_STRING_SIZE] = {};
    av_strerror(code, buffer, sizeof(buffer));
    return buffer;
}

void checkSrt(int result, const char* action) {
    if (result == SRT_ERROR) {
        throw std::runtime_error(std::string(action) + ": " + srt_getlasterror_str());
    }
}

class SrtRuntime {
public:
    SrtRuntime() { checkSrt(srt_startup(), "srt_startup"); }
    ~SrtRuntime() { srt_cleanup(); }
    SrtRuntime(const SrtRuntime&) = delete;
    SrtRuntime& operator=(const SrtRuntime&) = delete;
};

class Socket {
public:
    explicit Socket(SRTSOCKET value = SRT_INVALID_SOCK) : value_(value) {}
    ~Socket() { if (value_ != SRT_INVALID_SOCK) srt_close(value_); }
    Socket(const Socket&) = delete;
    Socket& operator=(const Socket&) = delete;
    SRTSOCKET get() const { return value_; }
private:
    SRTSOCKET value_;
};

class Poller {
public:
    Poller() : id_(srt_epoll_create()) { checkSrt(id_, "srt_epoll_create"); }
    ~Poller() { srt_epoll_release(id_); }
    Poller(const Poller&) = delete;
    Poller& operator=(const Poller&) = delete;
    int get() const { return id_; }
private:
    int id_;
};

struct ReadContext {
    SRTSOCKET socket;
    std::atomic_bool& stop;
    MediaReceiverObserver& observer;
    Clock::time_point nextStats = Clock::now();
    bool disconnected = false;

    void reportStats() {
        const auto now = Clock::now();
        if (now < nextStats) return;
        nextStats = now + std::chrono::seconds(1);
        SRT_TRACEBSTATS raw = {};
        if (srt_bstats(socket, &raw, 1) == SRT_ERROR) return;
        ConnectionStats stats;
        stats.receiveMbps = raw.mbpsRecvRate;
        stats.rttMs = raw.msRTT;
        stats.receivedBytes = static_cast<std::int64_t>(raw.byteRecvTotal);
        stats.lostPackets = raw.pktRcvLossTotal;
        stats.retransmittedPacketsInInterval = raw.pktRcvRetrans;
        stats.receiveBufferBytes = raw.byteRcvBuf;
        stats.receiveBufferMs = raw.msRcvBuf;
        observer.onStats(stats);
    }
};

int readSrt(void* opaque, std::uint8_t* buffer, int size) {
    auto& context = *static_cast<ReadContext*>(opaque);
    while (!context.stop.load()) {
        context.reportStats();
        // Live SRT preserves message boundaries. One AVIO read must be large
        // enough for a whole live payload, even when FFmpeg requests less.
        if (size < SRT_LIVE_MAX_PLSIZE) return AVERROR(EINVAL);
        const int count = srt_recvmsg(context.socket, reinterpret_cast<char*>(buffer), size);
        if (count > 0) return count;
        if (count == 0) {
            context.disconnected = true;
            return AVERROR_EOF;
        }
        const int error = srt_getlasterror(nullptr);
        if (error == SRT_ETIMEOUT || error == SRT_EASYNCRCV) continue;
        context.disconnected = true;
        return AVERROR_EOF;
    }
    return AVERROR_EXIT;
}

struct AvioDeleter {
    void operator()(AVIOContext* context) const { avio_context_free(&context); }
};
struct FormatDeleter {
    void operator()(AVFormatContext* context) const { avformat_close_input(&context); }
};
struct CodecDeleter {
    void operator()(AVCodecContext* context) const { avcodec_free_context(&context); }
};
struct PacketDeleter {
    void operator()(AVPacket* packet) const { av_packet_free(&packet); }
};
struct FrameDeleter {
    void operator()(AVFrame* frame) const { av_frame_free(&frame); }
};

using AvioPtr = std::unique_ptr<AVIOContext, AvioDeleter>;
using FormatPtr = std::unique_ptr<AVFormatContext, FormatDeleter>;
using CodecPtr = std::unique_ptr<AVCodecContext, CodecDeleter>;
using PacketPtr = std::unique_ptr<AVPacket, PacketDeleter>;
using FramePtr = std::unique_ptr<AVFrame, FrameDeleter>;

std::string layoutName(const AVChannelLayout& layout) {
    char text[128] = {};
    return av_channel_layout_describe(&layout, text, sizeof(text)) >= 0 ? text : "unknown";
}

std::string mediaKind(AVMediaType type) {
    if (type == AVMEDIA_TYPE_AUDIO) return "audio";
    if (type == AVMEDIA_TYPE_VIDEO) return "video";
    return "other";
}

void drainDecoder(AVCodecContext* decoder, AVFrame* frame, const AVStream* stream,
                  MediaReceiverObserver& observer) {
    while (true) {
        const int result = avcodec_receive_frame(decoder, frame);
        if (result == AVERROR(EAGAIN) || result == AVERROR_EOF) return;
        if (result < 0) throw std::runtime_error("avcodec_receive_frame: " + ffmpegError(result));

        FrameInfo info;
        info.streamIndex = stream->index;
        info.streamId = stream->id;
        info.kind = mediaKind(stream->codecpar->codec_type);
        info.pts = frame->best_effort_timestamp;
        info.hasPts = info.pts != AV_NOPTS_VALUE;
        info.timeBaseNum = stream->time_base.num;
        info.timeBaseDen = stream->time_base.den;
        info.channels = frame->ch_layout.nb_channels;
        info.sampleRate = frame->sample_rate;
        info.width = frame->width;
        info.height = frame->height;
        observer.onFrame(info, *frame);
        av_frame_unref(frame);
    }
}

void receiveSession(SRTSOCKET socket, std::atomic_bool& stop, MediaReceiverObserver& observer) {
    const int timeoutMs = 250;
    checkSrt(srt_setsockflag(socket, SRTO_RCVTIMEO, &timeoutMs, sizeof(timeoutMs)), "SRTO_RCVTIMEO");
    ReadContext read{socket, stop, observer};
    auto* buffer = static_cast<unsigned char*>(av_malloc(64 * 1024));
    if (!buffer) throw std::bad_alloc();
    AvioPtr io(avio_alloc_context(buffer, 64 * 1024, 0, &read, &readSrt, nullptr, nullptr));
    if (!io) {
        av_free(buffer);
        throw std::bad_alloc();
    }
    FormatPtr format(avformat_alloc_context());
    if (!format) throw std::bad_alloc();
    format->pb = io.get();
    format->flags |= AVFMT_FLAG_CUSTOM_IO;
    AVFormatContext* rawFormat = format.release();
    const int opened = avformat_open_input(&rawFormat, nullptr, nullptr, nullptr);
    format.reset(rawFormat);
    if (opened < 0) {
        if (stop.load() || read.disconnected) return;
        throw std::runtime_error("avformat_open_input: " + ffmpegError(opened));
    }
    const int probed = avformat_find_stream_info(format.get(), nullptr);
    if (probed < 0) {
        if (stop.load() || read.disconnected) return;
        throw std::runtime_error("avformat_find_stream_info: " + ffmpegError(probed));
    }

    std::vector<CodecPtr> decoders(format->nb_streams);
    for (unsigned i = 0; i < format->nb_streams; ++i) {
        AVStream* stream = format->streams[i];
        const AVCodecParameters* parameters = stream->codecpar;
        TrackInfo track;
        track.streamIndex = stream->index;
        track.streamId = stream->id;
        track.kind = mediaKind(parameters->codec_type);
        track.codec = avcodec_get_name(parameters->codec_id);
        track.channelLayout = parameters->codec_type == AVMEDIA_TYPE_AUDIO
                                  ? layoutName(parameters->ch_layout) : "";
        track.channels = parameters->ch_layout.nb_channels;
        track.sampleRate = parameters->sample_rate;
        track.width = parameters->width;
        track.height = parameters->height;
        track.timeBaseNum = stream->time_base.num;
        track.timeBaseDen = stream->time_base.den;
        observer.onTrack(track);

        if (parameters->codec_type != AVMEDIA_TYPE_AUDIO &&
            parameters->codec_type != AVMEDIA_TYPE_VIDEO) continue;
        const AVCodec* codec = avcodec_find_decoder(parameters->codec_id);
        if (!codec) {
            observer.onError("No decoder for stream " + std::to_string(i) + " (" + track.codec + ")");
            continue;
        }
        CodecPtr decoder(avcodec_alloc_context3(codec));
        if (!decoder) throw std::bad_alloc();
        int result = avcodec_parameters_to_context(decoder.get(), parameters);
        if (result < 0) throw std::runtime_error("avcodec_parameters_to_context: " + ffmpegError(result));
        decoder->pkt_timebase = stream->time_base;
        result = avcodec_open2(decoder.get(), codec, nullptr);
        if (result < 0) {
            observer.onError("Cannot open decoder for stream " + std::to_string(i) + ": " + ffmpegError(result));
            continue;
        }
        decoders[i] = std::move(decoder);
    }

    PacketPtr packet(av_packet_alloc());
    FramePtr frame(av_frame_alloc());
    if (!packet || !frame) throw std::bad_alloc();
    while (!stop.load()) {
        const int result = av_read_frame(format.get(), packet.get());
        if (result < 0) {
            if (result != AVERROR_EOF && result != AVERROR_EXIT && !read.disconnected && !stop.load())
                observer.onError("av_read_frame: " + ffmpegError(result));
            break;
        }
        const int index = packet->stream_index;
        if (index >= 0 && static_cast<std::size_t>(index) < decoders.size() && decoders[index]) {
            AVCodecContext* decoder = decoders[index].get();
            int sent = avcodec_send_packet(decoder, packet.get());
            if (sent == AVERROR(EAGAIN)) {
                drainDecoder(decoder, frame.get(), format->streams[index], observer);
                sent = avcodec_send_packet(decoder, packet.get());
            }
            if (sent < 0) {
                observer.onError("avcodec_send_packet stream " + std::to_string(index) + ": " + ffmpegError(sent));
            } else {
                drainDecoder(decoder, frame.get(), format->streams[index], observer);
            }
        }
        av_packet_unref(packet.get());
    }
    for (unsigned i = 0; i < decoders.size(); ++i) {
        if (!decoders[i]) continue;
        avcodec_send_packet(decoders[i].get(), nullptr);
        drainDecoder(decoders[i].get(), frame.get(), format->streams[i], observer);
    }
}

} // namespace

void runMediaReceiver(const ReceiveConfig& config, std::atomic_bool& stop,
                      MediaReceiverObserver& observer) {
    if (config.port == 0 || config.srtLatencyMs < 20 || config.srtLatencyMs > 8000)
        throw std::invalid_argument("port must be nonzero and SRT latency must be 20..8000 ms");
    SrtRuntime runtime;
    Socket listener(srt_create_socket());
    if (listener.get() == SRT_INVALID_SOCK)
        throw std::runtime_error(std::string("srt_create_socket: ") + srt_getlasterror_str());
    checkSrt(srt_setsockflag(listener.get(), SRTO_RCVLATENCY, &config.srtLatencyMs,
                             sizeof(config.srtLatencyMs)), "SRTO_RCVLATENCY");
    sockaddr_in address = {};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_ANY);
    address.sin_port = htons(config.port);
    checkSrt(srt_bind(listener.get(), reinterpret_cast<const sockaddr*>(&address), sizeof(address)), "srt_bind");
    checkSrt(srt_listen(listener.get(), 1), "srt_listen");
    Poller poller;
    const int events = SRT_EPOLL_IN;
    checkSrt(srt_epoll_add_usock(poller.get(), listener.get(), &events), "srt_epoll_add_usock");
    observer.onState("listening");
    while (!stop.load()) {
        SRTSOCKET ready = SRT_INVALID_SOCK;
        int count = 1;
        const int result = srt_epoll_wait(poller.get(), &ready, &count, nullptr, nullptr,
                                          200, nullptr, nullptr, nullptr, nullptr);
        if (result == SRT_ERROR) {
            if (srt_getlasterror(nullptr) == SRT_ETIMEOUT) continue;
            throw std::runtime_error(std::string("srt_epoll_wait: ") + srt_getlasterror_str());
        }
        if (result == 0 || count == 0) continue;
        {
            Socket connection(srt_accept(listener.get(), nullptr, nullptr));
            if (connection.get() == SRT_INVALID_SOCK) {
                observer.onError(std::string("srt_accept: ") + srt_getlasterror_str());
                continue;
            }
            observer.onState("connected");
            try {
                receiveSession(connection.get(), stop, observer);
            } catch (const std::exception& error) {
                observer.onError(error.what());
            }
        }
        observer.onState("disconnected");
        if (!stop.load()) observer.onState("listening");
    }
    observer.onState("stopped");
}

} // namespace rood
