#include <QApplication>
#include <QCoreApplication>
#include <QTranslator>
#include <QSettings>
#include <QIcon>
#include <iostream>
#ifdef Q_OS_WIN
#include <winsock2.h>
#endif
#include "mainwindow.h"
#include "keyring.h"
#include "apppaths.h"

int main(int argc, char* argv[]) {
#ifdef Q_OS_WIN
    WSADATA wsaData;
    WSAStartup(MAKEWORD(2, 2), &wsaData);
#endif
    if (qEnvironmentVariableIsSet("BANCHOXTERM_ASKPASS_ID")) {
        QCoreApplication app(argc, argv);
        QString sessionId = qEnvironmentVariable("BANCHOXTERM_ASKPASS_ID");
        QString password = Keyring::lookupPassword(sessionId);
        std::cout << password.toStdString() << std::endl;
        return 0;
    }

    QApplication app(argc, argv);
    app.setApplicationName("BanchoXterm");
    app.setApplicationDisplayName("BanchoXterm");
    app.setOrganizationName("BanchoXterm");
    app.setWindowIcon(QIcon(":/icons/logo.svg"));

    AppPaths::applyPortableSettings();

    QSettings settings;
    QString lang = settings.value("locale/lang", "en").toString();
    QTranslator translator;
    if (lang != "en") {
        if (translator.load(QString(":/translations/banchoxterm_%1.qm").arg(lang))) {
            app.installTranslator(&translator);
        }
    }

    MainWindow window;
    window.show();

    return app.exec();
}