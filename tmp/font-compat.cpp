#include <QFont>
#include <QCoreApplication>
#include <cstdio>
int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    QFont font(QStringLiteral("Consolas"), 13);
    std::printf("Qt %s\nSerialized: %s\n", qVersion(), font.toString().toUtf8().constData());
    if (argc > 1) {
        QFont parsed;
        const bool ok = parsed.fromString(QString::fromUtf8(argv[1]));
        std::printf("Parsed: %d; family: %s; size: %d\n", ok, parsed.family().toUtf8().constData(), parsed.pointSize());
    }
}
