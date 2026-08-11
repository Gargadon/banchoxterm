#include <QTest>
#include <QTemporaryDir>
#include <QTemporaryFile>
#include <QFile>
#include <QDir>
#include <QJsonDocument>
#include <QJsonObject>
#include "session.h"

class TestSession : public QObject {
    Q_OBJECT

private slots:
    void initTestCase() {
        qRegisterMetaType<Session>("Session");
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
};

QTEST_MAIN(TestSession)
#include "test_session.moc"
