#include "terminaltab.h"
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
#include <QDateTime>
#include <QDir>
#include <QStandardPaths>
#include <QRegularExpression>

#ifdef Q_OS_WIN
#include "conpty.h"
#include <QSerialPort>
#endif

#ifdef BANCHO_HAVE_RDP_AX
#include <QAxWidget>
#endif

#ifdef BANCHO_HAVE_VNC
#include "vncclientwidget.h"
#endif

TerminalTab::TerminalTab(const Session& session, QWidget* parent) : QWidget(parent), m_session(session) {
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

        m_terminal->setContextMenuPolicy(Qt::CustomContextMenu);
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
#ifdef Q_OS_WIN
            m_terminal->startExternal();
            m_serialPort = new QSerialPort(this);
            m_serialPort->setPortName(m_session.serialPort);
            m_serialPort->setBaudRate(m_session.baudRate);
            m_serialPort->setDataBits(QSerialPort::Data8);
            m_serialPort->setParity(QSerialPort::NoParity);
            m_serialPort->setStopBits(QSerialPort::OneStop);
            m_serialPort->setFlowControl(QSerialPort::NoFlowControl);
            connect(m_serialPort, &QSerialPort::readyRead, this, [this]() {
                const QByteArray data = m_serialPort->readAll();
                if (!data.isEmpty()) {
                    feedTerminalData(data);
                    logData(data);
                }
            });
            connect(m_serialPort, &QSerialPort::errorOccurred, this, [this](QSerialPort::SerialPortError error) {
                if (error == QSerialPort::NoError)
                    return;
                feedTerminalData(tr("\r\n[Serial error: %1]\r\n").arg(m_serialPort->errorString()).toUtf8());
                m_isActive = false;
                emit titleChanged(tr("[Closed] %1").arg(m_session.name));
            });
            if (!m_serialPort->open(QIODevice::ReadWrite)) {
                feedTerminalData(
                    tr("\r\n[Unable to open serial port: %1]\r\n").arg(m_serialPort->errorString()).toUtf8());
                m_isActive = false;
                emit titleChanged(tr("[Closed] %1").arg(m_session.name));
            }
#else
            QString tool = m_session.serialCmd;
            if (tool.isEmpty())
                tool = "picocom";

            m_terminal->setShellProgram("/usr/bin/" + tool);

            QStringList args;
            if (tool == "picocom") {
                args << "-b" << QString::number(m_session.baudRate) << m_session.serialPort;
            } else if (tool == "screen") {
                args << m_session.serialPort << QString::number(m_session.baudRate);
            } else if (tool == "minicom") {
                args << "-D" << m_session.serialPort << "-b" << QString::number(m_session.baudRate);
            }
            m_terminal->setArgs(args);
            m_terminal->startShellProgram();
#endif
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

#ifdef Q_OS_WIN
    if (m_serialPort) {
        disconnect(m_serialPort, nullptr, this, nullptr);
        if (m_serialPort->isOpen())
            m_serialPort->close();
        delete m_serialPort;
        m_serialPort = nullptr;
    }

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
    m_terminal->startExternal();

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
        maybeScheduleReconnect();
    });
    connect(m_connection, &SshConnection::connectionFailed, this, [this](const QString& error) {
        feedTerminalData(tr("\r\n[Connection failed: %1]\r\n").arg(error).toUtf8());
        m_isActive = false;
        emit titleChanged(tr("[Closed] %1").arg(m_session.name));
        maybeScheduleReconnect();
    });

    if (m_session.x11Forwarding) {
        QMetaObject::invokeMethod(m_connection, "setX11Forwarding", Qt::QueuedConnection, Q_ARG(bool, true));
    }
    applySshOptions();
}

void TerminalTab::onSendData(const char* data, int size) {
    if (m_connection && m_session.type == SessionType::SSH) {
        QMetaObject::invokeMethod(m_connection, "sendToShell", Qt::QueuedConnection,
                                  Q_ARG(QByteArray, QByteArray(data, size)));
    }
#ifdef Q_OS_WIN
    else if (m_serialPort && m_serialPort->isOpen() && m_session.type == SessionType::Serial) {
        m_serialPort->write(data, size);
    } else if (m_conpty) {
        m_conpty->write(QByteArray(data, size));
    }
#endif
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

    auto* clearAct = menu.addAction(tr("Clear Scrollback"));
    auto* zoomInAct = menu.addAction(tr("Zoom &In"));
    zoomInAct->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_Plus));
    auto* zoomOutAct = menu.addAction(tr("Zoom &Out"));
    zoomOutAct->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_Minus));

    auto* selected = menu.exec(view->mapToGlobal(pos));
    if (selected == copyAct) {
        doCopy();
    } else if (selected == pasteAct) {
        doPaste();
    } else if (selected == clearAct) {
        doClear();
    } else if (selected == zoomInAct) {
        doZoomIn();
    } else if (selected == zoomOutAct) {
        doZoomOut();
    }
}

void TerminalTab::onTerminalFinished() {
    m_isActive = false;
    emit titleChanged(tr("[Closed] %1").arg(m_session.name));
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

    const QString colorScheme = settings.value("terminal/colorScheme", "DarkPastels").toString();

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
    if (!m_session.autoReconnect || m_reconnectTimer)
        return;
    m_reconnectTimer = new QTimer(this);
    m_reconnectTimer->setSingleShot(true);
    m_reconnectTimer->setInterval(3000);
    connect(m_reconnectTimer, &QTimer::timeout, this, [this]() { emit reconnectRequested(m_session); });
    m_reconnectTimer->start();
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
            emit titleChanged(tr("[Closed] %1").arg(m_session.name));
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
        emit titleChanged(tr("[Closed] %1").arg(m_session.name));
    });
    connect(m_vncWidget, &VncClientWidget::errorOccurred, this, [this](const QString& msg) {
        m_isActive = false;
        if (m_embeddedContainer)
            m_embeddedContainer->hide();
        if (m_statusLabel) {
            m_statusLabel->show();
            m_statusLabel->setText(tr("VNC error: %1").arg(msg));
        }
        emit titleChanged(tr("[Closed] %1").arg(m_session.name));
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
        args << "/v:" + m_session.host + ":" + QString::number(m_session.port)
             << "/parent-window:" + QString::number(winId) << "/cert:ignore"
             << "/dynamic-resolution"
             << "+decoration";
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
                emit titleChanged(tr("[Closed] %1").arg(m_session.name));
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

void TerminalTab::sendInputText(const QString& text) {
    doSendText(text + "\n");
}

void TerminalTab::sendRaw(const QString& text) {
    doSendText(text);
}

bool TerminalTab::searchText(const QString& str, bool next, bool caseSensitive) {
    // QTermWidget has no programmatic search API; global search is not supported.
    Q_UNUSED(str);
    Q_UNUSED(next);
    Q_UNUSED(caseSensitive);
    return false;
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
    if (m_terminal) {
        m_terminal->feedData(data);
    }
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
