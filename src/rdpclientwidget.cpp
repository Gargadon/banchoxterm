#include "rdpclientwidget.h"
#include <QPainter>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QWheelEvent>
#include <QFocusEvent>
#include <QMutexLocker>
#include <freerdp/gdi/gdi.h>
#include <freerdp/input.h>
#include <freerdp/settings.h>
#include <freerdp/update.h>
#include <winpr/input.h>
#include <winpr/synch.h>

RdpClientWidget::RdpClientWidget(QWidget* parent) : QWidget(parent) {
    setFocusPolicy(Qt::StrongFocus);
    setMouseTracking(true);
    setAttribute(Qt::WA_OpaquePaintEvent);
}
RdpClientWidget::~RdpClientWidget() {
    stop();
}
void RdpClientWidget::start(const QString& host, int port, const QString& user, const QString& domain,
                            const QString& password) {
    stop();
    m_host = host;
    m_port = port;
    m_user = user;
    m_domain = domain;
    m_password = password;
    m_initialSize = QSize(qBound(200, width(), 8192), qBound(200, height(), 8192));
    {
        QMutexLocker lock(&m_mutex);
        m_inputs.clear();
        m_frame = QImage();
    }
    m_pressedKeys.clear();
    m_pressedButtons = {};
    m_stop = false;
    m_thread = std::thread([this] { run(); });
}
void RdpClientWidget::stop() {
    m_stop = true;
    {
        QMutexLocker lock(&m_instanceMutex);
        if (m_instance && m_instance->context)
            (void) freerdp_abort_connect_context(m_instance->context);
    }
    if (m_thread.joinable())
        m_thread.join();
    m_connected = false;
}
BOOL RdpClientWidget::authenticate(freerdp*, char**, char**, char**, rdp_auth_reason) {
    // Credentials are collected in Qt before connecting; never prompt on stdin.
    return FALSE;
}
BOOL RdpClientWidget::postConnect(freerdp* instance) {
    if (!gdi_init(instance, PIXEL_FORMAT_BGRX32))
        return FALSE;
    instance->context->update->BeginPaint = beginPaint;
    instance->context->update->EndPaint = endPaint;
    instance->context->update->DesktopResize = desktopResize;
    return TRUE;
}
BOOL RdpClientWidget::beginPaint(rdpContext* context) {
    context->gdi->primary->hdc->hwnd->invalid->null = TRUE;
    return TRUE;
}
BOOL RdpClientWidget::endPaint(rdpContext* context) {
    auto* self = reinterpret_cast<Context*>(context)->widget;
    auto* gdi = context->gdi;
    if (!gdi || !gdi->primary_buffer)
        return FALSE;
    if (gdi->primary->hdc->hwnd->invalid->null)
        return TRUE;
    {
        QMutexLocker lock(&self->m_mutex);
        self->m_frame = QImage(gdi->primary_buffer, gdi->width, gdi->height, gdi->stride, QImage::Format_RGB32).copy();
    }
    if (!self->m_updatePending.exchange(true))
        QMetaObject::invokeMethod(
            self,
            [self] {
                self->m_updatePending = false;
                self->update();
            },
            Qt::QueuedConnection);
    return TRUE;
}
BOOL RdpClientWidget::desktopResize(rdpContext* context) {
    return gdi_resize(context->gdi, freerdp_settings_get_uint32(context->settings, FreeRDP_DesktopWidth),
                      freerdp_settings_get_uint32(context->settings, FreeRDP_DesktopHeight));
}
void RdpClientWidget::run() {
    freerdp* instance = freerdp_new();
    if (!instance) {
        emit errorOccurred(tr("Failed to create RDP client."));
        return;
    }
    instance->ContextSize = sizeof(Context);
    instance->PostConnect = postConnect;
    instance->AuthenticateEx = authenticate;
    if (!freerdp_context_new(instance)) {
        freerdp_free(instance);
        emit errorOccurred(tr("Failed to create RDP context."));
        return;
    }
    reinterpret_cast<Context*>(instance->context)->widget = this;
    auto* settings = instance->context->settings;
    bool configured = freerdp_settings_set_string(settings, FreeRDP_ServerHostname, m_host.toUtf8().constData()) &&
                      freerdp_settings_set_uint32(settings, FreeRDP_ServerPort, m_port) &&
                      freerdp_settings_set_string(settings, FreeRDP_Username, m_user.toUtf8().constData()) &&
                      freerdp_settings_set_string(settings, FreeRDP_Domain, m_domain.toUtf8().constData()) &&
                      freerdp_settings_set_string(settings, FreeRDP_Password, m_password.toUtf8().constData()) &&
                      freerdp_settings_set_uint32(settings, FreeRDP_DesktopWidth, m_initialSize.width()) &&
                      freerdp_settings_set_uint32(settings, FreeRDP_DesktopHeight, m_initialSize.height()) &&
                      freerdp_settings_set_uint32(settings, FreeRDP_ColorDepth, 32) &&
                      freerdp_settings_set_uint32(settings, FreeRDP_TcpConnectTimeout, 10000) &&
                      freerdp_settings_set_bool(settings, FreeRDP_SoftwareGdi, TRUE) &&
                      freerdp_settings_set_bool(settings, FreeRDP_SupportGraphicsPipeline, FALSE) &&
                      // Preserve the existing external client's certificate policy.
                      freerdp_settings_set_bool(settings, FreeRDP_IgnoreCertificate, TRUE);
    m_password.clear();
    {
        QMutexLocker lock(&m_instanceMutex);
        m_instance = instance;
    }
    const bool connectedOk = configured && !m_stop && freerdp_connect(instance);
    if (connectedOk) {
        m_connected = true;
        emit connected();
        while (!m_stop && !freerdp_shall_disconnect_context(instance->context)) {
            HANDLE handles[MAXIMUM_WAIT_OBJECTS];
            const DWORD count = freerdp_get_event_handles(instance->context, handles, MAXIMUM_WAIT_OBJECTS);
            if (!count || WaitForMultipleObjects(count, handles, FALSE, 20) == WAIT_FAILED)
                break;
            if (!freerdp_check_event_handles(instance->context))
                break;
            QList<Input> batch;
            {
                QMutexLocker lock(&m_mutex);
                batch.swap(m_inputs);
            }
            for (const auto& input : batch) {
                const BOOL ok =
                    input.keyboard
                        ? freerdp_input_send_keyboard_event_ex(instance->context->input, input.flags != 0, FALSE,
                                                               input.code)
                        : freerdp_input_send_mouse_event(instance->context->input, input.flags, input.x, input.y);
                if (!ok) {
                    m_stop = true;
                    break;
                }
            }
        }
    }
    const UINT32 error = freerdp_get_last_error(instance->context);
    m_connected = false;
    (void) freerdp_disconnect(instance);
    if (instance->context->gdi)
        gdi_free(instance);
    {
        QMutexLocker lock(&m_instanceMutex);
        m_instance = nullptr;
        freerdp_context_free(instance);
        freerdp_free(instance);
    }
    if (!m_stop && (!connectedOk || error != 0))
        emit errorOccurred(
            tr("RDP connection failed: %1").arg(QString::fromUtf8(freerdp_get_last_error_string(error))));
    else if (!m_stop)
        emit disconnected();
}
QRect RdpClientWidget::imageRect() const {
    const QSize size = m_frame.size().scaled(this->size(), Qt::KeepAspectRatio);
    return QRect(QPoint((width() - size.width()) / 2, (height() - size.height()) / 2), size);
}
void RdpClientWidget::paintEvent(QPaintEvent*) {
    QPainter painter(this);
    painter.fillRect(rect(), Qt::black);
    QMutexLocker lock(&m_mutex);
    if (!m_frame.isNull())
        painter.drawImage(imageRect(), m_frame);
}
void RdpClientWidget::enqueue(const Input& input) {
    if (!m_connected)
        return;
    QMutexLocker lock(&m_mutex);
    m_inputs.append(input);
}
void RdpClientWidget::sendKey(QKeyEvent* event, bool down) {
    if (event->isAutoRepeat() && !down)
        return;
    // Qt reports XKB keycodes on both xcb and Wayland; map physical keys
    // rather than assuming a US layout or sending characters for shortcuts.
    DWORD vk = GetVirtualKeyCodeFromKeycode(event->nativeScanCode(), WINPR_KEYCODE_TYPE_XKB);
    DWORD scan = GetVirtualScanCodeFromVirtualKeyCode(vk, 4);
    if (scan) {
        if (down)
            m_pressedKeys.insert(scan);
        else
            m_pressedKeys.remove(scan);
        enqueue({true, static_cast<UINT16>(down), scan});
    }
    event->accept();
}
void RdpClientWidget::keyPressEvent(QKeyEvent* event) {
    sendKey(event, true);
}
void RdpClientWidget::keyReleaseEvent(QKeyEvent* event) {
    sendKey(event, false);
}
bool RdpClientWidget::event(QEvent* event) {
    if (event->type() == QEvent::ShortcutOverride && m_connected) {
        event->accept();
        return true;
    }
    // QWidget normally consumes Tab for focus traversal.
    if (event->type() == QEvent::KeyPress) {
        keyPressEvent(static_cast<QKeyEvent*>(event));
        return true;
    }
    if (event->type() == QEvent::KeyRelease) {
        keyReleaseEvent(static_cast<QKeyEvent*>(event));
        return true;
    }
    return QWidget::event(event);
}
void RdpClientWidget::sendMouse(const QPointF& position, UINT16 flags) {
    Input input{false, flags, 0};
    {
        QMutexLocker lock(&m_mutex);
        if (m_frame.isNull())
            return;
        const QRect target = imageRect();
        if (target.isEmpty())
            return;
        input.x = qBound(0, int((position.x() - target.x()) * m_frame.width() / target.width()), m_frame.width() - 1);
        input.y =
            qBound(0, int((position.y() - target.y()) * m_frame.height() / target.height()), m_frame.height() - 1);
    }
    enqueue(input);
}
static UINT16 mouseButton(Qt::MouseButton button) {
    if (button == Qt::LeftButton)
        return PTR_FLAGS_BUTTON1;
    if (button == Qt::RightButton)
        return PTR_FLAGS_BUTTON2;
    if (button == Qt::MiddleButton)
        return PTR_FLAGS_BUTTON3;
    return 0;
}
void RdpClientWidget::mousePressEvent(QMouseEvent* event) {
    setFocus();
    m_pressedButtons |= event->button();
    if (auto flag = mouseButton(event->button()))
        sendMouse(event->position(), flag | PTR_FLAGS_DOWN);
}
void RdpClientWidget::mouseReleaseEvent(QMouseEvent* event) {
    m_pressedButtons &= ~event->button();
    if (auto flag = mouseButton(event->button()))
        sendMouse(event->position(), flag);
}
void RdpClientWidget::mouseMoveEvent(QMouseEvent* event) {
    sendMouse(event->position(), PTR_FLAGS_MOVE);
}
void RdpClientWidget::wheelEvent(QWheelEvent* event) {
    const int delta = qBound(-255, event->angleDelta().y(), 255);
    if (delta)
        sendMouse(event->position(), PTR_FLAGS_WHEEL | (delta & 0x1ff));
    event->accept();
}
void RdpClientWidget::focusOutEvent(QFocusEvent* event) {
    for (auto scan : m_pressedKeys)
        enqueue({true, 0, scan});
    m_pressedKeys.clear();
    for (auto button : {Qt::LeftButton, Qt::RightButton, Qt::MiddleButton})
        if (m_pressedButtons.testFlag(button))
            sendMouse(QPointF(width() / 2, height() / 2), mouseButton(button));
    m_pressedButtons = {};
    QWidget::focusOutEvent(event);
}
