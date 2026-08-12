#include "mainwindow.h"
#include "theme.h"
#include "sessionssidebar.h"
#include "sftpsidebar.h"
#include "terminaltab.h"
#include <qtermwidget.h>
#include "sessiondialog.h"
#include "settingsdialog.h"
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

MainWindow::MainWindow(QWidget* parent) : QMainWindow(parent) {
    QSettings settings;
    m_isDarkTheme = settings.value("theme/dark", true).toBool();
    setupUi();
    applyTheme(m_isDarkTheme);

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
    for (int i = 0; i < m_tabWidget->count(); ++i) {
        auto* tab = qobject_cast<TerminalTab*>(m_tabWidget->widget(i));
        if (tab && tab->isSessionActive()) {
            hasActive = true;
            break;
        }
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
    // 1. Top custom bar (modern header)
    auto* centralWidget = new QWidget(this);
    setCentralWidget(centralWidget);

    auto* mainLayout = new QVBoxLayout(centralWidget);
    mainLayout->setContentsMargins(0, 0, 0, 0);
    mainLayout->setSpacing(0);

    auto* headerWidget = new QWidget(centralWidget);
    headerWidget->setObjectName("headerWidget");
    headerWidget->setFixedHeight(50);

    auto* headerLayout = new QHBoxLayout(headerWidget);
    headerLayout->setContentsMargins(15, 0, 15, 0);

    auto* titleLabel = new QLabel("BANCHOXTERM", headerWidget);
    titleLabel->setStyleSheet("font-weight: 900; font-size: 15px; color: #7aa2f7; letter-spacing: 2px;");
    headerLayout->addWidget(titleLabel);
    headerLayout->addSpacing(20);

    setupMenuBar();

    headerLayout->addStretch();

    auto* settingsBtn = new QPushButton(QIcon(":/icons/gear.svg"), tr("  Settings"), headerWidget);
    headerLayout->addWidget(settingsBtn);

    auto* multiInputBtn = new QPushButton(QIcon(":/icons/multiinput.svg"), tr("  Multi-Input"), headerWidget);
    headerLayout->addWidget(multiInputBtn);

    mainLayout->addWidget(headerWidget);

    // 2. Main splitter
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
    stripLayout->setContentsMargins(0, 10, 0, 0);
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

    sidebarLayout->addWidget(verticalTabStrip);

    // Right stacked content
    m_sidebarStacked = new QStackedWidget(m_sidebarContainer);
    m_sessionsSidebar = new SessionsSidebar(m_sidebarStacked);
    m_sftpSidebar = new SftpSidebar(m_sidebarStacked);
    m_sidebarStacked->addWidget(m_sessionsSidebar);
    m_sidebarStacked->addWidget(m_sftpSidebar);

    sidebarLayout->addWidget(m_sidebarStacked);
    m_mainSplitter->addWidget(m_sidebarContainer);

    // 4. Tab widget for terminals
    m_tabWidget = new QTabWidget(m_mainSplitter);
    m_tabWidget->setTabsClosable(true);
    m_tabWidget->setMovable(true);
    m_mainSplitter->addWidget(m_tabWidget);

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
    connect(settingsBtn, &QPushButton::clicked, this, &MainWindow::onOpenSettings);
    connect(multiInputBtn, &QPushButton::clicked, this, &MainWindow::toggleMultiInputBar);

    connect(m_multiInputEdit->lineEdit(), &QLineEdit::returnPressed, this, &MainWindow::onSendMultiInput);
    connect(sendMultiBtn, &QPushButton::clicked, this, &MainWindow::onSendMultiInput);

    connect(m_sessionsTabBtn, &QToolButton::clicked, this, [this]() { switchSidebarTab(0); });
    connect(m_sftpTabBtn, &QToolButton::clicked, this, [this]() { switchSidebarTab(1); });

    connect(m_sessionsSidebar, &SessionsSidebar::connectSession, this, &MainWindow::onConnectSession);
    connect(m_tabWidget, &QTabWidget::tabCloseRequested, this, &MainWindow::onTabCloseRequested);
    connect(m_tabWidget, &QTabWidget::currentChanged, this, &MainWindow::onCurrentTabChanged);
    connect(m_sftpSidebar, &SftpSidebar::remoteStatsUpdated, this, &MainWindow::onRemoteStatsUpdated);

    // Status bar remote stats label
    m_remoteMonitorLabel = new QLabel(this);
    m_remoteMonitorLabel->setStyleSheet("padding: 0px 15px; font-weight: bold; color: #7aa2f7;");
    m_remoteMonitorLabel->setVisible(false);
    statusBar()->addPermanentWidget(m_remoteMonitorLabel);

    // Open an initial local terminal tab
    onNewLocalTerminal();
}

void MainWindow::switchSidebarTab(int index) {
    m_sidebarStacked->setCurrentIndex(index);
    m_sessionsTabBtn->setChecked(index == 0);
    m_sftpTabBtn->setChecked(index == 1);
}

void MainWindow::onConnectSession(const Session& session) {
    auto* tab = new TerminalTab(session, m_tabWidget);
    int index = m_tabWidget->addTab(tab, session.name);

    if (session.type == SessionType::SSH) {
        m_tabWidget->setTabIcon(index, QIcon(":/icons/server.svg"));
        // Share the terminal's SSH connection with the SFTP browser.
        m_sftpSidebar->setConnection(tab->connection());
        m_sftpSidebar->startSession(session);
        m_sftpTabBtn->setEnabled(true);
        switchSidebarTab(1); // Switch sidebar to SFTP files

        // Keep the SFTP browser in sync with the terminal's working directory
        connect(tab, &TerminalTab::remoteDirChanged, m_sftpSidebar, &SftpSidebar::navigateTo);
    } else if (session.type == SessionType::Telnet) {
        m_tabWidget->setTabIcon(index, QIcon(":/icons/telnet.svg"));
    } else if (session.type == SessionType::RDP) {
        m_tabWidget->setTabIcon(index, QIcon(":/icons/rdp.svg"));
    } else if (session.type == SessionType::VNC) {
        m_tabWidget->setTabIcon(index, QIcon(":/icons/vnc.svg"));
    } else if (session.type == SessionType::Serial) {
        m_tabWidget->setTabIcon(index, QIcon(":/icons/serial.svg"));
    } else {
        m_tabWidget->setTabIcon(index, QIcon(":/icons/terminal.svg"));
    }

    m_tabWidget->setCurrentIndex(index);

    // Connect title updates
    connect(tab, &TerminalTab::titleChanged, this, [this, tab](const QString& title) {
        int idx = m_tabWidget->indexOf(tab);
        if (idx != -1 && !title.isEmpty()) {
            m_tabWidget->setTabText(idx, title);
            if (idx == m_tabWidget->currentIndex()) {
                setWindowTitle(QString("BanchoXterm - %1").arg(title));
            }
        }
    });
}

void MainWindow::onTabCloseRequested(int index) {
    auto* tab = qobject_cast<TerminalTab*>(m_tabWidget->widget(index));
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
        m_tabWidget->removeTab(index);
        delete tab;
    }
}

void MainWindow::onCurrentTabChanged(int index) {
    if (index < 0) {
        m_sftpSidebar->stopSession();
        m_sftpTabBtn->setEnabled(false);
        m_remoteMonitorLabel->setVisible(false);
        setWindowTitle("BanchoXterm");
        switchSidebarTab(0);
        return;
    }

    QString title = m_tabWidget->tabText(index);
    setWindowTitle(QString("BanchoXterm - %1").arg(title));

    auto* tab = qobject_cast<TerminalTab*>(m_tabWidget->widget(index));
    if (tab && tab->isSsh()) {
        m_sftpSidebar->setConnection(tab->connection());
        m_sftpSidebar->startSession(tab->getSession());
        m_sftpTabBtn->setEnabled(true);
    } else {
        m_sftpSidebar->stopSession();
        m_sftpTabBtn->setEnabled(false);
        m_remoteMonitorLabel->setVisible(false);
        if (m_sidebarStacked->currentIndex() == 1) {
            switchSidebarTab(0);
        }
    }
}

void MainWindow::onNewLocalTerminal() {
    Session localSession;
    localSession.name = tr("Local Shell");
    localSession.type = SessionType::Local;
    localSession.shellPath = "/bin/bash";
    onConnectSession(localSession);
}

void MainWindow::toggleTheme() {
    m_isDarkTheme = !m_isDarkTheme;
    applyTheme(m_isDarkTheme);
}

void MainWindow::applyTheme(bool dark) {
    if (dark) {
        qApp->setStyleSheet(Theme::getDarkTheme());
    } else {
        qApp->setStyleSheet(Theme::getLightTheme());
    }
}

void MainWindow::onOpenSettings() {
    SettingsDialog dialog(this);
    if (dialog.exec() == QDialog::Accepted) {
        // 1. Theme Configuration
        QSettings settings;
        bool darkTheme = settings.value("theme/dark", true).toBool();
        if (darkTheme != m_isDarkTheme) {
            toggleTheme();
        }

        // 2. Typography Configuration
        for (int i = 0; i < m_tabWidget->count(); ++i) {
            auto* tab = qobject_cast<TerminalTab*>(m_tabWidget->widget(i));
            if (tab) {
                tab->updateFontFromSettings();
            }
        }
    }
}

void MainWindow::toggleMultiInputBar() {
    bool visible = !m_multiInputBar->isVisible();
    m_multiInputBar->setVisible(visible);
    if (visible) {
        m_multiInputEdit->setFocus();
    }
}

void MainWindow::onSendMultiInput() {
    QString text = m_multiInputEdit->currentText().trimmed();
    if (text.isEmpty())
        return;

    // Send to all tabs
    for (int i = 0; i < m_tabWidget->count(); ++i) {
        auto* tab = qobject_cast<TerminalTab*>(m_tabWidget->widget(i));
        if (tab && tab->getTerminalWidget()) {
            tab->getTerminalWidget()->sendText(text + "\n");
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

    QString statsText = QString("  [Remote System Stats]   CPU: %1%  |  RAM: %2%  |  Disk: %3%  |  Uptime: %4  ")
                            .arg(cpu, 0, 'f', 0)
                            .arg(mem, 0, 'f', 0)
                            .arg(disk, 0, 'f', 0)
                            .arg(uptimeStr);

    m_remoteMonitorLabel->setText(statsText);
    m_remoteMonitorLabel->setVisible(true);
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
        if (tab && tab->getTerminalWidget()) {
            tab->getTerminalWidget()->clear();
        }
    });
}

TerminalTab* MainWindow::currentTerminalTab() const {
    int idx = m_tabWidget->currentIndex();
    if (idx < 0)
        return nullptr;
    return qobject_cast<TerminalTab*>(m_tabWidget->widget(idx));
}

void MainWindow::onCopy() {
    auto* tab = currentTerminalTab();
    if (tab && tab->getTerminalWidget()) {
        tab->getTerminalWidget()->copyClipboard();
    }
}

void MainWindow::onPaste() {
    auto* tab = currentTerminalTab();
    if (tab && tab->getTerminalWidget()) {
        tab->getTerminalWidget()->pasteClipboard();
    }
}
