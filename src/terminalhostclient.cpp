#include "terminalhostclient.h"
#include "termipc.h"
#include <QWidget>
#include <QLocalServer>
#include <QLocalSocket>
#include <QProcess>
#include <QCoreApplication>
#include <QFont>
#include <QPoint>
#include <QResizeEvent>
#include <QFile>
#include <QDir>
#include <QUuid>
#include <QDebug>
#include <windows.h>

TerminalHostClient::TerminalHostClient(QWidget* parent)
    : QObject(parent)
{
    m_container = new QWidget();
    m_container->installEventFilter(this);
    startHost();
}

TerminalHostClient::~TerminalHostClient()
{
    cleanup();
}

void TerminalHostClient::startHost()
{
    m_pipeName = "banchoxterm-term-" + QUuid::createUuid().toString(QUuid::WithoutBraces);
    m_server = new QLocalServer(this);
    if (!m_server->listen(m_pipeName)) {
        qWarning() << "TerminalHostClient: failed to create pipe" << m_pipeName;
        m_server->deleteLater();
        m_server = nullptr;
        return;
    }
    connect(m_server, &QLocalServer::newConnection, this, &TerminalHostClient::onNewConnection);

    QString exePath = QCoreApplication::applicationDirPath() + "/banchoxterm-term.exe";
    if (!QFile::exists(exePath))
        exePath = QCoreApplication::applicationDirPath() + "/Debug/banchoxterm-term.exe";
    if (!QFile::exists(exePath))
        exePath = QCoreApplication::applicationDirPath() + "/Release/banchoxterm-term.exe";
    if (!QFile::exists(exePath)) {
        qWarning() << "TerminalHostClient: banchoxterm-term.exe not found next to the application";
        return;
    }

    m_process = new QProcess(this);
    connect(m_process, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished), this,
            [this](int exitCode, QProcess::ExitStatus) {
                Q_UNUSED(exitCode);
                if (!m_closing)
                    emit finished();
            });
    m_process->start(exePath, {"--pipe=" + m_pipeName});
}

void TerminalHostClient::onNewConnection()
{
    m_socket = m_server->nextPendingConnection();
    if (!m_socket)
        return;
    m_socket->setParent(this);
    connect(m_socket, &QLocalSocket::readyRead, this, &TerminalHostClient::onReadyRead);
    connect(m_socket, &QLocalSocket::disconnected, this, &TerminalHostClient::onDisconnected);
    m_server->close();

    for (const auto& msg : m_pending)
        termipc::send(m_socket, TermMsg(msg.first), msg.second);
    m_pending.clear();
}

void TerminalHostClient::onReadyRead()
{
    m_rxBuffer.append(m_socket->readAll());
    m_rxBuffer = termipc::parse(m_rxBuffer,
                                [this](TermMsg type, const QByteArray& payload) {
                                    handleMessage(quint32(type), payload);
                                });
}

void TerminalHostClient::onDisconnected()
{
    if (!m_closing)
        emit finished();
}

void TerminalHostClient::handleMessage(quint32 type, const QByteArray& payload)
{
    switch (TermMsg(type)) {
    case TermMsg_Ready: {
        quint64 hwnd = 0;
        quint32 cols = 0, rows = 0;
        termipc::readReady(payload, hwnd, cols, rows);
        embedWindow(hwnd);
        m_cols = int(cols);
        m_rows = int(rows);
        emit ready();
        break;
    }
    case TermMsg_Input:
        emit inputReceived(termipc::readBytes(payload));
        break;
    case TermMsg_SizeChanged: {
        quint32 cols = 0, rows = 0;
        termipc::readSize(payload, cols, rows);
        m_cols = int(cols);
        m_rows = int(rows);
        emit sizeChanged(int(rows), int(cols));
        break;
    }
    case TermMsg_TitleChanged:
        emit titleChanged(termipc::readStr(payload));
        break;
    case TermMsg_CwdChanged:
        emit cwdChanged(termipc::readStr(payload));
        break;
    case TermMsg_Finished:
        emit finished();
        break;
    case TermMsg_CopyAvailable:
        m_hasSelection = termipc::readBool(payload);
        emit copyAvailable(m_hasSelection);
        break;
    case TermMsg_ContextMenu: {
        qint32 x = 0, y = 0;
        termipc::readPoint(payload, x, y);
        emit contextMenuRequested(QPoint(x, y));
        break;
    }
    case TermMsg_SelectionText:
        emit selectionText(termipc::readStr(payload));
        break;
    case TermMsg_CloseRequested:
        emit closeRequested();
        break;
    default:
        break;
    }
}

void TerminalHostClient::embedWindow(quint64 hwnd)
{
    if (!hwnd || !m_container)
        return;
    m_hwnd = reinterpret_cast<void*>(hwnd);
    HWND host = reinterpret_cast<HWND>(m_hwnd);
    HWND target = reinterpret_cast<HWND>(m_container->winId());

    ::SetParent(host, target);
    repositionChild();
    ::ShowWindow(host, SW_SHOW);
    termipc::send(m_socket, TermMsg_Show, QByteArray());
    ::SetFocus(host);
}

void TerminalHostClient::repositionChild()
{
    if (!m_hwnd || !m_container)
        return;
    const qreal dpr = m_container->devicePixelRatioF();
    const int w = qRound(m_container->width() * dpr);
    const int h = qRound(m_container->height() * dpr);
    if (w < 1 || h < 1)
        return;
    ::MoveWindow(reinterpret_cast<HWND>(m_hwnd), 0, 0, w, h, TRUE);
}

bool TerminalHostClient::eventFilter(QObject* obj, QEvent* event)
{
    if (obj == m_container && event->type() == QEvent::Resize)
        repositionChild();
    return QObject::eventFilter(obj, event);
}

void TerminalHostClient::cleanup()
{
    m_closing = true;
    if (m_socket) {
        termipc::send(m_socket, TermMsg_Close, QByteArray());
        m_socket->disconnectFromServer();
    }
    if (m_process) {
        m_process->waitForFinished(500);
        if (m_process->state() != QProcess::NotRunning)
            m_process->kill();
    }
}

void TerminalHostClient::sendOrQueue(quint32 type, const QByteArray& payload)
{
    if (m_socket && m_socket->state() == QLocalSocket::ConnectedState) {
        termipc::send(m_socket, TermMsg(type), payload);
    } else {
        m_pending.append(qMakePair(type, payload));
    }
}

void TerminalHostClient::setHistorySize(int lines)
{
    sendOrQueue(quint32(TermMsg_SetHistorySize), termipc::u32Msg(quint32(qMax(0, lines))));
}

void TerminalHostClient::setFont(const QFont& font)
{
    sendOrQueue(quint32(TermMsg_SetFont), termipc::str(font.toString()));
}

void TerminalHostClient::setColorScheme(const QString& name)
{
    sendOrQueue(quint32(TermMsg_SetColorScheme), termipc::str(name));
}

void TerminalHostClient::feedData(const QByteArray& data)
{
    if (data.isEmpty())
        return;
    sendOrQueue(quint32(TermMsg_FeedData), termipc::bytes(data));
}

void TerminalHostClient::sendText(const QString& text)
{
    sendOrQueue(quint32(TermMsg_SendText), termipc::str(text));
}

void TerminalHostClient::copy()
{
    sendOrQueue(quint32(TermMsg_Copy));
}

void TerminalHostClient::paste()
{
    sendOrQueue(quint32(TermMsg_Paste));
}

void TerminalHostClient::clear()
{
    sendOrQueue(quint32(TermMsg_Clear));
}

void TerminalHostClient::zoomIn()
{
    sendOrQueue(quint32(TermMsg_ZoomIn));
}

void TerminalHostClient::zoomOut()
{
    sendOrQueue(quint32(TermMsg_ZoomOut));
}

void TerminalHostClient::toggleSearchBar()
{
    sendOrQueue(quint32(TermMsg_ToggleSearchBar));
}

void TerminalHostClient::requestFocus()
{
    if (m_hwnd)
        ::SetFocus(reinterpret_cast<HWND>(m_hwnd));
    sendOrQueue(quint32(TermMsg_SetFocus));
}

void TerminalHostClient::requestSelection()
{
    sendOrQueue(quint32(TermMsg_SelectionRequest));
}