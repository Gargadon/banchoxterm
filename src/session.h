#pragma once
#include <QString>
#include <QJsonObject>
#include <QList>

enum class SessionType { SSH, Local, Telnet, Serial };

struct Session {
    QString id;
    QString name;
    SessionType type;

    // SSH / Telnet specific
    QString host;
    QString user;
    int port = 22; // For SSH (22) or Telnet (23)
    QString keyPath;
    bool x11Forwarding = false; // SSH only

    // Local specific
    QString shellPath;

    // Serial specific
    QString serialPort;
    int baudRate = 115200;
    QString serialCmd; // "picocom", "screen", "minicom"

    QJsonObject toJson() const;
    static Session fromJson(const QJsonObject& json);
};

class SessionManager {
public:
    static QList<Session> loadSessions();
    static void saveSessions(const QList<Session>& sessions);

private:
    static QString getFilePath();
};
