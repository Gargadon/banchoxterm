#pragma once
#include <QString>
#include <QByteArray>
#include <QMutex>

class MasterPasswordManager {
public:
    static MasterPasswordManager& instance();

    bool isEnabled() const;
    bool isUnlocked() const;
    
    // Establecer una nueva contraseña maestra
    bool setMasterPassword(const QString& password);
    
    // Desactivar la contraseña maestra (requiere la actual para descifrar y guardar en claro)
    bool disableMasterPassword(const QString& currentPassword);

    // Verificar y desbloquear
    bool unlock(const QString& password);
    
    // Bloquear (limpiar clave en memoria)
    void lock();

    // Cifrar una contraseña
    QString encryptPassword(const QString& plaintext);
    
    // Descifrar una contraseña (solicitará desbloqueo si no está activa la clave)
    QString decryptPassword(const QString& ciphertext);

private:
    MasterPasswordManager();
    ~MasterPasswordManager();

    QByteArray deriveKey(const QString& password, const QByteArray& salt);
    QByteArray generateRandomBytes(int size);
    QByteArray cryptStream(const QByteArray& data, const QByteArray& key, const QByteArray& iv);

    mutable QMutex m_mutex;
    QByteArray m_sessionKey;
    bool m_isUnlocked = false;
};
