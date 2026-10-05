#include <QApplication>
#include <QComboBox>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QProgressBar>
#include <QSettings>
#include <QSpinBox>
#include <QTemporaryDir>

#include <iostream>

extern "C" {
#include <libavutil/frame.h>
}

#include <cmath>
#include <cstdint>

// Exercise the actual window and its QSettings wiring in one process, without
// starting an SRT session or using the operator's settings file.
#include "../apps/gui/MediaWindow.cpp"

int main(int argc, char* argv[]) {
    QApplication app(argc, argv);
    QTemporaryDir directory;
    if (!directory.isValid()) return 1;
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, directory.path());

    {
        QSettings settings(QSettings::IniFormat, QSettings::UserScope,
                           QStringLiteral("Studio Sandix"), QStringLiteral("ROOD"));
        settings.setFallbacksEnabled(false);
        settings.setValue(QStringLiteral("v1/input/port"), 13001);
        settings.setValue(QStringLiteral("v1/audio/deviceIdentifier"), QStringLiteral("missing:test-device"));
        settings.setValue(QStringLiteral("v1/audio/deviceName"), QStringLiteral("Disconnected test device"));
        settings.setValue(QStringLiteral("v1/audio/deviceHostApi"), QStringLiteral("WASAPI"));
        settings.sync();
        if (settings.status() != QSettings::NoError) return 2;
    }

    {
        MediaWindow window;
        window.show();
        app.processEvents();
        auto* port = window.findChild<QSpinBox*>(QStringLiteral("inputPort"));
        auto* channels = window.findChild<QSpinBox*>(QStringLiteral("audioChannels"));
        auto* device = window.findChild<QComboBox*>(QStringLiteral("audioDevice"));
        auto* audioRoutes = window.findChild<QPlainTextEdit*>(QStringLiteral("audioRoutes"));
        auto* omtRoutes = window.findChild<QPlainTextEdit*>(QStringLiteral("omtRoutes"));
        auto* spoutName = window.findChild<QLineEdit*>(QStringLiteral("spoutName"));
        auto* queueMeter = window.findChild<QProgressBar*>(QStringLiteral("srtQueueMeter"));
        if (!port || !channels || !device || !audioRoutes || !omtRoutes || !spoutName ||
            !queueMeter ||
            port->value() != 13001 || !device->currentText().contains(QStringLiteral("待機中"))) {
            std::cerr << "Initial settings or disconnected device were not restored\n";
            return 3;
        }
        SessionSnapshot monitor;
        monitor.state = "connected";
        monitor.hasConnectionStats = true;
        monitor.connection.receiveBufferBytes = 512;
        monitor.connection.receiveBufferCapacityBytes = 1024;
        monitor.hasVideoTrack = true;
        monitor.hasAudioTrack = true;
        monitor.lastVideoFrame = std::chrono::steady_clock::now();
        monitor.lastAudioFrame = monitor.lastVideoFrame;
        AudioTrackSnapshot audioTrack;
        audioTrack.streamIndex = 1;
        audioTrack.streamId = 257;
        audioTrack.channels = 2;
        audioTrack.codec = "pcm_s16le";
        audioTrack.lastFrame = std::chrono::steady_clock::now();
        audioTrack.levels = {{-6.0f, -9.0f}, {-12.0f, -15.0f}};
        monitor.audioTracks.push_back(audioTrack);
        window.updateMonitor(monitor);
        auto* firstLevel = window.findChild<QWidget*>(QStringLiteral("audioLevel_1_0"));
        const auto labels = window.findChildren<QLabel*>();
        bool hasDbfs = false;
        bool hasActivity = false;
        for (const auto* label : labels)
            hasDbfs |= label->text().contains(QStringLiteral("P -6.0 / R -9.0 dBFS"));
        for (const auto* label : labels)
            hasActivity |= label->text().contains(QStringLiteral("映像: 受信中  |  音声: 受信中"));
        if (queueMeter->value() != 50 || !firstLevel || !hasDbfs || !hasActivity) {
            std::cerr << "SRT queue or audio level was not rendered in the monitor\n";
            return 9;
        }
        monitor.audioTracks.clear();
        monitor.hasConnectionStats = false;
        window.updateMonitor(monitor);
        if (window.findChild<QWidget*>(QStringLiteral("audioLevel_1_0")) ||
            queueMeter->value() != 0) {
            std::cerr << "Meters did not reset when input disconnected\n";
            return 10;
        }
        port->setValue(13002);
        channels->setValue(8);
        audioRoutes->setPlainText(QStringLiteral("257:0:0\n258:5:7"));
        omtRoutes->setPlainText(QStringLiteral("258:5:31"));
        spoutName->setText(QStringLiteral("ROOD settings test"));
        window.close();
    }

    {
        MediaWindow window;
        window.show();
        app.processEvents();
        auto* port = window.findChild<QSpinBox*>(QStringLiteral("inputPort"));
        auto* channels = window.findChild<QSpinBox*>(QStringLiteral("audioChannels"));
        auto* device = window.findChild<QComboBox*>(QStringLiteral("audioDevice"));
        auto* audioRoutes = window.findChild<QPlainTextEdit*>(QStringLiteral("audioRoutes"));
        auto* omtRoutes = window.findChild<QPlainTextEdit*>(QStringLiteral("omtRoutes"));
        auto* spoutName = window.findChild<QLineEdit*>(QStringLiteral("spoutName"));
        if (!port || !channels || !device || !audioRoutes || !omtRoutes || !spoutName ||
            port->value() != 13002 || channels->value() != 8 ||
            !device->currentText().contains(QStringLiteral("待機中")) ||
            audioRoutes->toPlainText() != QStringLiteral("257:0:0\n258:5:7") ||
            omtRoutes->toPlainText() != QStringLiteral("258:5:31") ||
            spoutName->text() != QStringLiteral("ROOD settings test")) {
            std::cerr << "Settings did not survive a window restart\n";
            return 4;
        }
        window.close();
    }
    {
        MediaSession session;
        session.onState("connected");
        rood::TrackInfo track;
        track.streamIndex = 1;
        track.streamId = 257;
        track.kind = "audio";
        track.codec = "pcm_s16le";
        track.channels = 2;
        session.onTrack(track);
        AVFrame* frame = av_frame_alloc();
        if (!frame) return 5;
        av_channel_layout_default(&frame->ch_layout, 2);
        frame->format = AV_SAMPLE_FMT_S16;
        frame->nb_samples = 4;
        if (av_frame_get_buffer(frame, 0) < 0) {
            av_frame_free(&frame);
            return 6;
        }
        auto* samples = reinterpret_cast<std::int16_t*>(frame->extended_data[0]);
        for (int i = 0; i < 4; ++i) {
            samples[i * 2] = 16384;
            samples[i * 2 + 1] = 8192;
        }
        rood::FrameInfo info;
        info.streamIndex = 1;
        info.streamId = 257;
        info.kind = "audio";
        session.onFrame(info, *frame);
        av_frame_free(&frame);
        const auto snapshot = session.snapshot();
        if (snapshot.audioTracks.size() != 1 || snapshot.audioTracks[0].levels.size() != 2 ||
            std::abs(snapshot.audioTracks[0].levels[0].peakDbfs + 6.02f) > 0.1f ||
            std::abs(snapshot.audioTracks[0].levels[1].peakDbfs + 12.04f) > 0.1f) {
            std::cerr << "Received channel levels were not recorded in the GUI snapshot\n";
            return 7;
        }
        session.onState("disconnected");
        if (!session.snapshot().audioTracks.empty()) return 8;
    }
    std::cout << "GUI settings, SRT meter and decoded input levels passed\n";
    return 0;
}
