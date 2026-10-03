#include <QApplication>
#include <QComboBox>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QSettings>
#include <QSpinBox>
#include <QTemporaryDir>

#include <iostream>

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
        if (!port || !channels || !device || !audioRoutes || !omtRoutes || !spoutName ||
            port->value() != 13001 || !device->currentText().contains(QStringLiteral("待機中"))) {
            std::cerr << "Initial settings or disconnected device were not restored\n";
            return 3;
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
    std::cout << "GUI settings and disconnected device restored from an isolated INI file\n";
    return 0;
}
