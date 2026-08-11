#include "session.h"
#include <QStandardPaths>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonArray>
#include <QUuid>

QJsonObject Session::toJson() const {
    QJsonObject json;
    json["id"] = id;
    json["name"] = name;

    QString typeStr = "local";
    if (type == SessionType::SSH)
        typeStr = "ssh";
    else if (type == SessionType::Telnet)
        typeStr = "telnet";
    else if (type == SessionType::Serial)
        typeStr = "serial";

    json["type"] = typeStr;
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

    QString typeStr = json["type"].toString();
    if (typeStr == "ssh")
        s.type = SessionType::SSH;
    else if (typeStr == "telnet")
        s.type = SessionType::Telnet;
    else if (typeStr == "serial")
        s.type = SessionType::Serial;
    else
        s.type = SessionType::Local;

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
        // Return default sessions (Local Shell) if file doesn't exist
        Session local;
        local.id = QUuid::createUuid().toString();
        local.name = "Local Terminal";
        local.type = SessionType::Local;
        local.shellPath = "/bin/bash";
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
