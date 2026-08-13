#include "terminaltab.h"
#include "sshconnection.h"
#include "vtterminalwidget.h"
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

#ifdef Q_OS_WIN
#include <io.h>
#else
#include <unistd.h>
#include <fcntl.h>
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

        QTimer::singleShot(100, this, &TerminalTab::launchExternalClient);
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
        if (m_terminal) {
            m_terminal->toggleShowSearchBar();
        } else if (m_vtTerminal) {
            showSearchFrame();
        }
    });

    auto* closeSearchShortcut = new QShortcut(QKeySequence(Qt::Key_Escape), this);
    connect(closeSearchShortcut, &QShortcut::activated, this, [this]() {
        if (m_searchFrame && m_searchFrame->isVisible()) {
            hideSearchFrame();
        }
    });
}

TerminalTab::~TerminalTab() {
    closeExternalProcess();

    if (m_flushTimer) {
        m_flushTimer->stop();
    }
    m_writeBuffer.clear();

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
    if (m_connection && m_terminal && m_session.type == SessionType::SSH) {
        int rows = m_terminal->screenLinesCount();
        int cols = m_terminal->screenColumnsCount();
        if (rows > 0 && cols > 0) {
            QMetaObject::invokeMethod(m_connection, "resizePty", Qt::QueuedConnection, Q_ARG(int, rows),
                                      Q_ARG(int, cols));
        }
    }
}

#ifdef Q_OS_WIN
void TerminalTab::setupWindowsTerminal() {
    m_vtTerminal = new VtTerminalWidget(this);
    layout()->addWidget(m_vtTerminal);

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
        connect(m_vtTerminal, &VtTerminalWidget::resized, this, [this](int rows, int cols) {
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
        });
        connect(m_connection, &SshConnection::shellClosed, this, [this]() {
            m_isActive = false;
            if (m_vtTerminal)
                m_vtTerminal->writeData(tr("\r\n[Connection closed]\r\n").toUtf8());
            emit titleChanged(tr("[Closed] %1").arg(m_session.name));
        });
        connect(m_connection, &SshConnection::connectionFailed, this, [this](const QString& error) {
            if (m_vtTerminal)
                m_vtTerminal->writeData(tr("\r\n[Connection failed: %1]\r\n").arg(error).toUtf8());
            m_isActive = false;
            emit titleChanged(tr("[Closed] %1").arg(m_session.name));
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
        if (shell.isEmpty())
            shell = "cmd.exe";
        m_conpty = new ConPty();
        m_conpty->start(shell, {}, 80, 24);
        startConPtyPolling();
    }
}

void TerminalTab::startConPtyPolling() {
    if (!m_conpty || !m_vtTerminal)
        return;

    connect(m_vtTerminal, &VtTerminalWidget::dataReady, this, [this](const QByteArray& data) {
        if (m_conpty)
            m_conpty->write(data);
    });
    connect(m_vtTerminal, &VtTerminalWidget::resized, this, [this](int rows, int cols) {
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
    if (!data.isEmpty())
        m_vtTerminal->writeData(data);

    if (!m_conpty->isRunning()) {
        m_conptyPollTimer->stop();
        m_isActive = false;
        emit titleChanged(tr("[Closed] %1").arg(m_session.name));
    }
}
#endif

void TerminalTab::setupSshTerminal() {
    m_terminal->startTerminalTeletype();

    // Make the pty slave fd non-blocking so a burst of remote output (btop,
    // git progress, etc.) never blocks the GUI thread. The emulator drains the
    // same pty on this thread, so a blocking write would deadlock it.
#ifndef Q_OS_WIN
    int fd = m_terminal->getPtySlaveFd();
    if (fd >= 0) {
        int flags = fcntl(fd, F_GETFL, 0);
        fcntl(fd, F_SETFL, flags | O_NONBLOCK);
    }
#endif
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
#ifdef Q_OS_WIN
                _write(fd, text.toUtf8().constData(), static_cast<unsigned int>(text.size()));
#else
                ::write(fd, text.toUtf8().constData(), static_cast<size_t>(text.size()));
#endif
            }
        }
        m_isActive = false;
        emit titleChanged(tr("[Closed] %1").arg(m_session.name));
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
#ifdef Q_OS_WIN
    int written = _write(fd, m_writeBuffer.constData(), static_cast<unsigned int>(m_writeBuffer.size()));
#else
    ssize_t written = ::write(fd, m_writeBuffer.constData(), static_cast<size_t>(m_writeBuffer.size()));
#endif
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
#ifdef Q_OS_WIN
            _write(fd, text.toUtf8().constData(), static_cast<unsigned int>(text.size()));
#else
            ::write(fd, text.toUtf8().constData(), static_cast<size_t>(text.size()));
#endif
        }
    }
    emit titleChanged(tr("[Closed] %1").arg(m_session.name));
}

void TerminalTab::onTerminalFinished() {
    m_isActive = false;
    emit titleChanged(tr("[Closed] %1").arg(m_session.name));
}

void TerminalTab::onTitleChanged() {
    if (m_terminal)
        emit titleChanged(m_terminal->title());
}

void TerminalTab::onRemoteDirChanged(const QString& dir) {
    emit remoteDirChanged(dir);
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

void TerminalTab::updateFontFromSettings() {
    if (!m_terminal)
        return;

    QSettings settings;

    QFont font;
    if (settings.contains("terminal/font")) {
        font.fromString(settings.value("terminal/font").toString());
    } else {
        font = QFont("Monospace", 11);
        font.setStyleHint(QFont::Monospace);
    }
    font.setFixedPitch(true);
    m_terminal->setTerminalFont(font);

    QString colorScheme = settings.value("terminal/colorScheme", "DarkPastels").toString();
    QStringList schemes = QTermWidget::availableColorSchemes();
    if (schemes.contains(colorScheme)) {
        m_terminal->setColorScheme(colorScheme);
    } else if (schemes.contains("DarkPastels")) {
        m_terminal->setColorScheme("DarkPastels");
    }
}

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
        if (m_terminal) {
            m_terminal->setFocus();
        } else if (m_vtTerminal) {
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
