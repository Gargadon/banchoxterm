#include "session.h"
#include "apppaths.h"
#include "masterpasswordmanager.h"
#include <QDir>
#include <QFile>
#include <QSaveFile>
#include <QJsonDocument>
#include <QJsonArray>
#include <QUuid>
#include <QRegularExpression>
#include <QUrl>
#include <QTextStream>
#include <QSettings>
#include <QFileInfo>
#include <QXmlStreamReader>

namespace {
bool setPrivateFilePermissions(const QString& path) {
    return QFile::setPermissions(path, QFileDevice::ReadOwner | QFileDevice::WriteOwner);
}
} // namespace

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
    json["socksUsername"] = socksUsername;
    return json;
}

TunnelConfig TunnelConfig::fromJson(const QJsonObject& json) {
    TunnelConfig c;
    c.type = tunnelTypeFromString(json["type"].toString());
    c.localPort = json["localPort"].toInt();
    c.remoteHost = json["remoteHost"].toString();
    c.remotePort = json["remotePort"].toInt();
    c.socksUsername = json["socksUsername"].toString();
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
    json["remoteDirectory"] = remoteDirectory;
    json["jumpHost"] = jumpHost;
    json["jumpUser"] = jumpUser;
    json["jumpPort"] = jumpPort;
    json["jumpKeyPath"] = jumpKeyPath;
    json["x11Forwarding"] = x11Forwarding;
    json["autoReconnect"] = autoReconnect;
    json["readOnly"] = readOnly;
    json["shellPath"] = shellPath;
    json["serialPort"] = serialPort;
    json["baudRate"] = baudRate;
    json["serialCmd"] = serialCmd;
    json["serialDataBits"] = serialDataBits;
    json["serialParity"] = serialParity;
    json["serialStopBits"] = serialStopBits;
    json["serialFlowControl"] = serialFlowControl;
    json["serialDtr"] = serialDtr;
    json["serialRts"] = serialRts;
    json["scrollback"] = scrollback;
    json["fontFamily"] = fontFamily;
    json["fontSize"] = fontSize;
    json["colorScheme"] = colorScheme;
    json["terminalType"] = terminalType;
    json["manufacturerProfile"] = manufacturerProfile;
    json["language"] = language;
    json["promptPattern"] = promptPattern;
    json["encoding"] = encoding;
    json["backspaceSequence"] = backspaceSequence;
    json["enterSequence"] = enterSequence;
    json["ciscoBreakSequence"] = ciscoBreakSequence;
    json["initialRows"] = initialRows;
    json["initialColumns"] = initialColumns;
    json["keepAliveSeconds"] = keepAliveSeconds;
    json["cryptCipher"] = cryptCipher;
    json["kexAlgo"] = kexAlgo;
    json["macAlgo"] = macAlgo;
    json["ftpTls"] = ftpTls;
    json["ftpTlsMinimumVersion"] = ftpTlsMinimumVersion;
    json["ftpTlsCaFile"] = ftpTlsCaFile;
    json["ftpTlsAllowInvalidCertificates"] = ftpTlsAllowInvalidCertificates;

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
    s.remoteDirectory = json["remoteDirectory"].toString();
    s.jumpHost = json["jumpHost"].toString();
    s.jumpUser = json["jumpUser"].toString();
    s.jumpPort = json["jumpPort"].toInt(22);
    s.jumpKeyPath = json["jumpKeyPath"].toString();
    s.x11Forwarding = json["x11Forwarding"].toBool(false);
    s.autoReconnect = json["autoReconnect"].toBool(false);
    s.readOnly = json["readOnly"].toBool(false);
    s.shellPath = json["shellPath"].toString();
    s.serialPort = json["serialPort"].toString();
    s.baudRate = json["baudRate"].toInt(115200);
    s.serialCmd = json["serialCmd"].toString();
    s.serialDataBits = json["serialDataBits"].toInt(8);
    s.serialParity = json["serialParity"].toInt(0);
    s.serialStopBits = json["serialStopBits"].toInt(1);
    s.serialFlowControl = json["serialFlowControl"].toInt(0);
    s.serialDtr = json.contains("serialDtr") ? json["serialDtr"].toBool(true) : true;
    s.serialRts = json.contains("serialRts") ? json["serialRts"].toBool(true) : true;
    s.scrollback = json["scrollback"].toInt(5000);
    s.fontFamily = json["fontFamily"].toString();
    s.fontSize = json["fontSize"].toInt(0);
    s.colorScheme = json["colorScheme"].toString();
    s.terminalType = json["terminalType"].toString("xterm-256color");
    s.manufacturerProfile = json["manufacturerProfile"].toString(QStringLiteral("generic"));
    s.language = json["language"].toString();
    s.promptPattern = json["promptPattern"].toString();
    s.encoding = json["encoding"].toString("UTF-8");
    s.backspaceSequence =
        json.contains("backspaceSequence") ? json["backspaceSequence"].toString() : QString(QChar(0x7f));
    s.enterSequence = json.contains("enterSequence") ? json["enterSequence"].toString() : QString(QChar('\r'));
    if (s.backspaceSequence.isEmpty())
        s.backspaceSequence = QString(QChar(0x7f));
    if (s.enterSequence.isEmpty())
        s.enterSequence = QString(QChar('\r'));
    s.ciscoBreakSequence = json["ciscoBreakSequence"].toString(QStringLiteral("1E")).remove(' ');
    if (s.ciscoBreakSequence.isEmpty())
        s.ciscoBreakSequence = QStringLiteral("1E");
    s.initialRows = json["initialRows"].toInt(24);
    s.initialColumns = json["initialColumns"].toInt(80);
    s.keepAliveSeconds = json["keepAliveSeconds"].toInt(0);
    s.cryptCipher = json["cryptCipher"].toString();
    s.kexAlgo = json["kexAlgo"].toString();
    s.macAlgo = json["macAlgo"].toString();
    s.ftpTls = json.contains("ftpTls") ? json["ftpTls"].toBool(true) : true;
    s.ftpTlsMinimumVersion = json["ftpTlsMinimumVersion"].toInt(12);
    if (s.ftpTlsMinimumVersion != 13)
        s.ftpTlsMinimumVersion = 12;
    s.ftpTlsCaFile = json["ftpTlsCaFile"].toString();
    s.ftpTlsAllowInvalidCertificates = json["ftpTlsAllowInvalidCertificates"].toBool(false);

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
    if (!file.commit())
        return;
    setPrivateFilePermissions(path);
}

bool SessionManager::exportSessions(const QList<Session>& sessions, const QString& path) {
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly))
        return false;
    QJsonArray arr;
    for (const Session& s : sessions) {
        arr.append(s.toJson());
    }
    QSettings settings;
    const QStringList macroNames = settings.value("macros/names").toStringList();
    const QStringList macroTexts = settings.value("macros/texts").toStringList();
    QJsonDocument doc;
    if (!macroNames.isEmpty()) {
        QJsonArray macros;
        for (int i = 0; i < macroNames.size() && i < macroTexts.size(); ++i) {
            QJsonObject macro;
            macro.insert(QStringLiteral("name"), macroNames.at(i));
            macro.insert(QStringLiteral("text"), macroTexts.at(i));
            macros.append(macro);
        }
        QJsonObject bundle;
        bundle.insert(QStringLiteral("sessions"), arr);
        bundle.insert(QStringLiteral("macros"), macros);
        doc = QJsonDocument(bundle);
    } else {
        doc = QJsonDocument(arr);
    }
    if (file.write(doc.toJson()) < 0)
        return false;
    if (!file.commit())
        return false;
    return setPrivateFilePermissions(path);
}

bool SessionManager::exportEncryptedSessions(const QList<Session>& sessions, const QString& path) {
    QJsonArray arr;
    for (const Session& s : sessions)
        arr.append(s.toJson());

    QSettings settings;
    const QStringList macroNames = settings.value("macros/names").toStringList();
    const QStringList macroTexts = settings.value("macros/texts").toStringList();
    QJsonObject bundle;
    bundle.insert(QStringLiteral("sessions"), arr);
    if (!macroNames.isEmpty()) {
        QJsonArray macros;
        for (int i = 0; i < macroNames.size() && i < macroTexts.size(); ++i) {
            QJsonObject macro;
            macro.insert(QStringLiteral("name"), macroNames.at(i));
            macro.insert(QStringLiteral("text"), macroTexts.at(i));
            macros.append(macro);
        }
        bundle.insert(QStringLiteral("macros"), macros);
    }

    const QString encrypted = MasterPasswordManager::instance().encryptPassword(
        QString::fromUtf8(QJsonDocument(bundle).toJson(QJsonDocument::Compact)));
    if (encrypted.isEmpty())
        return false;

    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text))
        return false;
    if (file.write(encrypted.toUtf8()) < 0 || !file.commit())
        return false;
    return setPrivateFilePermissions(path);
}

bool SessionManager::exportOpenSshConfig(const QList<Session>& sessions, const QString& path) {
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text))
        return false;

    QTextStream stream(&file);
    stream << "# Exported by BanchoXterm; credentials are intentionally omitted.\n\n";
    int exported = 0;
    for (const Session& session : sessions) {
        if (session.type != SessionType::SSH || session.host.trimmed().isEmpty())
            continue;

        QString alias = session.name.trimmed();
        if (alias.isEmpty())
            alias = session.host.trimmed();
        alias.replace(QRegularExpression(QStringLiteral("\\s+")), QStringLiteral("_"));
        stream << "Host " << alias << "\n";
        stream << "    HostName " << session.host.trimmed() << "\n";
        if (!session.user.trimmed().isEmpty())
            stream << "    User " << session.user.trimmed() << "\n";
        if (session.port > 0 && session.port != 22)
            stream << "    Port " << session.port << "\n";
        if (!session.keyPath.trimmed().isEmpty())
            stream << "    IdentityFile " << session.keyPath.trimmed() << "\n";
        if (!session.jumpHost.trimmed().isEmpty()) {
            QString jump = session.jumpHost.trimmed();
            if (!session.jumpUser.trimmed().isEmpty())
                jump.prepend(session.jumpUser.trimmed() + '@');
            if (session.jumpPort > 0 && session.jumpPort != 22)
                jump += ':' + QString::number(session.jumpPort);
            stream << "    ProxyJump " << jump << "\n";
        }
        stream << "\n";
        ++exported;
    }
    if (exported == 0)
        stream << "# No SSH sessions were available for export.\n";
    stream.flush();
    if (!file.commit())
        return false;
    return setPrivateFilePermissions(path);
}

QList<Session> SessionManager::importSessions(const QString& path, bool* ok, QStringList* importedMacroNames,
                                              QStringList* importedMacroTexts) {
    QList<Session> sessions;
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        if (ok)
            *ok = false;
        return sessions;
    }
    QByteArray data = file.readAll();
    QJsonDocument doc = QJsonDocument::fromJson(data);
    if (doc.isNull() && (data.startsWith("BANCHO2:") || data.startsWith("BANCHO:"))) {
        const QString decrypted = MasterPasswordManager::instance().decryptPassword(QString::fromUtf8(data));
        if (decrypted.isEmpty()) {
            if (ok)
                *ok = false;
            return sessions;
        }
        doc = QJsonDocument::fromJson(decrypted.toUtf8());
    }
    const bool isSessionBundle = doc.isObject() && doc.object().value(QStringLiteral("sessions")).isArray();
    if (!doc.isArray() && !isSessionBundle) {
        if (path.endsWith(".reg", Qt::CaseInsensitive))
            return importPuTTYRegistry(path, ok);
        if (path.endsWith(".mxtsessions", Qt::CaseInsensitive))
            return importMobaXtermSessions(path, ok);
        if (path.endsWith(".ini", Qt::CaseInsensitive))
            return importSecureCrtSession(path, ok);
        if (path.endsWith(".rtsx", Qt::CaseInsensitive) || path.endsWith(".rts", Qt::CaseInsensitive))
            return importRoyalTsDocument(path, ok);
        return importOpenSshConfig(path, ok);
    }
    QJsonArray arr;
    if (doc.isArray()) {
        arr = doc.array();
    } else if (doc.isObject() && doc.object().value(QStringLiteral("sessions")).isArray()) {
        arr = doc.object().value(QStringLiteral("sessions")).toArray();
        const QJsonValue macrosValue = doc.object().value(QStringLiteral("macros"));
        if (macrosValue.isArray() && importedMacroNames && importedMacroTexts) {
            for (const QJsonValue& value : macrosValue.toArray()) {
                if (!value.isObject())
                    continue;
                const QJsonObject macro = value.toObject();
                const QString name = macro.value(QStringLiteral("name")).toString().trimmed();
                if (name.isEmpty() || !macro.value(QStringLiteral("text")).isString())
                    continue;
                importedMacroNames->append(name);
                importedMacroTexts->append(macro.value(QStringLiteral("text")).toString());
            }
        }
    } else {
        if (ok)
            *ok = false;
        return sessions;
    }
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
        } else if (key == "identityfile")
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

QList<Session> SessionManager::importPuTTYRegistry(const QString& path, bool* ok) {
    QList<Session> sessions;
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        if (ok)
            *ok = false;
        return sessions;
    }

    Session current;
    bool inSession = false;
    const auto finishSession = [&]() {
        if (!inSession || current.host.isEmpty())
            return;
        if (current.id.isEmpty())
            current.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
        if (current.name.isEmpty())
            current.name = current.host;
        current.type = SessionType::SSH;
        sessions.append(current);
    };

    const auto decodeValue = [](QString value) {
        value = value.trimmed();
        if (value.startsWith('"') && value.endsWith('"') && value.size() >= 2) {
            value = value.mid(1, value.size() - 2);
            value.replace("\\\\", "\\");
            value.replace("\\\"", "\"");
        }
        return QUrl::fromPercentEncoding(value.toUtf8());
    };

    const QStringList lines = QString::fromUtf8(file.readAll()).split('\n');
    const QRegularExpression sectionPattern(R"(^\[HKEY_CURRENT_USER\\Software\\SimonTatham\\PuTTY\\Sessions\\(.+)\]$)");
    for (QString line : lines) {
        line = line.trimmed();
        const QRegularExpressionMatch section = sectionPattern.match(line);
        if (section.hasMatch()) {
            finishSession();
            current = Session();
            current.name = QUrl::fromPercentEncoding(section.captured(1).toUtf8());
            current.type = SessionType::SSH;
            current.port = 22;
            inSession = true;
            continue;
        }
        if (!inSession || line.isEmpty() || line.startsWith(';') || !line.startsWith('"'))
            continue;

        const int separator = line.indexOf("\"=");
        if (separator < 0)
            continue;
        const QString key = line.mid(1, separator - 1);
        const QString value = line.mid(separator + 2);
        if (key == "HostName") {
            current.host = decodeValue(value);
        } else if (key == "UserName") {
            current.user = decodeValue(value);
        } else if (key == "PortNumber") {
            const QString number = value.trimmed();
            bool valid = false;
            int port = 0;
            if (number.startsWith("dword:", Qt::CaseInsensitive))
                port = number.mid(6).toInt(&valid, 16);
            else
                port = number.toInt(&valid);
            if (valid && port > 0 && port <= 65535)
                current.port = port;
        } else if (key == "PublicKeyFile") {
            current.keyPath = decodeValue(value);
        } else if (key == "Compression") {
            // PuTTY stores this as a DWORD. Preserve the setting by using the
            // libssh2 default when compression is disabled; no password data
            // is imported from registry exports.
            Q_UNUSED(value);
        }
    }
    finishSession();
    if (ok)
        *ok = !sessions.isEmpty();
    return sessions;
}

QList<Session> SessionManager::importMobaXtermSessions(const QString& path, bool* ok) {
    QList<Session> sessions;
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        if (ok)
            *ok = false;
        return sessions;
    }

    Session current;
    bool inBookmark = false;
    const auto finish = [&]() {
        if (!inBookmark || current.host.trimmed().isEmpty())
            return;
        current.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
        if (current.name.trimmed().isEmpty())
            current.name = current.host;
        if (current.port <= 0)
            current.port = 22;
        current.type = SessionType::SSH;
        sessions.append(current);
    };

    for (QString line : QString::fromUtf8(file.readAll()).split('\n')) {
        line = line.trimmed();
        if (line.startsWith('[') && line.endsWith(']')) {
            finish();
            current = Session();
            const QString section = line.mid(1, line.size() - 2);
            inBookmark = section.startsWith(QStringLiteral("Bookmarks\\"), Qt::CaseInsensitive) ||
                         section.startsWith(QStringLiteral("Sessions\\"), Qt::CaseInsensitive);
            if (inBookmark)
                current.name = section.mid(section.indexOf('\\') + 1).replace('\\', '/');
            continue;
        }
        if (!inBookmark || line.isEmpty() || line.startsWith(';') || line.startsWith('#'))
            continue;
        const int separator = line.indexOf('=');
        if (separator < 0)
            continue;
        const QString key = line.left(separator).trimmed().toLower();
        const QString value = line.mid(separator + 1).trimmed();
        if (key == QStringLiteral("hostname") || key == QStringLiteral("host"))
            current.host = value;
        else if (key == QStringLiteral("username") || key == QStringLiteral("user"))
            current.user = value;
        else if (key == QStringLiteral("port")) {
            bool valid = false;
            const int port = value.toInt(&valid);
            if (valid && port > 0 && port <= 65535)
                current.port = port;
        } else if (key == QStringLiteral("name"))
            current.name = value;
        else if (key == QStringLiteral("protocol") && value.compare(QStringLiteral("ssh"), Qt::CaseInsensitive) != 0)
            inBookmark = false;
    }
    finish();
    if (ok)
        *ok = !sessions.isEmpty();
    return sessions;
}

QList<Session> SessionManager::importSecureCrtSession(const QString& path, bool* ok) {
    QList<Session> sessions;
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        if (ok)
            *ok = false;
        return sessions;
    }

    Session current;
    current.type = SessionType::SSH;
    current.name = QFileInfo(path).completeBaseName();
    current.port = 22;
    const auto decode = [](QString value) {
        value = value.trimmed();
        if (value.startsWith('"') && value.endsWith('"') && value.size() >= 2)
            value = value.mid(1, value.size() - 2);
        value.replace(QStringLiteral("\\\""), QStringLiteral("\""));
        return value;
    };

    for (QString line : QString::fromUtf8(file.readAll()).split('\n')) {
        line = line.trimmed();
        if (line.isEmpty() || line.startsWith(';') || line.startsWith('#'))
            continue;
        const int separator = line.indexOf('=');
        if (separator < 0)
            continue;
        QString key = line.left(separator).trimmed();
        const QString value = decode(line.mid(separator + 1));
        const int firstQuote = key.indexOf('"');
        const int lastQuote = key.lastIndexOf('"');
        if (firstQuote >= 0 && lastQuote > firstQuote)
            key = key.mid(firstQuote + 1, lastQuote - firstQuote - 1);
        key = key.toLower();
        if (key == QStringLiteral("hostname"))
            current.host = value;
        else if (key == QStringLiteral("username"))
            current.user = value;
        else if (key == QStringLiteral("session name"))
            current.name = value;
        else if (key == QStringLiteral("port")) {
            bool valid = false;
            int port = 0;
            if (value.size() == 8)
                port = value.toInt(&valid, 16);
            else
                port = value.toInt(&valid);
            if (!valid && value.startsWith(QStringLiteral("0x"), Qt::CaseInsensitive))
                port = value.mid(2).toInt(&valid, 16);
            if (valid && port > 0 && port <= 65535)
                current.port = port;
        }
    }
    if (!current.host.trimmed().isEmpty()) {
        current.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
        if (current.name.trimmed().isEmpty())
            current.name = current.host;
        sessions.append(current);
    }
    if (ok)
        *ok = !sessions.isEmpty();
    return sessions;
}

QList<Session> SessionManager::importRoyalTsDocument(const QString& path, bool* ok) {
    QList<Session> sessions;
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        if (ok)
            *ok = false;
        return sessions;
    }

    QXmlStreamReader xml(&file);
    Session current;
    QString property;
    QString propertyValue;
    bool inRoyalConnection = false;
    QString objectType;
    int depth = 0;
    int objectDepth = -1;
    const auto finish = [&]() {
        if (current.host.trimmed().isEmpty())
            return;
        current.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
        if (current.name.trimmed().isEmpty())
            current.name = current.host;
        if (current.port <= 0)
            current.port = 22;
        sessions.append(current);
    };
    const auto assignProperty = [&]() {
        const QString value = propertyValue.trimmed();
        if (property.compare(QStringLiteral("Name"), Qt::CaseInsensitive) == 0)
            current.name = value;
        else if (property.compare(QStringLiteral("URI"), Qt::CaseInsensitive) == 0)
            current.host = value;
        else if (property.compare(QStringLiteral("CredentialUsername"), Qt::CaseInsensitive) == 0)
            current.user = value;
        else if (property.compare(QStringLiteral("ConnectionType"), Qt::CaseInsensitive) == 0 &&
                 value.compare(QStringLiteral("telnet"), Qt::CaseInsensitive) == 0)
            current.type = SessionType::Telnet;
        else if (property.compare(QStringLiteral("Port"), Qt::CaseInsensitive) == 0) {
            bool valid = false;
            const int port = value.toInt(&valid);
            if (valid && port > 0 && port <= 65535)
                current.port = port;
        }
    };

    while (!xml.atEnd()) {
        xml.readNext();
        if (xml.isStartElement()) {
            ++depth;
            const QString element = xml.name().toString();
            if (element.compare(QStringLiteral("RoyalSSHConnection"), Qt::CaseInsensitive) == 0 ||
                element.compare(QStringLiteral("RoyalRDSConnection"), Qt::CaseInsensitive) == 0 ||
                element.compare(QStringLiteral("RoyalVNCConnection"), Qt::CaseInsensitive) == 0) {
                current = Session();
                current.type = element.compare(QStringLiteral("RoyalRDSConnection"), Qt::CaseInsensitive) == 0
                                   ? SessionType::RDP
                                   : (element.compare(QStringLiteral("RoyalVNCConnection"), Qt::CaseInsensitive) == 0
                                          ? SessionType::VNC
                                          : SessionType::SSH);
                current.port = current.type == SessionType::RDP ? 3389 : (current.type == SessionType::VNC ? 5900 : 22);
                inRoyalConnection = true;
                objectType = element;
                objectDepth = depth;
            } else if (inRoyalConnection && depth == objectDepth + 1) {
                property = element;
                propertyValue.clear();
            }
        } else if (xml.isCharacters() && !xml.isWhitespace() && inRoyalConnection && depth == objectDepth + 1 &&
                   !property.isEmpty()) {
            propertyValue += xml.text().toString();
        } else if (xml.isEndElement()) {
            const QString element = xml.name().toString();
            if (inRoyalConnection && depth == objectDepth + 1 && !property.isEmpty()) {
                assignProperty();
                property.clear();
                propertyValue.clear();
            }
            if (inRoyalConnection && depth == objectDepth && element == objectType) {
                finish();
                inRoyalConnection = false;
                objectType.clear();
                objectDepth = -1;
            }
            --depth;
        }
    }

    if (xml.hasError()) {
        sessions.clear();
        if (ok)
            *ok = false;
        return sessions;
    }
    if (ok)
        *ok = !sessions.isEmpty();
    return sessions;
}
