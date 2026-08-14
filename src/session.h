#pragma once
#include <QString>
#include <QJsonObject>
#include <QList>
#include <QDateTime>

enum class SessionType { SSH, Local, Telnet, Serial, RDP, VNC, FTP };

// A remote file entry shared by the SFTP and FTP backends.
struct SftpFile {
    QString name;
    bool isDirectory = false;
    qint64 size = 0;
    QDateTime mtime;
};

struct TunnelConfig {
    enum class Type { Local, Remote, Dynamic };
    Type type;
    int localPort;
    QString remoteHost;
    int remotePort;

    QJsonObject toJson() const;
    static TunnelConfig fromJson(const QJsonObject& json);
};

struct Session {
    QString id;
    QString name;
    QString group;
    SessionType type;

    // SSH / Telnet / RDP / VNC
    QString host;
    QString user;
    int port = 22;
    QString keyPath;
    bool x11Forwarding = false;
    bool autoReconnect = false;

    // Local
    QString shellPath;

    // Serial
    QString serialPort;
    int baudRate = 115200;
    QString serialCmd;

    // Terminal (SSH / Local / Telnet / Serial)
    int scrollback = 5000;
    QString fontFamily;
    int fontSize = 0; // 0 = inherit global settings

    // SSH advanced
    int keepAliveSeconds = 0; // 0 = disabled
    QString cryptCipher;      // comma-separated, empty = libssh2 default
    QString kexAlgo;          // empty = libssh2 default
    QString macAlgo;          // empty = libssh2 default

    QList<TunnelConfig> tunnels;

    QJsonObject toJson() const;
    static Session fromJson(const QJsonObject& json);
};

class SessionManager {
public:
    static QList<Session> loadSessions();
    static void saveSessions(const QList<Session>& sessions);
    static bool exportSessions(const QList<Session>& sessions, const QString& path);
    static QList<Session> importSessions(const QString& path, bool* ok = nullptr);

private:
    static QString getFilePath();
};
