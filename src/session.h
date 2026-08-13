#pragma once
#include <QString>
#include <QJsonObject>
#include <QList>

enum class SessionType { SSH, Local, Telnet, Serial, RDP, VNC };

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
