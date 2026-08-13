#include "masterpasswordmanager.h"
#include <QSettings>
#include <QRandomGenerator>
#include <QCryptographicHash>
#include <QInputDialog>
#include <QApplication>
#include <QThread>
#include <QMutexLocker>

MasterPasswordManager::MasterPasswordManager() {}

MasterPasswordManager::~MasterPasswordManager() {
    lock();
}

MasterPasswordManager& MasterPasswordManager::instance() {
    static MasterPasswordManager inst;
    return inst;
}

bool MasterPasswordManager::isEnabled() const {
    QSettings settings;
    return settings.value("security/master_password_enabled", false).toBool();
}

bool MasterPasswordManager::isUnlocked() const {
    QMutexLocker locker(&m_mutex);
    return m_isUnlocked;
}

void MasterPasswordManager::lock() {
    QMutexLocker locker(&m_mutex);
    m_isUnlocked = false;
    m_sessionKey.clear();
}

QByteArray MasterPasswordManager::deriveKey(const QString& password, const QByteArray& salt) {
    QByteArray input = salt + password.toUtf8();
    return QCryptographicHash::hash(input, QCryptographicHash::Sha256);
}

QByteArray MasterPasswordManager::generateRandomBytes(int size) {
    QByteArray bytes;
    bytes.reserve(size);
    while (bytes.size() < size) {
        quint32 val = QRandomGenerator::system()->generate();
        bytes.append(reinterpret_cast<const char*>(&val), qMin<int>(4, size - bytes.size()));
    }
    return bytes;
}

QByteArray MasterPasswordManager::cryptStream(const QByteArray& data, const QByteArray& key, const QByteArray& iv) {
    QByteArray result;
    result.reserve(data.size());

    QByteArray hashInput = key + iv;
    QByteArray keystreamBlock = QCryptographicHash::hash(hashInput, QCryptographicHash::Sha256);
    int keystreamOffset = 0;

    for (int i = 0; i < data.size(); ++i) {
        if (keystreamOffset >= 32) {
            hashInput = key + keystreamBlock;
            keystreamBlock = QCryptographicHash::hash(hashInput, QCryptographicHash::Sha256);
            keystreamOffset = 0;
        }
        char keystreamByte = keystreamBlock.at(keystreamOffset++);
        result.append(data.at(i) ^ keystreamByte);
    }
    return result;
}

bool MasterPasswordManager::unlock(const QString& password) {
    QMutexLocker locker(&m_mutex);
    QSettings settings;
    QByteArray salt = QByteArray::fromBase64(settings.value("security/salt").toByteArray());
    QByteArray storedVerifier = QByteArray::fromBase64(settings.value("security/verifier").toByteArray());

    if (salt.isEmpty() || storedVerifier.isEmpty())
        return false;

    QByteArray calculatedVerifier = deriveKey(password, salt);
    if (calculatedVerifier == storedVerifier) {
        m_sessionKey = calculatedVerifier;
        m_isUnlocked = true;
        return true;
    }
    return false;
}

bool MasterPasswordManager::setMasterPassword(const QString& password) {
    QByteArray salt = generateRandomBytes(16);
    QByteArray verifier = deriveKey(password, salt);

    QSettings settings;
    settings.setValue("security/master_password_enabled", true);
    settings.setValue("security/salt", salt.toBase64());
    settings.setValue("security/verifier", verifier.toBase64());

    QMutexLocker locker(&m_mutex);
    m_sessionKey = verifier;
    m_isUnlocked = true;
    return true;
}

bool MasterPasswordManager::disableMasterPassword(const QString& currentPassword) {
    if (!unlock(currentPassword))
        return false;

    QSettings settings;
    settings.setValue("security/master_password_enabled", false);
    settings.remove("security/salt");
    settings.remove("security/verifier");

    lock();
    return true;
}

QString MasterPasswordManager::encryptPassword(const QString& plaintext) {
    if (plaintext.isEmpty())
        return "";

    QMutexLocker locker(&m_mutex);
    if (!m_isUnlocked) {
        return plaintext; // Fallback
    }

    QByteArray iv = generateRandomBytes(16);
    QByteArray cipher = cryptStream(plaintext.toUtf8(), m_sessionKey, iv);

    return QString("BANCHO:%1:%2").arg(QString(iv.toBase64()), QString(cipher.toBase64()));
}

QString MasterPasswordManager::decryptPassword(const QString& ciphertext) {
    if (ciphertext.isEmpty())
        return "";

    if (!ciphertext.startsWith("BANCHO:")) {
        return ciphertext; // Contraseña heredada no cifrada
    }

    // Solicitar desbloqueo interactivo si no está desbloqueado
    if (isEnabled() && !isUnlocked()) {
        if (QThread::currentThread() != qApp->thread()) {
            QMetaObject::invokeMethod(qApp, [this]() {
                if (isUnlocked()) return;
                bool ok;
                QString password = QInputDialog::getText(nullptr, 
                    QObject::tr("Master Password Required"), 
                    QObject::tr("Please enter your Master Password to unlock your credentials:"), 
                    QLineEdit::Password, "", &ok);
                if (ok && !password.isEmpty()) {
                    unlock(password);
                }
            }, Qt::BlockingQueuedConnection);
        } else {
            bool ok;
            QString password = QInputDialog::getText(nullptr, 
                QObject::tr("Master Password Required"), 
                QObject::tr("Please enter your Master Password to unlock your credentials:"), 
                QLineEdit::Password, "", &ok);
            if (ok && !password.isEmpty()) {
                unlock(password);
            }
        }
    }

    QMutexLocker locker(&m_mutex);
    if (!m_isUnlocked) {
        return ""; // Cancelado o contraseña errónea
    }

    QStringList parts = ciphertext.split(":");
    if (parts.size() < 3)
        return "";

    QByteArray iv = QByteArray::fromBase64(parts[1].toUtf8());
    QByteArray cipher = QByteArray::fromBase64(parts[2].toUtf8());

    QByteArray plain = cryptStream(cipher, m_sessionKey, iv);
    return QString::fromUtf8(plain);
}
