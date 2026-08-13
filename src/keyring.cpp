#include "keyring.h"

#ifdef Q_OS_WIN
#include <windows.h>
#include <wincred.h>
#else
#include <QProcess>
#include <QDebug>
#endif
#include "masterpasswordmanager.h"

namespace Keyring {

#ifdef Q_OS_WIN

bool storePassword(const QString& sessionId, const QString& password) {
    if (sessionId.isEmpty() || password.isEmpty())
        return false;

    QString storedPassword = password;
    if (MasterPasswordManager::instance().isEnabled()) {
        storedPassword = MasterPasswordManager::instance().encryptPassword(password);
    }

    std::wstring targetName = QString("BanchoXterm:%1").arg(sessionId).toStdWString();
    std::wstring userName = sessionId.toStdWString();
    std::wstring blob = storedPassword.toStdWString();

    CREDENTIALW cred = {};
    cred.Type = CRED_TYPE_GENERIC;
    cred.TargetName = const_cast<LPWSTR>(targetName.c_str());
    cred.UserName = const_cast<LPWSTR>(userName.c_str());
    cred.CredentialBlob = (LPBYTE) blob.data();
    cred.CredentialBlobSize = static_cast<DWORD>((blob.size() + 1) * sizeof(wchar_t));
    cred.Persist = CRED_PERSIST_LOCAL_MACHINE;

    return CredWriteW(&cred, 0) != FALSE;
}

QString lookupPassword(const QString& sessionId) {
    if (sessionId.isEmpty())
        return QString();

    std::wstring targetName = QString("BanchoXterm:%1").arg(sessionId).toStdWString();
    PCREDENTIALW cred = nullptr;

    if (!CredReadW(targetName.c_str(), CRED_TYPE_GENERIC, 0, &cred))
        return QString();

    int len = static_cast<int>(cred->CredentialBlobSize / sizeof(wchar_t)) - 1;
    if (len < 0)
        len = 0;
    QString password = QString::fromWCharArray(reinterpret_cast<const wchar_t*>(cred->CredentialBlob), len);
    CredFree(cred);

    if (password.startsWith("BANCHO:")) {
        return MasterPasswordManager::instance().decryptPassword(password);
    }
    return password;
}

bool deletePassword(const QString& sessionId) {
    if (sessionId.isEmpty())
        return false;

    std::wstring targetName = QString("BanchoXterm:%1").arg(sessionId).toStdWString();
    return CredDeleteW(targetName.c_str(), CRED_TYPE_GENERIC, 0) != FALSE;
}

#else

bool storePassword(const QString& sessionId, const QString& password) {
    if (sessionId.isEmpty() || password.isEmpty())
        return false;

    QString storedPassword = password;
    if (MasterPasswordManager::instance().isEnabled()) {
        storedPassword = MasterPasswordManager::instance().encryptPassword(password);
    }

    QProcess process;
    process.start("secret-tool", {"store", "--label=BanchoXterm Session Password", "id", sessionId});
    if (!process.waitForStarted(2000)) {
        return false;
    }

    process.write(storedPassword.toUtf8());
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

    QString password = QString::fromUtf8(process.readAllStandardOutput()).trimmed();
    if (password.startsWith("BANCHO:")) {
        return MasterPasswordManager::instance().decryptPassword(password);
    }
    return password;
}

bool deletePassword(const QString& sessionId) {
    if (sessionId.isEmpty())
        return false;

    QProcess process;
    process.start("secret-tool", {"clear", "id", sessionId});
    return process.waitForFinished(3000) && process.exitCode() == 0;
}

#endif

} // namespace Keyring
