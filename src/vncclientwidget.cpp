#include "vncclientwidget.h"
#include <QPainter>
#include <QByteArray>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QWheelEvent>
#include <QMetaObject>
#include <QApplication>
#include <QClipboard>
#include <QMenu>
#include <QAction>
#include <cstring>
#include <cstdlib>

// X11 keysyms come from <rfb/keysym.h> (included via <rfb/rfbclient.h>).

VncClientWidget::VncClientWidget(QWidget* parent) : QWidget(parent) {
    setFocusPolicy(Qt::StrongFocus);
    setAttribute(Qt::WA_OpaquePaintEvent);
    setContextMenuPolicy(Qt::CustomContextMenu);
    connect(this, &QWidget::customContextMenuRequested, this, &VncClientWidget::showDisplayContextMenu);
    connect(QApplication::clipboard(), &QClipboard::dataChanged, this, [this]() {
        if (!m_connected || m_updatingClipboard)
            return;
        const QString text = QApplication::clipboard()->text();
        if (!text.isEmpty())
            enqueueClipboard(text.toUtf8());
    });
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

void VncClientWidget::onGotXCutText(rfbClient* client, const char* text, int textLength) {
    VncClientWidget* self = static_cast<VncClientWidget*>(rfbClientGetClientData(client, nullptr));
    if (!self || !text || textLength <= 0)
        return;
    const QString clipboardText = QString::fromUtf8(text, textLength);
    QMetaObject::invokeMethod(
        self,
        [self, clipboardText]() {
            self->m_updatingClipboard = true;
            QApplication::clipboard()->setText(clipboardText);
            self->m_updatingClipboard = false;
        },
        Qt::QueuedConnection);
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
    client->GotXCutText = &VncClientWidget::onGotXCutText;
    client->GetCredential = &VncClientWidget::onGetCredential;

    const QByteArray host = m_host.toUtf8();
    client->serverHost = qstrdup(host.constData());
    client->serverPort = m_port;
    client->connectTimeout = 10;
    // Prefer compressed encodings when the server supports them, with Raw as
    // a final fallback for older or minimal VNC implementations.
    client->appData.encodingsString = "tight,zrle,hextile,copyrect,raw";
    client->appData.enableJPEG = 1;
    client->appData.qualityLevel = 8;

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
        // rfbInitClient() owns and cleans up the client structure on failure.
        // Calling rfbClientCleanup() here again double-frees libvncclient's
        // partially initialized connection state.
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
        else if (in.type == PendingInput::Pointer)
            SendPointerEvent(client, in.x, in.y, in.buttonMask);
        else if (!in.clipboardText.isEmpty()) {
            QByteArray clipboardText = in.clipboardText;
            SendClientCutText(client, clipboardText.data(), clipboardText.size());
        }
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

void VncClientWidget::enqueueClipboard(const QByteArray& text) {
    QMutexLocker locker(&m_inputMutex);
    PendingInput in;
    in.type = PendingInput::Clipboard;
    in.clipboardText = text;
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
    painter.fillRect(rect(), Qt::black);
    if (m_scaleMode == ScaleMode::OneToOne) {
        const QPoint topLeft((width() - copy.width()) / 2, (height() - copy.height()) / 2);
        painter.drawImage(topLeft, copy);
        return;
    }

    const QImage scaled = copy.scaled(size(), Qt::KeepAspectRatio, Qt::SmoothTransformation);
    const QPoint topLeft((width() - scaled.width()) / 2, (height() - scaled.height()) / 2);
    painter.drawImage(topLeft, scaled);
}

void VncClientWidget::showDisplayContextMenu(const QPoint& position) {
    QMenu menu(this);
    QAction* fitAction = menu.addAction(tr("Fit to window (keep aspect ratio)"));
    fitAction->setCheckable(true);
    fitAction->setChecked(m_scaleMode == ScaleMode::Fit);
    QAction* oneToOneAction = menu.addAction(tr("1:1 pixel size"));
    oneToOneAction->setCheckable(true);
    oneToOneAction->setChecked(m_scaleMode == ScaleMode::OneToOne);
    menu.addSeparator();
    QAction* fullscreenAction = menu.addAction(window()->isFullScreen() ? tr("Exit fullscreen") : tr("Fullscreen"));

    QAction* selected = menu.exec(mapToGlobal(position));
    if (selected == fitAction) {
        m_scaleMode = ScaleMode::Fit;
        update();
    } else if (selected == oneToOneAction) {
        m_scaleMode = ScaleMode::OneToOne;
        update();
    } else if (selected == fullscreenAction) {
        if (window()->isFullScreen())
            window()->showNormal();
        else
            window()->showFullScreen();
    }
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
    case Qt::Key_Clear:
        return XK_Clear;
    case Qt::Key_Pause:
        return XK_Pause;
    case Qt::Key_Print:
        return XK_Print;
    case Qt::Key_CapsLock:
        return XK_Caps_Lock;
    case Qt::Key_NumLock:
        return XK_Num_Lock;
    case Qt::Key_ScrollLock:
        return XK_Scroll_Lock;
    case Qt::Key_Menu:
        return XK_Menu;
    case Qt::Key_Space:
        return XK_space;
    case Qt::Key_Asterisk:
        return XK_KP_Multiply;
    case Qt::Key_Plus:
        return XK_KP_Add;
    case Qt::Key_Minus:
        return XK_KP_Subtract;
    case Qt::Key_Slash:
        return XK_KP_Divide;
    case Qt::Key_Period:
        return XK_KP_Decimal;
    case Qt::Key_0:
        return (event->modifiers() & Qt::KeypadModifier) ? XK_KP_0 : '0';
    case Qt::Key_1:
        return (event->modifiers() & Qt::KeypadModifier) ? XK_KP_1 : '1';
    case Qt::Key_2:
        return (event->modifiers() & Qt::KeypadModifier) ? XK_KP_2 : '2';
    case Qt::Key_3:
        return (event->modifiers() & Qt::KeypadModifier) ? XK_KP_3 : '3';
    case Qt::Key_4:
        return (event->modifiers() & Qt::KeypadModifier) ? XK_KP_4 : '4';
    case Qt::Key_5:
        return (event->modifiers() & Qt::KeypadModifier) ? XK_KP_5 : '5';
    case Qt::Key_6:
        return (event->modifiers() & Qt::KeypadModifier) ? XK_KP_6 : '6';
    case Qt::Key_7:
        return (event->modifiers() & Qt::KeypadModifier) ? XK_KP_7 : '7';
    case Qt::Key_8:
        return (event->modifiers() & Qt::KeypadModifier) ? XK_KP_8 : '8';
    case Qt::Key_9:
        return (event->modifiers() & Qt::KeypadModifier) ? XK_KP_9 : '9';
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

    if (event->key() >= Qt::Key_F1 && event->key() <= Qt::Key_F24)
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
