#include "session.h"
#include <QStandardPaths>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonArray>
#include <QUuid>

static QString sessionTypeToString(SessionType type) {
    switch (type) {
    case SessionType::SSH:
        return "ssh";
    case SessionType::Telnet:
        return "telnet";
    case SessionType::Serial:
        return "serial";
    case SessionType::RDP:
        return "rdp";
    case SessionType::VNC:
        return "vnc";
    default:
        return "local";
    }
}

static SessionType sessionTypeFromString(const QString& s) {
    if (s == "ssh")
        return SessionType::SSH;
    if (s == "telnet")
        return SessionType::Telnet;
    if (s == "serial")
        return SessionType::Serial;
    if (s == "rdp")
        return SessionType::RDP;
    if (s == "vnc")
        return SessionType::VNC;
    return SessionType::Local;
}

QJsonObject Session::toJson() const {
    QJsonObject json;
    json["id"] = id;
    json["name"] = name;
    json["type"] = sessionTypeToString(type);
    json["host"] = host;
    json["user"] = user;
    json["port"] = port;
    json["keyPath"] = keyPath;
    json["x11Forwarding"] = x11Forwarding;
    json["shellPath"] = shellPath;
    json["serialPort"] = serialPort;
    json["baudRate"] = baudRate;
    json["serialCmd"] = serialCmd;
    return json;
}

Session Session::fromJson(const QJsonObject& json) {
    Session s;
    s.id = json["id"].toString();
    if (s.id.isEmpty()) {
        s.id = QUuid::createUuid().toString();
    }
    s.name = json["name"].toString();
    s.type = sessionTypeFromString(json["type"].toString());
    s.host = json["host"].toString();
    s.user = json["user"].toString();
    s.port = json["port"].toInt(22);
    s.keyPath = json["keyPath"].toString();
    s.x11Forwarding = json["x11Forwarding"].toBool(false);
    s.shellPath = json["shellPath"].toString();
    s.serialPort = json["serialPort"].toString();
    s.baudRate = json["baudRate"].toInt(115200);
    s.serialCmd = json["serialCmd"].toString();
    return s;
}

QString SessionManager::getFilePath() {
    QString configDir = QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation);
    QDir().mkpath(configDir);
    return configDir + "/sessions.json";
}

QList<Session> SessionManager::loadSessions() {
    QList<Session> sessions;
    QString path = getFilePath();
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        Session local;
        local.id = QUuid::createUuid().toString();
        local.name = "Local Terminal";
        local.type = SessionType::Local;
#ifdef Q_OS_WIN
        local.shellPath = "cmd.exe";
#else
        local.shellPath = "/bin/bash";
#endif
        sessions.append(local);
        return sessions;
    }

    QByteArray data = file.readAll();
    QJsonDocument doc = QJsonDocument::fromJson(data);
    if (doc.isArray()) {
        QJsonArray arr = doc.array();
        for (const QJsonValue& val : arr) {
            sessions.append(Session::fromJson(val.toObject()));
        }
    }
    return sessions;
}

void SessionManager::saveSessions(const QList<Session>& sessions) {
    QString path = getFilePath();
    QFile file(path);
    if (file.open(QIODevice::WriteOnly)) {
        QJsonArray arr;
        for (const Session& s : sessions) {
            arr.append(s.toJson());
        }
        QJsonDocument doc(arr);
        file.write(doc.toJson());
    }
}
