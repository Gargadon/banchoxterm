// banchoxterm-term: the GPL terminal host process.
//
// This process is the only component that links QTermWidget (GPLv2+). It owns
// the terminal emulator, renders it into a native window, and hands that window
// to the MIT application (banchoxterm.exe) for embedding. All terminal bytes
// and commands are exchanged over a local pipe using the protocol in
// termipc.h. Keeping QTermWidget isolated in its own process is what allows the
// rest of BanchoXterm to remain MIT-licensed.
//
// SPDX-License-Identifier: GPL-2.0-or-later

#include "termipc.h"
#include <qtermwidget.h>
#include <QApplication>
#include <QLocalSocket>
#include <QVBoxLayout>
#include <QResizeEvent>
#include <QCloseEvent>
#include <QFont>
#include <QTimer>
#include <QCoreApplication>

namespace {

class TermHostWindow : public QWidget {
    Q_OBJECT
public:
    explicit TermHostWindow(QLocalSocket* socket)
        : m_socket(socket)
    {
        setWindowFlags(Qt::Window | Qt::FramelessWindowHint);
        setAttribute(Qt::WA_NativeWindow, true);

        auto* layout = new QVBoxLayout(this);
        layout->setContentsMargins(0, 0, 0, 0);

        m_term = new QTermWidget(this);
        layout->addWidget(m_term);

        m_term->setHistorySize(5000);
        m_term->setScrollBarPosition(QTermWidget::ScrollBarRight);
        m_term->setContextMenuPolicy(Qt::CustomContextMenu);

        connect(m_term, &QWidget::customContextMenuRequested, this,
                [this](const QPoint& pos) {
                    termipc::send(m_socket, TermMsg_ContextMenu, termipc::pointMsg(pos.x(), pos.y()));
                });
        connect(m_term, &QTermWidget::sendData, this,
                [this](const char* data, int size) {
                    termipc::send(m_socket, TermMsg_Input, termipc::bytes(QByteArray(data, size)));
                });
        connect(m_term, &QTermWidget::titleChanged, this, [this]() {
            termipc::send(m_socket, TermMsg_TitleChanged, termipc::str(m_term->title()));
        });
        connect(m_term, &QTermWidget::currentDirectoryChanged, this, [this](const QString& dir) {
            termipc::send(m_socket, TermMsg_CwdChanged, termipc::str(dir));
        });
        connect(m_term, &QTermWidget::copyAvailable, this, [this](bool ok) {
            termipc::send(m_socket, TermMsg_CopyAvailable, termipc::boolMsg(ok));
        });
        connect(m_term, &QTermWidget::finished, this, [this]() {
            termipc::send(m_socket, TermMsg_Finished, QByteArray());
        });

        // External I/O mode: no PTY inside this process. Keystrokes are routed
        // out via TermMsg_Input; the app feeds remote/ConPTY output in.
        m_term->startExternal();

        // The socket is owned by main() (a stack variable), NOT by this window.
        // Taking ownership here would double-free it at shutdown: the window
        // (and its children) are destroyed first, then the stack socket, and the
        // QLocalSocket destructor would also emit disconnected() while this
        // window is mid-destruction. Plain connects auto-disconnect once either
        // endpoint is destroyed, which is exactly what we want here.
        connect(m_socket, &QLocalSocket::readyRead, this, &TermHostWindow::onReadyRead);
        connect(m_socket, &QLocalSocket::disconnected, this, &TermHostWindow::onDisconnected);

        // Create the (hidden) native window and announce it to the app. The app
        // will SetParent()/MoveWindow()/ShowWindow() it and then send
        // TermMsg_Show so Qt treats it as visible and starts rendering.
        const quint64 hwnd = quint64(winId());
        termipc::send(m_socket, TermMsg_Ready,
                      termipc::readyMsg(hwnd, quint32(m_term->screenColumnsCount()),
                                        quint32(m_term->screenLinesCount())));
    }

protected:
    void resizeEvent(QResizeEvent* event) override
    {
        QWidget::resizeEvent(event);
        QTimer::singleShot(0, this, [this]() { reportSize(); });
    }

    void closeEvent(QCloseEvent* event) override
    {
        // Alt+F4 / WM_CLOSE on the embedded window must never kill this
        // process (Qt would quit once its last window closes, dropping the
        // session with no confirmation). Forward the request to the app, which
        // shows the disconnect confirmation. This window is only torn down via
        // TermMsg_Close (QCoreApplication::quit) or the pipe dying.
        termipc::send(m_socket, TermMsg_CloseRequested, QByteArray());
        event->ignore();
    }

private slots:
    void onReadyRead()
    {
        m_rxBuffer.append(m_socket->readAll());
        m_rxBuffer = termipc::parse(m_rxBuffer,
                                    [this](TermMsg type, const QByteArray& payload) {
                                        handleMessage(type, payload);
                                    });
    }

    void onDisconnected()
    {
        QCoreApplication::quit();
    }

private:
    void reportSize()
    {
        termipc::send(m_socket, TermMsg_SizeChanged,
                      termipc::sizeMsg(quint32(m_term->screenColumnsCount()),
                                       quint32(m_term->screenLinesCount())));
    }

    void handleMessage(TermMsg type, const QByteArray& payload)
    {
        switch (type) {
        case TermMsg_FeedData:
            m_term->feedData(termipc::readBytes(payload));
            break;
        case TermMsg_SetFont: {
            QFont font;
            font.fromString(termipc::readStr(payload));
            m_term->setTerminalFont(font);
            QTimer::singleShot(0, this, [this]() { reportSize(); });
            break;
        }
        case TermMsg_SetColorScheme:
            m_term->setColorScheme(termipc::readStr(payload));
            break;
        case TermMsg_SetHistorySize:
            m_term->setHistorySize(qMax(0, int(termipc::readU32(payload))));
            break;
        case TermMsg_Copy:
            m_term->copyClipboard();
            break;
        case TermMsg_Paste:
            m_term->pasteClipboard();
            break;
        case TermMsg_Clear:
            m_term->clear();
            break;
        case TermMsg_ZoomIn:
            m_term->zoomIn();
            QTimer::singleShot(0, this, [this]() { reportSize(); });
            break;
        case TermMsg_ZoomOut:
            m_term->zoomOut();
            QTimer::singleShot(0, this, [this]() { reportSize(); });
            break;
        case TermMsg_ToggleSearchBar:
            m_term->toggleShowSearchBar();
            break;
        case TermMsg_SendText:
            m_term->sendText(termipc::readStr(payload));
            break;
        case TermMsg_SetFocus:
            m_term->setFocus();
            break;
        case TermMsg_SelectionRequest:
            termipc::send(m_socket, TermMsg_SelectionText, termipc::str(m_term->selectedText()));
            break;
        case TermMsg_Show:
            show();
            break;
        case TermMsg_Close:
            QCoreApplication::quit();
            break;
        default:
            break;
        }
    }

    QTermWidget* m_term = nullptr;
    QLocalSocket* m_socket = nullptr;
    QByteArray m_rxBuffer;
};

} // namespace

int main(int argc, char** argv)
{
    QApplication app(argc, argv);

    QString pipeName;
    for (int i = 1; i < argc; ++i) {
        if (qstrncmp(argv[i], "--pipe=", 7) == 0)
            pipeName = QString::fromLocal8Bit(argv[i] + 7);
    }
    if (pipeName.isEmpty())
        return 2;

    QLocalSocket socket;
    socket.connectToServer(pipeName);
    if (!socket.waitForConnected(10000))
        return 3;

    TermHostWindow window(&socket);
    window.resize(800, 600);

    return app.exec();
}

#include "termhost.moc"