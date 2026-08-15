#include "session.h"
#include "apppaths.h"
#include <QDir>
#include <QFile>
#include <QSaveFile>
#include <QJsonDocument>
#include <QJsonArray>
#include <QUuid>
#include <QRegularExpression>

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
    json["favorite"] = favorite;
    json["type"] = sessionTypeToString(type);
    json["host"] = host;
    json["user"] = user;
    json["port"] = port;
    json["keyPath"] = keyPath;
    json["jumpHost"] = jumpHost;
    json["jumpUser"] = jumpUser;
    json["jumpPort"] = jumpPort;
    json["jumpKeyPath"] = jumpKeyPath;
    json["x11Forwarding"] = x11Forwarding;
    json["autoReconnect"] = autoReconnect;
    json["shellPath"] = shellPath;
    json["serialPort"] = serialPort;
    json["baudRate"] = baudRate;
    json["serialCmd"] = serialCmd;
    json["scrollback"] = scrollback;
    json["fontFamily"] = fontFamily;
    json["fontSize"] = fontSize;
    json["keepAliveSeconds"] = keepAliveSeconds;
    json["cryptCipher"] = cryptCipher;
    json["kexAlgo"] = kexAlgo;
    json["macAlgo"] = macAlgo;
    json["ftpTls"] = ftpTls;

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
    s.favorite = json["favorite"].toBool(false);
    s.type = sessionTypeFromString(json["type"].toString());
    s.host = json["host"].toString();
    s.user = json["user"].toString();
    s.port = json["port"].toInt(22);
    s.keyPath = json["keyPath"].toString();
    s.jumpHost = json["jumpHost"].toString();
    s.jumpUser = json["jumpUser"].toString();
    s.jumpPort = json["jumpPort"].toInt(22);
    s.jumpKeyPath = json["jumpKeyPath"].toString();
    s.x11Forwarding = json["x11Forwarding"].toBool(false);
    s.autoReconnect = json["autoReconnect"].toBool(false);
    s.shellPath = json["shellPath"].toString();
    s.serialPort = json["serialPort"].toString();
    s.baudRate = json["baudRate"].toInt(115200);
    s.serialCmd = json["serialCmd"].toString();
    s.scrollback = json["scrollback"].toInt(5000);
    s.fontFamily = json["fontFamily"].toString();
    s.fontSize = json["fontSize"].toInt(0);
    s.keepAliveSeconds = json["keepAliveSeconds"].toInt(0);
    s.cryptCipher = json["cryptCipher"].toString();
    s.kexAlgo = json["kexAlgo"].toString();
    s.macAlgo = json["macAlgo"].toString();
    s.ftpTls = json.contains("ftpTls") ? json["ftpTls"].toBool(true) : true;

    if (json.contains("tunnels") && json["tunnels"].isArray()) {
        QJsonArray tunnelArray = json["tunnels"].toArray();
        for (const auto& val : tunnelArray) {
            s.tunnels.append(TunnelConfig::fromJson(val.toObject()));
        }
    }

    return s;
}

QString SessionManager::getFilePath() {
    QString configDir = AppPaths::configDir();
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
    QJsonArray arr;
    for (const Session& s : sessions)
        arr.append(s.toJson());

    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly))
        return;
    file.write(QJsonDocument(arr).toJson());
    file.commit();
}

bool SessionManager::exportSessions(const QList<Session>& sessions, const QString& path) {
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly))
        return false;
    QJsonArray arr;
    for (const Session& s : sessions) {
        arr.append(s.toJson());
    }
    QJsonDocument doc(arr);
    if (file.write(doc.toJson()) < 0)
        return false;
    return file.commit();
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
    if (!doc.isArray()) {
        return importOpenSshConfig(path, ok);
    }
    const QJsonArray arr = doc.array();
    for (const QJsonValue& val : arr) {
        if (!val.isObject()) {
            if (ok)
                *ok = false;
            sessions.clear();
            return sessions;
        }
        sessions.append(Session::fromJson(val.toObject()));
    }
    if (ok)
        *ok = true;
    return sessions;
}

QList<Session> SessionManager::importOpenSshConfig(const QString& path, bool* ok) {
    QList<Session> sessions;
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        if (ok)
            *ok = false;
        return sessions;
    }

    Session current;
    bool inHost = false;
    auto finishHost = [&]() {
        if (!inHost || current.host.isEmpty())
            return;
        if (current.name.isEmpty())
            current.name = current.host;
        if (current.id.isEmpty())
            current.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
        current.type = SessionType::SSH;
        sessions.append(current);
    };

    const QString home = QDir::homePath();
    const auto expandPath = [&home](QString value) {
        value = value.trimmed();
        if (value.startsWith("~/"))
            value.replace(0, 1, home);
        return value;
    };

    const QStringList lines = QString::fromUtf8(file.readAll()).split('\n');
    for (QString line : lines) {
        line = line.trimmed();
        if (line.isEmpty() || line.startsWith('#'))
            continue;
        const int comment = line.indexOf('#');
        if (comment >= 0)
            line = line.left(comment).trimmed();
        const QStringList parts = line.split(QRegularExpression("\\s+"), Qt::SkipEmptyParts);
        if (parts.size() < 2)
            continue;

        const QString key = parts.at(0).toLower();
        const QString value = parts.mid(1).join(' ').trimmed();
        if (key == "host") {
            finishHost();
            current = Session();
            const QString alias = parts.at(1);
            inHost = !alias.contains('*') && !alias.contains('?');
            if (inHost)
                current.name = alias;
            continue;
        }
        if (!inHost)
            continue;

        if (key == "hostname")
            current.host = value;
        else if (key == "user")
            current.user = value;
        else if (key == "port") {
            bool valid = false;
            const int port = value.toInt(&valid);
            if (valid && port > 0 && port <= 65535)
                current.port = port;
        }
        else if (key == "identityfile")
            current.keyPath = expandPath(value);
        else if (key == "proxyjump") {
            current.jumpHost = value;
            const int at = current.jumpHost.lastIndexOf('@');
            if (at >= 0) {
                current.jumpUser = current.jumpHost.left(at);
                current.jumpHost = current.jumpHost.mid(at + 1);
            }
            const int colon = current.jumpHost.lastIndexOf(':');
            if (colon > 0) {
                bool valid = false;
                const int port = current.jumpHost.mid(colon + 1).toInt(&valid);
                if (valid && port > 0 && port <= 65535)
                    current.jumpPort = port;
                current.jumpHost = current.jumpHost.left(colon);
            }
        }
    }
    finishHost();
    if (ok)
        *ok = !sessions.isEmpty();
    return sessions;
}
