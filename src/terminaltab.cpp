#include "terminaltab.h"
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
#include "keyring.h"

namespace {
// Remote shell integration: reports the current working directory back to the
// terminal via OSC 7 so the SFTP browser can follow terminal navigation.
const char* kShellIntegration = "if command -v bash >/dev/null 2>&1; then "
                                "PROMPT_COMMAND='printf \"\\033]7;file://%s%s\\007\" \"$HOSTNAME\" \"$PWD\"'; "
                                "exec bash -l; else exec \"$SHELL\"; fi";
} // namespace

TerminalTab::TerminalTab(const Session& session, QWidget* parent) : QWidget(parent), m_session(session) {
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);

    if (session.type == SessionType::RDP || session.type == SessionType::VNC) {
        m_statusLabel = new QLabel(this);
        m_statusLabel->setAlignment(Qt::AlignCenter);
        m_statusLabel->setWordWrap(true);

        QString proto = session.type == SessionType::RDP ? "RDP" : "VNC";
        m_statusLabel->setText(tr("%1 session to %2 launched in an external window.\n\nClose this tab when done.")
                                   .arg(proto, session.host));

        QFont statusFont = m_statusLabel->font();
        statusFont.setPointSize(12);
        m_statusLabel->setFont(statusFont);

        layout->addWidget(m_statusLabel);

        QTimer::singleShot(100, this, &TerminalTab::launchExternalClient);
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

        m_terminal->setHistorySize(5000);
        m_terminal->setScrollBarPosition(QTermWidget::ScrollBarRight);

        m_terminal->setContextMenuPolicy(Qt::CustomContextMenu);
        connect(m_terminal, &QWidget::customContextMenuRequested, this, &TerminalTab::showTerminalContextMenu);
        connect(m_terminal, &QTermWidget::currentDirectoryChanged, this, &TerminalTab::onRemoteDirChanged);

        if (m_session.type == SessionType::SSH) {
#ifdef Q_OS_WIN
            m_terminal->setShellProgram("ssh");
#else
            m_terminal->setShellProgram("/usr/bin/ssh");
#endif

            QStringList args;
            args << "-t" << "-p" << QString::number(m_session.port);

            if (m_session.x11Forwarding) {
                args << "-Y";
            }

            if (!m_session.keyPath.isEmpty()) {
                args << "-i" << m_session.keyPath;
            }

            args << QString("%1@%2").arg(m_session.user, m_session.host);
            args << QString::fromLatin1(kShellIntegration);
            m_terminal->setArgs(args);

            QStringList env = QProcess::systemEnvironment();
            bool hasTerm = false;
            for (const QString& e : env) {
                if (e.startsWith("TERM=")) {
                    hasTerm = true;
                    break;
                }
            }
            if (!hasTerm) {
                env << "TERM=xterm-256color";
            }

            QString password = Keyring::lookupPassword(m_session.id);
            if (!password.isEmpty()) {
                env << QString("SSH_ASKPASS=%1").arg(QCoreApplication::applicationFilePath());
                env << "SSH_ASKPASS_REQUIRE=force";
                env << QString("BANCHOXTERM_ASKPASS_ID=%1").arg(m_session.id);
            }

            m_terminal->setEnvironment(env);
        } else if (m_session.type == SessionType::Telnet) {
#ifdef Q_OS_WIN
            m_terminal->setShellProgram("telnet");
#else
            m_terminal->setShellProgram("/usr/bin/telnet");
#endif
            QStringList args;
            args << m_session.host << QString::number(m_session.port);
            m_terminal->setArgs(args);
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
        }

        layout->addWidget(m_terminal);

        connect(m_terminal, &QTermWidget::finished, this, &TerminalTab::onTerminalFinished);
        connect(m_terminal, &QTermWidget::titleChanged, this, &TerminalTab::onTitleChanged);

        m_terminal->startShellProgram();
    }
}

TerminalTab::~TerminalTab() {
    closeExternalProcess();
}

void TerminalTab::onTerminalFinished() {
    m_isActive = false;
    emit titleChanged("[Closed] " + m_session.name);
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

    if (m_session.type == SessionType::RDP) {
#ifdef Q_OS_WIN
        program = "mstsc";
        args << "/v:" + m_session.host + ":" + QString::number(m_session.port);
#else
        program = "xfreerdp";
        args << "/v:" + m_session.host + ":" + QString::number(m_session.port);
        if (!m_session.user.isEmpty()) {
            args << "/u:" + m_session.user;
        }
        args << "/cert:ignore"
             << "/dynamic-resolution";
#endif
    } else if (m_session.type == SessionType::VNC) {
#ifdef Q_OS_WIN
        program = "vncviewer";
#else
        program = "vncviewer";
#endif
        args << m_session.host + "::" + QString::number(m_session.port);
    }

    if (program.isEmpty())
        return;

    m_externalProcess = new QProcess(this);
    m_externalProcess->setProcessChannelMode(QProcess::ForwardedChannels);

    connect(m_externalProcess, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished), this,
            [this](int exitCode, QProcess::ExitStatus) {
                Q_UNUSED(exitCode);
                m_isActive = false;
                if (m_statusLabel) {
                    m_statusLabel->setText(tr("Session closed. Close this tab to continue."));
                }
                emit titleChanged("[Closed] " + m_session.name);
            });

    connect(m_externalProcess, &QProcess::errorOccurred, this, [this](QProcess::ProcessError error) {
        Q_UNUSED(error);
        if (m_statusLabel) {
            m_statusLabel->setText(tr("Failed to launch external client.\n\n") +
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
