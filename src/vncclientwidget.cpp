#include "vncclientwidget.h"
#include <QPainter>
#include <QByteArray>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QWheelEvent>
#include <QMetaObject>
#include <cstring>
#include <cstdlib>

// X11 keysyms come from <rfb/keysym.h> (included via <rfb/rfbclient.h>).

VncClientWidget::VncClientWidget(QWidget* parent) : QWidget(parent) {
    setFocusPolicy(Qt::StrongFocus);
    setAttribute(Qt::WA_OpaquePaintEvent);
}

VncClientWidget::~VncClientWidget() {
    stop();
}

void VncClientWidget::start(const QString& host, int port, const QString& password) {
    m_host = host;
    m_port = port;
    m_password = password;
    m_stopRequested = false;
    m_thread = std::thread([this]() { runVncLoop(); });
}

void VncClientWidget::stop() {
    m_stopRequested = true;
    if (m_thread.joinable())
        m_thread.join();
}

rfbBool VncClientWidget::onMallocFrameBuffer(rfbClient* client) {
    VncClientWidget* self = static_cast<VncClientWidget*>(rfbClientGetClientData(client, nullptr));
    if (!self)
        return 0;
    if (client->frameBuffer) {
        free(client->frameBuffer);
        client->frameBuffer = nullptr;
    }
    self->m_remoteWidth = client->width;
    self->m_remoteHeight = client->height;
    {
        QMutexLocker locker(&self->m_framebufferMutex);
        self->m_framebuffer = QImage(client->width, client->height, QImage::Format_RGB32);
    }
    client->frameBuffer = static_cast<uint8_t*>(calloc(static_cast<size_t>(client->width) * client->height, 4));
    return client->frameBuffer ? 1 : 0;
}

void VncClientWidget::onGotFrameBufferUpdate(rfbClient* client, int x, int y, int w, int h) {
    VncClientWidget* self = static_cast<VncClientWidget*>(rfbClientGetClientData(client, nullptr));
    if (!self)
        return;
    QMutexLocker locker(&self->m_framebufferMutex);
    QImage& img = self->m_framebuffer;
    if (img.isNull())
        return;
    const int x1 = qMin(x + w, img.width());
    const int y1 = qMin(y + h, img.height());
    for (int row = qMax(0, y); row < y1; ++row) {
        const uint8_t* src = client->frameBuffer + (static_cast<size_t>(row) * client->width + qMax(0, x)) * 4;
        uint8_t* dst = img.scanLine(row) + qMax(0, x) * 4;
        memcpy(dst, src, static_cast<size_t>(qMax(0, x1 - qMax(0, x))) * 4);
    }
    QMetaObject::invokeMethod(self, [self]() { self->update(); }, Qt::QueuedConnection);
}

rfbCredential* VncClientWidget::onGetCredential(rfbClient* client, int credentialType) {
    VncClientWidget* self = static_cast<VncClientWidget*>(rfbClientGetClientData(client, nullptr));
    if (!self || credentialType != rfbCredentialTypeUser)
        return nullptr;
    rfbCredential* cred = static_cast<rfbCredential*>(calloc(1, sizeof(rfbCredential)));
    if (!cred)
        return nullptr;
    const QByteArray pwd = self->m_password.toUtf8();
    cred->userCredential.username = qstrdup("");
    cred->userCredential.password = qstrdup(pwd.constData());
    return cred;
}

void VncClientWidget::runVncLoop() {
    rfbClient* client = rfbGetClient(8, 3, 4);
    if (!client) {
        emit errorOccurred(tr("Failed to create VNC client"));
        return;
    }

    client->MallocFrameBuffer = &VncClientWidget::onMallocFrameBuffer;
    client->GotFrameBufferUpdate = &VncClientWidget::onGotFrameBufferUpdate;
    client->GetCredential = &VncClientWidget::onGetCredential;

    const QByteArray host = m_host.toUtf8();
    client->serverHost = qstrdup(host.constData());
    client->serverPort = m_port;
    client->connectTimeout = 10;

    // Request a 32-bit RGB framebuffer (matches QImage::Format_RGB32 layout).
    client->format.bitsPerPixel = 32;
    client->format.depth = 24;
    client->format.bigEndian = 0;
    client->format.trueColour = 1;
    client->format.redMax = 255;
    client->format.greenMax = 255;
    client->format.blueMax = 255;
    client->format.redShift = 16;
    client->format.greenShift = 8;
    client->format.blueShift = 0;

    rfbClientSetClientData(client, this, nullptr);
    m_client = client;

    int argc = 1;
    char progname[] = "banchoxterm";
    char* argv[1] = {progname};

    if (!rfbInitClient(client, &argc, argv)) {
        m_client = nullptr;
        rfbClientCleanup(client);
        emit errorOccurred(tr("VNC connection failed"));
        return;
    }

    m_connected = true;
    emit connected();

    SendFramebufferUpdateRequest(client, 0, 0, client->width, client->height, 0);

    while (!m_stopRequested) {
        if (WaitForMessage(client, 20000) < 0)
            break;
        processPendingInput(client);
    }

    m_connected = false;
    rfbClientCleanup(client);
    m_client = nullptr;
    emit disconnected();
}

void VncClientWidget::processPendingInput(rfbClient* client) {
    QList<PendingInput> batch;
    {
        QMutexLocker locker(&m_inputMutex);
        batch.swap(m_pendingInputs);
    }
    for (const PendingInput& in : batch) {
        if (in.type == PendingInput::Key)
            SendKeyEvent(client, in.keysym, in.down);
        else
            SendPointerEvent(client, in.x, in.y, in.buttonMask);
    }
}

void VncClientWidget::enqueueKey(rfbKeySym keysym, bool down) {
    QMutexLocker locker(&m_inputMutex);
    PendingInput in;
    in.type = PendingInput::Key;
    in.keysym = keysym;
    in.down = down;
    m_pendingInputs.append(in);
}

void VncClientWidget::enqueuePointer(int x, int y, int buttonMask) {
    QMutexLocker locker(&m_inputMutex);
    PendingInput in;
    in.type = PendingInput::Pointer;
    in.x = x;
    in.y = y;
    in.buttonMask = buttonMask;
    m_pendingInputs.append(in);
}

void VncClientWidget::paintEvent(QPaintEvent*) {
    QPainter painter(this);
    QMutexLocker locker(&m_framebufferMutex);
    if (m_framebuffer.isNull()) {
        locker.unlock();
        painter.fillRect(rect(), QColor(0, 0, 0));
        return;
    }
    const QImage copy = m_framebuffer.copy();
    locker.unlock();
    painter.drawImage(rect(), copy);
}

void VncClientWidget::resizeEvent(QResizeEvent*) {
    update();
}

rfbKeySym VncClientWidget::keysymForEvent(QKeyEvent* event) const {
    switch (event->key()) {
    case Qt::Key_Return:
    case Qt::Key_Enter:
        return XK_Return;
    case Qt::Key_Backspace:
        return XK_BackSpace;
    case Qt::Key_Tab:
        return XK_Tab;
    case Qt::Key_Escape:
        return XK_Escape;
    case Qt::Key_Delete:
        return XK_Delete;
    case Qt::Key_Insert:
        return XK_Insert;
    case Qt::Key_Home:
        return XK_Home;
    case Qt::Key_End:
        return XK_End;
    case Qt::Key_PageUp:
        return XK_Page_Up;
    case Qt::Key_PageDown:
        return XK_Page_Down;
    case Qt::Key_Left:
        return XK_Left;
    case Qt::Key_Up:
        return XK_Up;
    case Qt::Key_Right:
        return XK_Right;
    case Qt::Key_Down:
        return XK_Down;
    case Qt::Key_Shift:
        return XK_Shift_L;
    case Qt::Key_Control:
        return XK_Control_L;
    case Qt::Key_Alt:
        return XK_Alt_L;
    case Qt::Key_Meta:
        return XK_Meta_L;
    default:
        break;
    }

    if (event->key() >= Qt::Key_F1 && event->key() <= Qt::Key_F12)
        return XK_F1 + (event->key() - Qt::Key_F1);

    const QString text = event->text();
    if (text.size() == 1) {
        const ushort ch = text.at(0).unicode();
        if (ch >= 0x20 && ch <= 0x7E)
            return ch;
    }
    return 0;
}

void VncClientWidget::keyPressEvent(QKeyEvent* event) {
    const rfbKeySym keysym = keysymForEvent(event);
    if (keysym == 0) {
        QWidget::keyPressEvent(event);
        return;
    }

    switch (event->key()) {
    case Qt::Key_Shift:
    case Qt::Key_Control:
    case Qt::Key_Alt:
    case Qt::Key_Meta:
        enqueueKey(keysym, 1);
        break;
    default:
        enqueueKey(keysym, 1);
        enqueueKey(keysym, 0);
        break;
    }
    event->accept();
}

void VncClientWidget::keyReleaseEvent(QKeyEvent* event) {
    const rfbKeySym keysym = keysymForEvent(event);
    switch (event->key()) {
    case Qt::Key_Shift:
    case Qt::Key_Control:
    case Qt::Key_Alt:
    case Qt::Key_Meta:
        if (keysym != 0)
            enqueueKey(keysym, 0);
        break;
    default:
        break;
    }
    event->accept();
}

void VncClientWidget::sendPointerEvent(QMouseEvent* event, bool down) {
    const int remoteW = m_remoteWidth.load();
    const int remoteH = m_remoteHeight.load();
    if (remoteW <= 0 || remoteH <= 0)
        return;

    const int x = static_cast<int>(static_cast<qreal>(event->position().x()) * remoteW / qMax(1, width()));
    const int y = static_cast<int>(static_cast<qreal>(event->position().y()) * remoteH / qMax(1, height()));

    int btn = 0;
    if (event->button() == Qt::LeftButton)
        btn = rfbButton1Mask;
    else if (event->button() == Qt::MiddleButton)
        btn = rfbButton2Mask;
    else if (event->button() == Qt::RightButton)
        btn = rfbButton3Mask;

    if (down)
        m_buttonMask |= btn;
    else
        m_buttonMask &= ~btn;

    enqueuePointer(x, y, m_buttonMask);
}

void VncClientWidget::mousePressEvent(QMouseEvent* event) {
    sendPointerEvent(event, true);
    event->accept();
}

void VncClientWidget::mouseReleaseEvent(QMouseEvent* event) {
    sendPointerEvent(event, false);
    event->accept();
}

void VncClientWidget::mouseMoveEvent(QMouseEvent* event) {
    const int remoteW = m_remoteWidth.load();
    const int remoteH = m_remoteHeight.load();
    if (remoteW <= 0 || remoteH <= 0)
        return;

    int mask = 0;
    if (event->buttons() & Qt::LeftButton)
        mask |= rfbButton1Mask;
    if (event->buttons() & Qt::MiddleButton)
        mask |= rfbButton2Mask;
    if (event->buttons() & Qt::RightButton)
        mask |= rfbButton3Mask;
    m_buttonMask = mask;

    const int x = static_cast<int>(static_cast<qreal>(event->position().x()) * remoteW / qMax(1, width()));
    const int y = static_cast<int>(static_cast<qreal>(event->position().y()) * remoteH / qMax(1, height()));
    enqueuePointer(x, y, mask);
    event->accept();
}

void VncClientWidget::wheelEvent(QWheelEvent* event) {
    const int remoteW = m_remoteWidth.load();
    const int remoteH = m_remoteHeight.load();
    if (remoteW <= 0 || remoteH <= 0)
        return;

    const int x = static_cast<int>(static_cast<qreal>(event->position().x()) * remoteW / qMax(1, width()));
    const int y = static_cast<int>(static_cast<qreal>(event->position().y()) * remoteH / qMax(1, height()));

    const bool up = event->angleDelta().y() > 0;
    enqueuePointer(x, y, m_buttonMask | (up ? rfbWheelUpMask : rfbWheelDownMask));
    enqueuePointer(x, y, m_buttonMask);
    event->accept();
}
