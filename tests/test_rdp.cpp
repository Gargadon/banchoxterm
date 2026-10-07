#include <QTest>
#include <QSignalSpy>
#include <QTcpServer>
#include <QElapsedTimer>
#include "rdpclientwidget.h"

class TestRdp : public QObject {
    Q_OBJECT
private slots:
    void stopDuringConnect() {
        // Covers abort before the worker publishes its context, and repeated cleanup.
        for (int i = 0; i < 10; ++i) {
            RdpClientWidget widget;
            QElapsedTimer timer;
            timer.start();
            widget.start("127.0.0.1", 1, "user", "", "password");
            widget.stop();
            widget.stop();
            QVERIFY(timer.elapsed() < 2000);
        }
    }
    void connectionFailure() {
        QTcpServer server;
        if (!server.listen(QHostAddress::LocalHost, 0))
            QSKIP("Local TCP sockets are unavailable");
        const int port = server.serverPort();
        server.close();
        RdpClientWidget widget;
        QSignalSpy errors(&widget, &RdpClientWidget::errorOccurred);
        QSignalSpy connected(&widget, &RdpClientWidget::connected);
        widget.start("127.0.0.1", port, "user", "", "password");
        QTRY_COMPARE_WITH_TIMEOUT(errors.count(), 1, 15000);
        QCOMPARE(connected.count(), 0);
        QVERIFY(!errors.first().first().toString().isEmpty());
        widget.stop();
    }
};
QTEST_MAIN(TestRdp)
#include "test_rdp.moc"
