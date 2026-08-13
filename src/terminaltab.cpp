#include "terminaltab.h"
#include "sshconnection.h"
#include "vtterminalwidget.h"
#include "keyring.h"
#ifndef Q_OS_WIN
#include <qtermwidget.h>
#endif
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
#include <io.h>
#else
#include <unistd.h>
#include <fcntl.h>
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
#ifdef Q_OS_WIN
        setupWindowsTerminal();
#else
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

        m_terminal->setHistorySize(5000);
        m_terminal->setScrollBarPosition(QTermWidget::ScrollBarRight);

        m_terminal->setContextMenuPolicy(Qt::CustomContextMenu);
        connect(m_terminal, &QWidget::customContextMenuRequested, this, &TerminalTab::showTerminalContextMenu);
        connect(m_terminal, &QTermWidget::currentDirectoryChanged, this, &TerminalTab::onRemoteDirChanged);

        layout->addWidget(m_terminal);

        connect(m_terminal, &QTermWidget::finished, this, &TerminalTab::onTerminalFinished);
        connect(m_terminal, &QTermWidget::titleChanged, this, &TerminalTab::onTitleChanged);

        if (m_session.type == SessionType::SSH) {
            setupSshTerminal();
        } else if (m_session.type == SessionType::Telnet) {
#ifdef Q_OS_WIN
            m_terminal->setShellProgram("telnet");
#else
            m_terminal->setShellProgram("/usr/bin/telnet");
#endif
            QStringList args;
            args << m_session.host << QString::number(m_session.port);
            m_terminal->setArgs(args);
            m_terminal->startShellProgram();
        } else if (m_session.type == SessionType::Serial) {
#ifdef Q_OS_WIN
            m_terminal->setShellProgram("cmd.exe");
            QStringList args;
            args << "/c" << "echo" << tr("Serial connections are not supported on Windows.");
            m_terminal->setArgs(args);
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
#endif
            m_terminal->startShellProgram();
        } else {
            QString shell = m_session.shellPath;
            if (shell.isEmpty()) {
#ifdef Q_OS_WIN
                shell = qEnvironmentVariable("COMSPEC");
                if (shell.isEmpty())
                    shell = "cmd.exe";
#else
                shell = qgetenv("SHELL");
                if (shell.isEmpty()) {
                    shell = "/bin/bash";
                }
#endif
            }
            m_terminal->setShellProgram(shell);
            m_terminal->startShellProgram();
        }
#endif
    }

    // Construir la barra de búsqueda (Ctrl+F)
    m_searchFrame = new QFrame(this);
    m_searchFrame->setFrameShape(QFrame::StyledPanel);
    m_searchFrame->setStyleSheet("QFrame { background-color: #1a1b26; border-top: 1px solid #414868; }");
    m_searchFrame->hide();

    auto* searchLayout = new QHBoxLayout(m_searchFrame);
    searchLayout->setContentsMargins(10, 4, 10, 4);
    searchLayout->setSpacing(8);

    auto* searchLabel = new QLabel(tr("Search:"), m_searchFrame);
    searchLabel->setStyleSheet("color: #a9b1d6; font-weight: bold;");

    m_searchEdit = new QLineEdit(m_searchFrame);
    m_searchEdit->setStyleSheet("QLineEdit { background-color: #16161e; color: #c0caf5; border: 1px solid #414868; padding: 4px; border-radius: 4px; }");
    m_searchEdit->setPlaceholderText(tr("Find text..."));

    m_btnPrev = new QPushButton(tr("Previous"), m_searchFrame);
    m_btnPrev->setStyleSheet("QPushButton { background-color: #24283b; color: #a9b1d6; border: 1px solid #414868; padding: 4px 8px; border-radius: 4px; } QPushButton:hover { background-color: #414868; }");

    m_btnNext = new QPushButton(tr("Next"), m_searchFrame);
    m_btnNext->setStyleSheet("QPushButton { background-color: #24283b; color: #a9b1d6; border: 1px solid #414868; padding: 4px 8px; border-radius: 4px; } QPushButton:hover { background-color: #414868; }");

    m_caseSensitiveCheck = new QCheckBox(tr("Case Sensitive"), m_searchFrame);
    m_caseSensitiveCheck->setStyleSheet("QCheckBox { color: #a9b1d6; }");

    auto* closeBtn = new QPushButton("X", m_searchFrame);
    closeBtn->setFlat(true);
    closeBtn->setStyleSheet("QPushButton { color: #f7768e; font-weight: bold; } QPushButton:hover { color: #ff8998; }");

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
    connect(searchShortcut, &QShortcut::activated, this, [this]() {
#ifndef Q_OS_WIN
        if (m_terminal) {
            m_terminal->toggleShowSearchBar();
        } else
#endif
        if (m_vtTerminal) {
            showSearchFrame();
        }
    });

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

    if (m_flushTimer) {
        m_flushTimer->stop();
    }
    m_writeBuffer.clear();

    if (m_logFile) {
        m_logFile->close();
        delete m_logFile;
        m_logFile = nullptr;
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
#ifndef Q_OS_WIN
    if (m_connection && m_terminal && m_session.type == SessionType::SSH) {
        int rows = m_terminal->screenLinesCount();
        int cols = m_terminal->screenColumnsCount();
        if (rows > 0 && cols > 0) {
            QMetaObject::invokeMethod(m_connection, "resizePty", Qt::QueuedConnection, Q_ARG(int, rows),
                                      Q_ARG(int, cols));
        }
    }
#endif
}

#ifdef Q_OS_WIN
void TerminalTab::setupWindowsTerminal() {
    m_vtTerminal = new VtTerminalWidget(this);
    layout()->addWidget(m_vtTerminal);

    QTimer::singleShot(0, this, &TerminalTab::updateFontFromSettings);

    connect(m_vtTerminal, &VtTerminalWidget::workingDirectoryChanged, this, &TerminalTab::onRemoteDirChanged);

    connect(m_vtTerminal, &VtTerminalWidget::titleChanged, this,
            [this](const QString& title) { emit titleChanged(title); });
    connect(m_vtTerminal, &VtTerminalWidget::finished, this, &TerminalTab::onTerminalFinished);

    if (m_session.type == SessionType::SSH) {
        connect(m_vtTerminal, &VtTerminalWidget::dataReady, this, [this](const QByteArray& data) {
            if (m_connection) {
                QMetaObject::invokeMethod(m_connection, "sendToShell", Qt::QueuedConnection, Q_ARG(QByteArray, data));
            }
        });
        connect(m_vtTerminal, &VtTerminalWidget::resized, this, [this](int cols, int rows) {
            if (m_connection) {
                QMetaObject::invokeMethod(m_connection, "resizePty", Qt::QueuedConnection, Q_ARG(int, rows),
                                          Q_ARG(int, cols));
            }
        });

        m_connection = new SshConnection();
        m_connectionThread = new QThread(this);
        m_connection->moveToThread(m_connectionThread);
        m_connectionThread->start();

        connect(m_connection, &SshConnection::shellDataReceived, this, [this](const QByteArray& data) {
            if (m_vtTerminal)
                m_vtTerminal->writeData(data);
            logData(data);
        });
        connect(m_connection, &SshConnection::shellClosed, this, [this]() {
            m_isActive = false;
            if (m_vtTerminal)
                m_vtTerminal->writeData(tr("\r\n[Connection closed]\r\n").toUtf8());
            emit titleChanged(tr("[Closed] %1").arg(m_session.name));
            maybeScheduleReconnect();
        });
        connect(m_connection, &SshConnection::connectionFailed, this, [this](const QString& error) {
            if (m_vtTerminal)
                m_vtTerminal->writeData(tr("\r\n[Connection failed: %1]\r\n").arg(error).toUtf8());
            m_isActive = false;
            emit titleChanged(tr("[Closed] %1").arg(m_session.name));
            maybeScheduleReconnect();
        });

        if (m_session.x11Forwarding) {
            QMetaObject::invokeMethod(m_connection, "setX11Forwarding", Qt::QueuedConnection, Q_ARG(bool, true));
        }
    } else if (m_session.type == SessionType::Telnet) {
        m_conpty = new ConPty();
        m_conpty->start("telnet", {m_session.host, QString::number(m_session.port)}, 80, 24);
        startConPtyPolling();
    } else if (m_session.type == SessionType::Serial) {
        m_vtTerminal->writeData(tr("Serial connections are not supported on Windows.\r\n").toUtf8());
        m_isActive = false;
    } else {
        QString shell = m_session.shellPath;
#ifdef Q_OS_WIN
        // Ignore POSIX-style paths saved from Linux (e.g. /bin/bash) on Windows
        if (shell.isEmpty() || shell.startsWith('/')) {
            shell = qEnvironmentVariable("COMSPEC");
            if (shell.isEmpty())
                shell = "cmd.exe";
        }
#endif
        if (shell.isEmpty())
            shell = "cmd.exe";

        // Defer ConPTY start until the widget has its real size.
        // This avoids cmd.exe going through a resize/reinit cycle.
        // The 'resized' signal fires from VtTerminalWidget::resizeEvent
        // the first time it is laid out and shown.
        auto* conn = new QMetaObject::Connection();
        *conn = connect(m_vtTerminal, &VtTerminalWidget::resized, this,
                        [this, shell, conn](int cols, int rows) {
                            // Disconnect so this fires only once
                            disconnect(*conn);
                            delete conn;

                            m_conpty = new ConPty();
                            if (!m_conpty->start(shell, {}, cols, rows)) {
                                DWORD err = m_conpty->startError();
                                m_vtTerminal->writeData(
                                    tr("\r\n[Failed to start '%1' (error 0x%2)]\r\n")
                                        .arg(shell)
                                        .arg(err, 8, 16, QChar('0'))
                                        .toUtf8());
                                m_isActive = false;
                                emit titleChanged(tr("[Closed] %1").arg(m_session.name));
                                return;
                            }
                            startConPtyPolling();
                        });
    }
}

void TerminalTab::startConPtyPolling() {
    if (!m_conpty || !m_vtTerminal)
        return;

    connect(m_vtTerminal, &VtTerminalWidget::dataReady, this, [this](const QByteArray& data) {
        if (m_conpty)
            m_conpty->write(data);
    });
    connect(m_vtTerminal, &VtTerminalWidget::resized, this, [this](int cols, int rows) {
        if (m_conpty)
            m_conpty->resize(cols, rows);
    });

    m_conptyPollTimer = new QTimer(this);
    connect(m_conptyPollTimer, &QTimer::timeout, this, &TerminalTab::pollConPtyOutput);
    m_conptyPollTimer->start(10);
}

void TerminalTab::pollConPtyOutput() {
    if (!m_conpty || !m_vtTerminal)
        return;

    QByteArray data = m_conpty->read();
    if (!data.isEmpty()) {
        m_vtTerminal->writeData(data);
        logData(data);
    }

    if (!m_conpty->isRunning()) {
        DWORD exitCode = m_conpty->exitCode();
        m_conptyPollTimer->stop();
        m_isActive = false;
        m_vtTerminal->writeData(
            tr("\r\n[Process exited with code %1]\r\n").arg(exitCode).toUtf8());
        emit titleChanged(tr("[Closed] %1").arg(m_session.name));
    }
}

#endif

#ifndef Q_OS_WIN
void TerminalTab::setupSshTerminal() {
    m_terminal->startTerminalTeletype();

    // Make the pty slave fd non-blocking so a burst of remote output (btop,
    // git progress, etc.) never blocks the GUI thread. The emulator drains the
    // same pty on this thread, so a blocking write would deadlock it.
    int fd = m_terminal->getPtySlaveFd();
    if (fd >= 0) {
        int flags = fcntl(fd, F_GETFL, 0);
        fcntl(fd, F_SETFL, flags | O_NONBLOCK);
    }

    m_flushTimer = new QTimer(this);
    connect(m_flushTimer, &QTimer::timeout, this, &TerminalTab::flushWriteBuffer);

    connect(m_terminal, &QTermWidget::sendData, this, &TerminalTab::onSendData);

    m_connection = new SshConnection();
    m_connectionThread = new QThread(this);
    m_connection->moveToThread(m_connectionThread);
    m_connectionThread->start();

    connect(m_connection, &SshConnection::shellDataReceived, this, &TerminalTab::onShellDataReceived);
        connect(m_connection, &SshConnection::shellClosed, this, &TerminalTab::onShellClosed);
        connect(m_connection, &SshConnection::connectionFailed, this, [this](const QString& error) {
            if (m_terminal) {
                QString text = tr("\r\n[Connection failed: %1]\r\n").arg(error);
                int fd = m_terminal->getPtySlaveFd();
                if (fd >= 0) {
                    ::write(fd, text.toUtf8().constData(), static_cast<size_t>(text.size()));
                }
            }
            m_isActive = false;
            emit titleChanged(tr("[Closed] %1").arg(m_session.name));
            maybeScheduleReconnect();
        });

    // The connection itself is triggered by SftpSidebar::startSession(), which
    // shares this same SshConnection with the terminal.
    if (m_session.x11Forwarding) {
        QMetaObject::invokeMethod(m_connection, "setX11Forwarding", Qt::QueuedConnection, Q_ARG(bool, true));
    }
}

void TerminalTab::onSendData(const char* data, int size) {
    if (m_connection) {
        QMetaObject::invokeMethod(m_connection, "sendToShell", Qt::QueuedConnection,
                                  Q_ARG(QByteArray, QByteArray(data, size)));
    }
}

void TerminalTab::onShellDataReceived(const QByteArray& data) {
    if (!m_terminal)
        return;
    m_writeBuffer.append(data);
    logData(data);
    flushWriteBuffer();
    if (!m_writeBuffer.isEmpty() && m_flushTimer) {
        m_flushTimer->start(5);
    }
}

void TerminalTab::flushWriteBuffer() {
    if (m_writeBuffer.isEmpty() || !m_terminal)
        return;
    int fd = m_terminal->getPtySlaveFd();
    if (fd < 0) {
        m_writeBuffer.clear();
        return;
    }
    ssize_t written = ::write(fd, m_writeBuffer.constData(), static_cast<size_t>(m_writeBuffer.size()));
    if (written > 0) {
        m_writeBuffer.remove(0, static_cast<int>(written));
    }
    if (m_writeBuffer.isEmpty() && m_flushTimer) {
        m_flushTimer->stop();
    }
}

void TerminalTab::onShellClosed() {
    m_isActive = false;
    if (m_terminal) {
        int fd = m_terminal->getPtySlaveFd();
        if (fd >= 0) {
            QString text = tr("\r\n[Connection closed]\r\n");
            ::write(fd, text.toUtf8().constData(), static_cast<size_t>(text.size()));
        }
    }
    emit titleChanged(tr("[Closed] %1").arg(m_session.name));
    maybeScheduleReconnect();
}

void TerminalTab::onTitleChanged() {
    if (m_terminal)
        emit titleChanged(m_terminal->title());
}

void TerminalTab::showTerminalContextMenu(const QPoint& pos) {
    if (!m_terminal)
        return;

    QMenu menu(this);

    auto* copyAct = menu.addAction(tr("&Copy"));
    copyAct->setShortcut(QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_C));
    copyAct->setEnabled(m_terminal->selectedText().isEmpty() == false);

    auto* pasteAct = menu.addAction(tr("&Paste"));
    pasteAct->setShortcut(QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_V));

    menu.addSeparator();

    auto* clearAct = menu.addAction(tr("Clear Scrollback"));
    auto* zoomInAct = menu.addAction(tr("Zoom &In"));
    zoomInAct->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_Plus));
    auto* zoomOutAct = menu.addAction(tr("Zoom &Out"));
    zoomOutAct->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_Minus));

    auto* selected = menu.exec(m_terminal->mapToGlobal(pos));
    if (selected == copyAct) {
        m_terminal->copyClipboard();
    } else if (selected == pasteAct) {
        m_terminal->pasteClipboard();
    } else if (selected == clearAct) {
        m_terminal->clear();
    } else if (selected == zoomInAct) {
        m_terminal->zoomIn();
    } else if (selected == zoomOutAct) {
        m_terminal->zoomOut();
    }
}
#endif

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
    if (settings.contains("terminal/font")) {
        font.fromString(settings.value("terminal/font").toString());
    } else {
        font = QFont("Monospace", 11);
        font.setStyleHint(QFont::Monospace);
    }
    font.setFixedPitch(true);

#ifndef Q_OS_WIN
    if (m_terminal) {
        m_terminal->setTerminalFont(font);

        QString colorScheme = settings.value("terminal/colorScheme", "DarkPastels").toString();
        QStringList schemes = QTermWidget::availableColorSchemes();
        if (schemes.contains(colorScheme)) {
            m_terminal->setColorScheme(colorScheme);
        } else if (schemes.contains("DarkPastels")) {
            m_terminal->setColorScheme("DarkPastels");
        }
    }
#else
    if (m_vtTerminal) {
        m_vtTerminal->setTerminalFont(font);
        QString colorScheme = settings.value("terminal/colorScheme", "DarkPastels").toString();
        m_vtTerminal->setColorScheme(colorScheme);
    }
#endif
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
        args << "/v:" + m_session.host + ":" + QString::number(m_session.port)
             << "/parent:" + QString::number(winId);
#else
        program = "xfreerdp";
        args << "/v:" + m_session.host + ":" + QString::number(m_session.port)
             << "/parent-window:" + QString::number(winId)
             << "/cert:ignore"
             << "/dynamic-resolution"
             << "+decoration";
        if (!m_session.user.isEmpty()) {
            args << "/u:" + m_session.user;
        }
#endif
    } else if (m_session.type == SessionType::VNC) {
        program = "vncviewer";
        args << m_session.host + "::" + QString::number(m_session.port)
             << "-parentwindow" << QString::number(winId);
    }

    if (program.isEmpty())
        return;

    m_externalProcess = new QProcess(this);
    m_externalProcess->setProcessChannelMode(QProcess::ForwardedChannels);

    connect(m_externalProcess, &QProcess::started, this, [this]() {
        if (m_statusLabel) m_statusLabel->hide();
        if (m_embeddedContainer) m_embeddedContainer->show();
    });

    connect(m_externalProcess, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished), this,
            [this](int exitCode, QProcess::ExitStatus) {
                Q_UNUSED(exitCode);
                m_isActive = false;
                if (m_embeddedContainer) m_embeddedContainer->hide();
                if (m_statusLabel) {
                    m_statusLabel->show();
                    m_statusLabel->setText(tr("Session closed. Close this tab to continue."));
                }
                emit titleChanged(tr("[Closed] %1").arg(m_session.name));
            });

    connect(m_externalProcess, &QProcess::errorOccurred, this, [this](QProcess::ProcessError error) {
        Q_UNUSED(error);
        if (m_embeddedContainer) m_embeddedContainer->hide();
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
#ifndef Q_OS_WIN
        if (m_terminal) {
            m_terminal->setFocus();
        } else
#endif
        if (m_vtTerminal) {
            m_vtTerminal->setFocus();
        }
    }
}

void TerminalTab::onSearchNext() {
    QString text = m_searchEdit->text();
    if (text.isEmpty())
        return;
    bool cs = m_caseSensitiveCheck->isChecked();
    if (m_vtTerminal) {
        m_vtTerminal->findText(text, true, cs);
    }
}

void TerminalTab::onSearchPrev() {
    QString text = m_searchEdit->text();
    if (text.isEmpty())
        return;
    bool cs = m_caseSensitiveCheck->isChecked();
    if (m_vtTerminal) {
        m_vtTerminal->findText(text, false, cs);
    }
}

void TerminalTab::sendInputText(const QString& text) {
#ifndef Q_OS_WIN
    if (m_terminal) {
        m_terminal->sendText(text + "\n");
        return;
    }
#endif
    if (m_vtTerminal) {
        m_vtTerminal->writeData((text + "\n").toUtf8());
    }
}

void TerminalTab::sendRaw(const QString& text) {
#ifndef Q_OS_WIN
    if (m_terminal) {
        m_terminal->sendText(text);
        return;
    }
#endif
    if (m_vtTerminal) {
        m_vtTerminal->writeData(text.toUtf8());
    }
}

bool TerminalTab::searchText(const QString& str, bool next, bool caseSensitive) {
    if (str.isEmpty())
        return false;
#ifdef Q_OS_WIN
    if (m_vtTerminal)
        return m_vtTerminal->findText(str, next, caseSensitive);
#else
    Q_UNUSED(str);
    Q_UNUSED(next);
    Q_UNUSED(caseSensitive);
#endif
    return false;
}

void TerminalTab::copySelection() {
#ifndef Q_OS_WIN
    if (m_terminal) {
        m_terminal->copyClipboard();
        return;
    }
#endif
    if (m_vtTerminal) {
        m_vtTerminal->copyClipboard();
    }
}

void TerminalTab::pasteSelection() {
#ifndef Q_OS_WIN
    if (m_terminal) {
        m_terminal->pasteClipboard();
        return;
    }
#endif
    if (m_vtTerminal) {
        m_vtTerminal->pasteClipboard();
    }
}

void TerminalTab::clearTerminal() {
#ifndef Q_OS_WIN
    if (m_terminal) {
        m_terminal->clear();
        return;
    }
#endif
}
