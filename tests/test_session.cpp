#include <QTest>
#include <QCoreApplication>
#include <QTemporaryDir>
#include <QTemporaryFile>
#include <QFile>
#include <QDir>
#include <QJsonDocument>
#include <QJsonObject>
#include "session.h"
#include "masterpasswordmanager.h"
#include "vtterminalwidget.h"

class TestSession : public QObject {
    Q_OBJECT

private slots:
    void initTestCase() {
        qRegisterMetaType<Session>("Session");
        // Match the app's QSettings namespace so MasterPasswordManager tests
        // use a valid settings path.
        QCoreApplication::setOrganizationName(QStringLiteral("BanchoXterm"));
        QCoreApplication::setApplicationName(QStringLiteral("BanchoXterm"));
    }

    void testSshSessionRoundtrip() {
        Session original;
        original.id = "test-id-ssh";
        original.name = "My Server";
        original.type = SessionType::SSH;
        original.host = "example.com";
        original.user = "admin";
        original.port = 2222;
        original.keyPath = "/home/user/.ssh/id_rsa";
        original.x11Forwarding = true;
        original.keepAliveSeconds = 30;
        original.cryptCipher = "aes128-ctr,aes256-ctr";
        original.kexAlgo = "curve25519-sha256";
        original.macAlgo = "hmac-sha2-256";
        original.scrollback = 10000;
        original.fontFamily = "JetBrains Mono";
        original.fontSize = 12;

        QJsonObject json = original.toJson();
        QCOMPARE(json["id"].toString(), QString("test-id-ssh"));
        QCOMPARE(json["name"].toString(), QString("My Server"));
        QCOMPARE(json["type"].toString(), QString("ssh"));
        QCOMPARE(json["host"].toString(), QString("example.com"));
        QCOMPARE(json["user"].toString(), QString("admin"));
        QCOMPARE(json["port"].toInt(), 2222);
        QCOMPARE(json["keyPath"].toString(), QString("/home/user/.ssh/id_rsa"));
        QVERIFY(json["x11Forwarding"].toBool());

        Session restored = Session::fromJson(json);
        QCOMPARE(restored.id, original.id);
        QCOMPARE(restored.name, original.name);
        QCOMPARE(restored.type, original.type);
        QCOMPARE(restored.host, original.host);
        QCOMPARE(restored.user, original.user);
        QCOMPARE(restored.port, original.port);
        QCOMPARE(restored.keyPath, original.keyPath);
        QCOMPARE(restored.x11Forwarding, original.x11Forwarding);
        QCOMPARE(restored.keepAliveSeconds, original.keepAliveSeconds);
        QCOMPARE(restored.cryptCipher, original.cryptCipher);
        QCOMPARE(restored.kexAlgo, original.kexAlgo);
        QCOMPARE(restored.macAlgo, original.macAlgo);
        QCOMPARE(restored.scrollback, original.scrollback);
        QCOMPARE(restored.fontFamily, original.fontFamily);
        QCOMPARE(restored.fontSize, original.fontSize);
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

    void testMasterPasswordEncryption() {
        MasterPasswordManager& mpm = MasterPasswordManager::instance();
        
        mpm.lock();
        QVERIFY(!mpm.isUnlocked());
        
        QVERIFY(mpm.setMasterPassword("SuperSecure123!"));
        QVERIFY(mpm.isEnabled());
        QVERIFY(mpm.isUnlocked());
        
        QString plaintext = "MySshSecretPassword";
        QString encrypted = mpm.encryptPassword(plaintext);
        QVERIFY(encrypted.startsWith("BANCHO:"));
        QVERIFY(encrypted != plaintext);
        
        QString decrypted = mpm.decryptPassword(encrypted);
        QCOMPARE(decrypted, plaintext);
        
        mpm.lock();
        QVERIFY(!mpm.isUnlocked());
        
        QVERIFY(mpm.unlock("SuperSecure123!"));
        QVERIFY(mpm.isUnlocked());
        QCOMPARE(mpm.decryptPassword(encrypted), plaintext);
        
        QVERIFY(mpm.disableMasterPassword("SuperSecure123!"));
        QVERIFY(!mpm.isEnabled());
    }

    void testVtTerminalWidgetSearch() {
        VtTerminalWidget widget;
        widget.resize(800, 600); // Trigger resize to allocate buffer
        
        widget.writeData("Hello World\r\nThis is a test line\r\nError: connection failed\r\n");
        
        QVERIFY(widget.findText("test", true, true));
        QVERIFY(!widget.findText("ERROR", true, true));
        QVERIFY(widget.findText("ERROR", true, false));
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
