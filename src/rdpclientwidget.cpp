#include "rdpclientwidget.h"
#include <freerdp/config.h>
#include <freerdp/client/cmdline.h>
#include <freerdp/client/channels.h>
#include <freerdp/addin.h>
#include <QApplication>
#include <QClipboard>
#include <QMimeData>
#include <QResizeEvent>
#include <QSettings>
#include <mutex>
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
    m_resizeTimer.setSingleShot(true);
    m_resizeTimer.setInterval(250);
    connect(&m_resizeTimer, &QTimer::timeout, this, [this] {
        if (!isVisible())
            return;
        QMutexLocker lock(&m_mutex);
        m_requestedSize = remoteSize(size());
    });
    connect(QApplication::clipboard(), &QClipboard::dataChanged, this, &RdpClientWidget::captureLocalClipboard);
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
    m_initialSize = remoteSize(size());
    m_clipboardEnabled = QSettings().value("rdp/clipboardEnabled", true).toBool();
    m_dynamicResolution = QSettings().value("rdp/dynamicResolution", true).toBool();
    m_clipboardFocused = hasFocus();
    m_cliprdr = nullptr;
    m_disp = nullptr;
    m_clipboardReady = m_displayReady = m_remoteHasText = m_clipboardRequestPending = false;
    m_lastSentSize = {};
    m_remoteFormatsSerial = m_requestedFormatsSerial = 0;
    {
        QMutexLocker lock(&m_mutex);
        m_inputs.clear();
        m_channelEvents.clear();
        m_requestedSize = m_dynamicResolution ? m_initialSize : QSize();
        m_clipboardText.clear();
        m_clipboardDirty = false;
        ++m_clipboardRevision;
        m_frame = QImage();
    }
    m_pressedKeys.clear();
    m_pressedButtons = {};
    captureLocalClipboard();
    m_stop = false;
    m_thread = std::thread([this] { run(); });
}
void RdpClientWidget::stop() {
    ++m_generation;
    m_resizeTimer.stop();
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
    // Continue with credentials already collected in Qt. FALSE would abort the connection.
    return TRUE;
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
    instance->PreConnect = preConnect;
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
                      freerdp_settings_set_bool(settings, FreeRDP_AudioPlayback, FALSE) &&
                      freerdp_settings_set_bool(settings, FreeRDP_AudioCapture, FALSE) &&
                      freerdp_settings_set_bool(settings, FreeRDP_DeviceRedirection, FALSE) &&
                      // Preserve the existing external client's certificate policy.
                      freerdp_settings_set_bool(settings, FreeRDP_IgnoreCertificate, TRUE);
    configured = configured && freerdp_settings_set_bool(settings, FreeRDP_RedirectClipboard, m_clipboardEnabled) &&
                 freerdp_settings_set_bool(settings, FreeRDP_DynamicResolutionUpdate, m_dynamicResolution) &&
                 freerdp_settings_set_bool(settings, FreeRDP_SupportDisplayControl, m_dynamicResolution);
    if (configured && m_dynamicResolution) {
        const char* channel[] = {"disp"};
        configured = freerdp_client_add_dynamic_channel(settings, 1, channel);
    }
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
            processChannels();
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
    PubSub_UnsubscribeChannelConnected(instance->context->pubSub, channelConnected);
    PubSub_UnsubscribeChannelDisconnected(instance->context->pubSub, channelDisconnected);
    m_cliprdr = nullptr;
    m_disp = nullptr;
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
    if (event->type() == QEvent::Show && m_dynamicResolution)
        m_resizeTimer.start();
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
    m_clipboardFocused = false;
    for (auto scan : m_pressedKeys)
        enqueue({true, 0, scan});
    m_pressedKeys.clear();
    for (auto button : {Qt::LeftButton, Qt::RightButton, Qt::MiddleButton})
        if (m_pressedButtons.testFlag(button))
            sendMouse(QPointF(width() / 2, height() / 2), mouseButton(button));
    m_pressedButtons = {};
    QWidget::focusOutEvent(event);
}

namespace {
constexpr int maxClipboardBytes = 2 * 1024 * 1024;
}
QSize RdpClientWidget::remoteSize(QSize size) {
    // MS-RDPEDISP requires an even width in [200,8192].
    return QSize(qBound(200, size.width(), 8192) & ~1, qBound(200, size.height(), 8192));
}
QByteArray RdpClientWidget::encodeClipboardText(QString text) {
    text.replace(QStringLiteral("\r\n"), QStringLiteral("\n"));
    text.replace('\r', '\n');
    text.replace(QStringLiteral("\n"), QStringLiteral("\r\n"));
    QByteArray bytes;
    bytes.reserve((text.size() + 1) * 2);
    for (QChar c : text) {
        bytes.append(char(c.unicode() & 0xff));
        bytes.append(char(c.unicode() >> 8));
    }
    bytes.append('\0');
    bytes.append('\0');
    return bytes;
}
QString RdpClientWidget::decodeClipboardText(const QByteArray& bytes) {
    QString text;
    text.reserve(bytes.size() / 2);
    for (qsizetype i = 0; i + 1 < bytes.size(); i += 2) {
        const auto c = ushort(uchar(bytes[i]) | (ushort(uchar(bytes[i + 1])) << 8));
        if (!c)
            break;
        text.append(QChar(c));
    }
    text.replace(QStringLiteral("\r\n"), QStringLiteral("\n"));
    return text;
}
void RdpClientWidget::captureLocalClipboard() {
    if (!m_clipboardEnabled || m_settingRemoteClipboard || !hasFocus())
        return;
    const auto* mime = QApplication::clipboard()->mimeData();
    QString text = mime && mime->hasText() ? mime->text() : QString();
    if (text.size() > maxClipboardBytes / 4)
        text.clear();
    QMutexLocker lock(&m_mutex);
    m_clipboardText = text;
    m_clipboardDirty = true;
    ++m_clipboardRevision;
}
void RdpClientWidget::focusInEvent(QFocusEvent* event) {
    QWidget::focusInEvent(event);
    m_clipboardFocused = true;
    captureLocalClipboard();
}
void RdpClientWidget::resizeEvent(QResizeEvent* event) {
    QWidget::resizeEvent(event);
    // Coalesce interactive resizing and never resize the remote to a hidden tab.
    if (m_dynamicResolution && isVisible())
        m_resizeTimer.start();
}
BOOL RdpClientWidget::preConnect(freerdp* instance) {
    static std::once_flag addinProvider;
    std::call_once(addinProvider,
                   [] { (void) freerdp_register_addin_provider(freerdp_channels_load_static_addin_entry, 0); });
    auto* context = instance->context;
    PubSub_SubscribeChannelConnected(context->pubSub, channelConnected);
    PubSub_SubscribeChannelDisconnected(context->pubSub, channelDisconnected);
    return freerdp_client_load_addins(context->channels, context->settings);
}
void RdpClientWidget::channelConnected(void* context, const ChannelConnectedEventArgs* event) {
    auto* self = reinterpret_cast<Context*>(context)->widget;
    QMutexLocker channelLock(&self->m_channelMutex);
    if (strcmp(event->name, CLIPRDR_SVC_CHANNEL_NAME) == 0 && self->m_clipboardEnabled) {
        auto* clip = static_cast<CliprdrClientContext*>(event->pInterface);
        if (!clip)
            return;
        self->m_cliprdr = clip;
        clip->custom = self;
        clip->MonitorReady = clipboardReady;
        clip->ServerFormatList = clipboardFormats;
        clip->ServerFormatDataRequest = clipboardRequest;
        clip->ServerFormatDataResponse = clipboardResponse;
    } else if (strcmp(event->name, DISP_DVC_CHANNEL_NAME) == 0 && self->m_dynamicResolution) {
        auto* disp = static_cast<DispClientContext*>(event->pInterface);
        if (!disp)
            return;
        self->m_disp = disp;
        disp->custom = self;
        disp->DisplayControlCaps = displayCaps;
    }
}
void RdpClientWidget::channelDisconnected(void* context, const ChannelDisconnectedEventArgs* event) {
    auto* self = reinterpret_cast<Context*>(context)->widget;
    QMutexLocker channelLock(&self->m_channelMutex);
    if (strcmp(event->name, CLIPRDR_SVC_CHANNEL_NAME) == 0) {
        self->m_cliprdr = nullptr;
        self->m_clipboardReady = false;
        self->m_clipboardRequestPending = false;
        self->m_remoteHasText = false;
    } else if (strcmp(event->name, DISP_DVC_CHANNEL_NAME) == 0) {
        self->m_disp = nullptr;
        self->m_displayReady = false;
        self->m_lastSentSize = {};
    }
}
UINT RdpClientWidget::queueChannelEvent(ChannelEvent event) {
    QMutexLocker lock(&m_mutex);
    if (m_channelEvents.size() >= 64)
        return ERROR_NOT_ENOUGH_MEMORY;
    m_channelEvents.append(std::move(event));
    return CHANNEL_RC_OK;
}
UINT RdpClientWidget::clipboardReady(CliprdrClientContext* context, const CLIPRDR_MONITOR_READY*) {
    return static_cast<RdpClientWidget*>(context->custom)->queueChannelEvent({ChannelEvent::Ready, context});
}
UINT RdpClientWidget::clipboardFormats(CliprdrClientContext* context, const CLIPRDR_FORMAT_LIST* formats) {
    ChannelEvent event{ChannelEvent::Formats, context};
    for (UINT32 i = 0; i < formats->numFormats; ++i) {
        if (formats->formats[i].formatId == CF_UNICODETEXT) {
            event.format = CF_UNICODETEXT;
            break;
        }
    }
    return static_cast<RdpClientWidget*>(context->custom)->queueChannelEvent(std::move(event));
}
UINT RdpClientWidget::clipboardRequest(CliprdrClientContext* context, const CLIPRDR_FORMAT_DATA_REQUEST* request) {
    return static_cast<RdpClientWidget*>(context->custom)
        ->queueChannelEvent({ChannelEvent::Request, context, request->requestedFormatId});
}
UINT RdpClientWidget::clipboardResponse(CliprdrClientContext* context, const CLIPRDR_FORMAT_DATA_RESPONSE* response) {
    ChannelEvent event{ChannelEvent::Response, context};
    event.success = (response->common.msgFlags & CB_RESPONSE_OK) && response->requestedFormatData &&
                    response->common.dataLen <= maxClipboardBytes && !(response->common.dataLen & 1);
    if (event.success)
        event.data = QByteArray(reinterpret_cast<const char*>(response->requestedFormatData), response->common.dataLen);
    return static_cast<RdpClientWidget*>(context->custom)->queueChannelEvent(std::move(event));
}
UINT RdpClientWidget::displayCaps(DispClientContext* context, UINT32 monitors, UINT32 factorA, UINT32 factorB) {
    ChannelEvent event{ChannelEvent::DisplayCaps, context, monitors};
    event.maxArea = quint64(factorA) * factorB;
    return static_cast<RdpClientWidget*>(context->custom)->queueChannelEvent(std::move(event));
}
void RdpClientWidget::requestRemoteClipboard() {
    if (!m_cliprdr || !m_clipboardReady || !m_remoteHasText || m_clipboardRequestPending)
        return;
    CLIPRDR_FORMAT_DATA_REQUEST request{};
    request.requestedFormatId = CF_UNICODETEXT;
    m_requestedFormatsSerial = m_remoteFormatsSerial;
    {
        QMutexLocker lock(&m_mutex);
        m_requestedClipboardRevision = m_clipboardRevision;
    }
    m_clipboardRequestPending = true;
    if (m_cliprdr->ClientFormatDataRequest(m_cliprdr, &request) != CHANNEL_RC_OK)
        m_clipboardRequestPending = false;
}
void RdpClientWidget::processChannels() {
    QMutexLocker channelLock(&m_channelMutex);
    QList<ChannelEvent> events;
    {
        QMutexLocker lock(&m_mutex);
        events.swap(m_channelEvents);
    }
    for (const auto& event : events) {
        if (event.type == ChannelEvent::DisplayCaps) {
            if (event.source == m_disp && event.format > 0) {
                m_displayReady = true;
                m_displayMaxArea = event.maxArea;
                m_lastSentSize = {};
            }
            continue;
        }
        if (!m_cliprdr || event.source != m_cliprdr)
            continue;
        if (event.type == ChannelEvent::Ready) {
            CLIPRDR_GENERAL_CAPABILITY_SET general{};
            general.capabilitySetType = CB_CAPSTYPE_GENERAL;
            general.capabilitySetLength = CB_CAPSTYPE_GENERAL_LEN;
            general.version = CB_CAPS_VERSION_2;
            general.generalFlags = CB_USE_LONG_FORMAT_NAMES;
            CLIPRDR_CAPABILITIES capabilities{};
            capabilities.cCapabilitiesSets = 1;
            capabilities.capabilitySets = reinterpret_cast<CLIPRDR_CAPABILITY_SET*>(&general);
            if (m_cliprdr->ClientCapabilities(m_cliprdr, &capabilities) == CHANNEL_RC_OK) {
                m_clipboardReady = true;
                QMutexLocker lock(&m_mutex);
                m_clipboardDirty = true;
            }
        } else if (event.type == ChannelEvent::Formats) {
            CLIPRDR_FORMAT_LIST_RESPONSE response{};
            response.common.msgFlags = CB_RESPONSE_OK;
            (void) m_cliprdr->ClientFormatListResponse(m_cliprdr, &response);
            ++m_remoteFormatsSerial;
            m_remoteHasText = event.format == CF_UNICODETEXT;
            requestRemoteClipboard();
        } else if (event.type == ChannelEvent::Request) {
            QString text;
            {
                QMutexLocker lock(&m_mutex);
                text = m_clipboardText;
            }
            const bool valid = event.format == CF_UNICODETEXT && m_clipboardFocused;
            const QByteArray bytes = valid ? encodeClipboardText(text) : QByteArray();
            CLIPRDR_FORMAT_DATA_RESPONSE response{};
            response.common.msgFlags = valid ? CB_RESPONSE_OK : CB_RESPONSE_FAIL;
            response.common.dataLen = UINT32(bytes.size());
            response.requestedFormatData = reinterpret_cast<const BYTE*>(bytes.constData());
            (void) m_cliprdr->ClientFormatDataResponse(m_cliprdr, &response);
        } else if (event.type == ChannelEvent::Response && m_clipboardRequestPending) {
            m_clipboardRequestPending = false;
            if (m_requestedFormatsSerial != m_remoteFormatsSerial) {
                requestRemoteClipboard(); // discard data for an obsolete remote selection
                continue;
            }
            if (!event.success)
                continue;
            const QString text = decodeClipboardText(event.data);
            const quint64 revision = m_requestedClipboardRevision;
            const quint64 generation = m_generation;
            QMetaObject::invokeMethod(
                this,
                [this, text, revision, generation] {
                    if (!m_connected || m_generation != generation || !hasFocus())
                        return;
                    {
                        QMutexLocker lock(&m_mutex);
                        if (m_clipboardRevision != revision)
                            return; // a newer local copy takes precedence
                        m_clipboardText = text;
                    }
                    m_settingRemoteClipboard = true;
                    QApplication::clipboard()->setText(text);
                    m_settingRemoteClipboard = false;
                },
                Qt::QueuedConnection);
        }
    }
    if (m_cliprdr && m_clipboardReady) {
        bool dirty;
        QString text;
        {
            QMutexLocker lock(&m_mutex);
            dirty = m_clipboardDirty;
            text = m_clipboardText;
            m_clipboardDirty = false;
        }
        if (dirty) {
            CLIPRDR_FORMAT format{CF_UNICODETEXT, nullptr};
            CLIPRDR_FORMAT_LIST list{};
            list.numFormats = !text.isEmpty() && m_clipboardFocused ? 1 : 0;
            list.formats = &format;
            (void) m_cliprdr->ClientFormatList(m_cliprdr, &list);
        }
    }
    if (m_disp && m_displayReady) {
        QSize size;
        {
            QMutexLocker lock(&m_mutex);
            size = m_requestedSize;
        }
        if (size.isValid() && size != m_lastSentSize && quint64(size.width()) * size.height() <= m_displayMaxArea) {
            DISPLAY_CONTROL_MONITOR_LAYOUT layout{};
            layout.Flags = DISPLAY_CONTROL_MONITOR_PRIMARY;
            layout.Width = size.width();
            layout.Height = size.height();
            layout.PhysicalWidth = qMax(10, qRound(size.width() * 25.4 / 96));
            layout.PhysicalHeight = qMax(10, qRound(size.height() * 25.4 / 96));
            layout.DesktopScaleFactor = 100;
            layout.DeviceScaleFactor = 100;
            if (m_disp->SendMonitorLayout(m_disp, 1, &layout) == CHANNEL_RC_OK)
                m_lastSentSize = size;
            else
                m_displayReady = false; // retain the scaled desktop on channel failure
        }
    }
}
