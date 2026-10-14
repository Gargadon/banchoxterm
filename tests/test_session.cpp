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
#include <QTimer>
#include <QDialog>
#include <QLineEdit>
#include <QElapsedTimer>
#include <QThread>
#include <QScopeGuard>
#include <QTabWidget>
#include <QFrame>
#include <QPushButton>
#include <QPointer>
#include <QStandardPaths>
#include <memory>
#include <cmath>
#include <vector>
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
#include "sessiondialog.h"
#include <QComboBox>
#include <QAbstractItemView>
#include "sftptransferworker.h"
#include "sftpsidebar.h"
#include <QTreeWidget>
#include <QInputDialog>
#include <QAction>
#include "terminalappearance.h"
#include "settingsdialog.h"
#include <QSpinBox>
#include <QCheckBox>
#include <QScreen>
#include <QPainter>
#ifdef Q_OS_WIN
#include "conpty.h"
#include <qt_windows.h>
#endif
#include <qtermwidget.h>
#include <TerminalDisplay.h>
#include <QScrollArea>
#include <QScrollBar>
#include <QGroupBox>

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
    void testSftpLocalActions() {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        SftpSidebar sidebar;
        auto* path = sidebar.findChild<QLineEdit*>("localPathEdit");
        auto* tree = sidebar.findChild<QTreeWidget*>("localFileTree");
        path->setText(dir.path());
        QVERIFY(QMetaObject::invokeMethod(path, "returnPressed"));
        QCOMPARE(path->text(), QDir::toNativeSeparators(dir.path()));
        QFile localFile(dir.filePath("sample.txt"));
        QVERIFY(localFile.open(QIODevice::WriteOnly));
        QCOMPARE(localFile.write(QByteArray(2048, 'x')), qint64(2048));
        localFile.close();
        QTRY_COMPARE(tree->findItems("sample.txt", Qt::MatchExactly).size(), 1);
        const QList<SftpFile> remoteFiles = {{"sample.txt", false, 2048, QFileInfo(localFile).lastModified()}};
        QVERIFY(QMetaObject::invokeMethod(&sidebar, "onDirectoryListed", Qt::DirectConnection,
                                         Q_ARG(QString, QString("/remote")), Q_ARG(QList<SftpFile>, remoteFiles)));
        auto* remoteTree = sidebar.findChild<QTreeWidget*>("sftpTree");
        auto* localItem = tree->findItems("sample.txt", Qt::MatchExactly).first();
        auto* remoteItem = remoteTree->findItems("sample.txt", Qt::MatchExactly).first();
        QCOMPARE(tree->columnCount(), remoteTree->columnCount());
        for (int column = 0; column < tree->columnCount(); ++column) {
            QCOMPARE(tree->headerItem()->text(column), remoteTree->headerItem()->text(column));
            QCOMPARE(localItem->text(column), remoteItem->text(column));
        }
        QCOMPARE(localItem->icon(0).cacheKey(), remoteItem->icon(0).cacheKey());
        path->setText(dir.filePath("missing"));
        QVERIFY(QMetaObject::invokeMethod(path, "returnPressed"));
        QCOMPARE(path->text(), QDir::toNativeSeparators(dir.path()));

        QTimer::singleShot(0, []() {
            auto* dialog = qobject_cast<QInputDialog*>(QApplication::activeModalWidget());
            QVERIFY(dialog);
            dialog->setTextValue("created");
            dialog->accept();
        });
        sidebar.findChild<QPushButton*>("localNewFolderButton")->click();
        QVERIFY(QFileInfo(dir.filePath("created")).isDir());
        QCOMPARE(tree->findItems("created", Qt::MatchExactly).size(), 1);
        tree->setCurrentItem(tree->findItems("created", Qt::MatchExactly).first());
        QTimer::singleShot(0, []() {
            auto* dialog = qobject_cast<QInputDialog*>(QApplication::activeModalWidget());
            QVERIFY(dialog);
            dialog->setTextValue("renamed");
            dialog->accept();
        });
        sidebar.findChild<QPushButton*>("localRenameButton")->click();
        QVERIFY(QFileInfo(dir.filePath("renamed")).isDir());
        QVERIFY(!QFileInfo::exists(dir.filePath("created")));
        QCOMPARE(tree->findItems("renamed", Qt::MatchExactly).size(), 1);
        auto* folderItem = tree->findItems("renamed", Qt::MatchExactly).first();
        QVERIFY(!tree->rootIsDecorated());
        QCOMPARE(tree->columnCount(), 3);
        QCOMPARE(folderItem->text(1), QString("<DIR>"));
        QVERIFY(QMetaObject::invokeMethod(tree, "itemDoubleClicked", Q_ARG(QTreeWidgetItem*, folderItem), Q_ARG(int, 0)));
        QCOMPARE(path->text(), QDir::toNativeSeparators(dir.filePath("renamed")));
        QCOMPARE(tree->topLevelItem(0)->text(0), QString("."));
        QCOMPARE(tree->topLevelItem(1)->text(0), QString(".."));
        sidebar.findChild<QPushButton*>("localUpButton")->click();
        QCOMPARE(path->text(), QDir::toNativeSeparators(dir.path()));
        sidebar.findChild<QPushButton*>("localUpButton")->click();
        QCOMPARE(path->text(), QDir::toNativeSeparators(QFileInfo(dir.path()).absolutePath()));
    }

    void testSftpDownloadDirectories_data() {
        QTest::addColumn<bool>("cancel");
        QTest::addColumn<bool>("remoteAction");
        QTest::newRow("local-download") << false << false;
        QTest::newRow("remote-download") << false << true;
        QTest::newRow("cancel") << true << false;
    }

    void testSftpDownloadDirectories() {
        QFETCH(bool, cancel);
        QFETCH(bool, remoteAction);
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        QVERIFY(QDir(dir.path()).mkdir("folder"));
        SftpSidebar sidebar;
        auto* path = sidebar.findChild<QLineEdit*>("localPathEdit");
        path->setText(dir.path());
        QVERIFY(QMetaObject::invokeMethod(path, "returnPressed"));
        QVERIFY(QMetaObject::invokeMethod(&sidebar, "onConnectionSuccess"));
        auto list = [&sidebar](const QString& remotePath, const QList<SftpFile>& files) {
            return QMetaObject::invokeMethod(&sidebar, "onDirectoryListed", Qt::DirectConnection,
                                              Q_ARG(QString, remotePath), Q_ARG(QList<SftpFile>, files));
        };
        QVERIFY(list("/remote", {{"folder", true}}));
        auto* remoteTree = sidebar.findChild<QTreeWidget*>("sftpTree");
        remoteTree->topLevelItem(2)->setSelected(true);
        auto* download = sidebar.findChild<QPushButton*>("localDownloadButton");
        QVERIFY(download->isEnabled());
        QSignalSpy listings(&sidebar, &SftpSidebar::requestList);
        if (remoteAction)
            QVERIFY(QMetaObject::invokeMethod(&sidebar, "onDownloadClicked"));
        else
            download->click();
        QCOMPARE(listings.last().first().toString(), QString("/remote/folder"));
        QVERIFY(QFileInfo(dir.filePath("folder (1)")).isDir());
        QVERIFY(!remoteTree->isEnabled());
        if (cancel)
            QVERIFY(QMetaObject::invokeMethod(&sidebar, "onCancelQueuedTransfers"));
        QVERIFY(list("/remote/folder", {{"nested", true}, {".", true}, {"..", true}}));
        if (cancel) {
            QVERIFY(!QFileInfo::exists(dir.filePath("folder (1)/nested")));
        } else {
            QCOMPARE(listings.last().first().toString(), QString("/remote/folder/nested"));
            QVERIFY(QFileInfo(dir.filePath("folder (1)/nested")).isDir());
            QVERIFY(list("/remote/folder/nested", {}));
        }
        QVERIFY(remoteTree->isEnabled());
        QCOMPARE(sidebar.findChild<QLineEdit*>("remotePathEdit")->text(), QString("/remote"));
        QCOMPARE(listings.last().first().toString(), QString("/remote"));
    }

    void testSessionDialogEscape() {
        for (int type = 0; type < 7; ++type) {
            for (int action = 0; action < 3; ++action) {
                Session session;
                session.type = static_cast<SessionType>(type);
                SessionDialog dialog(session);
                QTimer::singleShot(0, &dialog, [&dialog, action]() {
                    if (action == 0)
                        QTest::keyClick(&dialog, Qt::Key_Escape);
                    else if (action == 1)
                        dialog.findChild<QPushButton*>("cancelButton")->click();
                    else
                        dialog.close();
                });
                QCOMPARE(dialog.exec(), int(QDialog::Rejected));
            }
        }
    }

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
        original.jumpSessionId = "saved-bastion";
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
        QCOMPARE(restored.jumpSessionId, original.jumpSessionId);
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

    void testSavedBastionSelection() {
        const QList<Session> previous = SessionManager::loadSessions();
        const auto restore = qScopeGuard([&]() { SessionManager::saveSessions(previous); });
        Session bastion;
        bastion.id = "bastion-profile";
        bastion.name = "Bastion";
        bastion.type = SessionType::SSH;
        bastion.host = "bastion.example";
        bastion.port = 2222;
        bastion.user = "jump-user";
        bastion.keyPath = "jump-key";
        Session target;
        target.id = "target-profile";
        target.type = SessionType::SSH;
        target.host = "target.example";
        Session nested = target;
        nested.id = "nested-profile";
        nested.jumpHost = bastion.host;
        Session local;
        local.id = "local-profile";
        local.type = SessionType::Local;
        SessionManager::saveSessions({bastion, target, nested, local});
        {
            SessionDialog dialog(target);
            auto* selector = dialog.findChild<QComboBox*>("jumpSessionCombo");
            QVERIFY(selector);
            QCOMPARE(selector->count(), 2); // Manual and the direct bastion, excluding self.
            dialog.show();
            selector->showPopup();
            QCoreApplication::processEvents();
            for (int row = 0; row < 2; ++row) {
                const QRect item = selector->view()->visualRect(selector->model()->index(row, 0));
                QVERIFY(!item.isEmpty());
                QVERIFY(selector->view()->viewport()->rect().contains(item.center()));
            }
            selector->hidePopup();
            selector->setCurrentIndex(selector->findData(bastion.id));
            const Session selected = dialog.getSession();
            QCOMPARE(selected.jumpSessionId, bastion.id);
            QCOMPARE(selected.jumpHost, bastion.host);
            QCOMPARE(selected.jumpPort, bastion.port);
            QCOMPARE(selected.jumpUser, bastion.user);
            QCOMPARE(selected.jumpKeyPath, bastion.keyPath);
            target = selected;
        }
        bastion.host = "updated-bastion.example";
        bastion.keyPath.clear(); // Agent authentication must remain available.
        SessionManager::saveSessions({bastion, target});
        const QList<Session> updated = SessionManager::loadSessions();
        QCOMPARE(updated.at(1).jumpHost, bastion.host);
        QVERIFY(updated.at(1).jumpKeyPath.isEmpty());
        SessionManager::saveSessions({target});
        SessionDialog missing(target);
        QCOMPARE(missing.getSession().jumpSessionId, bastion.id);
        SshConnection connection;
        QSignalSpy failure(&connection, &SshConnection::connectionFailed);
        connection.connectToHost(target.host, target.port, target.user, target.keyPath, {}, {}, target.jumpHost,
                                 target.jumpPort, target.jumpUser, target.jumpKeyPath, target.id);
        QCOMPARE(failure.count(), 1); // Fail before any socket is opened; never connect directly.
    }

    void testSessionFilesArePrivate() {
#ifdef Q_OS_WIN
        QSKIP("This test verifies POSIX mode bits, not Windows ACLs.");
#endif
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
                            if (command == "LIST /tree")
                                dataSocket->write("drwxr-xr-x 1 user group 0 Jan 01 00:00 folder\r\n");
                            else if (command == "LIST /tree/folder")
                                dataSocket->write("drwxr-xr-x 1 user group 0 Jan 01 00:00 empty\r\n"
                                                  "-rw-r--r-- 1 user group 5 Jan 01 00:00 test.txt\r\n");
                            else if (command != "LIST /tree/folder/empty")
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
        QTest::qWait(50);
        SftpSidebar sidebar;
        Session session;
        session.type = SessionType::FTP;
        session.host = "127.0.0.1";
        session.port = controlServer.serverPort();
        session.user = "user";
        session.remoteDirectory = "/tree";
        session.ftpTls = false;
        auto* localPath = sidebar.findChild<QLineEdit*>("localPathEdit");
        localPath->setText(transferDir.path());
        QVERIFY(QMetaObject::invokeMethod(localPath, "returnPressed"));
        sidebar.startSession(session);
        auto* tree = sidebar.findChild<QTreeWidget*>("sftpTree");
        QTRY_COMPARE_WITH_TIMEOUT(tree->findItems("folder", Qt::MatchExactly).size(), 1, 3000);
        tree->findItems("folder", Qt::MatchExactly).first()->setSelected(true);
        sidebar.findChild<QPushButton*>("localDownloadButton")->click();
        QTRY_VERIFY_WITH_TIMEOUT(QFileInfo(transferDir.filePath("folder/empty")).isDir(), 3000);
        QTRY_VERIFY_WITH_TIMEOUT(tree->isEnabled() && tree->selectedItems().isEmpty(), 3000);
        QFile recursiveDownload(transferDir.filePath("folder/test.txt"));
        QVERIFY(recursiveDownload.open(QIODevice::ReadOnly));
        QCOMPARE(recursiveDownload.readAll(), QByteArrayLiteral("hello"));
        QCOMPARE(sidebar.findChild<QLineEdit*>("remotePathEdit")->text(), QString("/tree"));
        sidebar.stopSession();
        QTest::qWait(50);
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

        std::vector<std::unique_ptr<QTcpServer>> portReservations;
        auto reservePort = [&]() {
            auto probe = std::make_unique<QTcpServer>();
            if (!probe->listen(QHostAddress(QStringLiteral("127.0.0.1"))))
                return quint16(0);
            const quint16 reserved = probe->serverPort();
            // Keep earlier ports bound while selecting the other tunnel ports.
            portReservations.push_back(std::move(probe));
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
                                                 "AcceptEnv COLORTERM TERM_PROGRAM\n"
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
        portReservations.clear();

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

        // Exercise the sidebar queue with actual nested files and an empty directory.
        const QString sourceFolder = serverDir.filePath("sidebar-source");
        QVERIFY(QDir().mkpath(sourceFolder + "/nested/empty"));
        QFile sourceFile(sourceFolder + "/nested/payload.txt");
        QVERIFY(sourceFile.open(QIODevice::WriteOnly));
        const QByteArray sidebarPayload("recursive sidebar download\n");
        QCOMPARE(sourceFile.write(sidebarPayload), sidebarPayload.size());
        sourceFile.close();
        QTemporaryDir destination;
        QVERIFY(destination.isValid());
        {
            SftpSidebar sidebar;
            sidebar.setConnection(&connection);
            Session sidebarSession;
            sidebarSession.type = SessionType::SSH;
            sidebarSession.id = "sidebar-download-test";
            sidebarSession.host = "127.0.0.1";
            sidebarSession.port = port;
            sidebarSession.user = user;
            sidebarSession.keyPath = clientKey;
            sidebarSession.remoteDirectory = serverDir.path();
            sidebar.startSession(sidebarSession);
            auto* localPath = sidebar.findChild<QLineEdit*>("localPathEdit");
            localPath->setText(destination.path());
            QVERIFY(QMetaObject::invokeMethod(localPath, "returnPressed"));
            auto* remoteTree = sidebar.findChild<QTreeWidget*>("sftpTree");
            const auto matches = remoteTree->findItems("sidebar-source", Qt::MatchExactly);
            QCOMPARE(matches.size(), 1);
            matches.first()->setSelected(true);
            sidebar.findChild<QPushButton*>("localDownloadButton")->click();
            const QString downloadedPath = destination.filePath("sidebar-source/nested/payload.txt");
            QTRY_VERIFY_WITH_TIMEOUT(QFileInfo::exists(downloadedPath), 10000);
            QTRY_VERIFY_WITH_TIMEOUT(QFileInfo(destination.filePath("sidebar-source/nested/empty")).isDir(), 10000);
            QTRY_VERIFY_WITH_TIMEOUT(sidebar.findChild<QPushButton*>("localDownloadButton")->isEnabled() == false &&
                                        remoteTree->isEnabled(), 10000);
            QFile downloaded(downloadedPath);
            QVERIFY(downloaded.open(QIODevice::ReadOnly));
            QTRY_COMPARE_WITH_TIMEOUT(downloaded.size(), sidebarPayload.size(), 10000);
            QCOMPARE(downloaded.readAll(), sidebarPayload);
            QCOMPARE(sidebar.findChild<QLineEdit*>("remotePathEdit")->text(), serverDir.path());
        }

        directorySpy.clear();
        QSignalSpy colorEnvironmentSpy(&connection, &SshConnection::shellDataReceived);
        connection.sendToShell(QByteArrayLiteral("printf '\\nBANCHO_COLOR=%s\\n' \"$COLORTERM\"\r"));
        auto trueColorAnnounced = [&]() {
            QByteArray received;
            for (const auto& event : colorEnvironmentSpy)
                received += event.at(0).toByteArray();
            return received.contains("BANCHO_COLOR=truecolor");
        };
        QTRY_VERIFY_WITH_TIMEOUT(trueColorAnnounced(), 3000);

        connection.listDirectory(QStringLiteral("."));
        QTRY_COMPARE_WITH_TIMEOUT(directorySpy.count(), 1, 3000);
        QVERIFY(!qvariant_cast<QList<SftpFile>>(directorySpy.first().at(1)).isEmpty());

        const QString uploadSource = serverDir.filePath(QStringLiteral("folder-upload-source"));
        const QString nestedUploadSource = QDir(uploadSource).filePath(QStringLiteral("nested"));
        QVERIFY(QDir().mkpath(nestedUploadSource));
        const QList<QPair<QString, QByteArray>> folderFiles = {
            {QStringLiteral("first.txt"), QByteArrayLiteral("first")},
            {QStringLiteral("second.txt"), QByteArrayLiteral("second")},
            {QStringLiteral("third.txt"), QByteArrayLiteral("third")},
            {QStringLiteral("fourth.txt"), QByteArrayLiteral("fourth")},
            {QStringLiteral("nested/fifth.txt"), QByteArrayLiteral("fifth")},
        };
        for (const auto& file : folderFiles) {
            QFile output(QDir(uploadSource).filePath(file.first));
            QVERIFY(output.open(QIODevice::WriteOnly));
            QCOMPARE(output.write(file.second), file.second.size());
        }

        const QString uploadDestination = serverDir.filePath(QStringLiteral("folder-upload-destination"));
        QVERIFY(QDir().mkpath(uploadDestination));
        QSignalSpy folderUploadSpy(&connection, &SshConnection::operationFinished);
        connection.uploadDirectory(uploadSource, uploadDestination);
        QTRY_COMPARE_WITH_TIMEOUT(folderUploadSpy.count(), 1, 10000);
        QVERIFY2(folderUploadSpy.first().at(0).toBool(), qPrintable(folderUploadSpy.first().at(1).toString()));
        const QString uploadedRoot = QDir(uploadDestination).filePath(QFileInfo(uploadSource).fileName());
        for (const auto& file : folderFiles) {
            QFile uploaded(QDir(uploadedRoot).filePath(file.first));
            QVERIFY(uploaded.open(QIODevice::ReadOnly));
            QCOMPARE(uploaded.readAll(), file.second);
        }

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
        QVERIFY2(allTunnelsActive(), "All three SSH tunnels must be listening before testing their protocols");

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
        socksSocket.write(QByteArray::fromHex("050102"));
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
        std::atomic_bool uploadPaused{false};
        const auto cleanupWorkers = qScopeGuard([&]() {
            for (int i = 0; i < workerThreads.size(); ++i) {
                workers.at(i)->cancel();
                workerThreads.at(i)->quit();
                workerThreads.at(i)->wait();
                delete workers.at(i);
                delete workerThreads.at(i);
            }
        });
        QSignalSpy shellDuringTransfer(&connection, &SshConnection::shellDataReceived);
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
            if (i == 0) {
                connect(
                    worker, &SftpTransferWorker::progress, worker,
                    [worker, &uploadPaused](const QString&, const QString&, qint64 done, qint64) {
                        if (done > 0 && !uploadPaused.exchange(true))
                            worker->pause();
                    },
                    Qt::DirectConnection);
            }
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
        QTRY_VERIFY_WITH_TIMEOUT(uploadPaused.load(), 10000);
        connection.sendToShell(QByteArrayLiteral("printf '%s%s\\n' '__SFTP_' 'INTERACTIVE__'\n"));
        auto receivedShellMarker = [&]() {
            QByteArray output;
            for (const auto& event : shellDuringTransfer)
                output += event.first().toByteArray();
            return output.contains("__SFTP_INTERACTIVE__");
        };
        QTRY_VERIFY_WITH_TIMEOUT(receivedShellMarker(), 5000);
        workers.first()->resume();
        QTRY_COMPARE_WITH_TIMEOUT(parallelFinishedSpy.count(), 2, 10000);
        for (int i = 0; i < workerThreads.size(); ++i) {
            QVERIFY(workerThreads.at(i)->wait(3000));
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

        // Use the local server as both bastion and target to exercise the
        // nested SSH transport, including its callback context and SFTP.
        failureSpy.clear();
        connection.connectToHost(QStringLiteral("127.0.0.1"), port, user, clientKey, QString(), {},
                                 QStringLiteral("127.0.0.1"), port, user, clientKey);
        QTRY_VERIFY_WITH_TIMEOUT(connectedSpy.count() == 3 || !failureSpy.isEmpty(), 5000);
        QVERIFY2(failureSpy.isEmpty(), qPrintable(failureSpy.value(0).value(0).toString()));
        QCOMPARE(connectedSpy.count(), 3);
        connection.listDirectory(QStringLiteral("."));
        QTRY_COMPARE_WITH_TIMEOUT(directorySpy.count(), 3, 3000);
        QVERIFY(!qvariant_cast<QList<SftpFile>>(directorySpy.last().at(1)).isEmpty());
        connection.disconnectFromHost();

        const QList<Session> previousProfiles = SessionManager::loadSessions();
        const auto restoreProfiles = qScopeGuard([&]() { SessionManager::saveSessions(previousProfiles); });
        Session bastionProfile;
        bastionProfile.id = "integration-bastion";
        bastionProfile.type = SessionType::SSH;
        bastionProfile.host = "127.0.0.1";
        bastionProfile.port = port;
        bastionProfile.user = user;
        bastionProfile.keyPath = clientKey;
        Session targetProfile;
        targetProfile.id = "integration-target";
        targetProfile.type = SessionType::SSH;
        targetProfile.jumpSessionId = bastionProfile.id;
        SessionManager::saveSessions({bastionProfile, targetProfile});
        // A stale snapshot must not override the current saved bastion data.
        connection.connectToHost(QStringLiteral("127.0.0.1"), port, user, clientKey, QString(), {},
                                 QStringLiteral("stale.invalid"), 1, QString(), QString(), targetProfile.id);
        QTRY_VERIFY_WITH_TIMEOUT(connectedSpy.count() == 4 || !failureSpy.isEmpty(), 5000);
        QVERIFY2(failureSpy.isEmpty(), qPrintable(failureSpy.value(0).value(0).toString()));
        QCOMPARE(connectedSpy.count(), 4);
        connection.listDirectory(QStringLiteral("."));
        QTRY_COMPARE_WITH_TIMEOUT(directorySpy.count(), 4, 3000);
        QVERIFY(!qvariant_cast<QList<SftpFile>>(directorySpy.last().at(1)).isEmpty());
        connection.disconnectFromHost();
        sshd.terminate();
        QVERIFY(sshd.waitForFinished(3000));
    }

    void testTerminalSearchApi() {
        QTermWidget terminal(0);
        terminal.startExternal();
        terminal.setCodec(QStringLiteral("UTF-8"));
        terminal.feedData(QString::fromUtf8("Alpha café a.b\r\nalpha second\r\n").toUtf8());
        QTest::qWait(50);
        QSignalSpy results(&terminal, &QTermWidget::searchResult);
        terminal.searchText(QStringLiteral("café"), true, true, true);
        QCOMPARE(results.count(), 1);
        QCOMPARE(results.takeFirst().at(0).toBool(), true);
        QCOMPARE(terminal.selectedText(), QStringLiteral("café"));
        terminal.searchText(QStringLiteral("a.b"), true, true, true);
        QCOMPARE(results.count(), 1);
        QCOMPARE(results.takeFirst().at(0).toBool(), true);
        QCOMPARE(terminal.selectedText(), QStringLiteral("a.b"));
        terminal.searchText(QStringLiteral("ALPHA"), true, true, true);
        QCOMPARE(results.count(), 1);
        QCOMPARE(results.takeFirst().at(0).toBool(), false);
        terminal.searchText(QStringLiteral("ALPHA"), true, true, false);
        QCOMPARE(results.count(), 1);
        QCOMPARE(results.takeFirst().at(0).toBool(), true);
        terminal.searchText(QStringLiteral("missing"), false, false, false);
        QCOMPARE(results.count(), 1);
        QCOMPARE(results.takeFirst().at(0).toBool(), false);
        terminal.searchText(QString(), true, true, false);
        QCOMPARE(results.count(), 1);
        QCOMPARE(results.takeFirst().at(0).toBool(), false);
    }

    void testMacroAsyncContinuation_data() {
        QTest::addColumn<QString>("pause");
        QTest::addColumn<bool>("expect");
        QTest::addColumn<QString>("lineBreak");
        QTest::addColumn<bool>("firstEnter");
        QTest::newRow("compact-pause") << "{{PAUSE:80}}" << false << "" << true;
        QTest::newRow("spaced-pause") << "{{PAUSE: 80 }}" << false << "" << true;
        QTest::newRow("pause-and-expect") << "{{PAUSE: 80}}" << true << "" << true;
        QTest::newRow("formatted-lf") << "{{PAUSE:80}}" << false << "\n" << true;
        QTest::newRow("formatted-crlf") << "{{PAUSE:80}}" << false << "\r\n" << true;
        QTest::newRow("no-implicit-enter") << "{{PAUSE:80}}" << false << "\n" << false;
    }

    void testMacroAsyncContinuation() {
#ifdef Q_OS_WIN
        QSKIP("This integration test uses /bin/sh.");
#else
        QFETCH(QString, pause);
        QFETCH(bool, expect);
        QFETCH(QString, lineBreak);
        QFETCH(bool, firstEnter);
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString outputPath = dir.filePath("macro-output.txt");
        const QString name = "macro-async-regression";
        QSettings settings;
        const QVariant oldNames = settings.value("macros/names");
        const QVariant oldTexts = settings.value("macros/texts");
        const auto restore = qScopeGuard([&]() {
            settings.setValue("macros/names", oldNames);
            settings.setValue("macros/texts", oldTexts);
        });
        const QString wait = expect ? "{{EXPECT:READY_TO_CONTINUE}}" : QString();
        const QString macro = QString("printf first > '%1'").arg(outputPath) +
                              (firstEnter ? "{{ENTER}}" : "") + lineBreak + pause + lineBreak + wait +
                              (firstEnter ? "" : "{{ENTER}}") + "{{PAUSE:80}}" + lineBreak +
                              QString("printf second >> '%1'{{ENTER}}").arg(outputPath) + lineBreak + pause + lineBreak +
                              QString("printf third >> '%1'{{ENTER}}").arg(outputPath);
        settings.setValue("macros/names", QStringList{name});
        settings.setValue("macros/texts", QStringList{macro});
        MainWindow window;
        Session session;
        session.id = name;
        session.name = name;
        session.type = SessionType::Local;
        session.shellPath = "/bin/sh";
        QVERIFY(QMetaObject::invokeMethod(&window, "onConnectSession", Qt::DirectConnection, Q_ARG(Session, session)));
        auto* tab = window.findChild<TerminalTab*>();
        QVERIFY(tab);
        QAction* action = nullptr;
        for (auto* candidate : window.findChildren<QAction*>())
            if (candidate->text() == name)
                action = candidate;
        QVERIFY(action);
        auto contents = [&]() {
            QFile file(outputPath);
            return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
        };
        action->trigger();
        if (!firstEnter) {
            QTest::qWait(30);
            QVERIFY(!QFileInfo::exists(outputPath));
        }
        QTRY_COMPARE_WITH_TIMEOUT(contents(), QByteArray("first"), 2000);
        if (expect) {
            QTest::qWait(150);
            QCOMPARE(contents(), QByteArray("first"));
            QVERIFY(QMetaObject::invokeMethod(tab, "terminalDataReceived", Qt::DirectConnection,
                                              Q_ARG(QByteArray, QByteArray("READY_TO_CONTINUE"))));
        }
        QTRY_COMPARE_WITH_TIMEOUT(contents(), QByteArray("firstsecondthird"), 3000);
        const auto log = QSettings().value("macros/log").toStringList();
        QVERIFY(!log.isEmpty());
        QVERIFY(log.first().contains("Macro completed"));
#endif
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

#ifdef Q_OS_WIN
        // Offscreen windows cannot be relied on to handle WM_CLOSE from
        // QProcess::terminate(). This smoke test only checks process startup.
        application.kill();
#else
        application.terminate();
#endif
        QVERIFY(application.waitForFinished(3000));
    }

    void testWelcomeScreenLayout() {
        MainWindow window;
        window.resize(1280, 800);
        window.show();
        QTest::qWait(100);

        auto* welcome = window.findChild<QWidget*>(QStringLiteral("welcomeScreen"));
        QVERIFY(welcome != nullptr);
        QVERIFY(welcome->isVisible());
        const auto cards = welcome->findChildren<QFrame*>(QStringLiteral("welcomeCard"));
        QCOMPARE(cards.size(), 2);
        QVERIFY(cards.at(0)->geometry().right() < cards.at(1)->geometry().left());
        QVERIFY(welcome->findChild<QPushButton*>(QStringLiteral("welcomeSecondaryButton")) != nullptr);
        QVERIFY(welcome->findChild<QPushButton*>(QStringLiteral("welcomeLinkButton")) != nullptr);

        const QString screenshotPath = qEnvironmentVariable("BANCHO_WELCOME_SCREENSHOT");
        if (!screenshotPath.isEmpty())
            QVERIFY(window.grab().save(screenshotPath));
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
        // Windows may take several seconds to report a refused connection
        // when no VNC server is listening.  Allow the client timeout plus
        // the automatic reconnect delay before declaring the test failed.
        QTRY_VERIFY_WITH_TIMEOUT(firstTab.isNull(), 20000);
        QCOMPARE(window.findChildren<TerminalTab*>().size(), 1);
    }
#endif

#ifndef Q_OS_WIN
    void testRdpReconnectRequestAfterClientFailure() {
#ifndef BANCHO_HAVE_FREERDP
        QTemporaryDir emptyPath;
        QVERIFY(emptyPath.isValid());
        const QByteArray oldPath = qgetenv("PATH");
        qputenv("PATH", emptyPath.path().toUtf8());
#endif

        Session session;
        session.id = QStringLiteral("rdp-reconnect-test");
        session.name = QStringLiteral("Unavailable RDP");
        session.type = SessionType::RDP;
        session.host = QStringLiteral("127.0.0.1");
        session.port = 1;
        session.user = QStringLiteral("test-user");
        session.autoReconnect = true;

#ifdef BANCHO_HAVE_FREERDP
        // The embedded client collects credentials in Qt rather than failing
        // to find xfreerdp in PATH. Answer every reconnect's modal dialog.
        QTimer credentials;
        connect(&credentials, &QTimer::timeout, this, [] {
            auto* dialog = qobject_cast<QDialog*>(QApplication::activeModalWidget());
            if (!dialog || dialog->windowTitle() != QStringLiteral("RDP credentials"))
                return;
            for (auto* edit : dialog->findChildren<QLineEdit*>())
                if (edit->echoMode() == QLineEdit::Password)
                    edit->setText(QStringLiteral("test-password"));
            dialog->accept();
        });
        credentials.start(10);
#endif
        MainWindow window;
        window.show();
        QVERIFY(QMetaObject::invokeMethod(&window, "onConnectSession", Qt::DirectConnection, Q_ARG(Session, session)));
        const auto tabs = window.findChildren<TerminalTab*>();
        QCOMPARE(tabs.size(), 1);
        QPointer<TerminalTab> firstTab = tabs.first();
        QTRY_VERIFY_WITH_TIMEOUT(firstTab.isNull(), 8000);
        QCOMPARE(window.findChildren<TerminalTab*>().size(), 1);
#ifndef BANCHO_HAVE_FREERDP
        qputenv("PATH", oldPath);
#endif
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

    void testRestoredTerminalSize() {
#ifdef Q_OS_WIN
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const QString path = directory.filePath("console-size.txt");
        const QByteArray previous = qgetenv("BANCHO_CONSOLE_SIZE_PROBE");
        const auto restore = qScopeGuard([&]() {
            if (previous.isNull())
                qunsetenv("BANCHO_CONSOLE_SIZE_PROBE");
            else
                qputenv("BANCHO_CONSOLE_SIZE_PROBE", previous);
        });
        qputenv("BANCHO_CONSOLE_SIZE_PROBE", path.toLocal8Bit());
        Session session;
        session.type = SessionType::Local;
        session.shellPath = QDir(QCoreApplication::applicationDirPath()).filePath("conpty_input_probe.exe");
        QTabWidget tabs;
        tabs.resize(960, 640);
        auto* tab = new TerminalTab(session);
        tabs.addTab(tab, "Restored");
        tabs.addTab(new QWidget, "Active");
        tabs.setCurrentIndex(1);
        tabs.show();
        QTest::qWait(500);
        auto* terminal = tab->getTerminalWidget();
        QVERIFY(terminal);
        tabs.setCurrentIndex(0);
        QTest::qWait(100);
        QKeyEvent key(QEvent::KeyPress, Qt::Key_A, Qt::NoModifier, "a");
        terminal->sendKeyEvent(&key);
        const QByteArray expected =
            QByteArray::number(terminal->screenColumnsCount()) + " " + QByteArray::number(terminal->screenLinesCount());
        auto consoleSizeMatches = [&]() {
            QFile file(path);
            return file.open(QIODevice::ReadOnly) && file.readAll() == expected;
        };
        QTRY_VERIFY_WITH_TIMEOUT(consoleSizeMatches(), 5000);
#else
        QSKIP("ConPTY is only available on Windows.");
#endif
    }

    void testConPtyShiftEnter() {
#ifdef Q_OS_WIN
        ConPty console;
        QVERIFY(
            console.start(QDir(QCoreApplication::applicationDirPath()).filePath("conpty_input_probe.exe"), {}, 80, 25));
        QByteArray output;
        QTRY_VERIFY_WITH_TIMEOUT((output += console.read()).contains("INPUT_READY"), 5000);
        QTRY_VERIFY_WITH_TIMEOUT((output += console.read()).contains("COLORTERM=truecolor"), 5000);
        QVERIFY(console.write(QByteArrayLiteral("\x1b[1;2D")));
        QTRY_VERIFY_WITH_TIMEOUT((output += console.read()).contains("KEY=37 SHIFT=1"), 5000);
        QVERIFY(console.write(QByteArrayLiteral("\x1b[13;2u")));
        QTRY_VERIFY_WITH_TIMEOUT((output += console.read()).contains("KEY=13 SHIFT=1"), 5000);
#else
        QSKIP("ConPTY is only available on Windows.");
#endif
    }

    void testConPtyColorQueryRoundtrip() {
#ifdef Q_OS_WIN
        ConPty console;
        QVERIFY(console.start(QDir(QCoreApplication::applicationDirPath()).filePath("conpty_input_probe.exe"),
                              {"--query"}, 80, 25));
        QTermWidget terminal(0);
        terminal.startExternal();
        QByteArray replies;
        connect(&terminal, &QTermWidget::sendData, &terminal, [&console, &replies](const char* bytes, int size) {
            replies.append(bytes, size);
            console.write(QByteArray(bytes, size));
        });
        QByteArray output;
        auto received = [&]() {
            const QByteArray data = console.read();
            output += data;
            terminal.feedData(data);
            return output.contains("QUERY_REPLY_RECEIVED");
        };
        QTRY_VERIFY2_WITH_TIMEOUT(received(), ("output=" + output.toHex() + " replies=" + replies.toHex()).constData(),
                                  5000);
#else
        QSKIP("ConPTY is only available on Windows.");
#endif
    }

    void testConPtyRgbRendering() {
#ifdef Q_OS_WIN
        ConPty console;
        QVERIFY(console.start(QDir(QCoreApplication::applicationDirPath()).filePath("conpty_input_probe.exe"),
                              {"--colors"}, 80, 25));
        QByteArray output;
        QTRY_VERIFY_WITH_TIMEOUT((output += console.read()).contains("INPUT_READY"), 5000);
        QTermWidget terminal(0);
        terminal.resize(800, 600);
        terminal.startExternal();
        terminal.show();
        terminal.feedData(output);
        QTest::qWait(50);
        const QImage image = terminal.grab().toImage();
        int matchingPixels = 0;
        for (int y = 0; y < image.height(); ++y)
            for (int x = 0; x < image.width(); ++x)
                if (image.pixelColor(x, y) == QColor(17, 39, 61))
                    ++matchingPixels;
        QVERIFY2(matchingPixels > 100, output.toHex().constData());
#else
        QSKIP("ConPTY is only available on Windows.");
#endif
    }

    void testTerminalAppearanceSettings() {
        QSettings settings;
        const QVariant oldOpacity = settings.value("terminal/backgroundOpacity");
        const QVariant oldBlur = settings.value("terminal/backgroundBlur");
        const auto restore = qScopeGuard([&]() {
            for (const auto& pair : {qMakePair(QString("terminal/backgroundOpacity"), oldOpacity),
                                     qMakePair(QString("terminal/backgroundBlur"), oldBlur)}) {
                if (pair.second.isValid())
                    settings.setValue(pair.first, pair.second);
                else
                    settings.remove(pair.first);
            }
        });
        settings.setValue("terminal/backgroundOpacity", 100);
        settings.setValue("terminal/backgroundBlur", false);
        {
            SettingsDialog dialog;
            auto* opacity = dialog.findChild<QSpinBox*>("terminalBackgroundOpacity");
            auto* blur = dialog.findChild<QCheckBox*>("terminalBackgroundBlur");
            QVERIFY(opacity && blur);
            QCOMPARE(opacity->value(), 100);
            QVERIFY(!blur->isEnabled());
            opacity->setValue(60);
            QCOMPARE(blur->isEnabled(), TerminalAppearance::blurSupported());
            dialog.reject();
            QCOMPARE(settings.value("terminal/backgroundOpacity").toInt(), 100);
        }
        {
            SettingsDialog dialog;
            dialog.findChild<QSpinBox*>("terminalBackgroundOpacity")->setValue(0);
            QVERIFY(QMetaObject::invokeMethod(&dialog, "onAccept", Qt::DirectConnection));
            QCOMPARE(settings.value("terminal/backgroundOpacity").toInt(), 0);
            QCOMPARE(TerminalAppearance::opacityPercent(), 0);
        }
        SettingsDialog reopened;
        QCOMPARE(reopened.findChild<QSpinBox*>("terminalBackgroundOpacity")->value(), 0);
    }

    void testSettingsTypographyAccessible() {
        SettingsDialog dialog;
        dialog.resize(640, 480);
        dialog.show();
        auto* scroll = dialog.findChild<QScrollArea*>("appearanceScrollArea");
        auto* typography = dialog.findChild<QGroupBox*>("terminalTypography");
        QVERIFY(scroll && typography);
        QTRY_VERIFY(scroll->verticalScrollBar()->maximum() > 0);
        scroll->ensureWidgetVisible(typography);
        QCoreApplication::processEvents();
        const QRect typographyRect(typography->mapTo(scroll->viewport(), QPoint()), typography->size());
        QVERIFY(scroll->viewport()->rect().contains(typographyRect));
    }

    void testLinuxTerminalBlurToggle() {
        if (QApplication::platformName() != "wayland" && QApplication::platformName() != "xcb")
            QSKIP("Requires a Linux compositor with blur support.");
        QElapsedTimer timer;
        timer.start();
        while (!TerminalAppearance::blurSupported() && timer.elapsed() < 1000)
            QTest::qWait(10);
        if (!TerminalAppearance::blurSupported())
            QSKIP("Requires a Linux compositor with blur support.");
        QSettings settings;
        const QVariant oldOpacity = settings.value("terminal/backgroundOpacity");
        const QVariant oldBlur = settings.value("terminal/backgroundBlur");
        const auto restore = qScopeGuard([&]() {
            for (const auto& pair : {qMakePair(QString("terminal/backgroundOpacity"), oldOpacity),
                                     qMakePair(QString("terminal/backgroundBlur"), oldBlur)}) {
                if (pair.second.isValid())
                    settings.setValue(pair.first, pair.second);
                else
                    settings.remove(pair.first);
            }
        });
        settings.setValue("terminal/backgroundOpacity", 50);
        settings.setValue("terminal/backgroundBlur", true);
        QMainWindow window;
        TerminalAppearance::initializeWindow(&window);
        auto* terminal = new QTermWidget(0, &window);
        window.setCentralWidget(terminal);
        window.resize(640, 320);
        window.show();
        QTRY_VERIFY(window.property("terminalBlurActive").toBool());
        settings.setValue("terminal/backgroundBlur", false);
        TerminalAppearance::refreshWindow(&window);
        QVERIFY(!window.property("terminalBlurActive").toBool());
        settings.setValue("terminal/backgroundBlur", true);
        TerminalAppearance::refreshWindow(&window);
        QVERIFY(window.property("terminalBlurActive").toBool());
        terminal->hide();
        QTRY_VERIFY(!window.property("terminalBlurActive").toBool());
        terminal->show();
        QTRY_VERIFY(window.property("terminalBlurActive").toBool());
        settings.setValue("terminal/backgroundOpacity", 100);
        TerminalAppearance::refreshWindow(&window);
        QVERIFY(!window.property("terminalBlurActive").toBool());
    }

    void testNativeTerminalBlur() {
        if (QApplication::platformName() != "windows" || !TerminalAppearance::blurSupported())
            QSKIP("Requires a visible Windows 11 desktop compositor.");
        QSettings settings;
        const QVariant oldOpacity = settings.value("terminal/backgroundOpacity");
        const QVariant oldBlur = settings.value("terminal/backgroundBlur");
        const auto restore = qScopeGuard([&]() {
            for (const auto& pair : {qMakePair(QString("terminal/backgroundOpacity"), oldOpacity),
                                     qMakePair(QString("terminal/backgroundBlur"), oldBlur)}) {
                if (pair.second.isValid())
                    settings.setValue(pair.first, pair.second);
                else
                    settings.remove(pair.first);
            }
        });
        settings.setValue("terminal/backgroundOpacity", 50);
        settings.setValue("terminal/backgroundBlur", false);
        QPixmap pattern(20, 20);
        pattern.fill(Qt::red);
        {
            QPainter painter(&pattern);
            painter.fillRect(0, 0, 10, 10, Qt::blue);
            painter.fillRect(10, 10, 10, 10, Qt::blue);
        }
        QWidget background;
        QPalette palette = background.palette();
        palette.setBrush(QPalette::Window, pattern);
        background.setPalette(palette);
        background.setAutoFillBackground(true);
        background.setGeometry(100, 100, 900, 600);
        background.show();
        QMainWindow foreground;
        foreground.setWindowTitle("BanchoXterm - native title bar");
        TerminalAppearance::initializeWindow(&foreground);
        auto* terminal = new QTermWidget(0, &foreground);
        foreground.setCentralWidget(terminal);
        terminal->startExternal();
        foreground.setGeometry(180, 180, 640, 320);
        foreground.show();
        foreground.raise();
        foreground.activateWindow();
        TerminalAppearance::refreshWindow(&foreground);
        QVERIFY(!foreground.windowFlags().testFlag(Qt::FramelessWindowHint));
#ifdef Q_OS_WIN
        const HWND hwnd = reinterpret_cast<HWND>(foreground.winId());
        const auto style = GetWindowLongPtr(hwnd, GWL_STYLE);
        const LONG_PTR nativeFrame = WS_CAPTION | WS_SYSMENU | WS_THICKFRAME | WS_MINIMIZEBOX | WS_MAXIMIZEBOX;
        QCOMPARE(style & nativeFrame, nativeFrame);
        RECT outer{}, client{};
        QVERIFY(GetWindowRect(hwnd, &outer));
        QVERIFY(GetClientRect(hwnd, &client));
        QVERIFY(outer.bottom - outer.top > client.bottom - client.top);
#endif
        foreground.resize(700, 500);
        auto capture = [&]() {
            QTest::qWait(600);
            const QPoint position = terminal->mapToGlobal(QPoint(100, 100));
            return foreground.screen()->grabWindow(0, position.x(), position.y(), 120, 120).toImage();
        };
        const QImage clear = capture();
        settings.setValue("terminal/backgroundBlur", true);
        TerminalAppearance::refreshWindow(&foreground);
        QVERIFY(foreground.property("terminalBlurActive").toBool());
        const QImage blurred = capture();
        clear.save("build/terminal-clear.png");
        blurred.save("build/terminal-blur.png");
        QVERIFY(!clear.isNull() && !blurred.isNull());
        QVERIFY(clear != blurred);
        auto caption = [&]() {
            QTest::qWait(300);
            const QPoint position = foreground.mapToGlobal(QPoint(150, -16));
            return foreground.screen()->grabWindow(0, position.x(), position.y(), 120, 6).toImage();
        };
        const QImage titleBefore = caption();
        auto luminance = [](const QColor& color) {
            auto linear = [](double value) {
                return value <= 0.04045 ? value / 12.92 : std::pow((value + 0.055) / 1.055, 2.4);
            };
            return 0.2126 * linear(color.redF()) + 0.7152 * linear(color.greenF()) + 0.0722 * linear(color.blueF());
        };
        double darkest = 1.0, lightest = 0.0;
        for (int y = 0; y < titleBefore.height(); ++y)
            for (int x = 0; x < titleBefore.width(); ++x) {
                const double value = luminance(titleBefore.pixelColor(x, y));
                darkest = qMin(darkest, value);
                lightest = qMax(lightest, value);
            }
        QVERIFY2((lightest + 0.05) / (darkest + 0.05) >= 4.5,
                 "Native title text must contrast with its opaque caption background.");

        palette.setColor(QPalette::Window, Qt::green);
        background.setPalette(palette);
        background.update();
        const QImage titleAfter = caption();
        titleBefore.save("build/title-before.png");
        titleAfter.save("build/title-after.png");
        QCOMPARE(titleBefore, titleAfter);
        settings.setValue("terminal/backgroundBlur", false);
        TerminalAppearance::refreshWindow(&foreground);
        QCOMPARE(caption(), titleBefore);
        foreground.showMaximized();
        QTRY_VERIFY(foreground.isMaximized());
        foreground.showNormal();
        QTRY_VERIFY(!foreground.isMaximized());
    }

    void testTerminalTransparency() {
        QSettings settings;
        const QVariant previous = settings.value("terminal/backgroundOpacity");
        const auto restore = qScopeGuard([&]() {
            if (previous.isValid())
                settings.setValue("terminal/backgroundOpacity", previous);
            else
                settings.remove("terminal/backgroundOpacity");
        });
        settings.setValue("terminal/backgroundOpacity", 50);
        QMainWindow window;
        TerminalAppearance::initializeWindow(&window);
        QVERIFY(window.testAttribute(Qt::WA_TranslucentBackground));
        auto* terminal = new QTermWidget(0, &window);
        window.setCentralWidget(terminal);
        terminal->startExternal();
        terminal->setBlinkingCursor(false);
        window.resize(640, 320);
        window.show();
        TerminalAppearance::refreshWindow(&window);
        auto* display = terminal->findChild<Konsole::TerminalDisplay*>();
        QVERIFY(display);
        QVERIFY(!display->testAttribute(Qt::WA_OpaquePaintEvent));
        // Changing the color scheme clears the background image. It must
        // preserve the translucent backing-store configuration.
        terminal->setColorScheme("DarkPastels");
        QVERIFY(!display->testAttribute(Qt::WA_OpaquePaintEvent));
        terminal->feedData(QByteArrayLiteral("\x1b[?25l\x1b[48;2;0;0;0;38;2;255;255;255mMMMMMMMM"));
        QTest::qWait(50);
        const QImage image = window.grab().toImage();
        const QPoint origin = terminal->mapTo(&window, QPoint());
        int translucentBackground = 0, opaqueText = 0;
        for (int y = origin.y(); y < origin.y() + 40; ++y)
            for (int x = origin.x(); x < origin.x() + 180; ++x) {
                const QColor pixel = image.pixelColor(x, y);
                if (pixel.alpha() > 0 && pixel.alpha() < 255)
                    ++translucentBackground;
                if (pixel.alpha() == 255 && pixel.red() > 200)
                    ++opaqueText;
            }
        QVERIFY(translucentBackground > 100);
        QVERIFY(opaqueText > 0);
        settings.setValue("terminal/backgroundOpacity", 100);
        TerminalAppearance::refreshWindow(&window);
        QVERIFY(display->testAttribute(Qt::WA_OpaquePaintEvent));
        QTest::qWait(50);
        const QImage opaque = window.grab().toImage();
        QCOMPARE(opaque.pixelColor(origin + QPoint(150, 30)).alpha(), 255);
    }

    void testTerminalDimRendering() {
        auto checkDim = [](QWidget& widget, const std::function<void(const QByteArray&)>& feed) {
            widget.resize(640, 320);
            widget.show();
            auto render = [&](const QByteArray& rendition) {
                feed(QByteArray("\x1b[?25l\x1b[0;48;2;0;0;0;38;2;255;255;255m\x1b[2J\x1b[H") + rendition + "MMMMMMMM");
                QTest::qWait(50);
                return widget.grab().toImage();
            };
            const QImage normal = render(QByteArray());
            const QImage dim = render(QByteArrayLiteral("\x1b[2m"));
            const QImage reset = render(QByteArrayLiteral("\x1b[2;22m"));
            auto brightness = [](const QImage& image) {
                qint64 sum = 0;
                for (int y = 0; y < image.height(); ++y)
                    for (int x = 0; x < image.width(); ++x) {
                        const QColor pixel = image.pixelColor(x, y);
                        sum += pixel.red() + pixel.green() + pixel.blue();
                    }
                return sum;
            };
            QVERIFY(brightness(dim) < brightness(normal));
            QCOMPARE(reset, normal);
        };
        QTermWidget terminal(0);
        terminal.setBlinkingCursor(false);
        terminal.startExternal();
        checkDim(terminal, [&](const QByteArray& data) { terminal.feedData(data); });
        VtTerminalWidget fallback;
        checkDim(fallback, [&](const QByteArray& data) { fallback.writeData(data); });
    }

    void testTerminalRgbColors() {
        for (const QByteArray& parameters :
             {QByteArray("48;2;12;34;56"), QByteArray("48:2:12:34:56"), QByteArray("48:2::12:34:56")}) {
            QTermWidget terminal(0);
            terminal.resize(640, 320);
            terminal.setBlinkingCursor(false);
            terminal.startExternal();
            terminal.show();
            terminal.feedData(QByteArray("\x1b[") + parameters + "m          \x1b[0m");
            QTest::qWait(50);
            const QImage image = terminal.grab().toImage();
            int matchingPixels = 0;
            for (int y = 0; y < image.height(); ++y)
                for (int x = 0; x < image.width(); ++x)
                    if (image.pixelColor(x, y) == QColor(12, 34, 56))
                        ++matchingPixels;
            QVERIFY2(matchingPixels > 100, parameters.constData());
        }
    }

    void testTerminalColorQueries() {
        QTermWidget terminal(0);
        terminal.startExternal();
        QByteArray response;
        connect(&terminal, &QTermWidget::sendData, &terminal,
                [&response](const char* data, int size) { response.append(data, size); });
        terminal.feedData(QByteArrayLiteral("\x1b]10;?\x07\x1b]11;?\x1b\\"));
        QTRY_VERIFY(response.contains("\x1b]10;rgb:") && response.contains("\x1b]11;rgb:"));
        QVERIFY(QRegularExpression(QStringLiteral("rgb:[0-9a-f]{4}/[0-9a-f]{4}/[0-9a-f]{4}"))
                    .match(QString::fromLatin1(response))
                    .hasMatch());
    }

    void testShiftEnterEncoding() {
        const QByteArray shiftedEnter("\x1b[13;2u");
        VtTerminalWidget fallback;
        QSignalSpy fallbackSpy(&fallback, &VtTerminalWidget::dataReady);
        for (int key : {Qt::Key_Return, Qt::Key_Enter}) {
            QTest::keyClick(&fallback, static_cast<Qt::Key>(key), Qt::ShiftModifier);
            QCOMPARE(fallbackSpy.takeFirst().at(0).toByteArray(), shiftedEnter);
            QTest::keyClick(&fallback, static_cast<Qt::Key>(key));
            QCOMPARE(fallbackSpy.takeFirst().at(0).toByteArray(), QByteArray("\r"));
        }

        for (const QString& binding : {QStringLiteral("default"), QStringLiteral("linux")}) {
            QTermWidget terminal(0);
            terminal.setKeyBindings(binding);
            terminal.startExternal();
            QList<QByteArray> data;
            connect(&terminal, &QTermWidget::sendData, &terminal,
                    [&data](const char* bytes, int size) { data.append(QByteArray(bytes, size)); });
            for (int key : {Qt::Key_Left, Qt::Key_Right, Qt::Key_Up, Qt::Key_Down}) {
                QKeyEvent shifted(QEvent::KeyPress, key, Qt::ShiftModifier);
                terminal.sendKeyEvent(&shifted);
                const char suffix = key == Qt::Key_Left    ? 'D'
                                    : key == Qt::Key_Right ? 'C'
                                    : key == Qt::Key_Up    ? 'A'
                                                           : 'B';
                QCOMPARE(data.takeFirst(), QByteArray("\x1b[1;2") + suffix);
            }
            for (int key : {Qt::Key_Return, Qt::Key_Enter}) {
                for (auto modifiers : {Qt::KeyboardModifiers(Qt::ShiftModifier),
                                       Qt::KeyboardModifiers(Qt::ShiftModifier | Qt::KeypadModifier)}) {
                    QKeyEvent shifted(QEvent::KeyPress, key, modifiers, QStringLiteral("\r"));
                    terminal.sendKeyEvent(&shifted);
                    QCOMPARE(data.takeFirst(), shiftedEnter);
                }
                QKeyEvent plain(QEvent::KeyPress, key, Qt::NoModifier, QStringLiteral("\r"));
                terminal.sendKeyEvent(&plain);
                QCOMPARE(data.takeFirst(), QByteArray("\r"));
            }
        }
    }

    void testLinuxTerminalDeleteAndBackspaceSequences() {
        QVERIFY(QTermWidget::availableKeyBindings().contains(QStringLiteral("linux")));

        QTermWidget widget(0);
        widget.setKeyBindings(QStringLiteral("linux"));
        widget.startExternal();
        QList<QByteArray> emittedData;
        connect(&widget, &QTermWidget::sendData, &widget,
                [&emittedData](const char* data, int size) { emittedData.append(QByteArray(data, size)); });

        QKeyEvent backspaceEvent(QEvent::KeyPress, Qt::Key_Backspace, Qt::NoModifier);
        widget.sendKeyEvent(&backspaceEvent);
        QKeyEvent deleteEvent(QEvent::KeyPress, Qt::Key_Delete, Qt::NoModifier);
        widget.sendKeyEvent(&deleteEvent);

        QCOMPARE(emittedData.size(), 2);
        QCOMPARE(emittedData.at(0), QByteArray("\x7f", 1));
        QCOMPARE(emittedData.at(1), QByteArray("\x1b[3~", 4));
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
