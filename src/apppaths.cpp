#include "apppaths.h"
#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QSettings>
#include <QStandardPaths>

namespace AppPaths {

namespace {
bool portableMode = false;
bool portableInitialized = false;

bool detectPortable() {
    if (portableInitialized) {
        return portableMode;
    }
    portableInitialized = true;

    const QString dir = applicationDir();
    if (QFileInfo::exists(dir + "/portable.ini")) {
        portableMode = true;
    }

    if (!portableMode) {
        const QStringList args = QCoreApplication::arguments();
        if (args.contains("--portable")) {
            portableMode = true;
        }
    }
    return portableMode;
}
} // namespace

QString applicationDir() {
    return QCoreApplication::applicationDirPath();
}

bool isPortable() {
    return detectPortable();
}

QString configDir() {
    const QString override = qEnvironmentVariable("BANCHO_CONFIG_DIR").trimmed();
    if (!override.isEmpty()) {
        return override;
    }
    if (detectPortable()) {
        return applicationDir();
    }
    return QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation);
}

void applyPortableSettings() {
    // An explicit profile directory must isolate UI settings as well as
    // sessions/known hosts (used by tests and internal benchmarks).
    const QString override = qEnvironmentVariable("BANCHO_CONFIG_DIR").trimmed();
    if (!override.isEmpty()) {
        QSettings::setDefaultFormat(QSettings::IniFormat);
        QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, override);
        return;
    }
    if (!detectPortable()) {
        return;
    }

    // Store settings as an INI file inside the portable directory instead of
    // the registry / %APPDATA%.
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, applicationDir());
}
} // namespace AppPaths
