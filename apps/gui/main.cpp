#include <QApplication>
#include <QFont>
#include <QLabel>
#include <QMainWindow>
#include <QVBoxLayout>
#include <QWidget>

#ifdef ROOD_GUI_HAS_MEDIA
int runMediaGui(QApplication& app);
#endif

int main(int argc, char* argv[]) {
    QApplication app(argc, argv);
    app.setApplicationName(QStringLiteral("ROOD"));
    app.setApplicationVersion(QString::fromLatin1(ROOD_APP_VERSION));
#ifdef ROOD_GUI_HAS_MEDIA
    return runMediaGui(app);
#else

    QMainWindow window;
    window.setWindowTitle(QStringLiteral("ROOD — 開発用シェル"));
    window.resize(640, 260);

    auto* body = new QWidget(&window);
    auto* layout = new QVBoxLayout(body);
    auto* heading = new QLabel(QStringLiteral("ROOD | SRT受信・分配・接続監視"), body);
    QFont heading_font = heading->font();
    heading_font.setPointSize(17);
    heading_font.setBold(true);
    heading->setFont(heading_font);
    layout->addWidget(heading);
    layout->addWidget(new QLabel(QStringLiteral("現在: 最小プロジェクト。受信はまだ開始できません。"), body));
    layout->addWidget(new QLabel(QStringLiteral("実装済み: UIから独立した音声チャンネルルーティング"), body));
    layout->addWidget(new QLabel(QStringLiteral("次段階: SRT / FFmpeg / PortAudio / Spout2 / OMT と同期制御"), body));
    layout->addStretch();
    window.setCentralWidget(body);
    window.show();
    return app.exec();
#endif
}
