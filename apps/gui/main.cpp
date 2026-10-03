#include <QApplication>
#include <QFont>
#include <QLabel>
#include <QMainWindow>
#include <QVBoxLayout>
#include <QWidget>

int main(int argc, char* argv[]) {
    QApplication app(argc, argv);

    QMainWindow window;
    window.setWindowTitle(QStringLiteral("SRT Receiver — 開発用シェル"));
    window.resize(640, 260);

    auto* body = new QWidget(&window);
    auto* layout = new QVBoxLayout(body);
    auto* heading = new QLabel(QStringLiteral("SRT受信専用アプリ"), body);
    QFont heading_font = heading->font();
    heading_font.setPointSize(17);
    heading_font.setBold(true);
    heading->setFont(heading_font);
    layout->addWidget(heading);
    layout->addWidget(new QLabel(QStringLiteral("現在: 最小プロジェクト。受信はまだ開始できません。"), body));
    layout->addWidget(new QLabel(QStringLiteral("実装済み: UIから独立した音声チャンネルルーティング"), body));
    layout->addWidget(new QLabel(QStringLiteral("次段階: SRT / FFmpeg / PortAudio / Spout2 と同期制御"), body));
    layout->addStretch();
    window.setCentralWidget(body);
    window.show();
    return app.exec();
}
