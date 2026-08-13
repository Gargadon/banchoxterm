#include "session.h"
#include <QStandardPaths>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonArray>
#include <QUuid>

static QString tunnelTypeToString(TunnelConfig::Type type) {
    switch (type) {
    case TunnelConfig::Type::Local:
        return "local";
    case TunnelConfig::Type::Remote:
        return "remote";
    case TunnelConfig::Type::Dynamic:
        return "dynamic";
    }
    return "local";
}

static TunnelConfig::Type tunnelTypeFromString(const QString& s) {
    if (s == "remote")
        return TunnelConfig::Type::Remote;
    if (s == "dynamic")
        return TunnelConfig::Type::Dynamic;
    return TunnelConfig::Type::Local;
}

QJsonObject TunnelConfig::toJson() const {
    QJsonObject json;
    json["type"] = tunnelTypeToString(type);
    json["localPort"] = localPort;
    json["remoteHost"] = remoteHost;
    json["remotePort"] = remotePort;
    return json;
}

TunnelConfig TunnelConfig::fromJson(const QJsonObject& json) {
    TunnelConfig c;
    c.type = tunnelTypeFromString(json["type"].toString());
    c.localPort = json["localPort"].toInt();
    c.remoteHost = json["remoteHost"].toString();
    c.remotePort = json["remotePort"].toInt();
    return c;
}

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
    case SessionType::FTP:
        return "ftp";
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
    if (s == "ftp")
        return SessionType::FTP;
    return SessionType::Local;
}

QJsonObject Session::toJson() const {
    QJsonObject json;
    json["id"] = id;
    json["name"] = name;
    json["group"] = group;
    json["type"] = sessionTypeToString(type);
    json["host"] = host;
    json["user"] = user;
    json["port"] = port;
    json["keyPath"] = keyPath;
    json["x11Forwarding"] = x11Forwarding;
    json["autoReconnect"] = autoReconnect;
    json["shellPath"] = shellPath;
    json["serialPort"] = serialPort;
    json["baudRate"] = baudRate;
    json["serialCmd"] = serialCmd;

    QJsonArray tunnelArray;
    for (const auto& t : tunnels) {
        tunnelArray.append(t.toJson());
    }
    json["tunnels"] = tunnelArray;

    return json;
}

Session Session::fromJson(const QJsonObject& json) {
    Session s;
    s.id = json["id"].toString();
    if (s.id.isEmpty()) {
        s.id = QUuid::createUuid().toString();
    }
    s.name = json["name"].toString();
    s.group = json["group"].toString();
    s.type = sessionTypeFromString(json["type"].toString());
    s.host = json["host"].toString();
    s.user = json["user"].toString();
    s.port = json["port"].toInt(22);
    s.keyPath = json["keyPath"].toString();
    s.x11Forwarding = json["x11Forwarding"].toBool(false);
    s.autoReconnect = json["autoReconnect"].toBool(false);
    s.shellPath = json["shellPath"].toString();
    s.serialPort = json["serialPort"].toString();
    s.baudRate = json["baudRate"].toInt(115200);
    s.serialCmd = json["serialCmd"].toString();

    if (json.contains("tunnels") && json["tunnels"].isArray()) {
        QJsonArray tunnelArray = json["tunnels"].toArray();
        for (const auto& val : tunnelArray) {
            s.tunnels.append(TunnelConfig::fromJson(val.toObject()));
        }
    }

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

bool SessionManager::exportSessions(const QList<Session>& sessions, const QString& path) {
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly))
        return false;
    QJsonArray arr;
    for (const Session& s : sessions) {
        arr.append(s.toJson());
    }
    QJsonDocument doc(arr);
    file.write(doc.toJson());
    return true;
}

QList<Session> SessionManager::importSessions(const QString& path, bool* ok) {
    QList<Session> sessions;
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        if (ok)
            *ok = false;
        return sessions;
    }
    QByteArray data = file.readAll();
    QJsonDocument doc = QJsonDocument::fromJson(data);
    if (doc.isArray()) {
        const QJsonArray arr = doc.array();
        for (const QJsonValue& val : arr) {
            sessions.append(Session::fromJson(val.toObject()));
        }
    }
    if (ok)
        *ok = true;
    return sessions;
}
