#pragma once
#include <QWidget>
#include <QImage>
#include <QMutex>
#include <QList>
#include <QSet>
#include <atomic>
#include <thread>
#include <freerdp/freerdp.h>
#include <freerdp/client/cliprdr.h>
#include <freerdp/client/disp.h>
#include <freerdp/event.h>
#include <QTimer>

// All protocol and framebuffer access stays on the worker thread.
class RdpClientWidget : public QWidget {
    Q_OBJECT
public:
    explicit RdpClientWidget(QWidget* parent = nullptr);
    ~RdpClientWidget() override;
    void start(const QString& host, int port, const QString& user, const QString& domain, const QString& password);
    void stop();
signals:
    void connected();
    void disconnected();
    void errorOccurred(const QString& message);

protected:
    void paintEvent(QPaintEvent*) override;
    void keyPressEvent(QKeyEvent*) override;
    void keyReleaseEvent(QKeyEvent*) override;
    void mousePressEvent(QMouseEvent*) override;
    void mouseReleaseEvent(QMouseEvent*) override;
    void mouseMoveEvent(QMouseEvent*) override;
    void wheelEvent(QWheelEvent*) override;
    void focusOutEvent(QFocusEvent*) override;
    bool event(QEvent*) override;
    void focusInEvent(QFocusEvent*) override;
    void resizeEvent(QResizeEvent*) override;

private:
    struct Context {
        rdpContext base;
        RdpClientWidget* widget;
    };
    struct Input {
        bool keyboard;
        UINT16 flags;
        UINT32 code;
        UINT16 x = 0;
        UINT16 y = 0;
    };
    friend class TestRdp;
    static BOOL preConnect(freerdp*);
    static BOOL postConnect(freerdp*);
    static void channelConnected(void*, const ChannelConnectedEventArgs*);
    static void channelDisconnected(void*, const ChannelDisconnectedEventArgs*);
    static UINT clipboardReady(CliprdrClientContext*, const CLIPRDR_MONITOR_READY*);
    static UINT clipboardFormats(CliprdrClientContext*, const CLIPRDR_FORMAT_LIST*);
    static UINT clipboardRequest(CliprdrClientContext*, const CLIPRDR_FORMAT_DATA_REQUEST*);
    static UINT clipboardResponse(CliprdrClientContext*, const CLIPRDR_FORMAT_DATA_RESPONSE*);
    static UINT displayCaps(DispClientContext*, UINT32, UINT32, UINT32);
    struct ChannelEvent {
        enum Type { Ready, Formats, Request, Response, DisplayCaps } type;
        void* source = nullptr;
        UINT32 format = 0;
        QByteArray data;
        bool success = false;
        quint64 maxArea = 0;
    };
    UINT queueChannelEvent(ChannelEvent);
    void processChannels();
    void requestRemoteClipboard();
    void captureLocalClipboard();
    static QByteArray encodeClipboardText(QString);
    static QString decodeClipboardText(const QByteArray&);
    static QSize remoteSize(QSize);

    static BOOL beginPaint(rdpContext*);
    static BOOL endPaint(rdpContext*);
    static BOOL desktopResize(rdpContext*);
    static BOOL authenticate(freerdp*, char**, char**, char**, rdp_auth_reason);
    void run();
    void enqueue(const Input&);
    void sendKey(QKeyEvent*, bool down);
    void sendMouse(const QPointF&, UINT16 flags);
    QRect imageRect() const;
    QMutex m_mutex;
    QMutex m_instanceMutex;
    QMutex m_channelMutex;
    QImage m_frame;
    QList<Input> m_inputs;
    QSet<UINT32> m_pressedKeys;
    Qt::MouseButtons m_pressedButtons;
    std::thread m_thread;
    std::atomic<bool> m_stop{false};
    std::atomic<bool> m_connected{false};
    std::atomic<bool> m_updatePending{false};
    freerdp* m_instance = nullptr;
    QString m_host, m_user, m_domain, m_password;
    int m_port = 3389;
    QSize m_initialSize;
    QSize m_requestedSize;
    QSize m_lastSentSize;
    QTimer m_resizeTimer;
    QList<ChannelEvent> m_channelEvents;
    QString m_clipboardText;
    bool m_clipboardDirty = false;
    quint64 m_clipboardRevision = 0;
    bool m_settingRemoteClipboard = false; // GUI thread only
    bool m_clipboardEnabled = true;
    bool m_dynamicResolution = true;
    std::atomic<bool> m_clipboardFocused{false};
    std::atomic<quint64> m_generation{0};
    // Channel interfaces are protected against concurrent channel detach callbacks.
    // Protocol sends and queued callback processing stay on run().
    CliprdrClientContext* m_cliprdr = nullptr;
    DispClientContext* m_disp = nullptr;
    bool m_clipboardReady = false;
    bool m_displayReady = false;
    quint64 m_displayMaxArea = 0;
    bool m_remoteHasText = false;
    bool m_clipboardRequestPending = false;
    quint64 m_remoteFormatsSerial = 0;
    quint64 m_requestedFormatsSerial = 0;
    quint64 m_requestedClipboardRevision = 0;
};
