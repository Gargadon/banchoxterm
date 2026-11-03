#include <QTest>
#include <QSignalSpy>
#include <QTcpServer>
#include <QElapsedTimer>
#include "rdpclientwidget.h"
#include <QApplication>
#include <QClipboard>
#include <QSettings>
#include <QScopeGuard>
#include <freerdp/settings.h>
#include <freerdp/client/cmdline.h>
#include <freerdp/channels/channels.h>

namespace {
struct ChannelRecorder {
    int capabilities = 0;
    int announcements = 0;
    int formats = 0;
    int requests = 0;
    int acknowledgements = 0;
    QByteArray response;
    UINT16 responseFlags = 0;
    int layouts = 0;
    QSize size;
} recorder;
} // namespace

class TestRdp : public QObject {
    Q_OBJECT
private slots:
    void loadInstalledChannels() {
        RdpClientWidget widget;
        auto* instance = freerdp_new();
        QVERIFY(instance);
        instance->ContextSize = sizeof(RdpClientWidget::Context);
        QVERIFY(freerdp_context_new(instance));
        reinterpret_cast<RdpClientWidget::Context*>(instance->context)->widget = &widget;
        const auto cleanup = qScopeGuard([&] {
            freerdp_context_free(instance);
            freerdp_free(instance);
        });
        auto* settings = instance->context->settings;
        QVERIFY(freerdp_settings_set_bool(settings, FreeRDP_RedirectClipboard, TRUE));
        QVERIFY(freerdp_settings_set_bool(settings, FreeRDP_SupportDisplayControl, TRUE));
        QVERIFY(freerdp_settings_set_bool(settings, FreeRDP_DynamicResolutionUpdate, TRUE));
        const char* dynamic[] = {"disp"};
        QVERIFY(freerdp_client_add_dynamic_channel(settings, 1, dynamic));
        QVERIFY(RdpClientWidget::preConnect(instance));
        QVERIFY(freerdp_settings_get_uint32(settings, FreeRDP_ChannelCount) > 0);
        PubSub_UnsubscribeChannelConnected(instance->context->pubSub, RdpClientWidget::channelConnected);
        PubSub_UnsubscribeChannelDisconnected(instance->context->pubSub, RdpClientWidget::channelDisconnected);
    }

    void clipboardUnicodeAndResizeChannels() {
        recorder = {};
        RdpClientWidget widget;
        widget.resize(800, 600);
        widget.show();
        widget.activateWindow();
        widget.setFocus();
        QCoreApplication::processEvents();
        QVERIFY(widget.hasFocus());
        widget.m_connected = true;
        widget.m_clipboardFocused = true;
        widget.m_clipboardText = QString::fromUtf8("línea 1\n界 😀");
        widget.m_requestedSize = QSize(800, 600);
        CliprdrClientContext clipboard{};
        clipboard.custom = &widget;
        clipboard.ClientCapabilities = [](CliprdrClientContext*, const CLIPRDR_CAPABILITIES* caps) -> UINT {
            recorder.capabilities = caps->cCapabilitiesSets;
            return CHANNEL_RC_OK;
        };
        clipboard.ClientFormatList = [](CliprdrClientContext*, const CLIPRDR_FORMAT_LIST* list) -> UINT {
            ++recorder.announcements;
            recorder.formats = list->numFormats;
            return CHANNEL_RC_OK;
        };
        clipboard.ClientFormatListResponse = [](CliprdrClientContext*, const CLIPRDR_FORMAT_LIST_RESPONSE*) -> UINT {
            ++recorder.acknowledgements;
            return CHANNEL_RC_OK;
        };
        clipboard.ClientFormatDataRequest = [](CliprdrClientContext*,
                                               const CLIPRDR_FORMAT_DATA_REQUEST* request) -> UINT {
            if (request->requestedFormatId == CF_UNICODETEXT)
                ++recorder.requests;
            return CHANNEL_RC_OK;
        };
        clipboard.ClientFormatDataResponse = [](CliprdrClientContext*,
                                                const CLIPRDR_FORMAT_DATA_RESPONSE* response) -> UINT {
            recorder.responseFlags = response->common.msgFlags;
            recorder.response =
                QByteArray(reinterpret_cast<const char*>(response->requestedFormatData), response->common.dataLen);
            return CHANNEL_RC_OK;
        };
        widget.m_cliprdr = &clipboard;
        QCOMPARE(RdpClientWidget::clipboardReady(&clipboard, nullptr), UINT(CHANNEL_RC_OK));
        widget.processChannels();
        QCOMPARE(recorder.capabilities, 1);
        QCOMPARE(recorder.formats, 1);
        CLIPRDR_FORMAT_DATA_REQUEST request{};
        request.requestedFormatId = CF_UNICODETEXT;
        QCOMPARE(RdpClientWidget::clipboardRequest(&clipboard, &request), UINT(CHANNEL_RC_OK));
        widget.processChannels();
        QCOMPARE(recorder.responseFlags, UINT16(CB_RESPONSE_OK));
        QVERIFY(recorder.response.endsWith(QByteArray(2, '\0')));
        QCOMPARE(RdpClientWidget::decodeClipboardText(recorder.response), QString::fromUtf8("línea 1\n界 😀"));
        request.requestedFormatId = CF_DIB;
        QCOMPARE(RdpClientWidget::clipboardRequest(&clipboard, &request), UINT(CHANNEL_RC_OK));
        widget.processChannels();
        QCOMPARE(recorder.responseFlags, UINT16(CB_RESPONSE_FAIL));

        CLIPRDR_FORMAT format{CF_UNICODETEXT, nullptr};
        CLIPRDR_FORMAT_LIST list{};
        list.numFormats = 1;
        list.formats = &format;
        QCOMPARE(RdpClientWidget::clipboardFormats(&clipboard, &list), UINT(CHANNEL_RC_OK));
        widget.processChannels();
        QCOMPARE(recorder.acknowledgements, 1);
        QCOMPARE(recorder.requests, 1);
        const auto bytes = RdpClientWidget::encodeClipboardText(QString::fromUtf8("remoto\n日本語 😀"));
        CLIPRDR_FORMAT_DATA_RESPONSE response{};
        response.common.msgFlags = CB_RESPONSE_OK;
        response.common.dataLen = bytes.size();
        response.requestedFormatData = reinterpret_cast<const BYTE*>(bytes.constData());
        QCOMPARE(RdpClientWidget::clipboardResponse(&clipboard, &response), UINT(CHANNEL_RC_OK));
        widget.processChannels();
        QCoreApplication::processEvents();
        QCOMPARE(QApplication::clipboard()->text(), QString::fromUtf8("remoto\n日本語 😀"));
        const int announcements = recorder.announcements;
        widget.processChannels();
        QCOMPARE(recorder.announcements, announcements); // no clipboard feedback loop
        // A local copy while awaiting remote text must win over the old remote response.
        QCOMPARE(RdpClientWidget::clipboardFormats(&clipboard, &list), UINT(CHANNEL_RC_OK));
        widget.processChannels();
        QApplication::clipboard()->setText("new local copy");
        QCOMPARE(RdpClientWidget::clipboardResponse(&clipboard, &response), UINT(CHANNEL_RC_OK));
        widget.processChannels();
        QCoreApplication::processEvents();
        QCOMPARE(QApplication::clipboard()->text(), QString("new local copy"));
        // Malformed UTF-16 cannot replace a valid clipboard.
        QCOMPARE(RdpClientWidget::clipboardFormats(&clipboard, &list), UINT(CHANNEL_RC_OK));
        widget.processChannels();
        response.common.dataLen = 3;
        QCOMPARE(RdpClientWidget::clipboardResponse(&clipboard, &response), UINT(CHANNEL_RC_OK));
        widget.processChannels();
        QCoreApplication::processEvents();
        QCOMPARE(QApplication::clipboard()->text(), QString("new local copy"));

        DispClientContext display{};
        display.custom = &widget;
        display.SendMonitorLayout = [](DispClientContext*, UINT32 count,
                                       DISPLAY_CONTROL_MONITOR_LAYOUT* layout) -> UINT {
            recorder.layouts += count;
            recorder.size = QSize(layout->Width, layout->Height);
            return CHANNEL_RC_OK;
        };
        widget.m_disp = &display;
        QCOMPARE(RdpClientWidget::displayCaps(&display, 1, 8192, 8192), UINT(CHANNEL_RC_OK));
        widget.processChannels();
        QCOMPARE(recorder.layouts, 1);
        QCOMPARE(recorder.size, QSize(800, 600));
        widget.processChannels();
        QCOMPARE(recorder.layouts, 1); // no duplicate layout requests
        widget.m_requestedSize = RdpClientWidget::remoteSize(QSize(1001, 720));
        widget.processChannels();
        QCOMPARE(recorder.size, QSize(1000, 720));
        QCOMPARE(RdpClientWidget::remoteSize(QSize(1, 99999)), QSize(200, 8192));
        QCOMPARE(RdpClientWidget::displayCaps(&display, 1, 200, 200), UINT(CHANNEL_RC_OK));
        widget.processChannels();
        QCOMPARE(recorder.layouts, 2); // respect the server's advertised monitor area
        // Simulate channel detach: stale callback events cannot use dead interfaces.
        RdpClientWidget::Context context{};
        context.widget = &widget;
        ChannelDisconnectedEventArgs gone{};
        gone.name = DISP_DVC_CHANNEL_NAME;
        RdpClientWidget::channelDisconnected(&context, &gone);
        widget.m_requestedSize = QSize(1200, 900);
        widget.processChannels();
        QCOMPARE(recorder.layouts, 2);
        gone.name = CLIPRDR_SVC_CHANNEL_NAME;
        RdpClientWidget::channelDisconnected(&context, &gone);
        QCOMPARE(RdpClientWidget::clipboardRequest(&clipboard, &request), UINT(CHANNEL_RC_OK));
        widget.processChannels();
        QVERIFY(widget.m_cliprdr == nullptr);
        widget.m_connected = false;
    }

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
