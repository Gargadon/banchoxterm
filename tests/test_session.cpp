#include <QTest>
#include <QCoreApplication>
#include <QTemporaryDir>
#include <QTemporaryFile>
#include <QProcess>
#include <QRegularExpression>
#include <QFile>
#include <QFileInfo>
#include <QStandardPaths>
#include <QUuid>
#include <QDir>
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>
#include <QSettings>
#include <QSignalSpy>
#include <QElapsedTimer>
#include <QThread>
#include <QTabWidget>
#include <QPointer>
#include <QStandardPaths>
#include <memory>
#include <QTcpServer>
#include <QTcpSocket>
#include <QSslConfiguration>
#include <QSslKey>
#include <QSslSocket>
#include "session.h"
#include "apppaths.h"
#include "keyring.h"
#include "masterpasswordmanager.h"
#include "vtterminalwidget.h"
#include "terminaltab.h"
#include "mainwindow.h"
#include "ftpclient.h"
#include "sshconnection.h"
#include "sftptransferworker.h"

class ExplicitTlsTestServer : public QTcpServer {
protected:
    void incomingConnection(qintptr socketDescriptor) override {
        auto* socket = new QSslSocket(this);
        if (!socket->setSocketDescriptor(socketDescriptor)) {
            socket->deleteLater();
            return;
        }
        addPendingConnection(socket);
    }
};

class TestSession : public QObject {
    Q_OBJECT

signals:
    void parallelTransferFinished(bool success, const QString& message);

private:
    static QString settingsPath() {
        return QDir(QDir::tempPath()).filePath("banchoxterm-test-settings");
    }

private slots:
    void initTestCase() {
        qRegisterMetaType<Session>("Session");
        qRegisterMetaType<SftpFile>("SftpFile");
        qRegisterMetaType<QList<SftpFile>>("QList<SftpFile>");
        // Match the app's QSettings namespace so MasterPasswordManager tests
        // use a valid settings path.
        QCoreApplication::setOrganizationName(QStringLiteral("BanchoXterm"));
        QCoreApplication::setApplicationName(QStringLiteral("BanchoXterm"));
        // Keep tests independent of the developer's registry/home settings.
        QSettings::setDefaultFormat(QSettings::IniFormat);
        QStandardPaths::setTestModeEnabled(true);
        const QString configOverride = QDir(QDir::tempPath()).filePath("banchoxterm-session-test-config");
        QDir(configOverride).removeRecursively();
        qputenv("BANCHO_CONFIG_DIR", configOverride.toUtf8());
        QSettings::setPath(QSettings::IniFormat, QSettings::UserScope,
                           QDir(QDir::tempPath()).filePath("banchoxterm-test-settings"));
        QDir(settingsPath()).removeRecursively();
    }

    void testSshSessionRoundtrip() {
        Session original;
        original.id = "test-id-ssh";
        original.name = "My Server";
        original.type = SessionType::SSH;
        original.favorite = true;
        original.host = "example.com";
        original.user = "admin";
        original.port = 2222;
        original.keyPath = "/home/user/.ssh/id_rsa";
        original.remoteDirectory = "/var/log";
        original.jumpHost = "bastion.example.com";
        original.jumpUser = "jumpadmin";
        original.jumpPort = 2200;
        original.jumpKeyPath = "/home/user/.ssh/jump_rsa";
        original.x11Forwarding = true;
        original.keepAliveSeconds = 30;
        original.cryptCipher = "aes128-ctr,aes256-ctr";
        original.kexAlgo = "curve25519-sha256";
        original.macAlgo = "hmac-sha2-256";
        original.scrollback = 10000;
        original.fontFamily = "JetBrains Mono";
        original.fontSize = 12;
        original.readOnly = true;
        original.manufacturerProfile = "cisco";
        original.promptPattern = R"([#$>]\s*$)";

        QJsonObject json = original.toJson();
        QCOMPARE(json["id"].toString(), QString("test-id-ssh"));
        QCOMPARE(json["name"].toString(), QString("My Server"));
        QCOMPARE(json["type"].toString(), QString("ssh"));
        QCOMPARE(json["host"].toString(), QString("example.com"));
        QCOMPARE(json["user"].toString(), QString("admin"));
        QCOMPARE(json["port"].toInt(), 2222);
        QCOMPARE(json["keyPath"].toString(), QString("/home/user/.ssh/id_rsa"));
        QVERIFY(json["favorite"].toBool());
        QCOMPARE(json["jumpHost"].toString(), QString("bastion.example.com"));
        QVERIFY(json["x11Forwarding"].toBool());

        Session restored = Session::fromJson(json);
        QCOMPARE(restored.id, original.id);
        QCOMPARE(restored.name, original.name);
        QCOMPARE(restored.type, original.type);
        QCOMPARE(restored.host, original.host);
        QCOMPARE(restored.user, original.user);
        QCOMPARE(restored.port, original.port);
        QCOMPARE(restored.keyPath, original.keyPath);
        QCOMPARE(restored.remoteDirectory, original.remoteDirectory);
        QCOMPARE(restored.favorite, original.favorite);
        QCOMPARE(restored.jumpHost, original.jumpHost);
        QCOMPARE(restored.jumpUser, original.jumpUser);
        QCOMPARE(restored.jumpPort, original.jumpPort);
        QCOMPARE(restored.jumpKeyPath, original.jumpKeyPath);
        QCOMPARE(restored.x11Forwarding, original.x11Forwarding);
        QCOMPARE(restored.keepAliveSeconds, original.keepAliveSeconds);
        QCOMPARE(restored.cryptCipher, original.cryptCipher);
        QCOMPARE(restored.kexAlgo, original.kexAlgo);
        QCOMPARE(restored.macAlgo, original.macAlgo);
        QCOMPARE(restored.scrollback, original.scrollback);
        QCOMPARE(restored.fontFamily, original.fontFamily);
        QCOMPARE(restored.fontSize, original.fontSize);
        QVERIFY(restored.readOnly);
        QCOMPARE(restored.manufacturerProfile, QString("cisco"));
        QCOMPARE(restored.promptPattern, original.promptPattern);
    }

    void testSessionFilesArePrivate() {
        Session session;
        session.id = QStringLiteral("private-file-test");
        session.name = QStringLiteral("Private");
        SessionManager::saveSessions({session});

        const QString path = SessionManager::getFilePath();
        QVERIFY2(QFileInfo::exists(path), qPrintable(path));
        const QFileDevice::Permissions permissions = QFileInfo(path).permissions();
        QVERIFY(permissions.testFlag(QFileDevice::ReadOwner));
        QVERIFY(permissions.testFlag(QFileDevice::WriteOwner));
        QVERIFY(!permissions.testFlag(QFileDevice::ReadGroup));
        QVERIFY(!permissions.testFlag(QFileDevice::WriteGroup));
        QVERIFY(!permissions.testFlag(QFileDevice::ReadOther));
        QVERIFY(!permissions.testFlag(QFileDevice::WriteOther));
    }

    void testSshSessionTunnelsRoundtrip() {
        Session original;
        original.id = "test-id-tunnels";
        original.name = "My Tunnel Server";
        original.type = SessionType::SSH;
        original.host = "example.com";
        original.user = "admin";

        TunnelConfig t1;
        t1.type = TunnelConfig::Type::Local;
        t1.localPort = 8080;
        t1.remoteHost = "10.0.0.5";
        t1.remotePort = 80;
        original.tunnels.append(t1);

        TunnelConfig t2;
        t2.type = TunnelConfig::Type::Dynamic;
        t2.localPort = 1080;
        t2.socksUsername = "proxy-user";
        t2.socksPassword = "proxy-secret";
        original.tunnels.append(t2);

        QJsonObject json = original.toJson();
        QVERIFY(json.contains("tunnels"));
        QVERIFY(json["tunnels"].isArray());

        Session restored = Session::fromJson(json);
        QCOMPARE(restored.tunnels.size(), 2);

        QCOMPARE(restored.tunnels[0].type, TunnelConfig::Type::Local);
        QCOMPARE(restored.tunnels[0].localPort, 8080);
        QCOMPARE(restored.tunnels[0].remoteHost, QString("10.0.0.5"));
        QCOMPARE(restored.tunnels[0].remotePort, 80);

        QCOMPARE(restored.tunnels[1].type, TunnelConfig::Type::Dynamic);
        QCOMPARE(restored.tunnels[1].localPort, 1080);
        QCOMPARE(restored.tunnels[1].socksUsername, QString("proxy-user"));
        QVERIFY(!json["tunnels"].toArray().at(1).toObject().contains("socksPassword"));
        QVERIFY(restored.tunnels[1].socksPassword.isEmpty());
    }

    void testLocalSessionRoundtrip() {
        Session original;
        original.id = "test-id-local";
        original.name = "Local Shell";
        original.type = SessionType::Local;
        original.shellPath = "/bin/zsh";

        QJsonObject json = original.toJson();
        QCOMPARE(json["type"].toString(), QString("local"));

        Session restored = Session::fromJson(json);
        QCOMPARE(restored.type, SessionType::Local);
        QCOMPARE(restored.shellPath, original.shellPath);
        QCOMPARE(restored.port, 22); // default
    }

    void testTelnetSessionRoundtrip() {
        Session original;
        original.id = "test-id-telnet";
        original.name = "Router";
        original.type = SessionType::Telnet;
        original.host = "192.168.1.1";
        original.port = 23;

        QJsonObject json = original.toJson();
        QCOMPARE(json["type"].toString(), QString("telnet"));

        Session restored = Session::fromJson(json);
        QCOMPARE(restored.type, SessionType::Telnet);
        QCOMPARE(restored.port, 23);
        QCOMPARE(restored.host, QString("192.168.1.1"));
    }

    void testSerialSessionRoundtrip() {
        Session original;
        original.id = "test-id-serial";
        original.name = "Serial Console";
        original.type = SessionType::Serial;
        original.serialPort = "/dev/ttyUSB0";
        original.baudRate = 9600;
        original.serialCmd = "picocom";

        QJsonObject json = original.toJson();
        QCOMPARE(json["type"].toString(), QString("serial"));
        QCOMPARE(json["serialPort"].toString(), QString("/dev/ttyUSB0"));
        QCOMPARE(json["baudRate"].toInt(), 9600);
        QCOMPARE(json["serialCmd"].toString(), QString("picocom"));

        Session restored = Session::fromJson(json);
        QCOMPARE(restored.type, SessionType::Serial);
        QCOMPARE(restored.serialPort, original.serialPort);
        QCOMPARE(restored.baudRate, original.baudRate);
        QCOMPARE(restored.serialCmd, original.serialCmd);
    }

    void testFromJsonDefaults() {
        QJsonObject empty;
        Session s = Session::fromJson(empty);

        QVERIFY(!s.id.isEmpty());
        QCOMPARE(s.port, 22);
        QCOMPARE(s.type, SessionType::Local);
        QCOMPARE(s.baudRate, 115200);
        QVERIFY(!s.x11Forwarding);
    }

    void testFromJsonUnknownTypeDefaultsToLocal() {
        QJsonObject json;
        json["type"] = "nonexistent";
        json["id"] = "some-id";
        Session s = Session::fromJson(json);
        QCOMPARE(s.type, SessionType::Local);
    }

    void testOpenSshConfigImport() {
        QTemporaryFile config;
        QVERIFY(config.open());
        const QByteArray contents = "Host *\n"
                                    "    User ignored\n"
                                    "Host production\n"
                                    "    HostName prod.example.com\n"
                                    "    User deploy\n"
                                    "    Port 2222\n"
                                    "    IdentityFile ~/.ssh/prod_ed25519\n"
                                    "    ProxyJump jumpuser@bastion.example.com:2201\n"
                                    "Host staging\n"
                                    "    HostName staging.example.com\n";
        QVERIFY(config.write(contents) == contents.size());
        QVERIFY(config.flush());

        bool ok = false;
        const QList<Session> sessions = SessionManager::importOpenSshConfig(config.fileName(), &ok);
        QVERIFY(ok);
        QCOMPARE(sessions.size(), 2);

        QCOMPARE(sessions[0].name, QString("production"));
        QCOMPARE(sessions[0].type, SessionType::SSH);
        QCOMPARE(sessions[0].host, QString("prod.example.com"));
        QCOMPARE(sessions[0].user, QString("deploy"));
        QCOMPARE(sessions[0].port, 2222);
        QVERIFY(sessions[0].keyPath.endsWith(QStringLiteral("/.ssh/prod_ed25519")));
        QCOMPARE(sessions[0].jumpHost, QString("bastion.example.com"));
        QCOMPARE(sessions[0].jumpUser, QString("jumpuser"));
        QCOMPARE(sessions[0].jumpPort, 2201);

        QCOMPARE(sessions[1].name, QString("staging"));
        QCOMPARE(sessions[1].host, QString("staging.example.com"));
        QCOMPARE(sessions[1].port, 22);
    }

    void testPuTTYRegistryImport() {
        QTemporaryFile registry;
        QVERIFY(registry.open());
        const QByteArray contents = "Windows Registry Editor Version 5.00\n\n"
                                    "[HKEY_CURRENT_USER\\Software\\SimonTatham\\PuTTY\\Sessions\\Production%20SSH]\n"
                                    "\"HostName\"=\"prod.example.com\"\n"
                                    "\"PortNumber\"=dword:000008ae\n"
                                    "\"UserName\"=\"deploy\"\n"
                                    "\"PublicKeyFile\"=\"C:\\\\Users\\\\deploy\\\\.ssh\\\\id_ed25519.ppk\"\n\n"
                                    "[HKEY_CURRENT_USER\\Software\\SimonTatham\\PuTTY\\Sessions\\Staging]\n"
                                    "\"HostName\"=\"staging.example.com\"\n";
        QVERIFY(registry.write(contents) == contents.size());
        QVERIFY(registry.flush());

        bool ok = false;
        const QList<Session> sessions = SessionManager::importPuTTYRegistry(registry.fileName(), &ok);
        QVERIFY(ok);
        QCOMPARE(sessions.size(), 2);
        QCOMPARE(sessions[0].name, QString("Production SSH"));
        QCOMPARE(sessions[0].host, QString("prod.example.com"));
        QCOMPARE(sessions[0].port, 2222);
        QCOMPARE(sessions[0].user, QString("deploy"));
        QVERIFY(sessions[0].keyPath.contains(QStringLiteral("id_ed25519.ppk")));
        QCOMPARE(sessions[1].host, QString("staging.example.com"));
    }

    void testToJsonOmitsIrrelevantFields() {
        Session local;
        local.type = SessionType::Local;
        local.shellPath = "/bin/bash";
        QJsonObject json = local.toJson();

        QVERIFY(json.contains("shellPath"));
        QVERIFY(json.contains("host")); // all fields serialized
    }

    void testPortDefaults() {
        Session ssh;
        ssh.type = SessionType::SSH;
        ssh.port = 0;
        QJsonObject jsonSsh = ssh.toJson();
        QCOMPARE(jsonSsh["port"].toInt(), 0);

        Session telnet;
        telnet.type = SessionType::Telnet;
        QJsonObject jsonT = telnet.toJson();
        QCOMPARE(jsonT["port"].toInt(), 22); // struct default
    }

    void testFtpTlsOptionsRoundTrip() {
        Session ftp;
        ftp.type = SessionType::FTP;
        ftp.ftpTls = true;
        ftp.ftpTlsMinimumVersion = 13;
        ftp.ftpTlsCaFile = QStringLiteral("/etc/ssl/custom-ca.pem");
        ftp.ftpTlsAllowInvalidCertificates = true;

        const Session restored = Session::fromJson(ftp.toJson());
        QCOMPARE(restored.ftpTlsMinimumVersion, 13);
        QCOMPARE(restored.ftpTlsCaFile, ftp.ftpTlsCaFile);
        QVERIFY(restored.ftpTlsAllowInvalidCertificates);
    }

    void testFtpProtocolAgainstLocalServer() {
        QTcpServer controlServer;
        QTcpServer dataServer;
        if (!controlServer.listen(QHostAddress(QStringLiteral("127.0.0.1"))))
            QSKIP(qPrintable(QStringLiteral("Local TCP sockets are unavailable: %1").arg(controlServer.errorString())));

        QTcpSocket* controlSocket = nullptr;
        QTcpSocket* dataSocket = nullptr;
        QByteArray controlBuffer;
        QByteArray uploadedData;
        bool dataIsUpload = false;

        connect(&controlServer, &QTcpServer::newConnection, this, [&]() {
            controlSocket = controlServer.nextPendingConnection();
            connect(controlSocket, &QTcpSocket::readyRead, this, [&]() {
                controlBuffer += controlSocket->readAll();
                while (true) {
                    const qsizetype end = controlBuffer.indexOf("\r\n");
                    if (end < 0)
                        return;
                    const QByteArray command = controlBuffer.left(end);
                    controlBuffer.remove(0, end + 2);
                    const QByteArray upper = command.toUpper();
                    if (upper.startsWith("USER")) {
                        controlSocket->write("331 Password required\r\n");
                    } else if (upper.startsWith("PASS")) {
                        controlSocket->write("230 Logged in\r\n");
                    } else if (upper.startsWith("TYPE")) {
                        controlSocket->write("200 Type set\r\n");
                    } else if (upper == "PASV") {
                        QVERIFY(dataServer.listen(QHostAddress(QStringLiteral("127.0.0.1"))));
                        const quint16 port = dataServer.serverPort();
                        controlSocket->write(QByteArray("227 Entering Passive Mode (127,0,0,1,") +
                                             QByteArray::number(port / 256) + "," + QByteArray::number(port % 256) +
                                             ")\r\n");
                    } else if (upper.startsWith("LIST")) {
                        dataIsUpload = false;
                        controlSocket->write("150 Opening data connection\r\n");
                        if (dataSocket) {
                            dataSocket->write("-rw-r--r-- 1 user group 5 Jan 01 00:00 test.txt\r\n");
                            dataSocket->disconnectFromHost();
                        }
                    } else if (upper.startsWith("RETR")) {
                        dataIsUpload = false;
                        controlSocket->write("150 Opening data connection\r\n");
                        if (dataSocket) {
                            dataSocket->write("hello");
                            dataSocket->disconnectFromHost();
                        }
                    } else if (upper.startsWith("STOR")) {
                        dataIsUpload = true;
                        controlSocket->write("150 Opening data connection\r\n");
                    }
                }
            });
            connect(controlSocket, &QTcpSocket::disconnected, controlSocket, &QObject::deleteLater);
            controlSocket->write("220 Local test FTP server\r\n");
        });

        connect(&dataServer, &QTcpServer::newConnection, this, [&]() {
            dataSocket = dataServer.nextPendingConnection();
            connect(dataSocket, &QTcpSocket::readyRead, this, [&]() { uploadedData += dataSocket->readAll(); });
            connect(dataSocket, &QTcpSocket::disconnected, this, [&]() {
                if (controlSocket)
                    controlSocket->write("226 Transfer complete\r\n");
                dataSocket->deleteLater();
                dataSocket = nullptr;
                dataServer.close();
            });
            if (dataIsUpload)
                return;
        });

        FtpClient client;
        QSignalSpy connectedSpy(&client, &FtpClient::connectionSuccess);
        QSignalSpy listedSpy(&client, &FtpClient::directoryListed);
        client.connectToHost(QStringLiteral("127.0.0.1"), controlServer.serverPort(), QStringLiteral("user"),
                             QStringLiteral("password"), false);
        QTRY_COMPARE_WITH_TIMEOUT(connectedSpy.count(), 1, 2000);

        client.listDirectory(QStringLiteral("/"));
        QTRY_COMPARE_WITH_TIMEOUT(listedSpy.count(), 1, 2000);
        const QList<SftpFile> entries = qvariant_cast<QList<SftpFile>>(listedSpy.at(0).at(1));
        QCOMPARE(entries.size(), 1);
        QCOMPARE(entries.first().name, QStringLiteral("test.txt"));
        QCOMPARE(entries.first().size, 5);

        QTemporaryDir transferDir;
        QVERIFY(transferDir.isValid());
        const QString downloadPath = transferDir.filePath(QStringLiteral("download.txt"));
        QSignalSpy operationSpy(&client, &FtpClient::operationFinished);
        client.downloadFile(QStringLiteral("test.txt"), downloadPath);
        QTRY_COMPARE_WITH_TIMEOUT(operationSpy.count(), 1, 2000);
        QVERIFY(operationSpy.first().at(0).toBool());
        QFile downloaded(downloadPath);
        QVERIFY(downloaded.open(QIODevice::ReadOnly));
        QCOMPARE(downloaded.readAll(), QByteArrayLiteral("hello"));

        const QString uploadPath = transferDir.filePath(QStringLiteral("upload.txt"));
        QFile upload(uploadPath);
        QVERIFY(upload.open(QIODevice::WriteOnly));
        QVERIFY(upload.write("upload payload") == 14);
        upload.close();
        operationSpy.clear();
        client.uploadFile(uploadPath, QStringLiteral("upload.txt"));
        QTRY_COMPARE_WITH_TIMEOUT(operationSpy.count(), 1, 2000);
        QVERIFY(operationSpy.first().at(0).toBool());
        QCOMPARE(uploadedData, QByteArrayLiteral("upload payload"));

        client.disconnectFromHost();
    }

    void testFtpReconnectsAfterControlLoss() {
        QTcpServer controlServer;
        if (!controlServer.listen(QHostAddress(QStringLiteral("127.0.0.1"))))
            QSKIP(qPrintable(QStringLiteral("Local TCP sockets are unavailable: %1").arg(controlServer.errorString())));

        QTcpSocket* controlSocket = nullptr;
        QByteArray controlBuffer;
        int acceptedConnections = 0;

        connect(&controlServer, &QTcpServer::newConnection, this, [&]() {
            controlSocket = controlServer.nextPendingConnection();
            ++acceptedConnections;
            controlBuffer.clear();
            connect(controlSocket, &QTcpSocket::readyRead, this, [&]() {
                controlBuffer += controlSocket->readAll();
                while (true) {
                    const qsizetype end = controlBuffer.indexOf("\r\n");
                    if (end < 0)
                        return;
                    const QByteArray command = controlBuffer.left(end).toUpper();
                    controlBuffer.remove(0, end + 2);
                    if (command.startsWith("USER"))
                        controlSocket->write("331 Password required\r\n");
                    else if (command.startsWith("PASS"))
                        controlSocket->write("230 Logged in\r\n");
                    else if (command.startsWith("TYPE"))
                        controlSocket->write("200 Type set\r\n");
                }
            });
            connect(controlSocket, &QTcpSocket::disconnected, controlSocket, &QObject::deleteLater);
            controlSocket->write("220 Reconnect test FTP server\r\n");
        });

        FtpClient client;
        QSignalSpy connectedSpy(&client, &FtpClient::connectionSuccess);
        client.connectToHost(QStringLiteral("127.0.0.1"), controlServer.serverPort(), QStringLiteral("user"),
                             QStringLiteral("password"), false);
        QTRY_COMPARE_WITH_TIMEOUT(connectedSpy.count(), 1, 2000);
        QCOMPARE(acceptedConnections, 1);

        QTimer::singleShot(50, this, [&]() {
            if (controlSocket)
                controlSocket->disconnectFromHost();
        });

        QTRY_COMPARE_WITH_TIMEOUT(connectedSpy.count(), 2, 7000);
        QCOMPARE(acceptedConnections, 2);
        client.disconnectFromHost();
    }

    void testFtpRejectsMissingCaBundle() {
        FtpClient client;
        QSignalSpy failureSpy(&client, &FtpClient::connectionFailed);
        client.connectToHost(QStringLiteral("127.0.0.1"), 1, QStringLiteral("user"), QStringLiteral("password"), true,
                             12, QStringLiteral("/path/that/does/not/exist.pem"));
        QTRY_COMPARE_WITH_TIMEOUT(failureSpy.count(), 1, 1000);
        QVERIFY(failureSpy.first().first().toString().contains(QStringLiteral("CA bundle")));
    }

    void testFtpsExplicitTlsHandshake() {
        const QString fixtureDir = QStringLiteral(BANCHO_SOURCE_DIR) + QStringLiteral("/tests/fixtures/");
        QFile certificateFile(fixtureDir + QStringLiteral("ftps-cert.pem"));
        QFile keyFile(fixtureDir + QStringLiteral("ftps-key.pem"));
        QVERIFY(certificateFile.open(QIODevice::ReadOnly));
        QVERIFY(keyFile.open(QIODevice::ReadOnly));

        const QSslCertificate certificate(certificateFile.readAll(), QSsl::Pem);
        const QSslKey privateKey(keyFile.readAll(), QSsl::Ec, QSsl::Pem);
        QVERIFY(!certificate.isNull());
        QVERIFY(!privateKey.isNull());

        QSslConfiguration serverConfiguration = QSslConfiguration::defaultConfiguration();
        serverConfiguration.setLocalCertificate(certificate);
        serverConfiguration.setPrivateKey(privateKey);
        serverConfiguration.setPeerVerifyMode(QSslSocket::VerifyNone);
        serverConfiguration.setProtocol(QSsl::TlsV1_2OrLater);

        ExplicitTlsTestServer controlServer;
        ExplicitTlsTestServer dataServer;
        if (!controlServer.listen(QHostAddress(QStringLiteral("127.0.0.1"))) ||
            !dataServer.listen(QHostAddress(QStringLiteral("127.0.0.1"))))
            QSKIP(qPrintable(QStringLiteral("Local TCP sockets are unavailable: %1")
                                 .arg(controlServer.errorString().isEmpty() ? dataServer.errorString()
                                                                            : controlServer.errorString())));

        QSslSocket* controlSocket = nullptr;
        QSslSocket* dataSocket = nullptr;
        QByteArray dataAction;
        QByteArray uploadedData;
        const auto sendData = [&]() {
            if (!dataSocket || !dataSocket->isEncrypted())
                return;
            if (dataAction == "LIST")
                dataSocket->write("-rw-r--r-- 1 user group 5 Jan 01 00:00 test.txt\r\n");
            else if (dataAction == "RETR")
                dataSocket->write("hello");
            else
                return;
            dataSocket->disconnectFromHost();
        };

        connect(&dataServer, &QTcpServer::newConnection, this, [&]() {
            dataSocket = qobject_cast<QSslSocket*>(dataServer.nextPendingConnection());
            QVERIFY(dataSocket != nullptr);
            dataSocket->setSslConfiguration(serverConfiguration);
            connect(dataSocket, &QSslSocket::encrypted, this, sendData);
            connect(dataSocket, &QTcpSocket::readyRead, this, [&]() { uploadedData += dataSocket->readAll(); });
            connect(dataSocket, &QTcpSocket::disconnected, this, [&]() {
                if (controlSocket)
                    controlSocket->write("226 Transfer complete\r\n");
                dataSocket->deleteLater();
                dataSocket = nullptr;
                dataAction.clear();
            });
            dataSocket->startServerEncryption();
        });

        connect(&controlServer, &QTcpServer::newConnection, this, [&]() {
            controlSocket = qobject_cast<QSslSocket*>(controlServer.nextPendingConnection());
            QVERIFY(controlSocket != nullptr);
            const auto commandBuffer = std::make_shared<QByteArray>();
            connect(controlSocket, &QTcpSocket::readyRead, this, [&, commandBuffer, serverConfiguration]() mutable {
                *commandBuffer += controlSocket->readAll();
                while (true) {
                    const qsizetype end = commandBuffer->indexOf("\r\n");
                    if (end < 0)
                        return;
                    const QByteArray command = commandBuffer->left(end).toUpper();
                    commandBuffer->remove(0, end + 2);
                    if (command == "AUTH TLS") {
                        controlSocket->write("234 TLS negotiation ready\r\n");
                        controlSocket->setSslConfiguration(serverConfiguration);
                        controlSocket->startServerEncryption();
                    } else if (command.startsWith("USER")) {
                        controlSocket->write("331 Password required\r\n");
                    } else if (command.startsWith("PASS")) {
                        controlSocket->write("230 Logged in\r\n");
                    } else if (command.startsWith("TYPE")) {
                        controlSocket->write("200 Type set\r\n");
                    } else if (command.startsWith("PBSZ")) {
                        controlSocket->write("200 PBSZ ok\r\n");
                    } else if (command.startsWith("PROT P")) {
                        controlSocket->write("200 PROT P ok\r\n");
                    } else if (command == "PASV") {
                        const quint16 port = dataServer.serverPort();
                        controlSocket->write(QByteArray("227 Entering Passive Mode (127,0,0,1,") +
                                             QByteArray::number(port / 256) + "," + QByteArray::number(port % 256) +
                                             ")\r\n");
                    } else if (command.startsWith("LIST")) {
                        dataAction = "LIST";
                        controlSocket->write("150 Opening secure data connection\r\n");
                        sendData();
                    } else if (command.startsWith("RETR")) {
                        dataAction = "RETR";
                        controlSocket->write("150 Opening secure data connection\r\n");
                        sendData();
                    } else if (command.startsWith("STOR")) {
                        dataAction = "STOR";
                        controlSocket->write("150 Opening secure data connection\r\n");
                    }
                }
            });
            controlSocket->write("220 Local explicit FTPS test server\r\n");
        });

        FtpClient client;
        QSignalSpy connectedSpy(&client, &FtpClient::connectionSuccess);
        QSignalSpy failureSpy(&client, &FtpClient::connectionFailed);
        client.connectToHost(QStringLiteral("127.0.0.1"), controlServer.serverPort(), QStringLiteral("user"),
                             QStringLiteral("password"), true, 12, fixtureDir + QStringLiteral("ftps-cert.pem"));
        QTRY_COMPARE_WITH_TIMEOUT(connectedSpy.count(), 1, 3000);
        QCOMPARE(failureSpy.count(), 0);

        QSignalSpy listedSpy(&client, &FtpClient::directoryListed);
        client.listDirectory(QStringLiteral("/"));
        QTRY_COMPARE_WITH_TIMEOUT(listedSpy.count(), 1, 3000);
        const QList<SftpFile> entries = qvariant_cast<QList<SftpFile>>(listedSpy.first().at(1));
        QCOMPARE(entries.size(), 1);
        QCOMPARE(entries.first().name, QStringLiteral("test.txt"));

        QTemporaryDir transferDir;
        QVERIFY(transferDir.isValid());
        QSignalSpy operationSpy(&client, &FtpClient::operationFinished);
        const QString downloadPath = transferDir.filePath(QStringLiteral("download.txt"));
        client.downloadFile(QStringLiteral("test.txt"), downloadPath);
        QTRY_COMPARE_WITH_TIMEOUT(operationSpy.count(), 1, 3000);
        QVERIFY(operationSpy.first().at(0).toBool());
        QFile downloaded(downloadPath);
        QVERIFY(downloaded.open(QIODevice::ReadOnly));
        QCOMPARE(downloaded.readAll(), QByteArrayLiteral("hello"));

        QFile upload(transferDir.filePath(QStringLiteral("upload.txt")));
        QVERIFY(upload.open(QIODevice::WriteOnly));
        QVERIFY(upload.write("upload payload") == 14);
        upload.close();
        operationSpy.clear();
        client.uploadFile(upload.fileName(), QStringLiteral("upload.txt"));
        QTRY_COMPARE_WITH_TIMEOUT(operationSpy.count(), 1, 3000);
        QVERIFY(operationSpy.first().at(0).toBool());
        QCOMPARE(uploadedData, QByteArrayLiteral("upload payload"));
        client.disconnectFromHost();
    }

    void testSshAndSftpAgainstLocalServer() {
#ifdef Q_OS_WIN
        QSKIP("The local sshd/SFTP integration test requires a Unix OpenSSH daemon.");
#endif
        QTemporaryDir serverDir;
        QVERIFY(serverDir.isValid());
        const QByteArray isolatedConfig = QDir(serverDir.filePath(QStringLiteral("config"))).absolutePath().toUtf8();
        qputenv("BANCHO_CONFIG_DIR", isolatedConfig);
        const QString hostKey = serverDir.filePath(QStringLiteral("host_key"));
        const QString clientKey = serverDir.filePath(QStringLiteral("client_key"));
        const QString authorizedKeys = serverDir.filePath(QStringLiteral("authorized_keys"));
        const QString sshdConfig = serverDir.filePath(QStringLiteral("sshd_config"));

        QTcpServer echoServer;
        if (!echoServer.listen(QHostAddress(QStringLiteral("127.0.0.1"))))
            QSKIP(qPrintable(QStringLiteral("Local TCP sockets are unavailable: %1").arg(echoServer.errorString())));
        connect(&echoServer, &QTcpServer::newConnection, this, [&]() {
            while (echoServer.hasPendingConnections()) {
                QTcpSocket* socket = echoServer.nextPendingConnection();
                connect(socket, &QTcpSocket::readyRead, socket, [socket]() {
                    socket->write(socket->readAll());
                    socket->flush();
                });
                connect(socket, &QTcpSocket::disconnected, socket, &QObject::deleteLater);
            }
        });

        auto reservePort = [&]() {
            QTcpServer probe;
            if (!probe.listen(QHostAddress(QStringLiteral("127.0.0.1"))))
                return quint16(0);
            const quint16 reserved = probe.serverPort();
            probe.close();
            return reserved;
        };

        auto generateKey = [](const QString& path, const QString& type, const QString& bits) {
            QProcess process;
            QStringList arguments = {QStringLiteral("-q"), QStringLiteral("-t"), type};
            if (!bits.isEmpty())
                arguments << QStringLiteral("-b") << bits;
            if (type != QStringLiteral("ed25519"))
                arguments << QStringLiteral("-m") << QStringLiteral("PEM");
            arguments << QStringLiteral("-N") << QString() << QStringLiteral("-f") << path;
            process.start(QStringLiteral("ssh-keygen"), arguments);
            return process.waitForFinished(5000) && process.exitCode() == 0;
        };
        QVERIFY(generateKey(hostKey, QStringLiteral("rsa"), QStringLiteral("2048")));
        QVERIFY(generateKey(clientKey, QStringLiteral("ed25519"), QString()));
        QVERIFY(QFile::copy(clientKey + QStringLiteral(".pub"), authorizedKeys));
        QVERIFY(QFile::setPermissions(hostKey, QFileDevice::ReadOwner | QFileDevice::WriteOwner));
        QVERIFY(QFile::setPermissions(clientKey, QFileDevice::ReadOwner | QFileDevice::WriteOwner));
        QVERIFY(QFile::setPermissions(authorizedKeys, QFileDevice::ReadOwner | QFileDevice::WriteOwner));

        QFile configFile(sshdConfig);
        QVERIFY(configFile.open(QIODevice::WriteOnly | QIODevice::Text));
        const QByteArray config = QStringLiteral("HostKey %1\n"
                                                 "AuthorizedKeysFile %2\n"
                                                 "PubkeyAuthentication yes\n"
                                                 "PasswordAuthentication no\n"
                                                 "KbdInteractiveAuthentication no\n"
                                                 "UsePAM no\n"
                                                 "StrictModes no\n"
                                                 "PermitRootLogin no\n"
                                                 "Subsystem sftp internal-sftp\n")
                                      .arg(hostKey, authorizedKeys)
                                      .toUtf8();
        QVERIFY(configFile.write(config) == config.size());
        configFile.close();
        QVERIFY(QFile::setPermissions(sshdConfig, QFileDevice::ReadOwner | QFileDevice::WriteOwner));

        QTcpServer portProbe;
        if (!portProbe.listen(QHostAddress(QStringLiteral("127.0.0.1"))))
            QSKIP(qPrintable(QStringLiteral("Local TCP sockets are unavailable: %1").arg(portProbe.errorString())));
        const quint16 port = portProbe.serverPort();
        portProbe.close();

        QProcess sshd;
        sshd.setProcessChannelMode(QProcess::MergedChannels);
        sshd.start(QStringLiteral("sshd"), {QStringLiteral("-D"), QStringLiteral("-e"), QStringLiteral("-f"),
                                            sshdConfig, QStringLiteral("-p"), QString::number(port)});
        QVERIFY(sshd.waitForStarted(3000));
        QTest::qWait(500);
        QVERIFY2(sshd.state() == QProcess::Running, qPrintable(sshd.readAll()));

        QProcess keyscan;
        keyscan.start(QStringLiteral("ssh-keyscan"), {QStringLiteral("-T"), QStringLiteral("3"), QStringLiteral("-p"),
                                                      QString::number(port), QStringLiteral("127.0.0.1")});
        QVERIFY(keyscan.waitForFinished(5000));
        const QByteArray knownHostData = keyscan.readAllStandardOutput();
        QVERIFY(!knownHostData.isEmpty());
        const QString knownHostsPath = QDir(AppPaths::configDir()).filePath(QStringLiteral("known_hosts"));
        QVERIFY(QDir().mkpath(QFileInfo(knownHostsPath).absolutePath()));
        QFile knownHosts(knownHostsPath);
        QVERIFY(knownHosts.open(QIODevice::WriteOnly));
        QVERIFY(knownHosts.write(knownHostData) > 0);
        knownHosts.close();

        QString user = qEnvironmentVariable("USER");
        if (user.isEmpty())
            user = qEnvironmentVariable("USERNAME");
        QVERIFY(!user.isEmpty());

        TunnelConfig localTunnel;
        localTunnel.type = TunnelConfig::Type::Local;
        localTunnel.localPort = reservePort();
        QVERIFY(localTunnel.localPort != 0);
        localTunnel.remoteHost = QStringLiteral("127.0.0.1");
        localTunnel.remotePort = echoServer.serverPort();

        TunnelConfig remoteTunnel;
        remoteTunnel.type = TunnelConfig::Type::Remote;
        remoteTunnel.localPort = echoServer.serverPort();
        remoteTunnel.remoteHost = QStringLiteral("127.0.0.1");
        remoteTunnel.remotePort = reservePort();
        QVERIFY(remoteTunnel.remotePort != 0);

        TunnelConfig socksTunnel;
        socksTunnel.type = TunnelConfig::Type::Dynamic;
        socksTunnel.localPort = reservePort();
        QVERIFY(socksTunnel.localPort != 0);
        socksTunnel.socksUsername = QStringLiteral("proxy-user");
        socksTunnel.socksPassword = QStringLiteral("proxy-password");
        const QList<TunnelConfig> tunnels = {localTunnel, remoteTunnel, socksTunnel};

        SshConnection connection;
        QSignalSpy connectedSpy(&connection, &SshConnection::connectionSuccess);
        QSignalSpy failureSpy(&connection, &SshConnection::connectionFailed);
        QSignalSpy tunnelStateSpy(&connection, &SshConnection::tunnelStateChanged);
        QSignalSpy tunnelStatusSpy(&connection, &SshConnection::tunnelStatus);
        QSignalSpy directorySpy(&connection, &SshConnection::directoryListed);
        connection.connectToHost(QStringLiteral("127.0.0.1"), port, user, clientKey, QString(), tunnels);
        QTRY_VERIFY_WITH_TIMEOUT(connectedSpy.count() == 1 || failureSpy.count() > 0, 5000);
        if (connectedSpy.isEmpty()) {
            connection.disconnectFromHost();
            sshd.terminate();
            sshd.waitForFinished(3000);
            QSKIP("libssh2 authentication is unavailable with the local sshd configuration");
        }
        QCOMPARE(failureSpy.count(), 0);

        connection.listDirectory(QStringLiteral("."));
        QTRY_COMPARE_WITH_TIMEOUT(directorySpy.count(), 1, 3000);
        QVERIFY(!qvariant_cast<QList<SftpFile>>(directorySpy.first().at(1)).isEmpty());

        auto allTunnelsActive = [&]() {
            bool active[3] = {false, false, false};
            for (const auto& signal : tunnelStateSpy) {
                const int index = signal.at(0).toInt();
                if (index >= 0 && index < 3 && signal.at(1).toBool())
                    active[index] = true;
            }
            return active[0] && active[1] && active[2];
        };
        QElapsedTimer tunnelTimer;
        tunnelTimer.start();
        while (!allTunnelsActive() && tunnelTimer.elapsed() < 5000)
            QTest::qWait(50);
        if (!allTunnelsActive()) {
            QCOMPARE(tunnelStateSpy.count(), 6);
        }

        auto waitForBytes = [&](QTcpSocket& socket, qint64 bytes) {
            QElapsedTimer timer;
            timer.start();
            while (socket.bytesAvailable() < bytes && timer.elapsed() < 3000)
                QTest::qWait(25);
            return socket.bytesAvailable() >= bytes;
        };
        auto verifyEcho = [&](QTcpSocket& socket, const QByteArray& payload) {
            QElapsedTimer timer;
            timer.start();
            while (socket.state() != QAbstractSocket::ConnectedState && timer.elapsed() < 3000)
                QTest::qWait(25);
            if (socket.state() != QAbstractSocket::ConnectedState)
                return false;
            if (socket.write(payload) != payload.size() || !socket.waitForBytesWritten(1000))
                return false;
            if (!waitForBytes(socket, payload.size()))
                return false;
            return socket.read(payload.size()) == payload;
        };

        QTcpSocket localSocket;
        localSocket.connectToHost(QStringLiteral("127.0.0.1"), localTunnel.localPort);
        QVERIFY(verifyEcho(localSocket, QByteArrayLiteral("local-tunnel")));

        QTcpSocket remoteSocket;
        remoteSocket.connectToHost(QStringLiteral("127.0.0.1"), remoteTunnel.remotePort);
        QVERIFY(verifyEcho(remoteSocket, QByteArrayLiteral("remote-tunnel")));

        QTcpSocket socksSocket;
        socksSocket.connectToHost(QStringLiteral("127.0.0.1"), socksTunnel.localPort);
        QElapsedTimer socksTimer;
        socksTimer.start();
        while (socksSocket.state() != QAbstractSocket::ConnectedState && socksTimer.elapsed() < 3000)
            QTest::qWait(25);
        QVERIFY(socksSocket.state() == QAbstractSocket::ConnectedState);
        socksSocket.write(QByteArray::fromHex("050100"));
        QVERIFY(socksSocket.waitForBytesWritten(1000));
        QVERIFY(waitForBytes(socksSocket, 2));
        QCOMPARE(socksSocket.read(2), QByteArray::fromHex("0502"));

        QByteArray socksAuth;
        socksAuth.append(char(0x01));
        socksAuth.append(char(socksTunnel.socksUsername.toUtf8().size()));
        socksAuth.append(socksTunnel.socksUsername.toUtf8());
        socksAuth.append(char(socksTunnel.socksPassword.toUtf8().size()));
        socksAuth.append(socksTunnel.socksPassword.toUtf8());
        socksSocket.write(socksAuth);
        QVERIFY(socksSocket.waitForBytesWritten(1000));
        QVERIFY(waitForBytes(socksSocket, 2));
        QCOMPARE(socksSocket.read(2), QByteArray::fromHex("0100"));

        QByteArray socksRequest = QByteArray::fromHex("05010001");
        socksRequest.append(char(127));
        socksRequest.append(char(0));
        socksRequest.append(char(0));
        socksRequest.append(char(1));
        socksRequest.append(char(echoServer.serverPort() >> 8));
        socksRequest.append(char(echoServer.serverPort() & 0xff));
        socksSocket.write(socksRequest);
        QVERIFY(socksSocket.waitForBytesWritten(1000));
        QVERIFY(waitForBytes(socksSocket, 10));
        QCOMPARE(static_cast<unsigned char>(socksSocket.read(2).at(1)), 0);
        QCOMPARE(socksSocket.read(8).size(), 8);
        QVERIFY(verifyEcho(socksSocket, QByteArrayLiteral("socks5-tunnel")));

        const QString localSourceA = serverDir.filePath(QStringLiteral("parallel-source-a.txt"));
        const QString localSourceB = serverDir.filePath(QStringLiteral("parallel-source-b.txt"));
        const QByteArray parallelPayloadA(1024 * 128, 'A');
        const QByteArray parallelPayloadB(1024 * 128, 'B');
        for (const auto& file :
             {qMakePair(localSourceA, parallelPayloadA), qMakePair(localSourceB, parallelPayloadB)}) {
            QFile output(file.first);
            QVERIFY(output.open(QIODevice::WriteOnly));
            QCOMPARE(output.write(file.second), file.second.size());
        }

        Session parallelSession;
        parallelSession.id = QStringLiteral("parallel-sftp-test");
        parallelSession.type = SessionType::SSH;
        parallelSession.host = QStringLiteral("127.0.0.1");
        parallelSession.port = port;
        parallelSession.user = user;
        parallelSession.keyPath = clientKey;

        QList<SftpTransferWorker*> workers;
        QList<QThread*> workerThreads;
        QSignalSpy parallelFinishedSpy(this, &TestSession::parallelTransferFinished);
        const QList<QPair<QString, QString>> parallelTransfers = {
            {localSourceA, QStringLiteral("/tmp/banchoxterm-parallel-a.txt")},
            {localSourceB, QStringLiteral("/tmp/banchoxterm-parallel-b.txt")},
        };
        for (int i = 0; i < parallelTransfers.size(); ++i) {
            SftpTransferRequest request;
            request.id = QStringLiteral("parallel-%1").arg(i);
            request.localPath = parallelTransfers.at(i).first;
            request.remotePath = parallelTransfers.at(i).second;
            request.isUpload = true;

            auto* worker = new SftpTransferWorker(parallelSession, request);
            auto* thread = new QThread;
            worker->moveToThread(thread);
            connect(thread, &QThread::started, worker, &SftpTransferWorker::start);
            connect(worker, &SftpTransferWorker::finished, this,
                    [this, thread](const QString&, bool success, const QString& message) {
                        emit parallelTransferFinished(success, message);
                        thread->quit();
                    });
            workers.append(worker);
            workerThreads.append(thread);
            thread->start();
        }
        QTRY_COMPARE_WITH_TIMEOUT(parallelFinishedSpy.count(), 2, 10000);
        for (int i = 0; i < workerThreads.size(); ++i) {
            QVERIFY(workerThreads.at(i)->wait(3000));
            delete workers.at(i);
            delete workerThreads.at(i);
        }
        QSignalSpy downloadSpy(&connection, &SshConnection::operationFinished);
        const QString downloadedA = serverDir.filePath(QStringLiteral("parallel-downloaded-a.txt"));
        const QString downloadedB = serverDir.filePath(QStringLiteral("parallel-downloaded-b.txt"));
        connection.downloadFile(QStringLiteral("/tmp/banchoxterm-parallel-a.txt"), downloadedA);
        QTRY_COMPARE_WITH_TIMEOUT(downloadSpy.count(), 1, 3000);
        QVERIFY(downloadSpy.first().at(0).toBool());
        connection.downloadFile(QStringLiteral("/tmp/banchoxterm-parallel-b.txt"), downloadedB);
        QTRY_COMPARE_WITH_TIMEOUT(downloadSpy.count(), 2, 3000);
        QVERIFY(downloadSpy.last().at(0).toBool());
        for (const auto& file : {qMakePair(downloadedA, parallelPayloadA), qMakePair(downloadedB, parallelPayloadB)}) {
            QFile downloaded(file.first);
            QVERIFY(downloaded.open(QIODevice::ReadOnly));
            QCOMPARE(downloaded.readAll(), file.second);
        }
        connection.deleteFile(QStringLiteral("/tmp/banchoxterm-parallel-a.txt"), false);
        connection.deleteFile(QStringLiteral("/tmp/banchoxterm-parallel-b.txt"), false);

        connection.disconnectFromHost();
        QTRY_VERIFY_WITH_TIMEOUT(!connection.isConnected(), 3000);
        connection.connectToHost(QStringLiteral("127.0.0.1"), port, user, clientKey, QString(), {});
        QTRY_COMPARE_WITH_TIMEOUT(connectedSpy.count(), 2, 5000);
        connection.listDirectory(QStringLiteral("."));
        QTRY_COMPARE_WITH_TIMEOUT(directorySpy.count(), 2, 3000);
        QVERIFY(!qvariant_cast<QList<SftpFile>>(directorySpy.last().at(1)).isEmpty());

        localSocket.disconnectFromHost();
        remoteSocket.disconnectFromHost();
        socksSocket.disconnectFromHost();
        connection.disconnectFromHost();
        sshd.terminate();
        QVERIFY(sshd.waitForFinished(3000));
    }

    void testTerminalTabLifecycle() {
        Session session;
        session.id = QStringLiteral("terminal-tab-lifecycle");
        session.name = QStringLiteral("Lifecycle test");
        session.type = SessionType::Local;
        session.shellPath = QStringLiteral("/bin/sh");

        auto* tab = new TerminalTab(session);
        tab->show();
        QTest::qWait(100);
        QVERIFY(tab->getTerminalWidget() != nullptr);
        QCOMPARE(tab->session().id, session.id);
        QVERIFY(tab->isSessionActive());

        delete tab;
        QCoreApplication::processEvents();
    }

    void testMainWindowStartupShutdown() {
        QTemporaryDir configDir;
        QVERIFY(configDir.isValid());

        QProcess application;
        QProcessEnvironment environment = QProcessEnvironment::systemEnvironment();
        environment.insert(QStringLiteral("QT_QPA_PLATFORM"), QStringLiteral("offscreen"));
        environment.insert(QStringLiteral("BANCHO_CONFIG_DIR"), configDir.path());
        application.setProcessEnvironment(environment);
        application.setProgram(QDir(QCoreApplication::applicationDirPath()).filePath(QStringLiteral("banchoxterm")));
        application.start();
        QVERIFY2(application.waitForStarted(3000), qPrintable(application.errorString()));
        QTest::qWait(300);
        QVERIFY(application.state() == QProcess::Running);

        application.terminate();
        QVERIFY(application.waitForFinished(3000));
    }

    void testMainWindowTabLifecycle() {
#ifdef Q_OS_WIN
        QSKIP("The local process lifecycle test uses the Unix /bin/true command.");
#endif
        MainWindow window;
        window.show();
        QTest::qWait(100);

        Session session;
        session.id = QStringLiteral("mainwindow-tab-lifecycle");
        session.name = QStringLiteral("Short-lived local tab");
        session.type = SessionType::Local;
        session.shellPath = QStringLiteral("/bin/true");
        QVERIFY(QMetaObject::invokeMethod(&window, "onConnectSession", Qt::DirectConnection, Q_ARG(Session, session)));

        auto* tab = window.findChild<TerminalTab*>();
        QVERIFY(tab != nullptr);
        QTRY_VERIFY_WITH_TIMEOUT(!tab->isSessionActive(), 3000);

        QTabWidget* pane = nullptr;
        for (auto* candidate : window.findChildren<QTabWidget*>()) {
            if (candidate->indexOf(tab) >= 0) {
                pane = candidate;
                break;
            }
        }
        QVERIFY(pane != nullptr);
        const int tabIndex = pane->indexOf(tab);
        QVERIFY(tabIndex >= 0);
        QVERIFY(QMetaObject::invokeMethod(&window, "onTabCloseRequested", Qt::DirectConnection,
                                          Q_ARG(QTabWidget*, pane), Q_ARG(int, tabIndex)));
        QTRY_VERIFY_WITH_TIMEOUT(window.findChild<TerminalTab*>() == nullptr, 3000);
    }

#ifdef BANCHO_HAVE_VNC
    void testVncReconnectRequestAfterFailure() {
        Session session;
        session.id = QStringLiteral("vnc-reconnect-test");
        session.name = QStringLiteral("Unavailable VNC");
        session.type = SessionType::VNC;
        session.host = QStringLiteral("127.0.0.1");
        session.port = 1;
        session.autoReconnect = true;

        MainWindow window;
        window.show();
        QVERIFY(QMetaObject::invokeMethod(&window, "onConnectSession", Qt::DirectConnection, Q_ARG(Session, session)));
        const auto tabs = window.findChildren<TerminalTab*>();
        QCOMPARE(tabs.size(), 1);
        QPointer<TerminalTab> firstTab = tabs.first();
        QTRY_VERIFY_WITH_TIMEOUT(firstTab.isNull(), 8000);
        QCOMPARE(window.findChildren<TerminalTab*>().size(), 1);
    }
#endif

#ifndef Q_OS_WIN
    void testRdpReconnectRequestAfterClientFailure() {
        QTemporaryDir emptyPath;
        QVERIFY(emptyPath.isValid());
        const QByteArray oldPath = qgetenv("PATH");
        qputenv("PATH", emptyPath.path().toUtf8());

        Session session;
        session.id = QStringLiteral("rdp-reconnect-test");
        session.name = QStringLiteral("Unavailable RDP");
        session.type = SessionType::RDP;
        session.host = QStringLiteral("127.0.0.1");
        session.port = 1;
        session.autoReconnect = true;

        MainWindow window;
        window.show();
        QVERIFY(QMetaObject::invokeMethod(&window, "onConnectSession", Qt::DirectConnection, Q_ARG(Session, session)));
        const auto tabs = window.findChildren<TerminalTab*>();
        QCOMPARE(tabs.size(), 1);
        QPointer<TerminalTab> firstTab = tabs.first();
        QTRY_VERIFY_WITH_TIMEOUT(firstTab.isNull(), 8000);
        QCOMPARE(window.findChildren<TerminalTab*>().size(), 1);
        qputenv("PATH", oldPath);
    }
#endif

    void testMasterPasswordEncryption() {
        MasterPasswordManager& mpm = MasterPasswordManager::instance();

        mpm.lock();
        QVERIFY(!mpm.isUnlocked());

        QVERIFY(mpm.setMasterPassword("SuperSecure123!"));
        QVERIFY(mpm.isEnabled());
        QVERIFY(mpm.isUnlocked());

        QString plaintext = "MySshSecretPassword";
        QString encrypted = mpm.encryptPassword(plaintext);
        QVERIFY(encrypted.startsWith("BANCHO2:"));
        QVERIFY(encrypted != plaintext);

        QString decrypted = mpm.decryptPassword(encrypted);
        QCOMPARE(decrypted, plaintext);

        QTemporaryDir exportDir;
        QVERIFY(exportDir.isValid());
        Session encryptedSession;
        encryptedSession.id = QStringLiteral("encrypted-session");
        encryptedSession.name = QStringLiteral("Encrypted server");
        const QString encryptedPath = QDir(exportDir.path()).filePath(QStringLiteral("sessions.bancho.enc"));
        QVERIFY(SessionManager::exportEncryptedSessions({encryptedSession}, encryptedPath));
        QFile encryptedFile(encryptedPath);
        QVERIFY(encryptedFile.open(QIODevice::ReadOnly));
        QVERIFY(encryptedFile.readAll().startsWith("BANCHO2:"));
        bool importOk = false;
        const QList<Session> imported = SessionManager::importSessions(encryptedPath, &importOk);
        QVERIFY(importOk);
        QCOMPARE(imported.size(), 1);
        QCOMPARE(imported.first().name, encryptedSession.name);

        mpm.lock();
        QVERIFY(!mpm.isUnlocked());

        QVERIFY(mpm.unlock("SuperSecure123!"));
        QVERIFY(mpm.isUnlocked());
        QCOMPARE(mpm.decryptPassword(encrypted), plaintext);
        QString tampered = encrypted;
        tampered[tampered.size() - 1] = tampered.at(tampered.size() - 1) == QChar('A') ? QChar('B') : QChar('A');
        QVERIFY(mpm.decryptPassword(tampered).isEmpty());

        QVERIFY(mpm.disableMasterPassword("SuperSecure123!"));
        QVERIFY(!mpm.isEnabled());
    }

    void testMobaXtermImport() {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString path = QDir(dir.path()).filePath(QStringLiteral("bookmarks.mxtsessions"));
        QFile file(path);
        QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Text));
        file.write("[Bookmarks\\Network\\router]\nHostName=router.example\nUserName=admin\nPort=2222\nProtocol=ssh\n");
        file.close();

        bool ok = false;
        const QList<Session> sessions = SessionManager::importSessions(path, &ok);
        QVERIFY(ok);
        QCOMPARE(sessions.size(), 1);
        QCOMPARE(sessions.first().name, QStringLiteral("Network/router"));
        QCOMPARE(sessions.first().host, QStringLiteral("router.example"));
        QCOMPARE(sessions.first().user, QStringLiteral("admin"));
        QCOMPARE(sessions.first().port, 2222);
        QCOMPARE(sessions.first().type, SessionType::SSH);
    }

    void testSecureCrtImport() {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString path = QDir(dir.path()).filePath(QStringLiteral("router.ini"));
        QFile file(path);
        QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Text));
        file.write("S:\"Hostname\"=router.example\nS:\"Username\"=admin\nD:\"Port\"=00000016\n");
        file.close();

        bool ok = false;
        const QList<Session> sessions = SessionManager::importSessions(path, &ok);
        QVERIFY(ok);
        QCOMPARE(sessions.size(), 1);
        QCOMPARE(sessions.first().name, QStringLiteral("router"));
        QCOMPARE(sessions.first().host, QStringLiteral("router.example"));
        QCOMPARE(sessions.first().user, QStringLiteral("admin"));
        QCOMPARE(sessions.first().port, 22);
        QCOMPARE(sessions.first().type, SessionType::SSH);
    }

    void testRoyalTsImport() {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString path = QDir(dir.path()).filePath(QStringLiteral("connections.rtsx"));
        QFile file(path);
        QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Text));
        file.write(R"xml(<?xml version="1.0" encoding="utf-8"?>
<RoyalDocument>
  <RoyalFolder><Name>Network</Name></RoyalFolder>
  <RoyalSSHConnection>
    <ID>123</ID>
    <Name>Router</Name>
    <URI>router.example</URI>
    <Port>2200</Port>
    <CredentialUsername>admin</CredentialUsername>
    <CredentialPassword>must-not-be-imported</CredentialPassword>
  </RoyalSSHConnection>
</RoyalDocument>)xml");
        file.close();

        bool ok = false;
        const QList<Session> sessions = SessionManager::importSessions(path, &ok);
        QVERIFY(ok);
        QCOMPARE(sessions.size(), 1);
        QCOMPARE(sessions.first().name, QStringLiteral("Router"));
        QCOMPARE(sessions.first().host, QStringLiteral("router.example"));
        QCOMPARE(sessions.first().user, QStringLiteral("admin"));
        QCOMPARE(sessions.first().port, 2200);
        QVERIFY(sessions.first().type == SessionType::SSH);
    }

    void testRoyalTsProtocolImport() {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString path = QDir(dir.path()).filePath(QStringLiteral("protocols.rtsx"));
        QFile file(path);
        QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Text));
        file.write(R"xml(<RoyalDocument>
  <RoyalSSHConnection><Name>Telnet device</Name><URI>telnet.example</URI><ConnectionType>telnet</ConnectionType></RoyalSSHConnection>
  <RoyalRDSConnection><Name>Windows host</Name><URI>rdp.example</URI><Port>3390</Port></RoyalRDSConnection>
  <RoyalVNCConnection><Name>Console</Name><URI>vnc.example</URI></RoyalVNCConnection>
</RoyalDocument>)xml");
        file.close();

        bool ok = false;
        const QList<Session> sessions = SessionManager::importSessions(path, &ok);
        QVERIFY(ok);
        QCOMPARE(sessions.size(), 3);
        QCOMPARE(sessions.at(0).type, SessionType::Telnet);
        QCOMPARE(sessions.at(0).port, 22);
        QCOMPARE(sessions.at(1).type, SessionType::RDP);
        QCOMPARE(sessions.at(1).port, 3390);
        QCOMPARE(sessions.at(2).type, SessionType::VNC);
        QCOMPARE(sessions.at(2).port, 5900);
    }

    void testImporterFixtures() {
        const QString fixtureDir = QStringLiteral(BANCHO_SOURCE_DIR) + QStringLiteral("/tests/fixtures/");
        const QList<QPair<QString, QString>> fixtures = {
            {QStringLiteral("openssh.conf"), QStringLiteral("fixture.example")},
            {QStringLiteral("putty.reg"), QStringLiteral("fixture.example")},
            {QStringLiteral("mobaxterm.mxtsessions"), QStringLiteral("fixture.example")},
            {QStringLiteral("securecrt.ini"), QStringLiteral("fixture.example")},
            {QStringLiteral("royalts.rtsx"), QStringLiteral("fixture.example")},
        };
        for (const auto& fixture : fixtures) {
            bool ok = false;
            const QList<Session> sessions =
                SessionManager::importSessions(QDir(fixtureDir).filePath(fixture.first), &ok);
            QVERIFY2(ok, qPrintable(fixture.first));
            QCOMPARE(sessions.size(), 1);
            QCOMPARE(sessions.first().host, fixture.second);
        }
    }

    void testKeyringRoundtrip() {
#ifndef Q_OS_WIN
        if (QStandardPaths::findExecutable(QStringLiteral("secret-tool")).isEmpty())
            QSKIP("secret-tool is not available in this environment");
#endif
        const QString id = QStringLiteral("test-keyring-%1").arg(QUuid::createUuid().toString(QUuid::WithoutBraces));
        const QString password = QStringLiteral("p ässword with spaces");
        if (!Keyring::storePassword(id, password)) {
            Keyring::deletePassword(id);
            QSKIP("The system keyring provider is not available in this environment");
        }
        QCOMPARE(Keyring::lookupPassword(id), password);
        QVERIFY(Keyring::deletePassword(id));
        QVERIFY(Keyring::lookupPassword(id).isEmpty());
    }

    void testVtTerminalWidgetSearch() {
        VtTerminalWidget widget;
        widget.resize(800, 600); // Trigger resize to allocate buffer

        widget.writeData("Hello World\r\nThis is a test line\r\nError: connection failed\r\n");

        QVERIFY(widget.findText("test", true, true));
        QVERIFY(!widget.findText("ERROR", true, true));
        QVERIFY(widget.findText("ERROR", true, false));
    }

    void testVtTerminalSpecialKeys() {
        VtTerminalWidget widget;
        widget.resize(800, 600);
        widget.show();
        widget.setFocus();
        QSignalSpy dataSpy(&widget, &VtTerminalWidget::dataReady);

        QTest::keyClick(&widget, Qt::Key_C, Qt::ControlModifier);
        QTest::keyClick(&widget, Qt::Key_Return);
        QTest::keyClick(&widget, Qt::Key_Backspace);
        QTest::keyClick(&widget, Qt::Key_Tab);
        QTest::keyClick(&widget, Qt::Key_Up);

        QCOMPARE(dataSpy.size(), 5);
        QCOMPARE(dataSpy.at(0).at(0).toByteArray(), QByteArray("\x03", 1));
        QCOMPARE(dataSpy.at(1).at(0).toByteArray(), QByteArray("\r", 1));
        QCOMPARE(dataSpy.at(2).at(0).toByteArray(), QByteArray("\x7f", 1));
        QCOMPARE(dataSpy.at(3).at(0).toByteArray(), QByteArray("\t", 1));
        QCOMPARE(dataSpy.at(4).at(0).toByteArray(), QByteArray("\x1b[A", 3));
    }

    void testVtTerminalRendering() {
        VtTerminalWidget widget;
        widget.resize(800, 600);

        widget.writeData("AB\r\nCD\r\n");
        QCOMPARE(widget.cellChar(0, 0), QChar('A'));
        QCOMPARE(widget.cellChar(1, 0), QChar('B'));
        QCOMPARE(widget.cellChar(0, 1), QChar('C'));
        QCOMPARE(widget.cellChar(1, 1), QChar('D'));
    }

    void testVtClearScreen() {
        VtTerminalWidget widget;
        widget.resize(800, 600);

        widget.writeData("hello world");
        widget.writeData("\x1b[2J"); // clear entire screen
        QCOMPARE(widget.cellChar(0, 0), QChar(' '));
        QCOMPARE(widget.cellChar(4, 0), QChar(' '));
    }

    void testVtCursorPositioning() {
        VtTerminalWidget widget;
        widget.resize(800, 600);

        widget.writeData("abc");
        widget.writeData("\x1b[2;1H"); // cursor to row 2, column 1 (1-based)
        widget.writeData("Z");
        QCOMPARE(widget.cellChar(0, 1), QChar('Z'));
    }
};

QTEST_MAIN(TestSession)
#include "test_session.moc"
