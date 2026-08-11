#include "keyring.h"
#include <QProcess>
#include <QDebug>

namespace Keyring {

bool storePassword(const QString& sessionId, const QString& password) {
    if (sessionId.isEmpty() || password.isEmpty())
        return false;

    QProcess process;
    process.start("secret-tool", {"store", "--label=BanchoXterm Session Password", "id", sessionId});
    if (!process.waitForStarted(2000)) {
        return false;
    }

    process.write(password.toUtf8());
    process.write("\n");
    process.closeWriteChannel();

    if (!process.waitForFinished(3000)) {
        process.kill();
        return false;
    }

    return process.exitCode() == 0;
}

QString lookupPassword(const QString& sessionId) {
    if (sessionId.isEmpty())
        return "";

    QProcess process;
    process.start("secret-tool", {"lookup", "id", sessionId});
    if (!process.waitForFinished(3000)) {
        process.kill();
        return "";
    }

    return QString::fromUtf8(process.readAllStandardOutput()).trimmed();
}

bool deletePassword(const QString& sessionId) {
    if (sessionId.isEmpty())
        return false;

    QProcess process;
    process.start("secret-tool", {"clear", "id", sessionId});
    return process.waitForFinished(3000) && process.exitCode() == 0;
}

} // namespace Keyring
