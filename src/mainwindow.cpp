#include "mainwindow.h"
#include "theme.h"
#include "sessionssidebar.h"
#include "sftpsidebar.h"
#include "terminaltab.h"
#include "sessiondialog.h"
#include "settingsdialog.h"
#include "updater.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QSplitter>
#include <QTabWidget>
#include <QStackedWidget>
#include <QToolButton>
#include <QFrame>
#include <QPushButton>
#include <QLabel>
#include <QIcon>
#include <QApplication>
#include <QStyle>
#include <QStyleFactory>
#include <QStyleHints>
#include <QGuiApplication>
#include <QShortcut>
#include <QToolBar>
#include <QMenuBar>
#include <QAction>
#include <QStatusBar>
#include <QFontDialog>
#include <QSettings>
#include <QLineEdit>
#include <QComboBox>
#include <QCloseEvent>
#include <QMessageBox>
#include <QMenu>
#include <QListWidget>
#include <QPlainTextEdit>
#include <QDialog>
#include <QCheckBox>
#include <QVBoxLayout>
#include <QPushButton>
#include <QPair>

MainWindow::MainWindow(QWidget* parent) : QMainWindow(parent) {
    setWindowIcon(QIcon(":/icons/logo.svg"));
    QSettings settings;
    if (settings.contains("theme/mode")) {
        m_themeMode = settings.value("theme/mode", "system").toString();
    } else if (settings.contains("theme/dark")) {
        m_themeMode = settings.value("theme/dark", true).toBool() ? "dark" : "light";
    } else {
        m_themeMode = "system";
    }
    setupUi();
    applyThemeMode(m_themeMode);

    connect(QGuiApplication::styleHints(), &QStyleHints::colorSchemeChanged, this, [this]() {
        if (m_themeMode == "system") {
            applyThemeMode("system");
        }
    });

    // Restore window layout geometry & state
    if (settings.contains("window/geometry")) {
        restoreGeometry(settings.value("window/geometry").toByteArray());
        restoreState(settings.value("window/state").toByteArray());
    } else {
        resize(1200, 800);
    }

    // Restore splitter sizes
    if (settings.contains("window/splitter")) {
        m_mainSplitter->restoreState(settings.value("window/splitter").toByteArray());
    } else {
        m_mainSplitter->setSizes({350, 850});
    }

    setWindowTitle("BanchoXterm");
}

MainWindow::~MainWindow() {
}

void MainWindow::closeEvent(QCloseEvent* event) {
    bool hasActive = false;
    for (QTabWidget* pane : {m_tabWidget, m_tabWidget2}) {
        for (int i = 0; i < pane->count(); ++i) {
            auto* tab = qobject_cast<TerminalTab*>(pane->widget(i));
            if (tab && tab->isSessionActive()) {
                hasActive = true;
                break;
            }
        }
        if (hasActive)
            break;
    }

    if (hasActive) {
        QMessageBox::StandardButton res =
            QMessageBox::question(this, tr("Exit BanchoXterm"),
                                  tr("You have active terminal connections. Are you sure you want to disconnect all "
                                     "sessions and close the application?"),
                                  QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
        if (res != QMessageBox::Yes) {
            event->ignore();
            return;
        }
    }

    QSettings settings;
    settings.setValue("window/geometry", saveGeometry());
    settings.setValue("window/state", saveState());
    settings.setValue("window/splitter", m_mainSplitter->saveState());
    QMainWindow::closeEvent(event);
}

void MainWindow::setupUi() {
    auto* centralWidget = new QWidget(this);
    setCentralWidget(centralWidget);

    auto* mainLayout = new QVBoxLayout(centralWidget);
    mainLayout->setContentsMargins(0, 0, 0, 0);
    mainLayout->setSpacing(0);

    setupMenuBar();

    // Main splitter
    m_mainSplitter = new QSplitter(Qt::Horizontal, centralWidget);
    mainLayout->addWidget(m_mainSplitter);

    // 3. Sidebar container (Vertical toolbar strip + Stacked widget)
    m_sidebarContainer = new QFrame(m_mainSplitter);
    m_sidebarContainer->setObjectName("sidebarContainer");

    auto* sidebarLayout = new QHBoxLayout(m_sidebarContainer);
    sidebarLayout->setContentsMargins(0, 0, 0, 0);
    sidebarLayout->setSpacing(0);

    // Left vertical button strip
    auto* verticalTabStrip = new QFrame(m_sidebarContainer);
    verticalTabStrip->setObjectName("verticalTabStrip");
    verticalTabStrip->setFixedWidth(60);
    auto* stripLayout = new QVBoxLayout(verticalTabStrip);
    stripLayout->setContentsMargins(0, 10, 0, 10);
    stripLayout->setSpacing(10);
    stripLayout->setAlignment(Qt::AlignTop);

    m_sessionsTabBtn = new QToolButton(verticalTabStrip);
    m_sessionsTabBtn->setIcon(QIcon(":/icons/server.svg"));
    m_sessionsTabBtn->setToolTip(tr("Sessions Manager"));
    m_sessionsTabBtn->setCheckable(true);
    m_sessionsTabBtn->setChecked(true);
    m_sessionsTabBtn->setToolButtonStyle(Qt::ToolButtonIconOnly);
    m_sessionsTabBtn->setFixedSize(60, 50);
    stripLayout->addWidget(m_sessionsTabBtn);

    m_sftpTabBtn = new QToolButton(verticalTabStrip);
    m_sftpTabBtn->setIcon(QIcon(":/icons/folder.svg"));
    m_sftpTabBtn->setToolTip(tr("SFTP Files"));
    m_sftpTabBtn->setCheckable(true);
    m_sftpTabBtn->setEnabled(false); // Enable only when SSH is active
    m_sftpTabBtn->setToolButtonStyle(Qt::ToolButtonIconOnly);
    m_sftpTabBtn->setFixedSize(60, 50);
    stripLayout->addWidget(m_sftpTabBtn);

    stripLayout->addStretch();

    auto* settingsBtn = new QToolButton(verticalTabStrip);
    settingsBtn->setIcon(QIcon(":/icons/gear.svg"));
    settingsBtn->setToolTip(tr("Settings"));
    settingsBtn->setToolButtonStyle(Qt::ToolButtonIconOnly);
    settingsBtn->setFixedSize(60, 50);
    stripLayout->addWidget(settingsBtn);

    m_multiInputBtn = new QToolButton(verticalTabStrip);
    m_multiInputBtn->setIcon(QIcon(":/icons/multiinput.svg"));
    m_multiInputBtn->setToolTip(tr("Multi-Input (send commands to all terminals)"));
    m_multiInputBtn->setCheckable(true);
    m_multiInputBtn->setToolButtonStyle(Qt::ToolButtonIconOnly);
    m_multiInputBtn->setFixedSize(60, 50);
    stripLayout->addWidget(m_multiInputBtn);

    sidebarLayout->addWidget(verticalTabStrip);

    // Right stacked content
    m_sidebarStacked = new QStackedWidget(m_sidebarContainer);
    m_sessionsSidebar = new SessionsSidebar(m_sidebarStacked);
    m_sftpSidebar = new SftpSidebar(m_sidebarStacked);
    m_sidebarStacked->addWidget(m_sessionsSidebar);
    m_sidebarStacked->addWidget(m_sftpSidebar);

    sidebarLayout->addWidget(m_sidebarStacked);
    m_mainSplitter->addWidget(m_sidebarContainer);

    // 4. Tab widgets for terminals (split into two panes)
    m_tabSplitter = new QSplitter(Qt::Horizontal, m_mainSplitter);
    m_tabWidget = new QTabWidget(m_tabSplitter);
    m_tabWidget->setTabsClosable(true);
    m_tabWidget->setMovable(true);
    m_tabWidget2 = new QTabWidget(m_tabSplitter);
    m_tabWidget2->setTabsClosable(true);
    m_tabWidget2->setMovable(true);
    m_tabWidget2->hide();
    m_tabSplitter->addWidget(m_tabWidget);
    m_tabSplitter->addWidget(m_tabWidget2);
    m_activePane = m_tabWidget;
    m_mainSplitter->addWidget(m_tabSplitter);

    // Set initial sizes
    m_mainSplitter->setSizes({350, 850});

    // 5. Multi-Input Bar at the bottom
    m_multiInputBar = new QWidget(centralWidget);
    m_multiInputBar->setObjectName("multiInputBar");
    m_multiInputBar->setFixedHeight(45);
    m_multiInputBar->setVisible(false);

    auto* multiInputLayout = new QHBoxLayout(m_multiInputBar);
    multiInputLayout->setContentsMargins(15, 0, 15, 0);
    multiInputLayout->setSpacing(10);

    auto* multiInputLabel = new QLabel(tr("Write to all terminals:"), m_multiInputBar);
    multiInputLabel->setStyleSheet("font-weight: bold; color: #7aa2f7;");
    multiInputLayout->addWidget(multiInputLabel);

    m_multiInputEdit = new QComboBox(m_multiInputBar);
    m_multiInputEdit->setEditable(true);
    m_multiInputEdit->setInsertPolicy(QComboBox::NoInsert);
    m_multiInputEdit->lineEdit()->setPlaceholderText(
        tr("Type command here and press Enter to execute on all active terminals..."));

    // Load history from settings
    QSettings settings;
    QStringList history = settings.value("multiinput/history").toStringList();
    m_multiInputEdit->addItems(history);
    m_multiInputEdit->setCurrentIndex(-1);
    m_multiInputEdit->lineEdit()->clear();

    multiInputLayout->addWidget(m_multiInputEdit, 1);

    auto* sendMultiBtn = new QPushButton(QIcon(":/icons/multiinput.svg"), tr("Send"), m_multiInputBar);
    sendMultiBtn->setObjectName("primaryButton");
    multiInputLayout->addWidget(sendMultiBtn);

    mainLayout->addWidget(m_multiInputBar);

    // Connections
    connect(m_sessionsSidebar, &SessionsSidebar::newLocalSessionRequested, this, &MainWindow::onNewLocalTerminal);
    connect(settingsBtn, &QToolButton::clicked, this, &MainWindow::onOpenSettings);
    connect(m_multiInputBtn, &QToolButton::clicked, this, &MainWindow::toggleMultiInputBar);

    connect(m_multiInputEdit->lineEdit(), &QLineEdit::returnPressed, this, &MainWindow::onSendMultiInput);
    connect(sendMultiBtn, &QPushButton::clicked, this, &MainWindow::onSendMultiInput);

    connect(m_sessionsTabBtn, &QToolButton::clicked, this, [this]() { switchSidebarTab(0); });
    connect(m_sftpTabBtn, &QToolButton::clicked, this, [this]() { switchSidebarTab(1); });

    connect(m_sessionsSidebar, &SessionsSidebar::connectSession, this, &MainWindow::onConnectSession);
    connect(m_tabWidget, &QTabWidget::tabCloseRequested, this, [this](int i) { onTabCloseRequested(m_tabWidget, i); });
    connect(m_tabWidget, &QTabWidget::currentChanged, this, [this](int i) { onCurrentTabChanged(m_tabWidget, i); });
    connect(m_tabWidget2, &QTabWidget::tabCloseRequested, this,
            [this](int i) { onTabCloseRequested(m_tabWidget2, i); });
    connect(m_tabWidget2, &QTabWidget::currentChanged, this, [this](int i) { onCurrentTabChanged(m_tabWidget2, i); });
    connect(m_sftpSidebar, &SftpSidebar::remoteStatsUpdated, this, &MainWindow::onRemoteStatsUpdated);

    // Atajo Ctrl+W para cerrar la pestaña activa
    auto* closeTabShortcut = new QShortcut(QKeySequence(Qt::CTRL | Qt::Key_W), this);
    connect(closeTabShortcut, &QShortcut::activated, this, [this]() {
        QTabWidget* pane = activePane();
        int idx = pane ? pane->currentIndex() : -1;
        if (idx != -1) {
            onTabCloseRequested(pane, idx);
        }
    });

    // Status bar remote stats: compact metric cards instead of a single text line.
    m_remoteMonitorWidget = new QFrame(this);
    m_remoteMonitorWidget->setObjectName("remoteMonitorWidget");
    auto* statsLayout = new QHBoxLayout(m_remoteMonitorWidget);
    statsLayout->setContentsMargins(8, 2, 8, 2);
    statsLayout->setSpacing(5);

    auto addStatCard = [this, statsLayout](const QString& iconPath, const QString& caption,
                                            QLabel*& valueLabel) {
        auto* card = new QFrame(m_remoteMonitorWidget);
        card->setObjectName("remoteStatsCard");
        auto* cardLayout = new QHBoxLayout(card);
        cardLayout->setContentsMargins(6, 2, 8, 2);
        cardLayout->setSpacing(5);

        auto* icon = new QLabel(card);
        icon->setPixmap(QIcon(iconPath).pixmap(QSize(16, 16)));
        icon->setFixedSize(16, 16);
        cardLayout->addWidget(icon);

        auto* textLayout = new QVBoxLayout();
        textLayout->setContentsMargins(0, 0, 0, 0);
        textLayout->setSpacing(0);
        valueLabel = new QLabel("--", card);
        valueLabel->setObjectName("remoteStatsValue");
        valueLabel->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
        auto* captionLabel = new QLabel(caption, card);
        captionLabel->setObjectName("remoteStatsCaption");
        textLayout->addWidget(valueLabel);
        textLayout->addWidget(captionLabel);
        cardLayout->addLayout(textLayout);
        statsLayout->addWidget(card);
    };

    addStatCard(":/icons/cpu.svg", tr("CPU"), m_remoteCpuValue);
    addStatCard(":/icons/memory.svg", tr("RAM"), m_remoteMemValue);
    addStatCard(":/icons/disk.svg", tr("Disk"), m_remoteDiskValue);
    addStatCard(":/icons/uptime.svg", tr("Uptime"), m_remoteUptimeValue);
    m_remoteMonitorWidget->setVisible(false);
    statusBar()->addPermanentWidget(m_remoteMonitorWidget);

    // Open an initial local terminal tab
    onNewLocalTerminal();
}

void MainWindow::switchSidebarTab(int index) {
    m_sidebarStacked->setCurrentIndex(index);
    m_sessionsTabBtn->setChecked(index == 0);
    m_sftpTabBtn->setChecked(index == 1);
}

void MainWindow::onConnectSession(const Session& session) {
    if (session.type == SessionType::FTP) {
        m_sftpSidebar->startSession(session);
        m_sftpTabBtn->setEnabled(true);
        switchSidebarTab(1);
        return;
    }

    QTabWidget* pane = activePane();
    auto* tab = new TerminalTab(session, pane);
    int index = pane->addTab(tab, session.name);

    if (session.type == SessionType::SSH) {
        pane->setTabIcon(index, QIcon(":/icons/server.svg"));
        // Share the terminal's SSH connection with the SFTP browser.
        m_sftpSidebar->setConnection(tab->connection());
        m_sftpSidebar->startSession(session);
        m_sftpTabBtn->setEnabled(true);
        switchSidebarTab(1); // Switch sidebar to SFTP files

        // Keep the SFTP browser in sync with the terminal's working directory
        connect(tab, &TerminalTab::remoteDirChanged, m_sftpSidebar, &SftpSidebar::navigateTo);
    } else if (session.type == SessionType::Telnet) {
        pane->setTabIcon(index, QIcon(":/icons/telnet.svg"));
    } else if (session.type == SessionType::RDP) {
        pane->setTabIcon(index, QIcon(":/icons/rdp.svg"));
    } else if (session.type == SessionType::VNC) {
        pane->setTabIcon(index, QIcon(":/icons/vnc.svg"));
    } else if (session.type == SessionType::Serial) {
        pane->setTabIcon(index, QIcon(":/icons/serial.svg"));
    } else {
        pane->setTabIcon(index, QIcon(":/icons/terminal.svg"));
    }

    pane->setCurrentIndex(index);

    // Auto-reconnect: when a session drops and asks to reconnect, swap this tab
    // for a fresh one with the same session.
    connect(tab, &TerminalTab::reconnectRequested, this, &MainWindow::onReconnectRequested);

    // Alt+F4 on the embedded terminal hits the term host process, which
    // forwards it here; route it through close() so the normal confirmation
    // dialog (closeEvent) decides whether to actually quit.
    connect(tab, &TerminalTab::closeRequested, this, [this]() { close(); });

    // Connect title updates
    connect(tab, &TerminalTab::titleChanged, this, [this, tab, pane](const QString& title) {
        int idx = pane->indexOf(tab);
        if (idx != -1 && !title.isEmpty()) {
            pane->setTabText(idx, title);
            if (pane == activePane() && idx == pane->currentIndex()) {
                setWindowTitle(QString("BanchoXterm - %1").arg(title));
            }
        }
    });
}

void MainWindow::onTabCloseRequested(QTabWidget* pane, int index) {
    auto* tab = qobject_cast<TerminalTab*>(pane->widget(index));
    if (tab) {
        if (tab->isSessionActive()) {
            QMessageBox::StandardButton res = QMessageBox::question(
                this, tr("Close Session"),
                tr("This connection is still active. Are you sure you want to disconnect and close this tab?"),
                QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
            if (res != QMessageBox::Yes) {
                return; // User canceled
            }
        }
        if (tab->isSsh() && m_sftpSidebar->connection() == tab->connection()) {
            m_sftpSidebar->detachConnection();
            m_sftpSidebar->stopSession();
        }
        pane->removeTab(index);
        delete tab;
    }
}

void MainWindow::onCurrentTabChanged(QTabWidget* pane, int index) {
    m_activePane = pane;

    if (index < 0) {
        // If this pane has no tabs but the other does, switch active pane.
        if (pane == m_tabWidget && m_tabWidget2->count() > 0) {
            m_activePane = m_tabWidget2;
            return;
        }
        if (pane == m_tabWidget2 && m_tabWidget->count() > 0) {
            m_activePane = m_tabWidget;
            return;
        }
        m_sftpSidebar->stopSession();
        m_sftpTabBtn->setEnabled(false);
        m_remoteMonitorWidget->setVisible(false);
        setWindowTitle("BanchoXterm");
        switchSidebarTab(0);
        return;
    }

    QString title = pane->tabText(index);
    setWindowTitle(QString("BanchoXterm - %1").arg(title));

    auto* tab = qobject_cast<TerminalTab*>(pane->widget(index));
    if (tab && tab->isSsh()) {
        m_sftpSidebar->setConnection(tab->connection());
        m_sftpSidebar->startSession(tab->getSession());
        m_sftpTabBtn->setEnabled(true);
    } else {
        m_sftpSidebar->stopSession();
        m_sftpTabBtn->setEnabled(false);
        m_remoteMonitorWidget->setVisible(false);
        if (m_sidebarStacked->currentIndex() == 1) {
            switchSidebarTab(0);
        }
    }
}

void MainWindow::onNewLocalTerminal() {
    Session localSession;
    localSession.name = tr("Local Shell");
    localSession.type = SessionType::Local;
#ifdef Q_OS_WIN
    localSession.shellPath = qEnvironmentVariable("COMSPEC", "cmd.exe");
#else
    localSession.shellPath = "/bin/bash";
#endif
    onConnectSession(localSession);
}

void MainWindow::toggleTheme() {
    if (m_themeMode == "dark")
        applyThemeMode("light");
    else
        applyThemeMode("dark");
}

void MainWindow::applyThemeMode(const QString& mode) {
    m_themeMode = mode;
    if (mode == "system") {
        qApp->setStyleSheet("");
#ifdef Q_OS_WIN
        QStyle* nativeStyle = QStyleFactory::create("windowsvista");
        if (!nativeStyle)
            nativeStyle = QStyleFactory::create("windows11");
        if (nativeStyle)
            qApp->setStyle(nativeStyle);
#endif
    } else if (mode == "light") {
        qApp->setStyleSheet(Theme::getLightTheme());
    } else { // "dark"
        qApp->setStyleSheet(Theme::getDarkTheme());
    }
}

void MainWindow::onOpenSettings() {
    SettingsDialog dialog(this);
    if (dialog.exec() == QDialog::Accepted) {
        // 1. Theme Configuration
        if (dialog.themeMode() != m_themeMode) {
            applyThemeMode(dialog.themeMode());
        }

        // 2. Typography Configuration
        for (QTabWidget* pane : {m_tabWidget, m_tabWidget2}) {
            for (int i = 0; i < pane->count(); ++i) {
                auto* tab = qobject_cast<TerminalTab*>(pane->widget(i));
                if (tab) {
                    tab->updateFontFromSettings();
                }
            }
        }
    }
}

void MainWindow::toggleMultiInputBar() {
    bool visible = !m_multiInputBar->isVisible();
    m_multiInputBar->setVisible(visible);
    if (m_multiInputBtn) {
        m_multiInputBtn->setChecked(visible);
    }
    if (visible) {
        m_multiInputEdit->setFocus();
    }
}

void MainWindow::onSendMultiInput() {
    QString text = m_multiInputEdit->currentText().trimmed();
    if (text.isEmpty())
        return;

    // Send to all tabs
    for (QTabWidget* pane : {m_tabWidget, m_tabWidget2}) {
        for (int i = 0; i < pane->count(); ++i) {
            auto* tab = qobject_cast<TerminalTab*>(pane->widget(i));
            if (tab) {
                tab->sendInputText(text);
            }
        }
    }

    // Add to dropdown history and save
    QSettings settings;
    QStringList history = settings.value("multiinput/history").toStringList();
    history.removeAll(text);
    history.prepend(text);
    while (history.size() > 50)
        history.removeLast(); // Cap history size
    settings.setValue("multiinput/history", history);

    // Re-populate combo box items dynamically
    m_multiInputEdit->clear();
    m_multiInputEdit->addItems(history);
    m_multiInputEdit->setCurrentIndex(-1);
    m_multiInputEdit->lineEdit()->clear();
}

void MainWindow::onReconnectRequested(const Session& session) {
    auto* tab = qobject_cast<TerminalTab*>(sender());
    if (tab) {
        QTabWidget* pane = nullptr;
        if (m_tabWidget->indexOf(tab) != -1)
            pane = m_tabWidget;
        else if (m_tabWidget2->indexOf(tab) != -1)
            pane = m_tabWidget2;

        if (pane) {
            int idx = pane->indexOf(tab);
            if (tab->isSsh() && m_sftpSidebar->connection() == tab->connection()) {
                m_sftpSidebar->detachConnection();
                m_sftpSidebar->stopSession();
            }
            pane->removeTab(idx);
            tab->deleteLater();
        }
    }
    onConnectSession(session);
}

void MainWindow::onRemoteStatsUpdated(double cpu, double mem, double disk, double uptimeSecs) {
    int seconds = static_cast<int>(uptimeSecs);
    int days = seconds / 86400;
    int hours = (seconds % 86400) / 3600;
    int mins = (seconds % 3600) / 60;

    QString uptimeStr;
    if (days > 0) {
        uptimeStr = QString("%1d %2h %3m").arg(days).arg(hours).arg(mins);
    } else if (hours > 0) {
        uptimeStr = QString("%1h %2m").arg(hours).arg(mins);
    } else {
        uptimeStr = QString("%1m").arg(mins);
    }

    m_remoteCpuValue->setText(QString("%1%").arg(cpu, 0, 'f', 0));
    m_remoteMemValue->setText(QString("%1%").arg(mem, 0, 'f', 0));
    m_remoteDiskValue->setText(QString("%1%").arg(disk, 0, 'f', 0));
    m_remoteUptimeValue->setText(uptimeStr);
    m_remoteMonitorWidget->setVisible(true);
}

void MainWindow::setupMenuBar() {
    auto* fileMenu = menuBar()->addMenu(tr("&File"));

    auto* newTabAction = fileMenu->addAction(tr("&New Remote Session..."));
    connect(newTabAction, &QAction::triggered, this, [this]() {
        SessionDialog dialog(this);
        if (dialog.exec() == QDialog::Accepted) {
            onConnectSession(dialog.getSession());
        }
    });

    fileMenu->addSeparator();

    auto* exitAction = fileMenu->addAction(tr("E&xit"));
    exitAction->setShortcut(QKeySequence::Quit);
    connect(exitAction, &QAction::triggered, this, &QWidget::close);

    auto* editMenu = menuBar()->addMenu(tr("&Edit"));

    m_copyAction = editMenu->addAction(tr("&Copy"));
    m_copyAction->setShortcut(QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_C));
    connect(m_copyAction, &QAction::triggered, this, &MainWindow::onCopy);

    m_pasteAction = editMenu->addAction(tr("&Paste"));
    m_pasteAction->setShortcut(QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_V));
    connect(m_pasteAction, &QAction::triggered, this, &MainWindow::onPaste);

    editMenu->addSeparator();

    auto* clearAction = editMenu->addAction(tr("Clear Scrollback"));
    connect(clearAction, &QAction::triggered, this, [this]() {
        auto* tab = currentTerminalTab();
        if (tab) {
            tab->clearTerminal();
        }
    });

    editMenu->addSeparator();

    auto* globalSearchAction = editMenu->addAction(tr("Find in &All Sessions..."));
    globalSearchAction->setShortcut(QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_F));
    connect(globalSearchAction, &QAction::triggered, this, &MainWindow::onGlobalSearch);

    editMenu->addSeparator();

    auto* settingsAction = editMenu->addAction(tr("C&onfiguration..."));
    connect(settingsAction, &QAction::triggered, this, &MainWindow::onOpenSettings);

    auto* viewMenu = menuBar()->addMenu(tr("&View"));

    auto* splitAction = viewMenu->addAction(tr("Toggle &Split View"));
    splitAction->setShortcut(QKeySequence(Qt::CTRL | Qt::ALT | Qt::Key_S));
    connect(splitAction, &QAction::triggered, this, &MainWindow::toggleSplitView);

    auto* moveTabAction = viewMenu->addAction(tr("Move Tab to &Other Pane"));
    moveTabAction->setShortcut(QKeySequence(Qt::CTRL | Qt::ALT | Qt::Key_M));
    connect(moveTabAction, &QAction::triggered, this, &MainWindow::moveTabToOtherPane);

    m_macrosMenu = menuBar()->addMenu(tr("&Macros"));
    rebuildMacrosMenu();

    auto* helpMenu = menuBar()->addMenu(tr("&Help"));
    auto* aboutAction = helpMenu->addAction(tr("&About BanchoXterm"));
    connect(aboutAction, &QAction::triggered, this, &MainWindow::showAbout);
    auto* updateAction = helpMenu->addAction(tr("Check for &Updates..."));
    connect(updateAction, &QAction::triggered, this, [this]() { Updater::checkForUpdates(this); });
}

void MainWindow::showAbout() {
    QString content =
        QString("<h2>BanchoXterm</h2>"
                "<p><b>%1</b></p>"
                "<p>%2</p>"
                "<p style=\"font-size: 11px; color: #888;\">%3</p>"
                "<p style=\"font-size: 11px; color: #888;\">%4</p>")
            .arg(tr("Version %1").arg(QStringLiteral(BANCHO_VERSION)))
            .arg(tr("A multi-protocol terminal emulator and remote session manager designed for command-line rebels."))
            .arg(tr("Supports SSH, SFTP, Telnet, Serial, RDP, VNC, and local terminals."))
            .arg(tr("Copyright &copy; 2026 BanchoXterm contributors. Licensed under the GNU General Public License v2 "
                    "or later (GPL-2.0-or-later)."));

    QMessageBox::about(this, tr("About BanchoXterm"), content);
}

TerminalTab* MainWindow::currentTerminalTab() const {
    QTabWidget* pane = activePane();
    if (!pane)
        return nullptr;
    int idx = pane->currentIndex();
    if (idx < 0)
        return nullptr;
    return qobject_cast<TerminalTab*>(pane->widget(idx));
}

QTabWidget* MainWindow::activePane() const {
    if (m_activePane && (m_activePane == m_tabWidget || m_activePane == m_tabWidget2))
        return m_activePane;
    return m_tabWidget;
}

QTabWidget* MainWindow::otherPane(QTabWidget* pane) const {
    return (pane == m_tabWidget) ? m_tabWidget2 : m_tabWidget;
}

void MainWindow::toggleSplitView() {
    const bool visible = !m_tabWidget2->isVisible();
    m_tabWidget2->setVisible(visible);
    if (visible) {
        // Move the current tab of the active pane to the new pane if the new
        // pane is empty, so the user sees two sessions side by side immediately.
        QTabWidget* from = activePane();
        if (from == m_tabWidget2 && m_tabWidget2->count() == 0)
            from = m_tabWidget;
        if (m_tabWidget2->count() == 0 && from == m_tabWidget && m_tabWidget->count() > 0) {
            int idx = m_tabWidget->currentIndex();
            if (idx >= 0) {
                QWidget* w = m_tabWidget->widget(idx);
                QString title = m_tabWidget->tabText(idx);
                m_tabWidget->removeTab(idx);
                m_tabWidget2->addTab(w, title);
                m_tabWidget2->setCurrentIndex(m_tabWidget2->count() - 1);
            }
        }
        m_tabSplitter->setSizes({1, 1});
        m_activePane = m_tabWidget2;
    } else {
        // Move all tabs back to the primary pane.
        while (m_tabWidget2->count() > 0) {
            QWidget* w = m_tabWidget2->widget(0);
            QString title = m_tabWidget2->tabText(0);
            m_tabWidget2->removeTab(0);
            int idx = m_tabWidget->addTab(w, title);
            m_tabWidget->setCurrentIndex(idx);
        }
        m_activePane = m_tabWidget;
    }
}

void MainWindow::moveTabToOtherPane() {
    QTabWidget* from = activePane();
    QTabWidget* to = otherPane(from);
    if (!to->isVisible())
        toggleSplitView();
    int idx = from->currentIndex();
    if (idx < 0)
        return;
    QWidget* w = from->widget(idx);
    QString title = from->tabText(idx);
    from->removeTab(idx);
    int newIdx = to->addTab(w, title);
    to->setCurrentIndex(newIdx);
    m_activePane = to;
}

void MainWindow::onCopy() {
    auto* tab = currentTerminalTab();
    if (tab) {
        tab->copySelection();
    }
}

void MainWindow::onPaste() {
    auto* tab = currentTerminalTab();
    if (tab) {
        tab->pasteSelection();
    }
}

void MainWindow::rebuildMacrosMenu() {
    if (!m_macrosMenu)
        return;
    m_macrosMenu->clear();

    QSettings settings;
    QStringList names = settings.value("macros/names").toStringList();
    QStringList texts = settings.value("macros/texts").toStringList();

    if (names.isEmpty()) {
        auto* empty = m_macrosMenu->addAction(tr("(no macros defined)"));
        empty->setEnabled(false);
    } else {
        for (int i = 0; i < names.size() && i < texts.size(); ++i) {
            QAction* act = m_macrosMenu->addAction(names[i]);
            connect(act, &QAction::triggered, this, [this, texts, i]() {
                auto* tab = currentTerminalTab();
                if (tab)
                    tab->sendRaw(texts[i]);
            });
        }
    }

    m_macrosMenu->addSeparator();
    auto* manageAct = m_macrosMenu->addAction(tr("Manage Macros..."));
    connect(manageAct, &QAction::triggered, this, &MainWindow::onManageMacros);
}

void MainWindow::onManageMacros() {
    QSettings settings;
    QStringList names = settings.value("macros/names").toStringList();
    QStringList texts = settings.value("macros/texts").toStringList();

    QDialog dlg(this);
    dlg.setWindowTitle(tr("Manage Macros"));
    dlg.setMinimumWidth(440);
    auto* lay = new QVBoxLayout(&dlg);

    auto* list = new QListWidget(&dlg);
    list->addItems(names);
    lay->addWidget(list);

    auto* nameEdit = new QLineEdit(&dlg);
    nameEdit->setPlaceholderText(tr("Macro name"));
    lay->addWidget(nameEdit);

    auto* textEdit = new QPlainTextEdit(&dlg);
    textEdit->setPlaceholderText(tr("Text to send (\\n for newline)"));
    lay->addWidget(textEdit);

    auto* btnRow = new QHBoxLayout();
    auto* addBtn = new QPushButton(tr("Add / Update"), &dlg);
    auto* delBtn = new QPushButton(tr("Delete"), &dlg);
    btnRow->addWidget(addBtn);
    btnRow->addWidget(delBtn);
    lay->addLayout(btnRow);

    auto* closeBtn = new QPushButton(tr("Close"), &dlg);
    lay->addWidget(closeBtn);
    connect(closeBtn, &QPushButton::clicked, &dlg, &QDialog::accept);

    connect(list, &QListWidget::currentRowChanged, &dlg, [&](int row) {
        if (row >= 0 && row < names.size()) {
            nameEdit->setText(names[row]);
            textEdit->setPlainText(texts[row]);
        }
    });

    connect(addBtn, &QPushButton::clicked, &dlg, [&]() {
        QString name = nameEdit->text().trimmed();
        if (name.isEmpty())
            return;
        int idx = names.indexOf(name);
        if (idx >= 0) {
            texts[idx] = textEdit->toPlainText();
        } else {
            names.append(name);
            texts.append(textEdit->toPlainText());
        }
        list->clear();
        list->addItems(names);
    });

    connect(delBtn, &QPushButton::clicked, &dlg, [&]() {
        int row = list->currentRow();
        if (row >= 0 && row < names.size()) {
            names.removeAt(row);
            texts.removeAt(row);
            list->clear();
            list->addItems(names);
        }
    });

    dlg.exec();

    settings.setValue("macros/names", names);
    settings.setValue("macros/texts", texts);
    rebuildMacrosMenu();
}

void MainWindow::onGlobalSearch() {
    const int total = m_tabWidget->count() + m_tabWidget2->count();
    if (total == 0)
        return;

    QDialog dlg(this);
    dlg.setWindowTitle(tr("Find in All Sessions"));
    auto* lay = new QVBoxLayout(&dlg);

    auto* searchEdit = new QLineEdit(&dlg);
    searchEdit->setPlaceholderText(tr("Search text..."));
    lay->addWidget(searchEdit);

    auto* caseCheck = new QCheckBox(tr("Case Sensitive"), &dlg);
    lay->addWidget(caseCheck);

    auto* statusLabel = new QLabel(&dlg);
    lay->addWidget(statusLabel);

    auto* btnRow = new QHBoxLayout();
    auto* nextBtn = new QPushButton(tr("Find Next"), &dlg);
    auto* prevBtn = new QPushButton(tr("Find Previous"), &dlg);
    auto* closeBtn = new QPushButton(tr("Close"), &dlg);
    btnRow->addWidget(nextBtn);
    btnRow->addWidget(prevBtn);
    btnRow->addWidget(closeBtn);
    lay->addLayout(btnRow);

    connect(closeBtn, &QPushButton::clicked, &dlg, &QDialog::accept);

    auto doSearch = [this, total, searchEdit, caseCheck, statusLabel](bool next) {
        QString str = searchEdit->text();
        if (str.isEmpty())
            return;

        // Flatten both panes into an ordered list of tabs.
        QVector<QPair<QTabWidget*, int>> tabs;
        for (QTabWidget* pane : {m_tabWidget, m_tabWidget2})
            for (int i = 0; i < pane->count(); ++i)
                tabs.append(qMakePair(pane, i));

        QTabWidget* curPane = activePane();
        int start = 0;
        for (int k = 0; k < tabs.size(); ++k) {
            if (tabs[k].first == curPane && tabs[k].second == curPane->currentIndex()) {
                start = k;
                break;
            }
        }

        const int dir = next ? 1 : -1;
        for (int step = 0; step < tabs.size(); ++step) {
            int k = (start + dir * step + tabs.size()) % tabs.size();
            QTabWidget* pane = tabs[k].first;
            int idx = tabs[k].second;
            auto* tab = qobject_cast<TerminalTab*>(pane->widget(idx));
            if (!tab)
                continue;
            bool found = tab->searchText(str, next, caseCheck->isChecked());
            if (found) {
                pane->setCurrentIndex(idx);
                m_activePane = pane;
                statusLabel->setText(tr("Match found in session: %1").arg(pane->tabText(idx)));
                return;
            }
        }
        statusLabel->setText(tr("No match found."));
    };

    connect(nextBtn, &QPushButton::clicked, &dlg, [&]() { doSearch(true); });
    connect(prevBtn, &QPushButton::clicked, &dlg, [&]() { doSearch(false); });
    connect(searchEdit, &QLineEdit::returnPressed, &dlg, [&]() { doSearch(true); });

    dlg.exec();
}
