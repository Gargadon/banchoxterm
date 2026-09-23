#pragma once
#include <QWidget>
#include <QImage>
#include <QByteArray>
#include <QMutex>
#include <QList>
#include <thread>
#include <atomic>
#include <rfb/rfbclient.h>

// A minimal embedded VNC client based on libvncclient. Runs the RFB protocol
// loop in a worker thread and renders the remote framebuffer into this widget.
class VncClientWidget : public QWidget {
    Q_OBJECT
public:
    explicit VncClientWidget(QWidget* parent = nullptr);
    ~VncClientWidget() override;

    void start(const QString& host, int port, const QString& password);
    void stop();

    bool isConnected() const {
        return m_connected;
    }

signals:
    void connected();
    void disconnected();
    void errorOccurred(const QString& message);

protected:
    void paintEvent(QPaintEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;
    void keyReleaseEvent(QKeyEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;

private:
    enum class ScaleMode { Fit, OneToOne };

    struct PendingInput {
        enum Type { Key, Pointer, Clipboard } type = Key;
        rfbKeySym keysym = 0;
        rfbBool down = 0;
        int x = 0;
        int y = 0;
        int buttonMask = 0;
        QByteArray clipboardText;
    };

    void runVncLoop();
    void processPendingInput(rfbClient* client);
    void enqueueKey(rfbKeySym keysym, bool down);
    void enqueuePointer(int x, int y, int buttonMask);
    void enqueueClipboard(const QByteArray& text);
    void showDisplayContextMenu(const QPoint& position);
    void sendPointerEvent(QMouseEvent* event, bool down);
    rfbKeySym keysymForEvent(QKeyEvent* event) const;

    static rfbBool onMallocFrameBuffer(rfbClient* client);
    static void onGotFrameBufferUpdate(rfbClient* client, int x, int y, int w, int h);
    static void onGotXCutText(rfbClient* client, const char* text, int textLength);
    static rfbCredential* onGetCredential(rfbClient* client, int credentialType);

    rfbClient* m_client = nullptr;
    QImage m_framebuffer;
    QMutex m_framebufferMutex;
    QMutex m_inputMutex;
    QList<PendingInput> m_pendingInputs;

    std::thread m_thread;
    std::atomic<bool> m_stopRequested{false};
    std::atomic<bool> m_connected{false};
    std::atomic<int> m_remoteWidth{0};
    std::atomic<int> m_remoteHeight{0};
    std::atomic<bool> m_updatingClipboard{false};

    QString m_host;
    int m_port = 5900;
    QString m_password;
    int m_buttonMask = 0;
    ScaleMode m_scaleMode = ScaleMode::Fit;
};
