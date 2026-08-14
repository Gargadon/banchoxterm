#include "apppaths.h"
#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QSettings>
#include <QStandardPaths>

namespace AppPaths {

namespace {
bool g_portable = false;
bool g_portableInit = false;

bool detectPortable() {
    if (g_portableInit)
        return g_portable;
    g_portableInit = true;

    const QString dir = applicationDir();
    if (QFileInfo::exists(dir + "/portable.ini"))
        g_portable = true;

    if (!g_portable) {
        const QStringList args = QCoreApplication::arguments();
        if (args.contains("--portable"))
            g_portable = true;
    }
    return g_portable;
}
} // namespace

QString applicationDir() {
    return QCoreApplication::applicationDirPath();
}

bool isPortable() {
    return detectPortable();
}

QString configDir() {
    if (detectPortable())
        return applicationDir();
    return QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation);
}

void applyPortableSettings() {
    if (!detectPortable())
        return;

    // Store settings as an INI file inside the portable directory instead of
    // the registry / %APPDATA%.
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, applicationDir());
}
} // namespace AppPaths
