#include <QApplication>
#include <QCoreApplication>
#include <QTranslator>
#include <QSettings>
#include <iostream>
#include "mainwindow.h"
#include "keyring.h"

int main(int argc, char* argv[]) {
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
