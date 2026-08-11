#include "terminaltab.h"
#include <qtermwidget.h>
#include <QVBoxLayout>
#include <QFont>
#include <QDebug>
#include <QProcess>
#include <QCoreApplication>
#include <QSettings>
#include <QTimer>
#include "keyring.h"

TerminalTab::TerminalTab(const Session& session, QWidget* parent) : QWidget(parent), m_session(session) {
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);

    // Create terminal widget (startnow = 0, we start it manually after setting program and args)
    m_terminal = new QTermWidget(0, this);

    // Terminal Font (loaded from settings, deferred via singleShot to ensure it is applied after initialization)
    QTimer::singleShot(0, this, &TerminalTab::updateFontFromSettings);

    // Color Scheme
    QStringList schemes = QTermWidget::availableColorSchemes();
    if (schemes.contains("DarkPastels")) {
        m_terminal->setColorScheme("DarkPastels");
    } else if (schemes.contains("Tango")) {
        m_terminal->setColorScheme("Tango");
    } else if (!schemes.isEmpty()) {
        m_terminal->setColorScheme(schemes.first());
    }

    m_terminal->setHistorySize(5000); // 5000 lines scrollback
    m_terminal->setScrollBarPosition(QTermWidget::ScrollBarRight);

    // Configure program and arguments
    if (m_session.type == SessionType::SSH) {
        m_terminal->setShellProgram("/usr/bin/ssh");

        QStringList args;
        args << "-p" << QString::number(m_session.port);

        if (m_session.x11Forwarding) {
            args << "-Y"; // Trusted X11 Forwarding
        }

        if (!m_session.keyPath.isEmpty()) {
            args << "-i" << m_session.keyPath;
        }

        args << QString("%1@%2").arg(m_session.user, m_session.host);
        m_terminal->setArgs(args);

        // Check for saved password in keyring to bypass password prompts
        QString password = Keyring::lookupPassword(m_session.id);
        if (!password.isEmpty()) {
            QStringList env = QProcess::systemEnvironment();
            env << QString("SSH_ASKPASS=%1").arg(QCoreApplication::applicationFilePath());
            env << "SSH_ASKPASS_REQUIRE=force";
            env << QString("BANCHOXTERM_ASKPASS_ID=%1").arg(m_session.id);
            m_terminal->setEnvironment(env);
        }
    } else if (m_session.type == SessionType::Telnet) {
        m_terminal->setShellProgram("/usr/bin/telnet");
        QStringList args;
        args << m_session.host << QString::number(m_session.port);
        m_terminal->setArgs(args);
    } else if (m_session.type == SessionType::Serial) {
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
    } else {
        QString shell = m_session.shellPath;
        if (shell.isEmpty()) {
            shell = qgetenv("SHELL");
            if (shell.isEmpty()) {
                shell = "/bin/bash";
            }
        }
        m_terminal->setShellProgram(shell);
    }

    layout->addWidget(m_terminal);

    // Connect signals
    connect(m_terminal, &QTermWidget::finished, this, &TerminalTab::onTerminalFinished);
    connect(m_terminal, &QTermWidget::titleChanged, this, &TerminalTab::onTitleChanged);

    // Start shell
    m_terminal->startShellProgram();
}

TerminalTab::~TerminalTab() {
    // QTermWidget is deleted automatically as it's a child widget
}

void TerminalTab::onTerminalFinished() {
    m_isActive = false;
    emit titleChanged("[Closed] " + m_session.name);
}

void TerminalTab::onTitleChanged() {
    emit titleChanged(m_terminal->title());
}

void TerminalTab::updateFontFromSettings() {
    QSettings settings;

    // 1. Font
    QFont font;
    if (settings.contains("terminal/font")) {
        font.fromString(settings.value("terminal/font").toString());
    } else {
        font = QFont("Monospace", 11);
        font.setStyleHint(QFont::Monospace);
    }
    font.setFixedPitch(true);
    m_terminal->setTerminalFont(font);

    // 2. Color Scheme
    QString colorScheme = settings.value("terminal/colorScheme", "DarkPastels").toString();
    QStringList schemes = QTermWidget::availableColorSchemes();
    if (schemes.contains(colorScheme)) {
        m_terminal->setColorScheme(colorScheme);
    } else if (schemes.contains("DarkPastels")) {
        m_terminal->setColorScheme("DarkPastels");
    }
}
