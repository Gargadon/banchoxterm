#include "terminaltab.h"
#include "xservermanager.h"
#include "sshconnection.h"
#include "keyring.h"
#include <qtermwidget.h>
#include <QVBoxLayout>
#include <QLabel>
#include <QFont>
#include <QDebug>
#include <QProcess>
#include <QCoreApplication>
#include <QSettings>
#include <QTimer>
#include <QApplication>
#include <QMenu>
#include <QAction>
#include <QThread>
#include <QResizeEvent>
#include <QFrame>
#include <QLineEdit>
#include <QPushButton>
#include <QCheckBox>
#include <QKeyEvent>
#include <QShortcut>
#include <QFile>
#include <QFileDialog>
#include <QInputDialog>
#include <QDateTime>
#include <QDir>
#include <QStandardPaths>
#include <QRegularExpression>
#include <QPair>
#include <QSerialPort>
#include <QListWidget>
#include <QDialogButtonBox>

#ifdef Q_OS_WIN
#include "conpty.h"
#endif

#ifdef BANCHO_HAVE_RDP_AX
#include <QAxWidget>
#endif

#ifdef BANCHO_HAVE_VNC
#include "vncclientwidget.h"
#endif

TerminalTab::TerminalTab(const Session& session, QWidget* parent) : QWidget(parent), m_session(session) {
    if (!session.promptPattern.isEmpty())
        m_promptPattern = QRegularExpression(session.promptPattern);
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);

    if (session.type == SessionType::RDP || session.type == SessionType::VNC) {
        m_statusLabel = new QLabel(this);
        m_statusLabel->setAlignment(Qt::AlignCenter);
        m_statusLabel->setWordWrap(true);

        QString proto = session.type == SessionType::RDP ? "RDP" : "VNC";
        m_statusLabel->setText(tr("Connecting %1 session to %2...").arg(proto, session.host));

        QFont statusFont = m_statusLabel->font();
        statusFont.setPointSize(12);
        m_statusLabel->setFont(statusFont);

        layout->addWidget(m_statusLabel);

        m_reconnectButton = new QPushButton(tr("Reconnect"), this);
        m_reconnectButton->setVisible(false);
        layout->addWidget(m_reconnectButton, 0, Qt::AlignHCenter);
        connect(m_reconnectButton, &QPushButton::clicked, this, &TerminalTab::requestReconnect);

        m_embeddedContainer = new QWidget(this);
        m_embeddedContainer->hide();
        layout->addWidget(m_embeddedContainer);

#ifdef BANCHO_HAVE_RDP_AX
        if (session.type == SessionType::RDP) {
            QTimer::singleShot(100, this, &TerminalTab::setupWindowsRdpActiveX);
        } else
#endif
#ifdef BANCHO_HAVE_VNC
            if (session.type == SessionType::VNC) {
            QTimer::singleShot(100, this, &TerminalTab::setupEmbeddedVnc);
        } else
#endif
        {
            QTimer::singleShot(100, this, &TerminalTab::launchExternalClient);
        }
    } else {
        m_terminal = new QTermWidget(0, this);

        QTimer::singleShot(0, this, &TerminalTab::updateFontFromSettings);

        QStringList schemes = QTermWidget::availableColorSchemes();
        if (schemes.contains("DarkPastels")) {
            m_terminal->setColorScheme("DarkPastels");
        } else if (schemes.contains("Tango")) {
            m_terminal->setColorScheme("Tango");
        } else if (!schemes.isEmpty()) {
            m_terminal->setColorScheme(schemes.first());
        }

        m_terminal->setHistorySize(m_session.scrollback > 0 ? m_session.scrollback : 5000);
        m_terminal->setScrollBarPosition(QTermWidget::ScrollBarRight);
        QStringList environment;
        if (!m_session.terminalType.isEmpty())
            environment << QStringLiteral("TERM=") + m_session.terminalType;
        if (!m_session.language.isEmpty())
            environment << QStringLiteral("LANG=") + m_session.language;
        if (!environment.isEmpty())
            m_terminal->setEnvironment(environment);
        m_terminal->setCodec(m_session.encoding);
        if (m_session.initialRows > 0 && m_session.initialColumns > 0)
            m_terminal->setSize(QSize(m_session.initialColumns, m_session.initialRows));

        m_terminal->setContextMenuPolicy(Qt::CustomContextMenu);
        // QTermWidget forwards keyboard input through an internal child
        // widget, so filtering only m_terminal would miss the focused view.
        // Install at application level and limit handling to this terminal's
        // widget hierarchy.
        qApp->installEventFilter(this);
        connect(m_terminal, &QWidget::customContextMenuRequested, this, &TerminalTab::showTerminalContextMenu);
        connect(m_terminal, &QTermWidget::currentDirectoryChanged, this, &TerminalTab::onRemoteDirChanged);
        connect(m_terminal, &QTermWidget::sendData, this, &TerminalTab::onSendData);

        layout->addWidget(m_terminal);

        connect(m_terminal, &QTermWidget::finished, this, &TerminalTab::onTerminalFinished);
        connect(m_terminal, &QTermWidget::titleChanged, this, &TerminalTab::onTitleChanged);

        if (m_session.type == SessionType::SSH) {
            setupSshTerminal();
        } else if (m_session.type == SessionType::Telnet) {
#ifdef Q_OS_WIN
            // Windows has no native PTY for telnet; run it under ConPTY.
            setupWindowsConPty("telnet", {m_session.host, QString::number(m_session.port)});
#else
            m_terminal->setShellProgram("/usr/bin/telnet");
            QStringList args;
            args << m_session.host << QString::number(m_session.port);
            m_terminal->setArgs(args);
            m_terminal->startShellProgram();
#endif
        } else if (m_session.type == SessionType::Serial) {
            m_terminal->startExternal();
            m_serialPort = new QSerialPort(this);
            m_serialPort->setPortName(m_session.serialPort);
            m_serialPort->setBaudRate(m_session.baudRate);
            m_serialPort->setDataBits(static_cast<QSerialPort::DataBits>(m_session.serialDataBits));
            m_serialPort->setParity(static_cast<QSerialPort::Parity>(m_session.serialParity));
            m_serialPort->setStopBits(static_cast<QSerialPort::StopBits>(m_session.serialStopBits));
            m_serialPort->setFlowControl(static_cast<QSerialPort::FlowControl>(m_session.serialFlowControl));
            connect(m_serialPort, &QSerialPort::readyRead, this, [this]() {
                const QByteArray data = m_serialPort->readAll();
                if (!data.isEmpty()) {
                    if (m_zmodemProcess) {
                        m_zmodemProcess->write(data);
                    } else if (m_xmodemFile) {
                        handleXmodemInput(data);
                    } else {
                        feedTerminalData(data);
                    }
                    logData(data);
                }
            });
            connect(m_serialPort, &QSerialPort::errorOccurred, this, [this](QSerialPort::SerialPortError error) {
                if (error == QSerialPort::NoError)
                    return;
                feedTerminalData(tr("\r\n[Serial error: %1]\r\n").arg(m_serialPort->errorString()).toUtf8());
                m_isActive = false;
                emit titleChanged(tr("[Closed] %1").arg(m_session.name));
                showStoppedPrompt();
            });
            if (!m_serialPort->open(QIODevice::ReadWrite)) {
                feedTerminalData(
                    tr("\r\n[Unable to open serial port: %1]\r\n").arg(m_serialPort->errorString()).toUtf8());
                m_isActive = false;
                emit titleChanged(tr("[Closed] %1").arg(m_session.name));
                showStoppedPrompt();
            } else {
                if (!m_serialPort->setDataTerminalReady(m_session.serialDtr))
                    feedTerminalData(tr("\r\n[Unable to set DTR: %1]\r\n").arg(m_serialPort->errorString()).toUtf8());
                if (!m_serialPort->setRequestToSend(m_session.serialRts))
                    feedTerminalData(tr("\r\n[Unable to set RTS: %1]\r\n").arg(m_serialPort->errorString()).toUtf8());
            }
        } else {
            setupLocalTerminal();
        }
    }

    // Construir la barra de búsqueda (Ctrl+F)
    m_searchFrame = new QFrame(this);
    m_searchFrame->setFrameShape(QFrame::StyledPanel);
    m_searchFrame->hide();

    auto* searchLayout = new QHBoxLayout(m_searchFrame);
    searchLayout->setContentsMargins(10, 4, 10, 4);
    searchLayout->setSpacing(8);

    auto* searchLabel = new QLabel(tr("Search:"), m_searchFrame);

    m_searchEdit = new QLineEdit(m_searchFrame);
    m_searchEdit->setPlaceholderText(tr("Find text..."));

    m_btnPrev = new QPushButton(tr("Previous"), m_searchFrame);

    m_btnNext = new QPushButton(tr("Next"), m_searchFrame);

    m_caseSensitiveCheck = new QCheckBox(tr("Case Sensitive"), m_searchFrame);

    auto* closeBtn = new QPushButton("X", m_searchFrame);
    closeBtn->setFlat(true);

    searchLayout->addWidget(searchLabel);
    searchLayout->addWidget(m_searchEdit, 1);
    searchLayout->addWidget(m_btnPrev);
    searchLayout->addWidget(m_btnNext);
    searchLayout->addWidget(m_caseSensitiveCheck);
    searchLayout->addWidget(closeBtn);

    layout->addWidget(m_searchFrame);

    connect(m_searchEdit, &QLineEdit::returnPressed, this, &TerminalTab::onSearchNext);
    connect(m_btnNext, &QPushButton::clicked, this, &TerminalTab::onSearchNext);
    connect(m_btnPrev, &QPushButton::clicked, this, &TerminalTab::onSearchPrev);
    connect(closeBtn, &QPushButton::clicked, this, &TerminalTab::hideSearchFrame);

    // Atajos de teclado utilizando QShortcut para interceptar eventos de forma limpia
    auto* searchShortcut = new QShortcut(QKeySequence(Qt::CTRL | Qt::Key_F), this);
    connect(searchShortcut, &QShortcut::activated, this, [this]() { doToggleSearchBar(); });

    auto* closeSearchShortcut = new QShortcut(QKeySequence(Qt::Key_Escape), this);
    connect(closeSearchShortcut, &QShortcut::activated, this, [this]() {
        if (m_searchFrame && m_searchFrame->isVisible()) {
            hideSearchFrame();
        }
    });

    startLogging();
}

TerminalTab::~TerminalTab() {
    m_closing = true;
    closeExternalProcess();

    if (m_reconnectTimer) {
        m_reconnectTimer->stop();
        delete m_reconnectTimer;
        m_reconnectTimer = nullptr;
    }

    if (m_logFile) {
        m_logFile->close();
        delete m_logFile;
        m_logFile = nullptr;
    }

    if (m_xmodemTimer) {
        m_xmodemTimer->stop();
        delete m_xmodemTimer;
        m_xmodemTimer = nullptr;
    }
    if (m_zmodemProcess) {
        m_zmodemProcess->kill();
        m_zmodemProcess->waitForFinished(1000);
        delete m_zmodemProcess;
        m_zmodemProcess = nullptr;
    }
    if (m_xmodemFile) {
        m_xmodemFile->close();
        delete m_xmodemFile;
        m_xmodemFile = nullptr;
    }

    if (m_serialPort) {
        disconnect(m_serialPort, nullptr, this, nullptr);
        if (m_serialPort->isOpen())
            m_serialPort->close();
        delete m_serialPort;
        m_serialPort = nullptr;
    }

#ifdef Q_OS_WIN
    if (m_conptyPollTimer) {
        m_conptyPollTimer->stop();
        delete m_conptyPollTimer;
        m_conptyPollTimer = nullptr;
    }
    if (m_conpty) {
        delete m_conpty;
        m_conpty = nullptr;
    }
#endif

    if (m_connection)
        disconnect(m_connection, nullptr, this, nullptr);

    if (m_connection && m_connectionThread && m_connectionThread->isRunning()) {
        QMetaObject::invokeMethod(m_connection, "disconnectFromHost", Qt::BlockingQueuedConnection);
    }
    if (m_connectionThread) {
        m_connectionThread->quit();
        if (!m_connectionThread->wait(3000)) {
            m_connectionThread->terminate();
            m_connectionThread->wait();
        }
        delete m_connectionThread;
        m_connectionThread = nullptr;
    }
    if (m_connection) {
        delete m_connection;
        m_connection = nullptr;
    }
}

void TerminalTab::resizeEvent(QResizeEvent* event) {
    QWidget::resizeEvent(event);
#ifdef BANCHO_HAVE_RDP_AX
    if (m_rdpWidget && m_isActive && m_embeddedContainer) {
        int w = m_embeddedContainer->width();
        int h = m_embeddedContainer->height();
        if (w > 1 && h > 1) {
            m_rdpWidget->setProperty("DesktopWidth", w);
            m_rdpWidget->setProperty("DesktopHeight", h);
        }
    }
#endif

    syncTerminalSize();
}

void TerminalTab::syncTerminalSize() {
    if (!m_terminal)
        return;

    const int rows = m_terminal->screenLinesCount();
    const int cols = m_terminal->screenColumnsCount();
    if (rows <= 0 || cols <= 0)
        return;

    applyTerminalSize(rows, cols);
}

void TerminalTab::applyTerminalSize(int rows, int cols) {
    if (rows <= 0 || cols <= 0)
        return;

    if (m_connection && m_session.type == SessionType::SSH) {
        QMetaObject::invokeMethod(m_connection, "resizePty", Qt::QueuedConnection, Q_ARG(int, rows), Q_ARG(int, cols));
    }

#ifdef Q_OS_WIN
    if (m_conpty) {
        m_conpty->resize(cols, rows);
    } else if (!m_conptyStarted && (m_session.type == SessionType::Local)) {
        // Start the local shell (ConPTY) once the widget has its real size.
        m_conptyStarted = true;
        m_conpty = new ConPty();
        if (!m_conpty->start(m_pendingShell, {}, cols, rows)) {
            DWORD err = m_conpty->startError();
            feedTerminalData(tr("\r\n[Failed to start '%1' (error 0x%2)]\r\n")
                                 .arg(m_pendingShell)
                                 .arg(err, 8, 16, QChar('0'))
                                 .toUtf8());
            m_isActive = false;
            emit titleChanged(tr("[Closed] %1").arg(m_session.name));
            showStoppedPrompt();
            return;
        }
        startConPtyPolling();
    }
#endif
}

#ifdef Q_OS_WIN
void TerminalTab::setupWindowsConPty(const QString& program, const QStringList& args) {
    // The vendored QTermWidget fork has no PTY support on Windows, so it runs
    // in "external" mode: keystrokes come out via sendData() and remote/conpty
    // output is fed in via feedData(). ConPTY provides the actual terminal.
    m_terminal->startExternal();

    m_conpty = new ConPty();
    if (!m_conpty->start(program, args, 80, 24)) {
        DWORD err = m_conpty->startError();
        feedTerminalData(
            tr("\r\n[Failed to start '%1' (error 0x%2)]\r\n").arg(program).arg(err, 8, 16, QChar('0')).toUtf8());
        m_isActive = false;
        emit titleChanged(tr("[Closed] %1").arg(m_session.name));
        return;
    }
    m_conptyStarted = true;
    startConPtyPolling();
}

void TerminalTab::startConPtyPolling() {
    if (!m_conpty)
        return;

    m_conptyPollTimer = new QTimer(this);
    connect(m_conptyPollTimer, &QTimer::timeout, this, &TerminalTab::pollConPtyOutput);
    // Fast polling while there is output; pollConPtyOutput() adapts the
    // interval up when the process is idle to avoid burning CPU at 100 Hz.
    m_conptyPollTimer->start(10);
}

void TerminalTab::pollConPtyOutput() {
    if (!m_conpty)
        return;

    QByteArray data = m_conpty->read();
    if (!data.isEmpty()) {
        feedTerminalData(data);
        logData(data);
        // Output arrived: resume fast polling.
        if (m_conptyPollTimer->interval() != 10)
            m_conptyPollTimer->start(10);
    } else if (m_conptyPollTimer->interval() < 50) {
        // No output: back off gradually (10 -> 20 -> 30 -> 40 -> 50 ms) so
        // idle sessions stop waking up the UI thread constantly.
        m_conptyPollTimer->start(m_conptyPollTimer->interval() + 10);
    }

    if (!m_conpty->isRunning()) {
        DWORD exitCode = m_conpty->exitCode();
        m_conptyPollTimer->stop();
        m_isActive = false;
        feedTerminalData(tr("\r\n[Process exited with code %1]\r\n").arg(exitCode).toUtf8());
        emit titleChanged(tr("[Closed] %1").arg(m_session.name));
        showStoppedPrompt();
    }
}

#endif

void TerminalTab::setupLocalTerminal() {
#ifdef Q_OS_WIN
    // Ignore POSIX-style paths saved from Linux (e.g. /bin/bash) on Windows.
    QString shell = m_session.shellPath;
    if (shell.isEmpty() || shell.startsWith('/')) {
        shell = qEnvironmentVariable("COMSPEC");
        if (shell.isEmpty())
            shell = "cmd.exe";
    }
    if (shell.isEmpty())
        shell = "cmd.exe";

    // ConPTY expects DEL for the interactive erase character.  QTermWidget's
    // default keymap emits BS, which cmd/PowerShell can interpret as a control
    // operation instead of deleting a single character.
    if (QTermWidget::availableKeyBindings().contains(QStringLiteral("linux")))
        m_terminal->setKeyBindings(QStringLiteral("linux"));
    m_terminal->startExternal();

    // Defer ConPTY start until the widget reports its real size.
    m_pendingShell = shell;
#else
    QString shell = m_session.shellPath;
    if (shell.isEmpty()) {
        shell = qgetenv("SHELL");
        if (shell.isEmpty()) {
            shell = "/bin/bash";
        }
    }
    m_terminal->setShellProgram(shell);
    m_terminal->startShellProgram();
#endif
}

void TerminalTab::setupSshTerminal() {
    // No local PTY needed: libssh2 owns the remote pty. Run the emulator in
    // external mode and bridge bytes with the SSH connection.
    // The Linux/default keymap emits BS (0x08), while the SSH PTY below is
    // configured with DEL (0x7f) as TTY_OP_ERASE. Use the Linux terminal
    // binding explicitly so Backspace matches the requested remote setting.
    if (QTermWidget::availableKeyBindings().contains(QStringLiteral("linux")))
        m_terminal->setKeyBindings(QStringLiteral("linux"));
    m_terminal->startExternal();

    bool enableX11 = m_session.x11Forwarding;
#ifdef Q_OS_WIN
    if (enableX11) {
        QString x11Error;
        if (!XServerManager::instance()->ensureVcXsrvRunning(&x11Error)) {
            feedTerminalData(tr("\r\n[X11 forwarding desactivado: %1]\r\n").arg(x11Error).toUtf8());
            enableX11 = false;
        }
    }
#endif

    m_connection = new SshConnection();
    m_connectionThread = new QThread(this);
    m_connection->moveToThread(m_connectionThread);
    m_connectionThread->start();

    connect(m_connection, &SshConnection::shellDataReceived, this, [this](const QByteArray& data) {
        feedTerminalData(data);
        logData(data);
    });
    connect(m_connection, &SshConnection::shellClosed, this, [this]() {
        m_isActive = false;
        feedTerminalData(tr("\r\n[Connection closed]\r\n").toUtf8());
        emit titleChanged(tr("[Closed] %1").arg(m_session.name));
        showStoppedPrompt();
        maybeScheduleReconnect();
    });
    connect(m_connection, &SshConnection::connectionFailed, this, [this](const QString& error) {
        feedTerminalData(tr("\r\n[Connection failed: %1]\r\n").arg(error).toUtf8());
        m_isActive = false;
        emit titleChanged(tr("[Closed] %1").arg(m_session.name));
        showStoppedPrompt();
        maybeScheduleReconnect();
    });
    connect(m_connection, &SshConnection::tunnelStatus, this, [this](const QString& message, bool active) {
        const QByteArray prefix = active ? QByteArrayLiteral("\r\n[Info: ") : QByteArrayLiteral("\r\n[Warning: ");
        feedTerminalData(prefix + message.toUtf8() + QByteArrayLiteral("]\r\n"));
    });
    if (enableX11) {
        QMetaObject::invokeMethod(m_connection, "setX11Forwarding", Qt::QueuedConnection, Q_ARG(bool, true));
    }
    applySshOptions();
}

void TerminalTab::onSendData(const char* data, int size) {
    if (!m_isActive) {
        const QByteArray input(data, size);
        for (const char ch : input) {
            if (ch == '\r' || ch == '\n') {
                emit exitRequested();
                return;
            }
            if (ch == 'r' || ch == 'R') {
                requestReconnect();
                return;
            }
            if (ch == 's' || ch == 'S') {
                saveTerminalOutput();
                return;
            }
        }
        return;
    }

    QByteArray input(data, size);
    recordTypedInput(input);
    if (m_session.type == SessionType::SSH || m_session.type == SessionType::Telnet ||
        m_session.type == SessionType::Serial) {
        input.replace(QByteArrayLiteral("\x7f"), m_session.backspaceSequence.toUtf8());
        if (input.contains(QByteArrayLiteral("\r\n")))
            input.replace(QByteArrayLiteral("\r\n"), QByteArrayLiteral("\r"));
        input.replace(QByteArrayLiteral("\r"), m_session.enterSequence.toUtf8());
    }
    if (m_connection && m_session.type == SessionType::SSH) {
        QMetaObject::invokeMethod(m_connection, "sendToShell", Qt::QueuedConnection, Q_ARG(QByteArray, input));
    } else if (m_serialPort && m_serialPort->isOpen() && m_session.type == SessionType::Serial) {
        if (m_zmodemProcess)
            return;
        m_serialPort->write(input);
#ifdef Q_OS_WIN
    } else if (m_conpty) {
        m_conpty->write(input);
#endif
    }
}

void TerminalTab::applySshOptions() {
    if (!m_connection)
        return;
    if (m_session.keepAliveSeconds > 0)
        QMetaObject::invokeMethod(m_connection, "setKeepAliveSeconds", Qt::QueuedConnection,
                                  Q_ARG(int, m_session.keepAliveSeconds));
    if (!m_session.cryptCipher.isEmpty())
        QMetaObject::invokeMethod(m_connection, "setCipherAlgorithms", Qt::QueuedConnection,
                                  Q_ARG(QString, m_session.cryptCipher));
    if (!m_session.kexAlgo.isEmpty())
        QMetaObject::invokeMethod(m_connection, "setKexAlgorithm", Qt::QueuedConnection,
                                  Q_ARG(QString, m_session.kexAlgo));
    if (!m_session.macAlgo.isEmpty())
        QMetaObject::invokeMethod(m_connection, "setMacAlgorithm", Qt::QueuedConnection,
                                  Q_ARG(QString, m_session.macAlgo));
}

void TerminalTab::onTitleChanged() {
    if (m_terminal)
        emit titleChanged(m_terminal->title());
}

void TerminalTab::showTerminalContextMenu(const QPoint& pos) {
    QWidget* view = terminalView();
    if (!view)
        return;

    QMenu menu(this);

    auto* copyAct = menu.addAction(tr("&Copy"));
    copyAct->setShortcut(QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_C));
    copyAct->setEnabled(hasSelection());

    auto* pasteAct = menu.addAction(tr("&Paste"));
    pasteAct->setShortcut(QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_V));

    menu.addSeparator();

    auto* specialMenu = menu.addMenu(tr("Special Characters"));
    const QList<QPair<QString, QByteArray>> specialCharacters = {
        {tr("Break (Cisco)"), QByteArray("\x1e")},
        {tr("Ctrl+C"), QByteArray("\x03")},
        {tr("Ctrl+D"), QByteArray("\x04")},
        {tr("Ctrl+Z"), QByteArray("\x1a")},
        {tr("Escape"), QByteArray("\x1b")},
        {tr("Enter"), QByteArray("\r")},
        {tr("Tab"), QByteArray("\t")},
        {tr("Backspace"), QByteArray("\x7f")},
        {tr("Telnet escape (Ctrl+])"), QByteArray("\x1d")},
    };
    for (const auto& special : specialCharacters) {
        auto* action = specialMenu->addAction(special.first);
        action->setData(special.second);
        action->setProperty("physicalBreak", special.first == tr("Break (Cisco)"));
    }

    auto* clearAct = menu.addAction(tr("Clear Scrollback"));
    QAction* serialSendAct = nullptr;
    if (m_session.type == SessionType::Serial) {
        if (m_xmodemFile || m_zmodemProcess) {
            serialSendAct = menu.addAction(tr("Cancel serial file transfer"));
        } else {
            serialSendAct = menu.addAction(tr("Send File with XMODEM/YMODEM/ZMODEM..."));
        }
    }
    menu.addSeparator();
    auto* historyAct = menu.addAction(tr("Command History..."));
    auto* zoomInAct = menu.addAction(tr("Zoom &In"));
    zoomInAct->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_Plus));
    auto* zoomOutAct = menu.addAction(tr("Zoom &Out"));
    zoomOutAct->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_Minus));

    QAction* reconnectAct = nullptr;
    if (!m_isActive) {
        menu.addSeparator();
        reconnectAct = menu.addAction(tr("Reconnect"));
    }

    auto* selected = menu.exec(view->mapToGlobal(pos));
    if (selected == copyAct) {
        doCopy();
    } else if (selected == pasteAct) {
        doPaste();
    } else if (selected && selected->parent() == specialMenu) {
        if (selected->property("physicalBreak").toBool())
            sendBreak();
        else
            sendRaw(QString::fromLatin1(selected->data().toByteArray()));
    } else if (selected == clearAct) {
        doClear();
    } else if (selected == serialSendAct) {
        if (m_xmodemFile)
            cancelSerialFile();
        else
            sendSerialFile();
    } else if (selected == historyAct) {
        showCommandHistory();
    } else if (selected == zoomInAct) {
        doZoomIn();
    } else if (selected == zoomOutAct) {
        doZoomOut();
    } else if (selected == reconnectAct) {
        requestReconnect();
    }
}

void TerminalTab::onTerminalFinished() {
    m_isActive = false;
    emit titleChanged(tr("[Closed] %1").arg(m_session.name));
    showStoppedPrompt();
    maybeScheduleReconnect();
}

void TerminalTab::onRemoteDirChanged(const QString& dir) {
    emit remoteDirChanged(dir);
}

void TerminalTab::updateFontFromSettings() {
    QSettings settings;

    QFont font;
    if (!m_session.fontFamily.isEmpty()) {
        // Per-session font overrides the global one.
        font = QFont(m_session.fontFamily, m_session.fontSize > 0 ? m_session.fontSize : 11);
        font.setStyleHint(QFont::Monospace);
    } else if (settings.contains("terminal/font")) {
        font.fromString(settings.value("terminal/font").toString());
    } else {
        font = QFont("Monospace", 11);
        font.setStyleHint(QFont::Monospace);
    }
    font.setFixedPitch(true);

    const QString colorScheme = m_session.colorScheme.isEmpty()
                                    ? settings.value("terminal/colorScheme", "DarkPastels").toString()
                                    : m_session.colorScheme;

    if (m_terminal) {
        m_terminal->setTerminalFont(font);

        QStringList schemes = QTermWidget::availableColorSchemes();
        if (schemes.contains(colorScheme)) {
            m_terminal->setColorScheme(colorScheme);
        } else if (schemes.contains("DarkPastels")) {
            m_terminal->setColorScheme("DarkPastels");
        }

        // The font change alters how many columns/rows fit in the widget.
        // Push the corrected size to the remote PTY / ConPTY so the content
        // matches the viewer without requiring a manual window resize.
        syncTerminalSize();
    }
}

void TerminalTab::startLogging() {
    if (m_session.type == SessionType::RDP || m_session.type == SessionType::VNC)
        return;

    QSettings settings;
    if (!settings.value("terminal/loggingEnabled", false).toBool())
        return;

    QString dir = settings.value("terminal/logDirectory", "").toString().trimmed();
    if (dir.isEmpty())
        dir = QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation);
    if (!QDir().mkpath(dir))
        return;

    QString stamp = QDateTime::currentDateTime().toString("yyyyMMdd_HHmmss");
    QString safeName = m_session.name;
    safeName.replace(QRegularExpression("[^A-Za-z0-9_-]"), "_");

    QString path = dir + "/" + safeName + "_" + stamp + ".log";
    m_logFile = new QFile(path);
    if (!m_logFile->open(QIODevice::WriteOnly | QIODevice::Append)) {
        delete m_logFile;
        m_logFile = nullptr;
    }
}

void TerminalTab::logData(const QByteArray& data) {
    if (m_logFile && m_logFile->isOpen() && !data.isEmpty())
        m_logFile->write(data);
}

void TerminalTab::maybeScheduleReconnect() {
    if (m_closing || !m_session.autoReconnect || m_reconnectTimer)
        return;
    m_reconnectTimer = new QTimer(this);
    m_reconnectTimer->setSingleShot(true);
    m_reconnectTimer->setInterval(3000);
    connect(m_reconnectTimer, &QTimer::timeout, this, [this]() { emit reconnectRequested(m_session); });
    m_reconnectTimer->start();
}

void TerminalTab::requestReconnect() {
    if (m_isActive)
        return;
    if (m_reconnectTimer) {
        m_reconnectTimer->stop();
        m_reconnectTimer->deleteLater();
        m_reconnectTimer = nullptr;
    }
    if (m_reconnectButton)
        m_reconnectButton->setEnabled(false);
    emit reconnectRequested(m_session);
}

void TerminalTab::showStoppedPrompt() {
    if (m_stopPromptShown || !m_terminal)
        return;

    m_stopPromptShown = true;
    feedTerminalData((QByteArray("\r\n\x1b[38;5;214m") + tr("Session stopped\r\n").toUtf8() +
                      tr(" - Press <return> to exit tab\r\n").toUtf8() +
                      tr(" - Press R to restart session\r\n").toUtf8() +
                      tr(" - Press S to save terminal output to file\r\n").toUtf8() + QByteArray("\x1b[0m")));
}

void TerminalTab::saveTerminalOutput() {
    if (m_outputBuffer.isEmpty())
        return;

    QString safeName = m_session.name;
    safeName.replace(QRegularExpression("[^A-Za-z0-9_-]"), "_");
    if (safeName.isEmpty())
        safeName = QStringLiteral("terminal");

    const QString path = QFileDialog::getSaveFileName(
        this, tr("Save Terminal Output"),
        QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation) + "/" + safeName + ".log",
        tr("Text files (*.txt *.log);;All files (*)"));
    if (path.isEmpty())
        return;

    QFile file(path);
    if (!file.open(QIODevice::WriteOnly))
        return;
    file.write(m_outputBuffer);
}

#ifdef BANCHO_HAVE_RDP_AX
void TerminalTab::setupWindowsRdpActiveX() {
    m_rdpWidget = new QAxWidget(m_embeddedContainer);
    auto* rdpLayout = new QVBoxLayout(m_embeddedContainer);
    rdpLayout->setContentsMargins(0, 0, 0, 0);
    rdpLayout->addWidget(m_rdpWidget);

    // Newest to oldest CLSIDs; the first one available on this Windows wins.
    static const char* kClsids[] = {
        "{A0C63C30-F08D-4AB4-907C-34905D770C7D}", // MsRdpClient10NotSafeForScripting
        "{301B94BA-5F25-4A12-BFFE-3B6B7A616585}", // MsRdpClient9NotSafeForScripting
        "{A3BC03A0-041D-42E3-AD22-882B7865C9C5}", // MsRdpClient8NotSafeForScripting
        "{54D38BF7-B1EF-4479-9674-1BD6EA465258}", // MsRdpClient7NotSafeForScripting
        "{8C11EFA1-92C3-11D1-BC1E-00C04FA31489}", // MsTscAxNotSafeForScripting (legacy)
    };

    bool created = false;
    for (const char* clsid : kClsids) {
        if (m_rdpWidget->setControl(QString::fromLatin1(clsid))) {
            created = true;
            break;
        }
    }

    if (!created) {
        delete m_rdpWidget;
        m_rdpWidget = nullptr;
        if (m_statusLabel) {
            m_statusLabel->show();
            m_statusLabel->setText(tr("RDP control unavailable; falling back to mstsc.exe."));
        }
        launchExternalClient();
        return;
    }

    m_rdpWidget->setProperty("Server", m_session.host + ":" + QString::number(m_session.port));
    m_rdpWidget->setProperty("Domain", QString());
    if (!m_session.user.isEmpty())
        m_rdpWidget->setProperty("UserName", m_session.user);

    if (m_embeddedContainer->width() > 1 && m_embeddedContainer->height() > 1) {
        m_rdpWidget->setProperty("DesktopWidth", m_embeddedContainer->width());
        m_rdpWidget->setProperty("DesktopHeight", m_embeddedContainer->height());
    }

    if (m_statusLabel)
        m_statusLabel->hide();
    if (m_reconnectButton)
        m_reconnectButton->setVisible(false);
    m_embeddedContainer->show();
    m_isActive = true;

    // Poll the "Connected" property to detect disconnection.
    m_rdpPollTimer = new QTimer(this);
    connect(m_rdpPollTimer, &QTimer::timeout, this, [this]() {
        if (!m_rdpWidget)
            return;
        QVariant connected = m_rdpWidget->property("Connected");
        if (!connected.isValid())
            return;
        if (connected.toBool()) {
            m_rdpWasConnected = true;
        } else if (m_rdpWasConnected && m_isActive) {
            m_isActive = false;
            if (m_rdpPollTimer)
                m_rdpPollTimer->stop();
            if (m_embeddedContainer)
                m_embeddedContainer->hide();
            if (m_statusLabel) {
                m_statusLabel->show();
                m_statusLabel->setText(tr("Session closed. Close this tab to continue."));
            }
            if (m_reconnectButton)
                m_reconnectButton->setVisible(true);
            emit titleChanged(tr("[Closed] %1").arg(m_session.name));
            maybeScheduleReconnect();
        }
    });
    m_rdpPollTimer->start(2000);

    m_rdpWidget->dynamicCall("Connect()");
}
#endif

#ifdef BANCHO_HAVE_VNC
void TerminalTab::setupEmbeddedVnc() {
    m_vncWidget = new VncClientWidget(m_embeddedContainer);
    auto* vncLayout = new QVBoxLayout(m_embeddedContainer);
    vncLayout->setContentsMargins(0, 0, 0, 0);
    vncLayout->addWidget(m_vncWidget);

    const QString password = Keyring::lookupPassword(m_session.id);

    connect(m_vncWidget, &VncClientWidget::connected, this, [this]() {
        m_isActive = true;
        if (m_reconnectButton)
            m_reconnectButton->setVisible(false);
        if (m_statusLabel)
            m_statusLabel->hide();
        if (m_embeddedContainer)
            m_embeddedContainer->show();
    });
    connect(m_vncWidget, &VncClientWidget::disconnected, this, [this]() {
        m_isActive = false;
        if (m_embeddedContainer)
            m_embeddedContainer->hide();
        if (m_statusLabel) {
            m_statusLabel->show();
            m_statusLabel->setText(tr("Session closed. Close this tab to continue."));
        }
        if (m_reconnectButton)
            m_reconnectButton->setVisible(true);
        emit titleChanged(tr("[Closed] %1").arg(m_session.name));
        maybeScheduleReconnect();
    });
    connect(m_vncWidget, &VncClientWidget::errorOccurred, this, [this](const QString& msg) {
        m_isActive = false;
        if (m_embeddedContainer)
            m_embeddedContainer->hide();
        if (m_statusLabel) {
            m_statusLabel->show();
            m_statusLabel->setText(tr("VNC error: %1").arg(msg));
        }
        if (m_reconnectButton)
            m_reconnectButton->setVisible(true);
        emit titleChanged(tr("[Closed] %1").arg(m_session.name));
        maybeScheduleReconnect();
    });

    m_embeddedContainer->show();
    m_vncWidget->start(m_session.host, m_session.port, password);
}
#endif

void TerminalTab::launchExternalClient() {
    QString program;
    QStringList args;
    WId winId = m_embeddedContainer->winId();

    if (m_session.type == SessionType::RDP) {
#ifdef Q_OS_WIN
        program = "mstsc";
        args << "/v:" + m_session.host + ":" + QString::number(m_session.port) << "/parent:" + QString::number(winId);
#else
        program = "xfreerdp";
        args << "/v:" + m_session.host + ":" + QString::number(m_session.port);
        args << "/parent-window:" + QString::number(winId);
        args << "/cert:ignore";
        args << "/dynamic-resolution";
        args << "+decoration";
        if (!m_session.user.isEmpty()) {
            args << "/u:" + m_session.user;
        }
#endif
    } else if (m_session.type == SessionType::VNC) {
        program = "vncviewer";
        args << m_session.host + "::" + QString::number(m_session.port) << "-parentwindow" << QString::number(winId);
    }

    if (program.isEmpty())
        return;

    m_externalProcess = new QProcess(this);
    m_externalProcess->setProcessChannelMode(QProcess::ForwardedChannels);

    connect(m_externalProcess, &QProcess::started, this, [this]() {
        m_isActive = true;
        if (m_reconnectButton)
            m_reconnectButton->setVisible(false);
        if (m_statusLabel)
            m_statusLabel->hide();
        if (m_embeddedContainer)
            m_embeddedContainer->show();
    });

    connect(m_externalProcess, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished), this,
            [this](int exitCode, QProcess::ExitStatus) {
                Q_UNUSED(exitCode);
                m_isActive = false;
                if (m_embeddedContainer)
                    m_embeddedContainer->hide();
                if (m_statusLabel) {
                    m_statusLabel->show();
                    m_statusLabel->setText(tr("Session closed. Close this tab to continue."));
                }
                if (m_reconnectButton)
                    m_reconnectButton->setVisible(true);
                emit titleChanged(tr("[Closed] %1").arg(m_session.name));
                maybeScheduleReconnect();
            });

    connect(m_externalProcess, &QProcess::errorOccurred, this, [this](QProcess::ProcessError error) {
        Q_UNUSED(error);
        if (m_embeddedContainer)
            m_embeddedContainer->hide();
        if (m_statusLabel) {
            m_statusLabel->show();
            m_statusLabel->setText(tr("Failed to launch embedded client.\n\n") +
                                   tr("Make sure the required program is installed:\n") +
                                   (m_session.type == SessionType::RDP ? tr("RDP: xfreerdp (Linux) or mstsc (Windows)")
                                                                       : tr("VNC: vncviewer")));
        }
        m_isActive = false;
        if (m_reconnectButton)
            m_reconnectButton->setVisible(true);
        emit titleChanged(tr("[Closed] %1").arg(m_session.name));
        maybeScheduleReconnect();
    });

    m_externalProcess->start(program, args);
}

void TerminalTab::closeExternalProcess() {
    if (m_externalProcess) {
        m_externalProcess->terminate();
        if (!m_externalProcess->waitForFinished(3000)) {
            m_externalProcess->kill();
        }
    }
}

void TerminalTab::showSearchFrame() {
    if (m_searchFrame) {
        m_searchFrame->show();
        m_searchEdit->setFocus();
        m_searchEdit->selectAll();
    }
}

void TerminalTab::hideSearchFrame() {
    if (m_searchFrame) {
        m_searchFrame->hide();
        doFocusTerminal();
    }
}

void TerminalTab::onSearchNext() {
    // QTermWidget provides its own search bar (Ctrl+F); no programmatic search.
}

void TerminalTab::onSearchPrev() {
    // QTermWidget provides its own search bar (Ctrl+F); no programmatic search.
}

bool TerminalTab::eventFilter(QObject* watched, QEvent* event) {
    if (m_terminal && event->type() == QEvent::KeyPress) {
        auto* widget = qobject_cast<QWidget*>(watched);
        if (widget && (widget == m_terminal || m_terminal->isAncestorOf(widget))) {
            auto* keyEvent = static_cast<QKeyEvent*>(event);
            const Qt::KeyboardModifiers modifiers = keyEvent->modifiers();
            const bool clipboardShortcut = (modifiers & Qt::ControlModifier) && (modifiers & Qt::ShiftModifier) &&
                                           !(modifiers & Qt::AltModifier) && !(modifiers & Qt::MetaModifier);
            if (clipboardShortcut && keyEvent->key() == Qt::Key_C) {
                doCopy();
                keyEvent->accept();
                return true;
            }
            if (clipboardShortcut && keyEvent->key() == Qt::Key_V) {
                doPaste();
                keyEvent->accept();
                return true;
            }
            if (m_session.readOnly) {
                if ((modifiers & Qt::ControlModifier) && keyEvent->key() == Qt::Key_F)
                    return QWidget::eventFilter(watched, event);
                keyEvent->accept();
                return true;
            }
        }
    }
    return QWidget::eventFilter(watched, event);
}

void TerminalTab::sendInputText(const QString& text) {
    if (m_session.readOnly) {
        reportMacroMessage(tr("Session is read-only; input was blocked."));
        return;
    }
    recordCommand(text);
    doSendText(text + "\n");
}

void TerminalTab::sendRaw(const QString& text) {
    if (m_session.readOnly) {
        reportMacroMessage(tr("Session is read-only; input was blocked."));
        return;
    }
    doSendText(text);
}

void TerminalTab::sendBreak() {
    if (m_session.readOnly) {
        reportMacroMessage(tr("Session is read-only; Break was blocked."));
        return;
    }
    if (m_serialPort && m_serialPort->isOpen() && m_session.type == SessionType::Serial) {
        m_serialPort->setBreakEnabled(true);
        QTimer::singleShot(250, this, [this]() {
            if (m_serialPort && m_serialPort->isOpen())
                m_serialPort->setBreakEnabled(false);
        });
        return;
    }
    QByteArray sequence = QByteArray::fromHex(m_session.ciscoBreakSequence.toLatin1());
    if (sequence.isEmpty())
        sequence = QByteArrayLiteral("\x1e");
    sendRaw(QString::fromLatin1(sequence));
}

void TerminalTab::sendSerialFile() {
    if (m_session.readOnly || !m_serialPort || !m_serialPort->isOpen())
        return;
    const QStringList protocols = {QStringLiteral("XMODEM"), QStringLiteral("YMODEM"), QStringLiteral("ZMODEM")};
    bool accepted = false;
    const QString protocol =
        QInputDialog::getItem(this, tr("Serial file transfer"), tr("Protocol:"), protocols, 0, false, &accepted);
    if (!accepted)
        return;
    const QString path = QFileDialog::getOpenFileName(this, tr("Select file for %1").arg(protocol));
    if (path.isEmpty())
        return;

    if (protocol == QStringLiteral("ZMODEM")) {
        const QString sz = QStandardPaths::findExecutable(QStringLiteral("sz"));
        if (sz.isEmpty()) {
            reportMacroMessage(tr("ZMODEM requires the 'sz' program from lrzsz."));
            return;
        }

        m_zmodemFileName = path;
        m_zmodemProcess = new QProcess(this);
        m_zmodemProcess->setProcessChannelMode(QProcess::SeparateChannels);
        connect(m_zmodemProcess, &QProcess::readyReadStandardOutput, this, [this]() {
            if (m_zmodemProcess && m_serialPort && m_serialPort->isOpen())
                m_serialPort->write(m_zmodemProcess->readAllStandardOutput());
        });
        connect(m_zmodemProcess, &QProcess::readyReadStandardError, this, [this]() {
            if (m_zmodemProcess) {
                const QByteArray status = m_zmodemProcess->readAllStandardError();
                if (!status.isEmpty())
                    feedTerminalData(QByteArrayLiteral("\r\n[ZMODEM: ") + status.trimmed() +
                                     QByteArrayLiteral("]\r\n"));
            }
        });
        connect(m_zmodemProcess, &QProcess::errorOccurred, this, [this](QProcess::ProcessError error) {
            if (error == QProcess::FailedToStart)
                finishZmodem(false, tr("Could not start the 'sz' program."));
        });
        connect(m_zmodemProcess, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished), this,
                [this](int exitCode, QProcess::ExitStatus status) {
                    finishZmodem(status == QProcess::NormalExit && exitCode == 0,
                                 status == QProcess::NormalExit ? tr("ZMODEM transfer finished.")
                                                                : tr("ZMODEM process terminated unexpectedly."));
                });
        m_zmodemProcess->start(sz, {QStringLiteral("--binary"), path});
        feedTerminalData(tr("\r\n[ZMODEM sending %1... ]\r\n").arg(QFileInfo(path).fileName()).toUtf8());
        return;
    }

    auto* file = new QFile(path, this);
    if (!file->open(QIODevice::ReadOnly)) {
        delete file;
        reportMacroMessage(tr("Could not open the selected file."));
        return;
    }
    m_xmodemFile = file;
    m_xmodemPacket.clear();
    m_xmodemBlock = 1;
    m_xmodemRetries = 0;
    m_xmodemCrcMode = true;
    m_xmodemWaitingEotAck = false;
    m_ymodem = protocol == QStringLiteral("YMODEM");
    m_ymodemHeaderPending = m_ymodem;
    m_ymodemFinalHeader = false;
    m_ymodemFileName = QFileInfo(path).fileName();
    if (!m_xmodemTimer) {
        m_xmodemTimer = new QTimer(this);
        m_xmodemTimer->setInterval(1000);
        connect(m_xmodemTimer, &QTimer::timeout, this, [this]() {
            if (!m_xmodemFile)
                return;
            if (++m_xmodemRetries > 10) {
                finishXmodem(false, tr("XMODEM receiver timed out."));
                return;
            }
            if (m_xmodemWaitingEotAck) {
                m_serialPort->write(QByteArray(1, char(0x04)));
            } else if (!m_xmodemPacket.isEmpty()) {
                m_serialPort->write(m_xmodemPacket);
            }
        });
    }
    m_xmodemTimer->start();
    feedTerminalData(tr("\r\n[%1 waiting for receiver...]\r\n").arg(protocol).toUtf8());
}

void TerminalTab::cancelSerialFile() {
    if (m_zmodemProcess)
        finishZmodem(false, tr("ZMODEM transfer cancelled locally."));
    else if (m_xmodemFile)
        finishXmodem(false, tr("XMODEM transfer cancelled locally."));
}

static quint16 xmodemCrc16(const QByteArray& data) {
    quint16 crc = 0;
    for (const unsigned char byte : data) {
        crc ^= static_cast<quint16>(byte) << 8;
        for (int bit = 0; bit < 8; ++bit)
            crc = (crc & 0x8000) ? static_cast<quint16>((crc << 1) ^ 0x1021) : static_cast<quint16>(crc << 1);
    }
    return crc;
}

void TerminalTab::sendXmodemPacket() {
    if (!m_xmodemFile || !m_serialPort)
        return;
    QByteArray payload;
    int block = m_xmodemBlock;
    if (m_ymodem && m_ymodemHeaderPending) {
        payload = m_ymodemFileName.toUtf8();
        payload.append('\0');
        payload.append(QByteArray::number(m_xmodemFile->size()));
        payload.append('\0');
        block = 0;
    } else {
        payload = m_xmodemFile->read(128);
    }
    if (payload.isEmpty()) {
        m_xmodemWaitingEotAck = true;
        m_xmodemRetries = 0;
        m_serialPort->write(QByteArray(1, char(0x04)));
        return;
    }
    payload.append(QByteArray(128 - payload.size(), char(0x1a)));
    m_xmodemPacket = QByteArray(1, char(0x01));
    m_xmodemPacket.append(char(block & 0xff));
    m_xmodemPacket.append(char(255 - (block & 0xff)));
    m_xmodemPacket.append(payload);
    if (m_xmodemCrcMode) {
        const quint16 crc = xmodemCrc16(payload);
        m_xmodemPacket.append(char(crc >> 8));
        m_xmodemPacket.append(char(crc & 0xff));
    } else {
        unsigned char checksum = 0;
        for (const unsigned char byte : payload)
            checksum = static_cast<unsigned char>(checksum + byte);
        m_xmodemPacket.append(char(checksum));
    }
    m_xmodemRetries = 0;
    m_serialPort->write(m_xmodemPacket);
}

void TerminalTab::handleXmodemInput(const QByteArray& data) {
    if (!m_xmodemFile || !m_serialPort)
        return;
    for (const unsigned char byte : data) {
        if (byte == 0x18) {
            finishXmodem(false, tr("XMODEM cancelled by receiver."));
            return;
        }
        if (m_xmodemWaitingEotAck) {
            if (byte == 0x06) {
                if (m_ymodem && !m_ymodemFinalHeader) {
                    m_xmodemWaitingEotAck = false;
                    m_ymodemFinalHeader = true;
                    m_xmodemPacket.clear();
                    QByteArray payload(128, '\0');
                    m_xmodemPacket = QByteArray(1, char(0x01));
                    m_xmodemPacket.append(char(0));
                    m_xmodemPacket.append(char(0xff));
                    m_xmodemPacket.append(payload);
                    if (m_xmodemCrcMode) {
                        const quint16 crc = xmodemCrc16(payload);
                        m_xmodemPacket.append(char(crc >> 8));
                        m_xmodemPacket.append(char(crc & 0xff));
                    } else {
                        unsigned char checksum = 0;
                        for (const unsigned char value : payload)
                            checksum = static_cast<unsigned char>(checksum + value);
                        m_xmodemPacket.append(char(checksum));
                    }
                    m_xmodemRetries = 0;
                    m_serialPort->write(m_xmodemPacket);
                } else {
                    finishXmodem(true, m_ymodem ? tr("YMODEM transfer complete.") : tr("XMODEM transfer complete."));
                }
            }
            continue;
        }
        if (m_xmodemPacket.isEmpty()) {
            if (byte == 0x15 || byte == 'C') {
                m_xmodemCrcMode = byte == 'C';
                sendXmodemPacket();
            }
            continue;
        }
        if (byte == 0x06) {
            if (m_ymodem && m_ymodemFinalHeader) {
                finishXmodem(true, tr("YMODEM transfer complete."));
                return;
            }
            const bool headerAcked = m_ymodem && m_ymodemHeaderPending;
            if (headerAcked) {
                m_ymodemHeaderPending = false;
                m_xmodemBlock = 1;
            }
            if (!headerAcked)
                ++m_xmodemBlock;
            m_xmodemPacket.clear();
            sendXmodemPacket();
        } else if (byte == 0x15) {
            if (++m_xmodemRetries > 10)
                finishXmodem(false, tr("XMODEM transfer failed after retries."));
            else
                m_serialPort->write(m_xmodemPacket);
        }
    }
}

void TerminalTab::finishXmodem(bool success, const QString& message) {
    const QString protocol = m_ymodem ? QStringLiteral("YMODEM") : QStringLiteral("XMODEM");
    if (m_xmodemTimer)
        m_xmodemTimer->stop();
    if (!success && m_serialPort && m_serialPort->isOpen())
        m_serialPort->write(QByteArray(1, char(0x18)));
    if (m_xmodemFile) {
        m_xmodemFile->close();
        delete m_xmodemFile;
        m_xmodemFile = nullptr;
    }
    m_xmodemPacket.clear();
    m_xmodemWaitingEotAck = false;
    m_ymodem = false;
    m_ymodemHeaderPending = false;
    m_ymodemFinalHeader = false;
    feedTerminalData(QByteArrayLiteral("\r\n[") + protocol.toUtf8() + QByteArrayLiteral(": ") + message.toUtf8() +
                     QByteArrayLiteral("]\r\n"));
}

void TerminalTab::finishZmodem(bool success, const QString& message) {
    if (!m_zmodemProcess)
        return;

    QProcess* process = m_zmodemProcess;
    m_zmodemProcess = nullptr;
    if (!success && process->state() != QProcess::NotRunning) {
        process->kill();
        process->waitForFinished(1000);
    }
    process->deleteLater();
    feedTerminalData(QByteArrayLiteral("\r\n[ZMODEM: ") + message.toUtf8() + QByteArrayLiteral("]\r\n"));
    m_zmodemFileName.clear();
}

void TerminalTab::recordTypedInput(const QByteArray& data) {
    for (const char byte : data) {
        if (byte == '\r' || byte == '\n') {
            recordCommand(QString::fromUtf8(m_commandInputBuffer).trimmed());
            m_commandInputBuffer.clear();
        } else if (byte == '\b' || static_cast<unsigned char>(byte) == 0x7f) {
            if (!m_commandInputBuffer.isEmpty())
                m_commandInputBuffer.chop(1);
        } else if (byte >= 0x20 && byte != 0x7f) {
            m_commandInputBuffer.append(byte);
        }
    }
}

void TerminalTab::recordCommand(const QString& command) {
    const QString value = command.trimmed();
    if (value.isEmpty())
        return;

    QSettings settings;
    const QStringList keys = {QStringLiteral("commandHistory/global"),
                              QStringLiteral("commandHistory/session/") + m_session.id};
    for (const QString& key : keys) {
        QStringList history = settings.value(key).toStringList();
        history.removeAll(value);
        history.prepend(value);
        while (history.size() > 100)
            history.removeLast();
        settings.setValue(key, history);
    }
}

void TerminalTab::showCommandHistory() {
    QSettings settings;
    const QString sessionKey = QStringLiteral("commandHistory/session/") + m_session.id;
    const QStringList sessionHistory = settings.value(sessionKey).toStringList();
    const QStringList globalHistory = settings.value(QStringLiteral("commandHistory/global")).toStringList();

    QDialog dialog(this);
    dialog.setWindowTitle(tr("Command History"));
    dialog.resize(560, 420);
    auto* layout = new QVBoxLayout(&dialog);
    auto* filter = new QLineEdit(&dialog);
    filter->setPlaceholderText(tr("Filter commands..."));
    layout->addWidget(filter);
    auto* list = new QListWidget(&dialog);
    layout->addWidget(list);
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Close, &dialog);
    auto* sendButton = buttons->addButton(tr("Send"), QDialogButtonBox::AcceptRole);
    layout->addWidget(buttons);

    const auto populate = [list, sessionHistory, globalHistory, filter]() {
        list->clear();
        const QString query = filter->text().trimmed();
        QStringList combined = sessionHistory;
        for (const QString& command : globalHistory) {
            if (!combined.contains(command))
                combined.append(command);
        }
        for (const QString& command : combined) {
            if (query.isEmpty() || command.contains(query, Qt::CaseInsensitive))
                list->addItem(command);
        }
    };
    populate();
    connect(filter, &QLineEdit::textChanged, &dialog, populate);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    connect(sendButton, &QPushButton::clicked, &dialog, &QDialog::accept);
    connect(list, &QListWidget::itemDoubleClicked, &dialog, &QDialog::accept);

    if (dialog.exec() == QDialog::Accepted && list->currentItem())
        sendInputText(list->currentItem()->text());
}

bool TerminalTab::searchText(const QString& str, bool next, bool caseSensitive) {
    if (!m_terminal || str.isEmpty())
        return false;

    bool found = false;
    const QMetaObject::Connection connection = connect(
        m_terminal, &QTermWidget::searchResult, this, [&found](bool result) { found = result; }, Qt::DirectConnection);
    m_terminal->searchText(str, next, next, caseSensitive);
    disconnect(connection);
    return found;
}

void TerminalTab::copySelection() {
    doCopy();
}

void TerminalTab::pasteSelection() {
    doPaste();
}

void TerminalTab::clearTerminal() {
    doClear();
}

QWidget* TerminalTab::terminalView() const {
    return m_terminal;
}

bool TerminalTab::hasSelection() const {
    return m_terminal && !m_terminal->selectedText().isEmpty();
}

void TerminalTab::feedTerminalData(const QByteArray& data) {
    if (!data.isEmpty()) {
        m_outputBuffer.append(data);
        emit terminalDataReceived(data);
        if (m_promptPattern.isValid() && !m_promptPattern.pattern().isEmpty()) {
            m_promptBuffer.append(data);
            if (m_promptBuffer.size() > 8192)
                m_promptBuffer.remove(0, m_promptBuffer.size() - 4096);
            const QString output = QString::fromUtf8(m_promptBuffer);
            const QRegularExpressionMatch match = m_promptPattern.match(output);
            if (match.hasMatch()) {
                const QString prompt = match.captured(0).trimmed();
                if (!prompt.isEmpty() && prompt != m_lastDetectedPrompt) {
                    m_lastDetectedPrompt = prompt;
                    emit promptDetected(prompt);
                }
            } else {
                m_lastDetectedPrompt.clear();
            }
        }
    }
    if (m_terminal) {
        m_terminal->feedData(data);
    }
}

void TerminalTab::reportMacroMessage(const QString& message) {
    feedTerminalData(QByteArrayLiteral("\r\n[Macro: ") + message.toUtf8() + QByteArrayLiteral("]\r\n"));
}

void TerminalTab::doSendText(const QString& text) {
    if (m_terminal)
        m_terminal->sendText(text);
}

void TerminalTab::doToggleSearchBar() {
    if (m_terminal)
        m_terminal->toggleShowSearchBar();
}

void TerminalTab::doFocusTerminal() {
    if (m_terminal)
        m_terminal->setFocus();
}

void TerminalTab::doCopy() {
    if (m_terminal)
        m_terminal->copyClipboard();
}

void TerminalTab::doPaste() {
    if (m_session.readOnly) {
        reportMacroMessage(tr("Session is read-only; paste was blocked."));
        return;
    }
    if (m_terminal)
        m_terminal->pasteClipboard();
}

void TerminalTab::doClear() {
    if (m_terminal)
        m_terminal->clear();
}

void TerminalTab::doZoomIn() {
    if (m_terminal)
        m_terminal->zoomIn();
}

void TerminalTab::doZoomOut() {
    if (m_terminal)
        m_terminal->zoomOut();
}
