#include "masterpasswordmanager.h"
#include <QSettings>
#include <QRandomGenerator>
#include <QCryptographicHash>
#include <QInputDialog>
#include <QApplication>
#include <QThread>
#include <QMutexLocker>
#include <sodium.h>

namespace {
constexpr int kKeyBytes = crypto_aead_xchacha20poly1305_ietf_KEYBYTES;
constexpr int kSaltBytes = crypto_pwhash_SALTBYTES;
constexpr int kNonceBytes = crypto_aead_xchacha20poly1305_ietf_NPUBBYTES;
constexpr auto kFormat = "BANCHO2";

bool sodiumReady() {
    return sodium_init() >= 0;
}
}

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
    m_legacyKey = false;
}

QByteArray MasterPasswordManager::deriveKey(const QString& password, const QByteArray& salt) {
    if (!sodiumReady() || salt.size() != kSaltBytes)
        return {};
    QByteArray key(kKeyBytes, Qt::Uninitialized);
    const QByteArray pass = password.toUtf8();
    if (crypto_pwhash(reinterpret_cast<unsigned char*>(key.data()), key.size(), pass.constData(), pass.size(),
                      reinterpret_cast<const unsigned char*>(salt.constData()),
                      crypto_pwhash_OPSLIMIT_INTERACTIVE, crypto_pwhash_MEMLIMIT_INTERACTIVE,
                      crypto_pwhash_ALG_ARGON2ID13) != 0)
        return {};
    return key;
}

QByteArray MasterPasswordManager::legacyDeriveKey(const QString& password, const QByteArray& salt) {
    return QCryptographicHash::hash(salt + password.toUtf8(), QCryptographicHash::Sha256);
}

QByteArray MasterPasswordManager::generateRandomBytes(int size) {
    if (sodiumReady()) {
        QByteArray bytes(size, Qt::Uninitialized);
        randombytes_buf(bytes.data(), static_cast<size_t>(size));
        return bytes;
    }
    QByteArray bytes;
    bytes.reserve(size);
    while (bytes.size() < size) {
        quint32 val = QRandomGenerator::system()->generate();
        bytes.append(reinterpret_cast<const char*>(&val), qMin<int>(4, size - bytes.size()));
    }
    return bytes;
}

bool MasterPasswordManager::unlock(const QString& password) {
    QMutexLocker locker(&m_mutex);
    QSettings settings;
    QByteArray salt = QByteArray::fromBase64(settings.value("security/salt").toByteArray());
    QByteArray storedVerifier = QByteArray::fromBase64(settings.value("security/verifier").toByteArray());

    if (salt.isEmpty() || storedVerifier.isEmpty())
        return false;

    const bool legacy = settings.value("security/master_password_version", 1).toInt() < 2;
    QByteArray calculatedVerifier = legacy ? legacyDeriveKey(password, salt) : deriveKey(password, salt);
    if (calculatedVerifier == storedVerifier) {
        m_sessionKey = calculatedVerifier;
        m_legacyKey = legacy;
        m_isUnlocked = true;
        return true;
    }
    return false;
}

bool MasterPasswordManager::setMasterPassword(const QString& password) {
    if (password.isEmpty() || !sodiumReady())
        return false;
    QByteArray salt = generateRandomBytes(16);
    QByteArray verifier = deriveKey(password, salt);

    QSettings settings;
    settings.setValue("security/master_password_enabled", true);
    settings.setValue("security/master_password_version", 2);
    settings.setValue("security/salt", salt.toBase64());
    settings.setValue("security/verifier", verifier.toBase64());

    QMutexLocker locker(&m_mutex);
    m_sessionKey = verifier;
    m_legacyKey = false;
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
    settings.remove("security/master_password_version");

    lock();
    return true;
}

QString MasterPasswordManager::encryptPassword(const QString& plaintext) {
    if (plaintext.isEmpty())
        return "";

    QMutexLocker locker(&m_mutex);
    if (!m_isUnlocked)
        return {}; // Never silently persist a plaintext secret.

    QByteArray nonce = generateRandomBytes(kNonceBytes);
    QByteArray plain = plaintext.toUtf8();
    QByteArray cipher(plain.size() + crypto_aead_xchacha20poly1305_ietf_ABYTES, Qt::Uninitialized);
    unsigned long long cipherLen = 0;
    if (crypto_aead_xchacha20poly1305_ietf_encrypt(
            reinterpret_cast<unsigned char*>(cipher.data()), &cipherLen,
            reinterpret_cast<const unsigned char*>(plain.constData()), static_cast<unsigned long long>(plain.size()),
            nullptr, 0, nullptr, reinterpret_cast<const unsigned char*>(nonce.constData()),
            reinterpret_cast<const unsigned char*>(m_sessionKey.constData())) != 0)
        return {};
    cipher.resize(static_cast<int>(cipherLen));

    return QString("%1:%2:%3").arg(kFormat, QString(nonce.toBase64()), QString(cipher.toBase64()));
}

QString MasterPasswordManager::decryptPassword(const QString& ciphertext) {
    if (ciphertext.isEmpty())
        return "";

    if (!ciphertext.startsWith("BANCHO:") && !ciphertext.startsWith("BANCHO2:")) {
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
    if (parts.size() != 3)
        return "";

    QByteArray nonce = QByteArray::fromBase64(parts[1].toUtf8());
    QByteArray cipher = QByteArray::fromBase64(parts[2].toUtf8());
    if (parts[0] == "BANCHO")
        return decryptLegacy(ciphertext, m_sessionKey);
    if (nonce.size() != kNonceBytes || cipher.size() < crypto_aead_xchacha20poly1305_ietf_ABYTES)
        return "";
    QByteArray plain(cipher.size() - crypto_aead_xchacha20poly1305_ietf_ABYTES, Qt::Uninitialized);
    unsigned long long plainLen = 0;
    if (crypto_aead_xchacha20poly1305_ietf_decrypt(
            reinterpret_cast<unsigned char*>(plain.data()), &plainLen, nullptr,
            reinterpret_cast<const unsigned char*>(cipher.constData()), static_cast<unsigned long long>(cipher.size()),
            nullptr, 0, reinterpret_cast<const unsigned char*>(nonce.constData()),
            reinterpret_cast<const unsigned char*>(m_sessionKey.constData())) != 0)
        return "";
    plain.resize(static_cast<int>(plainLen));
    return QString::fromUtf8(plain);
}

QString MasterPasswordManager::decryptLegacy(const QString& ciphertext, const QByteArray& key) {
    const QStringList parts = ciphertext.split(":");
    if (parts.size() != 3)
        return {};
    const QByteArray iv = QByteArray::fromBase64(parts[1].toUtf8());
    const QByteArray cipher = QByteArray::fromBase64(parts[2].toUtf8());
    if (iv.isEmpty() || key.isEmpty())
        return {};

    QByteArray result;
    result.reserve(cipher.size());
    QByteArray hashInput = key + iv;
    QByteArray block = QCryptographicHash::hash(hashInput, QCryptographicHash::Sha256);
    int offset = 0;
    for (char byte : cipher) {
        if (offset == block.size()) {
            hashInput = key + block;
            block = QCryptographicHash::hash(hashInput, QCryptographicHash::Sha256);
            offset = 0;
        }
        result.append(byte ^ block.at(offset++));
    }
    return QString::fromUtf8(result);
}
