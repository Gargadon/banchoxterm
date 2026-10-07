#pragma once
#include <QWidget>
#include <QImage>
#include <QMutex>
#include <QList>
#include <QSet>
#include <atomic>
#include <thread>
#include <freerdp/freerdp.h>

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
    static BOOL postConnect(freerdp*);
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
};
