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

namespace {
bool isEncryptedPassword(const QString& password) {
    return password.startsWith(QStringLiteral("BANCHO:")) || password.startsWith(QStringLiteral("BANCHO2:"));
}
} // namespace

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

    if (isEncryptedPassword(password)) {
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

namespace {
constexpr auto kKWalletName = "kdewallet";
constexpr auto kKWalletFolder = "BanchoXterm";

bool kwalletWrite(const QString& sessionId, const QString& password) {
    QProcess process;
    process.start(QStringLiteral("kwallet-query"),
                  {QString::fromLatin1(kKWalletName), "-f", QString::fromLatin1(kKWalletFolder), "-w", sessionId});
    if (!process.waitForStarted(2000))
        return false;
    process.write(password.toUtf8());
    process.closeWriteChannel();
    return process.waitForFinished(3000) && process.exitCode() == 0;
}

QString kwalletRead(const QString& sessionId) {
    QProcess process;
    process.start(QStringLiteral("kwallet-query"),
                  {QString::fromLatin1(kKWalletName), "-f", QString::fromLatin1(kKWalletFolder), "-r", sessionId});
    if (!process.waitForFinished(3000) || process.exitCode() != 0)
        return {};
    QString password = QString::fromUtf8(process.readAllStandardOutput());
    if (password.endsWith(QLatin1Char('\n')))
        password.chop(1);
    return password;
}
} // namespace

bool storePassword(const QString& sessionId, const QString& password) {
    if (sessionId.isEmpty() || password.isEmpty())
        return false;

    QString storedPassword = password;
    if (MasterPasswordManager::instance().isEnabled()) {
        storedPassword = MasterPasswordManager::instance().encryptPassword(password);
    }

    QByteArray passwordData = storedPassword.toUtf8();
    QProcess process;
    process.start("secret-tool", {"store", "--label=BanchoXterm Session Password", "id", sessionId});
    if (process.waitForStarted(2000)) {
        process.write(passwordData);
        process.closeWriteChannel();
        if (process.waitForFinished(3000) && process.exitCode() == 0)
            return true;
    }
    return kwalletWrite(sessionId, storedPassword);
}

QString lookupPassword(const QString& sessionId) {
    if (sessionId.isEmpty())
        return "";

    QProcess process;
    process.start("secret-tool", {"lookup", "id", sessionId});
    if (process.waitForFinished(3000) && process.exitCode() == 0) {
        QString password = QString::fromUtf8(process.readAllStandardOutput());
        // Remove only the newline appended by secret-tool, preserving password whitespace.
        if (password.endsWith(QLatin1Char('\n')))
            password.chop(1);
        if (isEncryptedPassword(password))
            return MasterPasswordManager::instance().decryptPassword(password);
        if (!password.isEmpty())
            return password;
    }
    const QString password = kwalletRead(sessionId);
    if (isEncryptedPassword(password))
        return MasterPasswordManager::instance().decryptPassword(password);
    return password;
}

bool deletePassword(const QString& sessionId) {
    if (sessionId.isEmpty())
        return false;

    QProcess process;
    process.start("secret-tool", {"clear", "id", sessionId});
    const bool secretServiceDeleted = process.waitForFinished(3000) && process.exitCode() == 0;
    const bool kwalletDeleted = kwalletWrite(sessionId, QString());
    return secretServiceDeleted || kwalletDeleted;
}

#endif

} // namespace Keyring
