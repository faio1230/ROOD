#include "rood/MediaReceiver.hpp"
#include "rood/OmtOutput.hpp"
#include "rood/RecoveringAudioOutput.hpp"
#include "rood/SpoutVideoOutput.hpp"

#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMainWindow>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QScrollArea>
#include <QSpinBox>
#include <QSplitter>
#include <QTimer>
#include <QVBoxLayout>
#include <QWidget>

#include <atomic>
#include <cstdint>
#include <exception>
#include <memory>
#include <mutex>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace {

struct SessionConfig {
    rood::ReceiveConfig receive;
    std::optional<rood::AudioOutputConfig> audio;
    std::optional<rood::SpoutVideoConfig> spout;
    std::optional<rood::OmtOutputConfig> omt;
};

struct SessionSnapshot {
    std::string state = "停止中";
    std::string error;
    std::vector<std::string> tracks;
    rood::ConnectionStats connection;
    rood::RecoveringAudioOutputStats audio;
    rood::SpoutVideoStats spout;
    rood::OmtOutputStats omt;
    std::uint64_t videoFrames = 0;
    std::uint64_t audioFrames = 0;
    bool hasConnectionStats = false;
    bool hasAudio = false;
    bool hasSpout = false;
    bool hasOmt = false;
};

class MediaSession final : public rood::MediaReceiverObserver {
public:
    ~MediaSession() override { stop(); }

    void start(SessionConfig config) {
        stop();
        {
            std::lock_guard<std::mutex> lock(mutex_);
            snapshot_ = SessionSnapshot{};
            snapshot_.state = "起動中";
            snapshot_.hasAudio = config.audio.has_value();
            snapshot_.hasSpout = config.spout.has_value();
            snapshot_.hasOmt = config.omt.has_value();
        }
        stopRequested_.store(false);
        running_.store(true);
        try { worker_ = std::thread([this, config = std::move(config)]() mutable {
            try {
                std::unique_ptr<rood::RecoveringAudioOutput> audio;
                std::unique_ptr<rood::SpoutVideoOutput> spout;
                std::unique_ptr<rood::OmtOutput> omt;
                if (config.audio)
                    audio = std::make_unique<rood::RecoveringAudioOutput>(std::move(*config.audio));
                if (config.spout) {
                    rood::SpoutVideoOutput::AudioMediaClock clock;
                    if (audio) {
                        auto* source = audio.get();
                        clock = [source] { return source->playbackMediaSeconds(); };
                    }
                    spout = std::make_unique<rood::SpoutVideoOutput>(
                        std::move(*config.spout), std::move(clock));
                }
                if (config.omt)
                    omt = std::make_unique<rood::OmtOutput>(std::move(*config.omt));
                audio_ = audio.get();
                spout_ = spout.get();
                omt_ = omt.get();
                rood::runMediaReceiver(config.receive, stopRequested_, *this);
                audio_ = nullptr;
                spout_ = nullptr;
                omt_ = nullptr;
            } catch (const std::exception& error) {
                audio_ = nullptr;
                spout_ = nullptr;
                omt_ = nullptr;
                std::lock_guard<std::mutex> lock(mutex_);
                snapshot_.state = "エラー";
                snapshot_.error = error.what();
            }
            running_.store(false);
        }); } catch (...) {
            running_.store(false);
            throw;
        }
    }

    void stop() {
        stopRequested_.store(true);
        if (worker_.joinable()) worker_.join();
        running_.store(false);
    }

    bool running() const { return running_.load(); }

    SessionSnapshot snapshot() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return snapshot_;
    }

    void onState(const std::string& state) override {
        if (state == "disconnected") {
            if (spout_) spout_->reset();
            if (omt_) omt_->reset();
            if (audio_) audio_->reset();
        }
        std::lock_guard<std::mutex> lock(mutex_);
        snapshot_.state = state;
        if (state == "connected") {
            snapshot_.tracks.clear();
            spoutFailed_ = omtFailed_ = false;
        }
    }

    void onTrack(const rood::TrackInfo& track) override {
        if (omt_ && track.kind == "video")
            omt_->setVideoFrameRate(track.frameRateNum, track.frameRateDen);
        std::ostringstream stream;
        stream << "ID " << track.streamId << "  " << track.kind << "  " << track.codec;
        if (track.kind == "audio")
            stream << "  " << track.channels << " ch / " << track.sampleRate << " Hz";
        if (track.kind == "video")
            stream << "  " << track.width << 'x' << track.height;
        std::lock_guard<std::mutex> lock(mutex_);
        snapshot_.tracks.push_back(stream.str());
    }

    void onFrame(const rood::FrameInfo& info, const AVFrame& frame) override {
        if (audio_ && info.kind == "audio") {
            try { audio_->pushFrame(info, frame); }
            catch (const std::exception& error) { onError(error.what()); }
        }
        if (spout_ && !spoutFailed_ && info.kind == "video") {
            try { spout_->pushFrame(info, frame); }
            catch (const std::exception& error) { spoutFailed_ = true; onError(error.what()); }
        }
        if (omt_ && !omtFailed_) {
            try { omt_->pushFrame(info, frame); }
            catch (const std::exception& error) { omtFailed_ = true; onError(error.what()); }
        }
        std::lock_guard<std::mutex> lock(mutex_);
        if (info.kind == "video") ++snapshot_.videoFrames;
        if (info.kind == "audio") ++snapshot_.audioFrames;
    }

    void onStats(const rood::ConnectionStats& connection) override {
        const auto audio = audio_ ? audio_->stats() : rood::RecoveringAudioOutputStats{};
        const auto spout = spout_ ? spout_->stats() : rood::SpoutVideoStats{};
        const auto omt = omt_ ? omt_->stats() : rood::OmtOutputStats{};
        std::lock_guard<std::mutex> lock(mutex_);
        snapshot_.connection = connection;
        snapshot_.audio = audio;
        snapshot_.spout = spout;
        snapshot_.omt = omt;
        snapshot_.hasConnectionStats = true;
        if (!spout.lastError.empty()) snapshot_.error = spout.lastError;
        if (!audio.lastError.empty()) snapshot_.error = audio.lastError;
    }

    void onError(const std::string& error) override {
        std::lock_guard<std::mutex> lock(mutex_);
        snapshot_.error = error;
    }

private:
    mutable std::mutex mutex_;
    SessionSnapshot snapshot_;
    std::atomic_bool stopRequested_{false};
    std::atomic_bool running_{false};
    std::thread worker_;
    rood::RecoveringAudioOutput* audio_ = nullptr;
    rood::SpoutVideoOutput* spout_ = nullptr;
    rood::OmtOutput* omt_ = nullptr;
    bool spoutFailed_ = false;
    bool omtFailed_ = false;
};

rood::ChannelRoute parseRoute(const std::string& line) {
    std::vector<std::string> parts;
    std::size_t begin = 0;
    while (true) {
        const auto separator = line.find(':', begin);
        parts.push_back(line.substr(begin, separator - begin));
        if (separator == std::string::npos) break;
        begin = separator + 1;
    }
    if (parts.size() < 3 || parts.size() > 4)
        throw std::invalid_argument("経路は TRACK:SOURCE:OUTPUT[:GAIN] の形式で指定してください");
    auto integer = [](const std::string& text) -> std::uint32_t {
        std::size_t consumed = 0;
        const long value = std::stol(text, &consumed);
        if (consumed != text.size() || value < 0 || value > 1000000)
            throw std::invalid_argument("経路の番号が不正です");
        return static_cast<std::uint32_t>(value);
    };
    rood::ChannelRoute route{};
    route.track_id = integer(parts[0]);
    route.source_channel = integer(parts[1]);
    route.device_channel = integer(parts[2]);
    if (parts.size() == 4) {
        std::size_t consumed = 0;
        route.gain = std::stof(parts[3], &consumed);
        if (consumed != parts[3].size())
            throw std::invalid_argument("ゲインが不正です");
    }
    return route;
}

std::vector<rood::ChannelRoute> parseRoutes(const QString& text) {
    std::vector<rood::ChannelRoute> routes;
    for (const auto& line : text.split('\n')) {
        const QString trimmed = line.trimmed();
        if (!trimmed.isEmpty()) routes.push_back(parseRoute(trimmed.toStdString()));
    }
    if (routes.empty()) throw std::invalid_argument("出力経路を1件以上指定してください");
    return routes;
}

QSpinBox* spin(int minimum, int maximum, int value, QWidget* parent) {
    auto* box = new QSpinBox(parent);
    box->setRange(minimum, maximum);
    box->setValue(value);
    return box;
}

class MediaWindow final : public QMainWindow {
public:
    MediaWindow() {
        setWindowTitle(QStringLiteral("ROOD | SRT受信・分配"));
        resize(1100, 780);
        auto* central = new QWidget(this);
        auto* root = new QVBoxLayout(central);
        auto* heading = new QLabel(QStringLiteral("ROOD  •  SRT RECEIVE & ROUTE"), central);
        QFont font = heading->font();
        font.setPointSize(16);
        font.setBold(true);
        heading->setFont(font);
        heading->setStyleSheet(QStringLiteral("color: #b92e35;"));
        root->addWidget(heading);

        auto* actions = new QHBoxLayout();
        startButton_ = new QPushButton(QStringLiteral("受信開始"), central);
        stopButton_ = new QPushButton(QStringLiteral("停止"), central);
        stopButton_->setEnabled(false);
        actions->addWidget(startButton_);
        actions->addWidget(stopButton_);
        actions->addStretch();
        root->addLayout(actions);

        auto* splitter = new QSplitter(Qt::Horizontal, central);
        auto* scroll = new QScrollArea(splitter);
        scroll->setWidgetResizable(true);
        auto* settings = new QWidget(scroll);
        auto* settingsLayout = new QVBoxLayout(settings);

        auto* inputGroup = new QGroupBox(QStringLiteral("SRT入力"), settings);
        auto* inputForm = new QFormLayout(inputGroup);
        port_ = spin(1, 65535, 9000, inputGroup);
        latency_ = spin(20, 8000, 120, inputGroup);
        inputForm->addRow(QStringLiteral("待受UDPポート"), port_);
        inputForm->addRow(QStringLiteral("SRT遅延 (ms)"), latency_);
        settingsLayout->addWidget(inputGroup);

        auto* audioGroup = new QGroupBox(QStringLiteral("音声デバイス"), settings);
        auto* audioForm = new QFormLayout(audioGroup);
        audioEnabled_ = new QCheckBox(QStringLiteral("ASIO / WASAPIへ出力"), audioGroup);
        audioForm->addRow(audioEnabled_);
        device_ = new QComboBox(audioGroup);
        refreshButton_ = new QPushButton(QStringLiteral("更新"), audioGroup);
        auto* deviceRow = new QWidget(audioGroup);
        auto* deviceLayout = new QHBoxLayout(deviceRow);
        deviceLayout->setContentsMargins(0, 0, 0, 0);
        deviceLayout->addWidget(device_, 1);
        deviceLayout->addWidget(refreshButton_);
        audioForm->addRow(QStringLiteral("出力デバイス"), deviceRow);
        audioChannels_ = spin(1, 256, 2, audioGroup);
        audioRate_ = spin(8000, 384000, 48000, audioGroup);
        audioDelay_ = spin(0, 3000, 250, audioGroup);
        exclusive_ = new QCheckBox(QStringLiteral("WASAPI排他"), audioGroup);
        audioForm->addRow(QStringLiteral("出力チャンネル"), audioChannels_);
        audioForm->addRow(QStringLiteral("出力レート (Hz)"), audioRate_);
        audioForm->addRow(QStringLiteral("出力遅延 (ms)"), audioDelay_);
        audioForm->addRow(exclusive_);
        audioRoutes_ = new QPlainTextEdit(audioGroup);
        audioRoutes_->setPlainText(QStringLiteral("257:0:0\n257:1:1"));
        audioRoutes_->setMaximumHeight(90);
        audioForm->addRow(QStringLiteral("経路 (1行1件)"), audioRoutes_);
        settingsLayout->addWidget(audioGroup);

        auto* spoutGroup = new QGroupBox(QStringLiteral("Spout2映像"), settings);
        auto* spoutForm = new QFormLayout(spoutGroup);
        spoutEnabled_ = new QCheckBox(QStringLiteral("Spoutへ出力"), spoutGroup);
        spoutForm->addRow(spoutEnabled_);
        spoutName_ = new QLineEdit(QStringLiteral("ROOD"), spoutGroup);
        videoDelay_ = spin(0, 5000, 250, spoutGroup);
        videoOffset_ = spin(-5000, 5000, 0, spoutGroup);
        spoutForm->addRow(QStringLiteral("送信名"), spoutName_);
        spoutForm->addRow(QStringLiteral("映像遅延 (ms)"), videoDelay_);
        spoutForm->addRow(QStringLiteral("音声とのオフセット (ms)"), videoOffset_);
        settingsLayout->addWidget(spoutGroup);

        auto* omtGroup = new QGroupBox(QStringLiteral("OMT分配"), settings);
        auto* omtForm = new QFormLayout(omtGroup);
        omtEnabled_ = new QCheckBox(QStringLiteral("OMTへ映像・音声を出力"), omtGroup);
        omtForm->addRow(omtEnabled_);
        omtName_ = new QLineEdit(QStringLiteral("ROOD"), omtGroup);
        omtChannels_ = spin(1, 32, 2, omtGroup);
        omtRate_ = spin(8000, 192000, 48000, omtGroup);
        omtDelay_ = spin(0, 5000, 250, omtGroup);
        omtRoutes_ = new QPlainTextEdit(omtGroup);
        omtRoutes_->setPlainText(QStringLiteral("257:0:0\n257:1:1"));
        omtRoutes_->setMaximumHeight(90);
        omtForm->addRow(QStringLiteral("送信名"), omtName_);
        omtForm->addRow(QStringLiteral("音声チャンネル"), omtChannels_);
        omtForm->addRow(QStringLiteral("音声レート (Hz)"), omtRate_);
        omtForm->addRow(QStringLiteral("出力遅延 (ms)"), omtDelay_);
        omtForm->addRow(QStringLiteral("経路 (1行1件)"), omtRoutes_);
        settingsLayout->addWidget(omtGroup);
        settingsLayout->addStretch();
        scroll->setWidget(settings);

        status_ = new QPlainTextEdit(splitter);
        status_->setReadOnly(true);
        status_->setFont(QFont(QStringLiteral("Consolas"), 10));
        splitter->addWidget(scroll);
        splitter->addWidget(status_);
        splitter->setStretchFactor(0, 1);
        splitter->setStretchFactor(1, 1);
        root->addWidget(splitter, 1);
        setCentralWidget(central);

        connect(refreshButton_, &QPushButton::clicked, this, [this] { refreshDevices(); });
        connect(startButton_, &QPushButton::clicked, this, [this] { start(); });
        connect(stopButton_, &QPushButton::clicked, this, [this] { session_.stop(); updateStatus(); });
        auto* timer = new QTimer(this);
        connect(timer, &QTimer::timeout, this, [this] { updateStatus(); });
        timer->start(250);
        refreshDevices();
        updateStatus();
    }

private:
    void refreshDevices() {
        device_->clear();
        device_->addItem(QStringLiteral("デバイスを選択"), -1);
        try {
            for (const auto& device : rood::listAudioOutputDevices()) {
                const QString label = QStringLiteral("%1  |  %2  |  %3 ch  |  %4")
                    .arg(device.index)
                    .arg(QString::fromStdString(device.hostApi))
                    .arg(device.maxOutputChannels)
                    .arg(QString::fromStdString(device.name));
                device_->addItem(label, device.index);
            }
        } catch (const std::exception& error) {
            QMessageBox::warning(this, QStringLiteral("音声デバイス"),
                QString::fromStdString(error.what()));
        }
    }

    void start() {
        try {
            SessionConfig config;
            config.receive.port = static_cast<std::uint16_t>(port_->value());
            config.receive.srtLatencyMs = latency_->value();
            if (audioEnabled_->isChecked()) {
                rood::AudioOutputConfig audio;
                audio.deviceIndex = device_->currentData().toInt();
                if (audio.deviceIndex < 0)
                    throw std::invalid_argument("音声出力デバイスを選択してください");
                audio.channels = static_cast<std::uint32_t>(audioChannels_->value());
                audio.sampleRate = audioRate_->value();
                audio.outputDelayMs = audioDelay_->value();
                audio.wasapiExclusive = exclusive_->isChecked();
                audio.routes = parseRoutes(audioRoutes_->toPlainText());
                config.audio = std::move(audio);
            }
            if (spoutEnabled_->isChecked()) {
                rood::SpoutVideoConfig spout;
                spout.senderName = spoutName_->text().trimmed().toStdString();
                spout.outputDelayMs = videoDelay_->value();
                spout.videoOffsetMs = videoOffset_->value();
                config.spout = std::move(spout);
            }
            if (omtEnabled_->isChecked()) {
                rood::OmtOutputConfig omt;
                omt.name = omtName_->text().trimmed().toStdString();
                omt.audioChannels = static_cast<std::uint32_t>(omtChannels_->value());
                omt.audioSampleRate = omtRate_->value();
                omt.outputDelayMs = omtDelay_->value();
                omt.audioRoutes = parseRoutes(omtRoutes_->toPlainText());
                config.omt = std::move(omt);
            }
            session_.start(std::move(config));
            updateStatus();
        } catch (const std::exception& error) {
            QMessageBox::warning(this, QStringLiteral("設定を確認してください"),
                QString::fromStdString(error.what()));
        }
    }

    void updateStatus() {
        const auto snapshot = session_.snapshot();
        QString text = QStringLiteral("状態: %1\n").arg(QString::fromStdString(snapshot.state));
        if (!snapshot.error.empty())
            text += QStringLiteral("直近エラー: %1\n").arg(QString::fromStdString(snapshot.error));
        text += QStringLiteral("\n受信フレーム  映像: %1  音声: %2\n")
            .arg(snapshot.videoFrames).arg(snapshot.audioFrames);
        if (snapshot.hasConnectionStats) {
            text += QStringLiteral("SRT  %1 Mb/s  RTT %2 ms\n")
                .arg(snapshot.connection.receiveMbps, 0, 'f', 2)
                .arg(snapshot.connection.rttMs, 0, 'f', 1);
            text += QStringLiteral("受信バッファ: %1 ms / %2 bytes\n")
                .arg(snapshot.connection.receiveBufferMs)
                .arg(snapshot.connection.receiveBufferBytes);
            text += QStringLiteral("損失: %1  再送: %2\n")
                .arg(snapshot.connection.lostPackets)
                .arg(snapshot.connection.retransmittedPacketsInInterval);
        }
        if (snapshot.hasAudio)
            text += QStringLiteral("\n音声出力  callback %1  再生フレーム %2\n"
                                   "underflow %3  破棄 %4  稼働 %5\n"
                                   "クロック補正 %6 ppm  誤差 %7 ms  安定 %8\n"
                                   "デバイス %9  再試行 %10  復帰 %11\n")
                .arg(snapshot.audio.callbackCount)
                .arg(snapshot.audio.renderedFrames)
                .arg(snapshot.audio.deviceUnderflows)
                .arg(snapshot.audio.rejectedFrames)
                .arg(snapshot.audio.streamActive ? QStringLiteral("はい") : QStringLiteral("いいえ"))
                .arg(snapshot.audio.driftCorrectionPpm, 0, 'f', 1)
                .arg(snapshot.audio.driftErrorMs, 0, 'f', 2)
                .arg(snapshot.audio.driftLocked ? QStringLiteral("はい") : QStringLiteral("いいえ"))
                .arg(snapshot.audio.deviceAvailable ? QStringLiteral("利用可能") : QStringLiteral("待機中"))
                .arg(snapshot.audio.reopenAttempts)
                .arg(snapshot.audio.recoveries);
        if (snapshot.hasSpout)
            text += QStringLiteral("\nSpout  受信 %1  送信 %2  破棄 %3  失敗 %4\n")
                .arg(snapshot.spout.receivedFrames)
                .arg(snapshot.spout.sentFrames)
                .arg(snapshot.spout.droppedFrames)
                .arg(snapshot.spout.failedFrames);
        if (snapshot.hasSpout && snapshot.spout.hasAudioSyncError)
            text += QStringLiteral("音声との推定時差: %1 ms\n")
                .arg(snapshot.spout.lastAudioSyncErrorMs, 0, 'f', 2);
        if (snapshot.hasOmt)
            text += QStringLiteral("\nOMT  映像 %1  音声パケット %2  接続 %3\n"
                                   "映像破棄 %4  音声破棄 %5\n")
                .arg(snapshot.omt.videoFrames)
                .arg(snapshot.omt.audioPackets)
                .arg(snapshot.omt.connections)
                .arg(snapshot.omt.droppedVideoFrames)
                .arg(snapshot.omt.rejectedAudioFrames);
        text += QStringLiteral("\n検出トラック\n");
        for (const auto& track : snapshot.tracks)
            text += QString::fromStdString(track) + '\n';
        status_->setPlainText(text);
        startButton_->setEnabled(!session_.running());
        stopButton_->setEnabled(session_.running());
        refreshButton_->setEnabled(!session_.running());
    }

    MediaSession session_;
    QSpinBox* port_ = nullptr;
    QSpinBox* latency_ = nullptr;
    QCheckBox* audioEnabled_ = nullptr;
    QComboBox* device_ = nullptr;
    QSpinBox* audioChannels_ = nullptr;
    QSpinBox* audioRate_ = nullptr;
    QSpinBox* audioDelay_ = nullptr;
    QCheckBox* exclusive_ = nullptr;
    QPlainTextEdit* audioRoutes_ = nullptr;
    QCheckBox* spoutEnabled_ = nullptr;
    QLineEdit* spoutName_ = nullptr;
    QSpinBox* videoDelay_ = nullptr;
    QSpinBox* videoOffset_ = nullptr;
    QCheckBox* omtEnabled_ = nullptr;
    QLineEdit* omtName_ = nullptr;
    QSpinBox* omtChannels_ = nullptr;
    QSpinBox* omtRate_ = nullptr;
    QSpinBox* omtDelay_ = nullptr;
    QPlainTextEdit* omtRoutes_ = nullptr;
    QPlainTextEdit* status_ = nullptr;
    QPushButton* startButton_ = nullptr;
    QPushButton* stopButton_ = nullptr;
    QPushButton* refreshButton_ = nullptr;
};

} // namespace

int runMediaGui(QApplication& app) {
    MediaWindow window;
    window.show();
    return app.exec();
}
