#include "mainwindow.h"
#include "sessionssidebar.h"
#include "sftpsidebar.h"
#include "terminaltab.h"
#include "sessiondialog.h"
#include "settingsdialog.h"
#include "updater.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGridLayout>
#include <QSplitter>
#include <QTabWidget>
#include <QTabBar>
#include <QStackedWidget>
#include <QToolButton>
#include <QFrame>
#include <QPushButton>
#include <QLabel>
#include <QIcon>
#include <QFont>
#include <QApplication>
#include <QPalette>
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
#include <QInputDialog>
#include <QSettings>
#include <QLineEdit>
#include <QComboBox>
#include <QCloseEvent>
#include <QEvent>
#include <QMouseEvent>
#include <QMessageBox>
#include "localizedmessagebox.h"
#include <QMenu>
#include <QSize>
#include <functional>

namespace {
QString sessionTypeName(SessionType type) {
    switch (type) {
    case SessionType::SSH:
        return QStringLiteral("SSH");
    case SessionType::Local:
        return QStringLiteral("LOCAL");
    case SessionType::Telnet:
        return QStringLiteral("TELNET");
    case SessionType::Serial:
        return QStringLiteral("SERIAL");
    case SessionType::RDP:
        return QStringLiteral("RDP");
    case SessionType::VNC:
        return QStringLiteral("VNC");
    case SessionType::FTP:
        return QStringLiteral("FTP");
    }
    return QStringLiteral("SESSION");
}
} // namespace

class DetachedTabWindow final : public QMainWindow {
public:
    using CloseHandler = std::function<void(DetachedTabWindow*)>;
    explicit DetachedTabWindow(const QString& title, QWidget* parent = nullptr) : QMainWindow(parent) {
        setWindowTitle(title);
        setWindowFlag(Qt::Window, true);
    }

    CloseHandler closeHandler;

protected:
    void closeEvent(QCloseEvent* event) override {
        if (m_closing) {
            event->accept();
            return;
        }

        m_closing = true;
        hide();

        // Finish the reparenting while the window is hidden, then defer the
        // actual destruction until Qt has finished processing this event.
        auto handler = std::move(closeHandler);
        if (handler)
            handler(this);
        event->accept();
        deleteLater();
    }

private:
    bool m_closing = false;
};
#include <QListWidget>
#include <QPlainTextEdit>
#include <QDialog>
#include <QDialogButtonBox>
#include <QCheckBox>
#include <QVBoxLayout>
#include <QPushButton>
#include <QPair>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

MainWindow::MainWindow(QWidget* parent) : QMainWindow(parent) {
    setWindowIcon(QIcon(":/icons/logo.svg"));
    m_systemPalette = qApp->palette();
    if (QStyle* fusion = QStyleFactory::create("Fusion"))
        qApp->setStyle(fusion);

    QSettings settings;
    m_themeMode = settings.value("theme/mode", "system").toString();
    if (m_themeMode != "system" && m_themeMode != "light" && m_themeMode != "dark")
        m_themeMode = "system";
    setupUi();
    applyThemeMode(m_themeMode);
    m_ribbonPinned = settings.value("window/ribbonPinned", true).toBool();
    setRibbonExpanded(m_ribbonPinned);

    connect(QGuiApplication::styleHints(), &QStyleHints::colorSchemeChanged, this, [this]() {
        if (m_themeMode == "system") {
            m_systemPalette = qApp->palette();
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

    setPaneLayout(settings.value("window/tabLayoutMode", 0).toInt());
    const int savedPane = settings.value("window/activePane", 0).toInt();
    const QList<QTabWidget*> panes = visiblePanes();
    if (savedPane >= 0 && savedPane < panes.size())
        m_activePane = panes[savedPane];
    restoreOpenTabs(settings);

    setWindowTitle("BanchoXterm");
}

MainWindow::~MainWindow() {
}

void MainWindow::closeEvent(QCloseEvent* event) {
    bool hasActive = false;
    for (QTabWidget* pane : allPanes()) {
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
        QMessageBox::StandardButton res = localizedQuestion(
            this, tr("Exit BanchoXterm"),
            tr("You have active terminal connections. Are you sure you want to disconnect all sessions and close the "
               "application?"),
            QMessageBox::Yes | QMessageBox::No);
        if (res != QMessageBox::Yes) {
            event->ignore();
            return;
        }
    }

    // Detached windows are children of MainWindow. Reattach their terminal
    // widgets explicitly before the main window starts its destruction so
    // that Qt never destroys a detached QMainWindow and its live terminal
    // child recursively in an unspecified order.
    reattachDetachedTabs();

    m_sessionsSidebar->saveCurrentOrder();
    m_sftpSidebar->saveLayout();

    QSettings settings;
    settings.setValue("window/geometry", saveGeometry());
    settings.setValue("window/state", saveState());
    settings.setValue("window/splitter", m_mainSplitter->saveState());
    settings.setValue("window/tabLayoutMode", m_paneLayoutMode);
    settings.setValue("window/activePane", qMax(0, visiblePanes().indexOf(activePane())));
    settings.setValue("window/ribbonPinned", m_ribbonPinned);
    saveOpenTabs(settings);
    QMainWindow::closeEvent(event);
}

void MainWindow::reattachDetachedTabs() {
    if (m_detachedTabs.isEmpty())
        return;

    const auto detachedTabs = m_detachedTabs;
    for (auto it = detachedTabs.cbegin(); it != detachedTabs.cend(); ++it) {
        TerminalTab* tab = it.key();
        auto* window = static_cast<DetachedTabWindow*>(it.value());
        if (!tab || !window)
            continue;

        m_detachedTabs.remove(tab);
        QWidget* content = window->takeCentralWidget();
        if (content) {
            QTabWidget* target = activePane();
            if (target) {
                content->setParent(target);
                const int newIndex = target->addTab(content, window->windowTitle());
                target->setCurrentIndex(newIndex);
                content->show();
            }
        }

        // The close handler is now empty from the map's perspective, so this
        // only hides and schedules the detached shell for deletion.
        window->close();
    }
}

void MainWindow::setupUi() {
    auto* centralWidget = new QWidget(this);
    setCentralWidget(centralWidget);

    auto* mainLayout = new QVBoxLayout(centralWidget);
    mainLayout->setContentsMargins(0, 0, 0, 0);
    mainLayout->setSpacing(0);

    setupMenuBar();

    auto* toolBar = addToolBar(tr("Main Toolbar"));
    toolBar->setObjectName("mainToolBar");
    toolBar->setMovable(false);
    toolBar->setFloatable(false);
    toolBar->setToolButtonStyle(Qt::ToolButtonTextUnderIcon);
    toolBar->setIconSize(QSize(22, 22));
    toolBar->setFixedHeight(78);
    toolBar->setContentsMargins(8, 3, 8, 3);

    auto addToolbarSection = [toolBar](const QString& title) {
        auto* label = new QLabel(title, toolBar);
        label->setObjectName("toolbarSectionLabel");
        label->setAlignment(Qt::AlignCenter);
        QFont font = label->font();
        font.setPointSize(qMax(8, font.pointSize() - 1));
        font.setBold(true);
        label->setFont(font);
        label->setMinimumWidth(58);
        label->setContentsMargins(6, 0, 6, 0);
        toolBar->addWidget(label);
    };

    addToolbarSection(tr("SESSION"));
    auto* newRemoteAction = toolBar->addAction(QIcon(":/icons/add.svg"), tr("New Session"));
    newRemoteAction->setToolTip(tr("Create a new remote session"));
    connect(newRemoteAction, &QAction::triggered, this, [this]() {
        SessionDialog dialog(this);
        if (dialog.exec() == QDialog::Accepted)
            onConnectSession(dialog.getSession());
    });

    auto* localAction = toolBar->addAction(QIcon(":/icons/terminal.svg"), tr("Local"));
    localAction->setToolTip(tr("Open a local terminal"));
    connect(localAction, &QAction::triggered, this, &MainWindow::onNewLocalTerminal);

    toolBar->addSeparator();

    addToolbarSection(tr("VIEW"));
    auto* splitAction = toolBar->addAction(QIcon(":/icons/split.svg"), tr("Split"));
    splitAction->setToolTip(tr("Toggle split view"));
    connect(splitAction, &QAction::triggered, this, &MainWindow::toggleSplitView);

    auto* gridAction = toolBar->addAction(QIcon(":/icons/grid.svg"), tr("Grid"));
    gridAction->setToolTip(tr("Toggle 2x2 grid view"));
    connect(gridAction, &QAction::triggered, this, &MainWindow::toggleGridView);

    auto* moveAction = toolBar->addAction(QIcon(":/icons/move.svg"), tr("Move"));
    moveAction->setToolTip(tr("Move the current tab to another pane"));
    connect(moveAction, &QAction::triggered, this, &MainWindow::moveTabToOtherPane);

    toolBar->addSeparator();

    addToolbarSection(tr("TOOLS"));
    auto* multiAction = toolBar->addAction(QIcon(":/icons/multiinput.svg"), tr("Multi-Input"));
    multiAction->setCheckable(true);
    multiAction->setToolTip(tr("Send input to all terminal sessions"));
    m_multiInputAction = multiAction;
    connect(multiAction, &QAction::triggered, this, &MainWindow::toggleMultiInputBar);

    auto* settingsAction = toolBar->addAction(QIcon(":/icons/gear.svg"), tr("Settings"));
    connect(settingsAction, &QAction::triggered, this, &MainWindow::onOpenSettings);

    const QList<QAction*> ribbonActions = {newRemoteAction, localAction, splitAction, gridAction,
                                           moveAction, multiAction, settingsAction};
    for (QAction* action : ribbonActions) {
        if (auto* button = qobject_cast<QToolButton*>(toolBar->widgetForAction(action))) {
            button->setToolButtonStyle(Qt::ToolButtonTextUnderIcon);
            button->setMinimumWidth(78);
            button->setMinimumHeight(66);
            button->setAutoRaise(true);
        }
    }

    // Replace the traditional menu bar with a tabbed Ribbon.  The original
    // menus remain as the source of the shared actions and keyboard shortcuts,
    // while the Ribbon provides the primary mouse-oriented interface.
    menuBar()->setVisible(false);
    const QList<QAction*> persistentRibbonActions = {newRemoteAction, localAction, splitAction, gridAction,
                                                     moveAction, multiAction, settingsAction};
    for (QAction* action : persistentRibbonActions)
        action->setParent(this);
    removeToolBar(toolBar);
    toolBar->deleteLater();

    auto* ribbonToolBar = addToolBar(tr("Ribbon"));
    m_ribbonToolBar = ribbonToolBar;
    ribbonToolBar->setObjectName("ribbonToolBar");
    ribbonToolBar->setMovable(false);
    ribbonToolBar->setFloatable(false);
    ribbonToolBar->setContentsMargins(4, 0, 4, 0);
    ribbonToolBar->setFixedHeight(98);

    auto* ribbonTabs = new QTabWidget(ribbonToolBar);
    ribbonTabs->setObjectName("ribbonTabs");
    ribbonTabs->setDocumentMode(true);
    ribbonTabs->setTabPosition(QTabWidget::North);

    auto makeRibbonPage = [ribbonTabs](const QString& title) {
        auto* page = new QWidget(ribbonTabs);
        auto* layout = new QHBoxLayout(page);
        layout->setContentsMargins(8, 3, 8, 3);
        layout->setSpacing(5);
        layout->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
        ribbonTabs->addTab(page, title);
        return qMakePair(page, layout);
    };

    auto addRibbonAction = [this](QHBoxLayout* layout, QAction* action) {
        auto* button = new QToolButton(layout->parentWidget());
        button->setDefaultAction(action);
        button->setToolButtonStyle(Qt::ToolButtonTextUnderIcon);
        button->setIconSize(QSize(20, 20));
        button->setMinimumSize(76, 61);
        button->setAutoRaise(true);
        layout->addWidget(button);
        connect(action, &QAction::triggered, this, [this]() {
            if (!m_ribbonPinned)
                setRibbonExpanded(false);
        });
    };

    auto sessionPage = makeRibbonPage(tr("Session"));
    addRibbonAction(sessionPage.second, newRemoteAction);
    addRibbonAction(sessionPage.second, localAction);
    auto* exitRibbonAction = new QAction(QIcon(":/icons/close.svg"), tr("Exit"), this);
    exitRibbonAction->setToolTip(tr("Close BanchoXterm"));
    connect(exitRibbonAction, &QAction::triggered, this, &QWidget::close);
    addRibbonAction(sessionPage.second, exitRibbonAction);

    auto viewPage = makeRibbonPage(tr("View"));
    addRibbonAction(viewPage.second, splitAction);
    addRibbonAction(viewPage.second, gridAction);
    addRibbonAction(viewPage.second, moveAction);
    auto* detachRibbonAction = new QAction(QIcon(":/icons/detach.svg"), tr("Detach"), this);
    detachRibbonAction->setToolTip(tr("Detach current tab"));
    connect(detachRibbonAction, &QAction::triggered, this, &MainWindow::detachCurrentTab);
    addRibbonAction(viewPage.second, detachRibbonAction);

    auto toolsPage = makeRibbonPage(tr("Tools"));
    addRibbonAction(toolsPage.second, multiAction);
    addRibbonAction(toolsPage.second, settingsAction);
    addRibbonAction(toolsPage.second, m_copyAction);
    addRibbonAction(toolsPage.second, m_pasteAction);
    auto* clearRibbonAction = new QAction(QIcon(":/icons/delete.svg"), tr("Clear"), this);
    clearRibbonAction->setToolTip(tr("Clear terminal scrollback"));
    connect(clearRibbonAction, &QAction::triggered, this, [this]() {
        if (auto* tab = currentTerminalTab())
            tab->clearTerminal();
    });
    addRibbonAction(toolsPage.second, clearRibbonAction);

    auto* searchRibbonAction = new QAction(QIcon(":/icons/server.svg"), tr("Search"), this);
    searchRibbonAction->setToolTip(tr("Find in all sessions"));
    searchRibbonAction->setShortcut(QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_F));
    connect(searchRibbonAction, &QAction::triggered, this, &MainWindow::onGlobalSearch);
    addRibbonAction(toolsPage.second, searchRibbonAction);

    auto* themeRibbonAction = new QAction(QIcon(":/icons/palette.svg"), tr("Theme"), this);
    themeRibbonAction->setToolTip(tr("Toggle light and dark theme"));
    connect(themeRibbonAction, &QAction::triggered, this, &MainWindow::toggleTheme);
    addRibbonAction(toolsPage.second, themeRibbonAction);

    auto macrosPage = makeRibbonPage(tr("Macros"));
    m_macrosRibbonPage = macrosPage.first;
    m_macrosRibbonLayout = macrosPage.second;
    rebuildMacrosRibbon();

    auto helpPage = makeRibbonPage(tr("Help"));
    auto* aboutRibbonAction = new QAction(QIcon(":/icons/logo.svg"), tr("About"), this);
    connect(aboutRibbonAction, &QAction::triggered, this, &MainWindow::showAbout);
    addRibbonAction(helpPage.second, aboutRibbonAction);
    auto* updateRibbonAction = new QAction(QIcon(":/icons/refresh.svg"), tr("Updates"), this);
    connect(updateRibbonAction, &QAction::triggered, this, [this]() { Updater::checkForUpdates(this); });
    addRibbonAction(helpPage.second, updateRibbonAction);

    ribbonTabs->setFixedHeight(94);

    auto* sessionContext = new QWidget(ribbonTabs);
    sessionContext->setObjectName("sessionContextWidget");
    auto* sessionContextLayout = new QHBoxLayout(sessionContext);
    sessionContextLayout->setContentsMargins(6, 0, 8, 0);
    sessionContextLayout->setSpacing(6);
    m_contextProtocolLabel = new QLabel(tr("NO SESSION"), sessionContext);
    m_contextProtocolLabel->setObjectName("sessionContextProtocol");
    m_contextSessionLabel = new QLabel(tr("Open a session to begin"), sessionContext);
    m_contextSessionLabel->setObjectName("sessionContextSession");
    sessionContextLayout->addWidget(m_contextProtocolLabel);
    sessionContextLayout->addWidget(m_contextSessionLabel);
    auto* statusContext = new QWidget(ribbonTabs);
    statusContext->setObjectName("statusContextWidget");
    auto* statusContextLayout = new QHBoxLayout(statusContext);
    statusContextLayout->setContentsMargins(8, 0, 2, 0);
    statusContextLayout->setSpacing(14);
    statusContextLayout->addWidget(sessionContext);
    m_contextStateLabel = new QLabel(statusContext);
    m_contextStateLabel->setObjectName("sessionContextState");
    m_contextStateLabel->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    m_contextStateLabel->setMinimumWidth(78);
    statusContextLayout->addWidget(m_contextStateLabel);

    auto* ribbonToggle = new QToolButton(ribbonToolBar);
    m_ribbonToggle = ribbonToggle;
    ribbonToggle->setObjectName("ribbonToggleButton");
    ribbonToggle->setIcon(QIcon(":/icons/chevron-up.svg"));
    ribbonToggle->setToolTip(tr("Collapse Ribbon"));
    ribbonToggle->setAutoRaise(true);
    ribbonToggle->setFixedSize(28, 28);
    statusContextLayout->addWidget(ribbonToggle);
    ribbonTabs->setCornerWidget(statusContext, Qt::TopRightCorner);
    connect(ribbonToggle, &QToolButton::clicked, this, [this]() {
        m_ribbonPinned = !m_ribbonPinned;
        setRibbonExpanded(m_ribbonPinned);
        m_ribbonToggle->setToolTip(m_ribbonPinned ? tr("Collapse Ribbon") : tr("Keep Ribbon expanded"));
    });
    connect(ribbonTabs, &QTabWidget::currentChanged, this, [this]() {
        if (!m_ribbonPinned)
            setRibbonExpanded(true);
    });
    connect(ribbonTabs->tabBar(), &QTabBar::tabBarClicked, this, [this](int) {
        if (!m_ribbonPinned)
            setRibbonExpanded(true);
    });
    ribbonToolBar->addWidget(ribbonTabs);
    m_ribbonTabs = ribbonTabs;
    qApp->installEventFilter(this);

    auto* paletteShortcut = new QShortcut(QKeySequence(Qt::CTRL | Qt::Key_K), this);
    connect(paletteShortcut, &QShortcut::activated, this, &MainWindow::showCommandPalette);

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

    sidebarLayout->addWidget(verticalTabStrip);

    // Right stacked content
    m_sidebarStacked = new QStackedWidget(m_sidebarContainer);
    m_sessionsSidebar = new SessionsSidebar(m_sidebarStacked);
    m_sftpSidebar = new SftpSidebar(m_sidebarStacked);
    m_sidebarStacked->addWidget(m_sessionsSidebar);
    m_sidebarStacked->addWidget(m_sftpSidebar);

    sidebarLayout->addWidget(m_sidebarStacked);
    m_mainSplitter->addWidget(m_sidebarContainer);

    // 4. Terminal panes. The same four-pane container supports the default
    // single view, a two-pane split, and a 2x2 grid without moving widgets
    // between layouts when the user changes mode.
    m_tabSplitter = new QWidget(m_mainSplitter);
    m_tabGrid = new QGridLayout(m_tabSplitter);
    m_tabGrid->setContentsMargins(0, 0, 0, 0);
    m_tabGrid->setSpacing(1);

    auto createPane = [this]() {
        auto* pane = new QTabWidget(m_tabSplitter);
        pane->setTabsClosable(true);
        pane->setMovable(true);
        pane->setDocumentMode(true);
        pane->tabBar()->setExpanding(false);
        pane->tabBar()->setElideMode(Qt::ElideRight);
        return pane;
    };
    m_tabWidget = createPane();
    m_tabWidget2 = createPane();
    m_tabWidget3 = createPane();
    m_tabWidget4 = createPane();
    m_tabGrid->addWidget(m_tabWidget, 0, 0);
    m_tabGrid->addWidget(m_tabWidget2, 0, 1);
    m_tabGrid->addWidget(m_tabWidget3, 1, 0);
    m_tabGrid->addWidget(m_tabWidget4, 1, 1);
    m_tabGrid->setRowStretch(0, 1);
    m_tabGrid->setColumnStretch(0, 1);

    m_welcomeWidget = new QWidget(m_tabSplitter);
    m_welcomeWidget->setObjectName("welcomeScreen");
    auto* welcomeLayout = new QVBoxLayout(m_welcomeWidget);
    welcomeLayout->setContentsMargins(30, 30, 30, 30);
    welcomeLayout->setSpacing(12);
    welcomeLayout->setAlignment(Qt::AlignCenter);

    auto* welcomeTitle = new QLabel(tr("Welcome to BanchoXterm"), m_welcomeWidget);
    welcomeTitle->setObjectName("welcomeTitle");
    welcomeTitle->setAlignment(Qt::AlignCenter);
    welcomeLayout->addWidget(welcomeTitle);

    auto* welcomeSubtitle = new QLabel(tr("Open a saved session or start a new terminal to begin."), m_welcomeWidget);
    welcomeSubtitle->setObjectName("welcomeSubtitle");
    welcomeSubtitle->setAlignment(Qt::AlignCenter);
    welcomeSubtitle->setWordWrap(true);
    welcomeLayout->addWidget(welcomeSubtitle);

    auto* welcomeActions = new QHBoxLayout();
    welcomeActions->setSpacing(8);
    auto* welcomeRemoteButton = new QPushButton(QIcon(":/icons/add.svg"), tr("New Session"), m_welcomeWidget);
    welcomeRemoteButton->setObjectName("primaryButton");
    auto* welcomeLocalButton = new QPushButton(QIcon(":/icons/terminal.svg"), tr("Local Terminal"), m_welcomeWidget);
    welcomeLocalButton->setObjectName("sidebarAction");
    welcomeActions->addWidget(welcomeRemoteButton);
    welcomeActions->addWidget(welcomeLocalButton);
    welcomeLayout->addLayout(welcomeActions);

    auto* recentTitle = new QLabel(tr("Recent sessions"), m_welcomeWidget);
    recentTitle->setObjectName("welcomeSectionTitle");
    recentTitle->setAlignment(Qt::AlignCenter);
    welcomeLayout->addWidget(recentTitle);

    const QList<Session> savedSessions = SessionManager::loadSessions();
    const QStringList recentNames = QSettings().value("sessions/recent").toStringList();
    int recentCount = 0;
    for (const QString& recentName : recentNames) {
        for (const Session& recentSession : savedSessions) {
            if (recentSession.name != recentName)
                continue;
            auto* recentButton = new QPushButton(
                recentSession.favorite ? QStringLiteral("★  %1").arg(recentName) : recentName, m_welcomeWidget);
            recentButton->setObjectName("welcomeRecentButton");
            recentButton->setIcon(QIcon(recentSession.type == SessionType::SSH ? ":/icons/server.svg"
                                                                                : ":/icons/terminal.svg"));
            recentButton->setToolTip(QStringLiteral("%1@%2:%3")
                                         .arg(recentSession.user, recentSession.host)
                                         .arg(recentSession.port));
            welcomeLayout->addWidget(recentButton, 0, Qt::AlignHCenter);
            connect(recentButton, &QPushButton::clicked, this,
                    [this, recentSession]() { onConnectSession(recentSession); });
            if (++recentCount >= 5)
                break;
        }
        if (recentCount >= 5)
            break;
    }
    recentTitle->setVisible(recentCount > 0);

    m_tabGrid->addWidget(m_welcomeWidget, 0, 0, 2, 2);

    connect(welcomeRemoteButton, &QPushButton::clicked, this, [this]() {
        SessionDialog dialog(this);
        if (dialog.exec() == QDialog::Accepted)
            onConnectSession(dialog.getSession());
    });
    connect(welcomeLocalButton, &QPushButton::clicked, this, &MainWindow::onNewLocalTerminal);

    m_tabWidget2->hide();
    m_tabWidget3->hide();
    m_tabWidget4->hide();
    m_activePane = m_tabWidget;
    m_mainSplitter->addWidget(m_tabSplitter);
    updateWelcomeScreen();

    m_statusConnectionLabel = new QLabel(tr("Ready"), this);
    statusBar()->addWidget(m_statusConnectionLabel, 1);

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
    connect(m_tabWidget3, &QTabWidget::tabCloseRequested, this,
            [this](int i) { onTabCloseRequested(m_tabWidget3, i); });
    connect(m_tabWidget3, &QTabWidget::currentChanged, this, [this](int i) { onCurrentTabChanged(m_tabWidget3, i); });
    connect(m_tabWidget4, &QTabWidget::tabCloseRequested, this,
            [this](int i) { onTabCloseRequested(m_tabWidget4, i); });
    connect(m_tabWidget4, &QTabWidget::currentChanged, this, [this](int i) { onCurrentTabChanged(m_tabWidget4, i); });
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

    auto addStatCard = [this, statsLayout](const QString& iconPath, const QString& caption, QLabel*& valueLabel) {
        auto* card = new QFrame(m_remoteMonitorWidget);
        card->setObjectName("remoteStatsCard");
        auto* cardLayout = new QHBoxLayout(card);
        cardLayout->setContentsMargins(6, 2, 8, 2);
        cardLayout->setSpacing(5);

        auto* icon = new QLabel(card);
        icon->setPixmap(QIcon(iconPath).pixmap(QSize(16, 16)));
        icon->setFixedSize(16, 16);
        cardLayout->addWidget(icon);

        auto* textLayout = new QHBoxLayout();
        textLayout->setContentsMargins(0, 0, 0, 0);
        textLayout->setSpacing(4);
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

    // Start with an empty workspace.  The welcome screen is the bootstrap
    // view; a local terminal is opened only when the user requests it.
    updateSessionContext();
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
        connect(tab, &TerminalTab::remoteDirChanged, this, [this, tab](const QString& path) {
            tab->setRemoteDirectory(path);
            m_sftpSidebar->navigateTo(path);
        });
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

    const QString endpoint = session.type == SessionType::Local
                                 ? session.shellPath
                                 : QStringLiteral("%1@%2:%3").arg(session.user, session.host).arg(session.port);
    pane->setTabToolTip(index, endpoint.isEmpty() ? session.name : endpoint);

    pane->setCurrentIndex(index);
    updateWelcomeScreen();
    updateSessionContext();

    // Auto-reconnect: when a session drops and asks to reconnect, swap this tab
    // for a fresh one with the same session.
    connect(tab, &TerminalTab::reconnectRequested, this, &MainWindow::onReconnectRequested);

    // Alt+F4 on the embedded terminal hits the term host process, which
    // forwards it here; route it through close() so the normal confirmation
    // dialog (closeEvent) decides whether to actually quit.
    connect(tab, &TerminalTab::closeRequested, this, [this]() { close(); });

    connect(tab, &TerminalTab::exitRequested, this, [this, tab, pane]() {
        const int tabIndex = pane->indexOf(tab);
        if (tabIndex >= 0)
            onTabCloseRequested(pane, tabIndex);
    });

    // Connect title updates
    connect(tab, &TerminalTab::titleChanged, this, [this, tab, pane](const QString& title) {
        int idx = pane->indexOf(tab);
        if (idx != -1 && !title.isEmpty()) {
            pane->setTabText(idx, title);
            const bool disconnected = title.startsWith(tr("[Closed]"));
            pane->tabBar()->setTabTextColor(idx, disconnected ? QColor("#e06c75") : QColor("#36b37e"));
            if (m_statusConnectionLabel)
                m_statusConnectionLabel->setText(title);
            statusBar()->showMessage(title, 3000);
            if (pane == activePane() && idx == pane->currentIndex()) {
                setWindowTitle(QString("BanchoXterm - %1").arg(title));
            }
            updateSessionContext();
        }
    });
}

void MainWindow::updateSessionContext() {
    TerminalTab* tab = currentTerminalTab();
    if (!tab) {
        m_contextProtocolLabel->setText(tr("NO SESSION"));
        m_contextSessionLabel->setText(tr("Open a session to begin"));
        m_contextStateLabel->clear();
        m_contextStateLabel->setStyleSheet(QString());
        if (m_statusConnectionLabel)
            m_statusConnectionLabel->setText(tr("Ready"));
        return;
    }

    const Session& session = tab->session();
    QString endpoint;
    if (session.type == SessionType::Local) {
        endpoint = session.shellPath.isEmpty() ? tr("Local shell") : session.shellPath;
    } else if (session.type == SessionType::Serial) {
        endpoint = QStringLiteral("%1 @ %2 baud").arg(session.serialPort).arg(session.baudRate);
    } else if (!session.host.isEmpty()) {
        endpoint = QStringLiteral("%1@%2:%3").arg(session.user, session.host).arg(session.port);
    } else {
        endpoint = session.name;
    }

    const bool active = tab->isSessionActive();
    m_contextProtocolLabel->setText(sessionTypeName(session.type));
    m_contextSessionLabel->setText(QStringLiteral("%1  ·  %2").arg(session.name, endpoint));
    m_contextStateLabel->setText(active ? tr("Connected") : tr("Disconnected"));
    m_contextStateLabel->setStyleSheet(active ? QStringLiteral("color: #36b37e; font-weight: 600;")
                                               : QStringLiteral("color: #e06c75; font-weight: 600;"));
    if (m_statusConnectionLabel)
        m_statusConnectionLabel->setText(QStringLiteral("%1  |  %2").arg(sessionTypeName(session.type), endpoint));
}

void MainWindow::onTabCloseRequested(QTabWidget* pane, int index) {
    auto* tab = qobject_cast<TerminalTab*>(pane->widget(index));
    if (tab) {
        if (tab->isSessionActive()) {
            QMessageBox::StandardButton res = localizedQuestion(
                this, tr("Close Session"),
                tr("This connection is still active. Are you sure you want to disconnect and close this tab?"),
                QMessageBox::Yes | QMessageBox::No);
            if (res != QMessageBox::Yes) {
                return; // User canceled
            }
        }
        if (tab->isSsh() && m_sftpSidebar->connection() == tab->connection()) {
            m_sftpSidebar->detachConnection();
            m_sftpSidebar->stopSession();
        }
        pane->removeTab(index);
        // Defer destruction until pending signals and timers finish their
        // current event-loop iteration.
        tab->deleteLater();
        updateWelcomeScreen();
        updateSessionContext();
    }
}

void MainWindow::onCurrentTabChanged(QTabWidget* pane, int index) {
    m_activePane = pane;
    updateSessionContext();

    if (index < 0) {
        // If this pane has no tabs but another visible pane does, switch to it.
        for (QTabWidget* candidate : visiblePanes()) {
            if (candidate->count() > 0) {
                m_activePane = candidate;
                return;
            }
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

void MainWindow::updateWelcomeScreen() {
    if (!m_welcomeWidget)
        return;

    bool hasSession = false;
    for (QTabWidget* pane : visiblePanes()) {
        if (pane->count() > 0) {
            hasSession = true;
            break;
        }
    }
    m_welcomeWidget->setVisible(!hasSession);
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
    applyThemeMode(m_themeMode == "dark" ? "light" : "dark");
}

void MainWindow::applyThemeMode(const QString& mode) {
    m_themeMode = mode;
    QPalette palette = m_systemPalette;
    if (mode == "light") {
        palette = QPalette(QColor("#f5f6f8"));
        palette.setColor(QPalette::Window, QColor("#f5f6f8"));
        palette.setColor(QPalette::Base, Qt::white);
        palette.setColor(QPalette::AlternateBase, QColor("#f5f6f8"));
        palette.setColor(QPalette::Button, QColor("#e9ebef"));
        palette.setColor(QPalette::Text, QColor("#1d2430"));
        palette.setColor(QPalette::WindowText, QColor("#1d2430"));
        palette.setColor(QPalette::ButtonText, QColor("#1d2430"));
        palette.setColor(QPalette::PlaceholderText, QColor("#687386"));
        palette.setColor(QPalette::Highlight, QColor("#2f6fed"));
        palette.setColor(QPalette::HighlightedText, Qt::white);
    } else if (mode == "dark") {
        palette = QPalette(QColor("#202124"));
        palette.setColor(QPalette::Window, QColor("#202124"));
        palette.setColor(QPalette::Base, QColor("#17181b"));
        palette.setColor(QPalette::AlternateBase, QColor("#17181b"));
        palette.setColor(QPalette::Button, QColor("#2b2d31"));
        palette.setColor(QPalette::Text, QColor("#e7e9ed"));
        palette.setColor(QPalette::WindowText, QColor("#e7e9ed"));
        palette.setColor(QPalette::ButtonText, QColor("#e7e9ed"));
        palette.setColor(QPalette::PlaceholderText, QColor("#9aa3b2"));
        palette.setColor(QPalette::Highlight, QColor("#3d75d6"));
        palette.setColor(QPalette::HighlightedText, Qt::white);
    }
    qApp->setPalette(mode == "system" ? m_systemPalette : palette);

    // Fusion is used for consistent rendering across platforms.  Re-polish
    // existing widgets as well; Windows otherwise keeps parts of the previous
    // palette until the next native style refresh.
    if (QStyle* style = qApp->style()) {
        const auto widgets = QApplication::allWidgets();
        for (QWidget* widget : widgets) {
            style->unpolish(widget);
            style->polish(widget);
            widget->update();
        }
    }
}

void MainWindow::onOpenSettings() {
    SettingsDialog dialog(this);
    if (dialog.exec() == QDialog::Accepted) {
        if (dialog.themeMode() != m_themeMode)
            applyThemeMode(dialog.themeMode());

        // Typography Configuration
        for (QTabWidget* pane : allPanes()) {
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
    if (m_multiInputAction)
        m_multiInputAction->setChecked(visible);
    if (visible) {
        m_multiInputEdit->setFocus();
    }
}

void MainWindow::onSendMultiInput() {
    QString text = m_multiInputEdit->currentText().trimmed();
    if (text.isEmpty())
        return;

    QList<TerminalTab*> targets;
    QStringList targetNames;
    for (QTabWidget* pane : allPanes()) {
        for (int i = 0; i < pane->count(); ++i) {
            auto* tab = qobject_cast<TerminalTab*>(pane->widget(i));
            if (!tab)
                continue;
            targets.append(tab);
            targetNames.append(pane->tabText(i));
        }
    }
    if (targets.isEmpty())
        return;

    QDialog targetDialog(this);
    targetDialog.setWindowTitle(tr("Confirm Multi-Input"));
    auto* targetLayout = new QVBoxLayout(&targetDialog);
    targetLayout->addWidget(
        new QLabel(tr("Select the terminal sessions that should receive this command:"), &targetDialog));
    auto* targetList = new QListWidget(&targetDialog);
    for (const QString& name : targetNames) {
        auto* item = new QListWidgetItem(name, targetList);
        item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
        item->setCheckState(Qt::Checked);
    }
    targetLayout->addWidget(targetList);
    auto* commandLabel = new QLabel(tr("Command: %1").arg(text), &targetDialog);
    commandLabel->setWordWrap(true);
    targetLayout->addWidget(commandLabel);
    auto* targetButtons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &targetDialog);
    targetLayout->addWidget(targetButtons);
    connect(targetButtons, &QDialogButtonBox::accepted, &targetDialog, &QDialog::accept);
    connect(targetButtons, &QDialogButtonBox::rejected, &targetDialog, &QDialog::reject);

    if (targetDialog.exec() != QDialog::Accepted)
        return;

    QList<TerminalTab*> selectedTargets;
    for (int i = 0; i < targets.size(); ++i) {
        if (targetList->item(i)->checkState() == Qt::Checked) {
            selectedTargets.append(targets.at(i));
        }
    }
    if (selectedTargets.isEmpty())
        return;

    for (TerminalTab* tab : selectedTargets)
        tab->sendInputText(text);

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
        else {
            for (QTabWidget* candidate : allPanes()) {
                if (candidate->indexOf(tab) != -1) {
                    pane = candidate;
                    break;
                }
            }
        }

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
    int seconds = qMax(0, static_cast<int>(uptimeSecs));
    int days = seconds / 86400;
    int hours = (seconds % 86400) / 3600;
    int mins = (seconds % 3600) / 60;
    int secs = seconds % 60;

    QString uptimeStr;
    if (days > 0) {
        uptimeStr = QString("%1d %2h %3m %4s").arg(days).arg(hours).arg(mins).arg(secs);
    } else if (hours > 0) {
        uptimeStr = QString("%1h %2m %3s").arg(hours).arg(mins).arg(secs);
    } else if (mins > 0) {
        uptimeStr = QString("%1m %2s").arg(mins).arg(secs);
    } else {
        uptimeStr = QString("%1s").arg(secs);
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
    m_copyAction->setIcon(QIcon(":/icons/copy.svg"));
    // TerminalTab handles Ctrl+Shift+C through its application event filter.
    // Keeping a global shortcut here would execute the copy path twice when
    // the focused widget is a QTermWidget terminal.
    m_copyAction->setToolTip(tr("Copy selection (Ctrl+Shift+C)"));
    m_copyAction->setStatusTip(tr("Copy selection (Ctrl+Shift+C)"));
    connect(m_copyAction, &QAction::triggered, this, &MainWindow::onCopy);

    m_pasteAction = editMenu->addAction(tr("&Paste"));
    m_pasteAction->setIcon(QIcon(":/icons/paste.svg"));
    // Same reasoning as Copy: the terminal event filter is the sole keyboard
    // path, preventing one paste through the filter and another through this
    // global QAction (which is especially visible with bracketed paste).
    m_pasteAction->setToolTip(tr("Paste (Ctrl+Shift+V)"));
    m_pasteAction->setStatusTip(tr("Paste (Ctrl+Shift+V)"));
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

    auto* gridAction = viewMenu->addAction(tr("Toggle 2x2 &Grid View"));
    gridAction->setShortcut(QKeySequence(Qt::CTRL | Qt::ALT | Qt::Key_G));
    connect(gridAction, &QAction::triggered, this, &MainWindow::toggleGridView);

    auto* moveTabAction = viewMenu->addAction(tr("Move Tab to &Other Pane"));
    moveTabAction->setShortcut(QKeySequence(Qt::CTRL | Qt::ALT | Qt::Key_M));
    connect(moveTabAction, &QAction::triggered, this, &MainWindow::moveTabToOtherPane);

    auto* detachTabAction = viewMenu->addAction(tr("&Detach Current Tab"));
    detachTabAction->setShortcut(QKeySequence(Qt::CTRL | Qt::ALT | Qt::Key_D));
    connect(detachTabAction, &QAction::triggered, this, &MainWindow::detachCurrentTab);

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

void MainWindow::showCommandPalette() {
    const QStringList commands = {
        tr("New Remote Session"),     tr("Open Local Terminal"), tr("Toggle Split View"), tr("Toggle 2x2 Grid View"),
        tr("Move Tab to Other Pane"), tr("Toggle Multi-Input"),  tr("Open Settings"),     tr("Manage Macros"),
        tr("Find in All Sessions"),   tr("Toggle Theme"),
    };

    bool accepted = false;
    const QString command =
        QInputDialog::getItem(this, tr("Command Palette"), tr("Action:"), commands, 0, true, &accepted);
    if (!accepted || command.isEmpty())
        return;

    if (command == tr("New Remote Session")) {
        SessionDialog dialog(this);
        if (dialog.exec() == QDialog::Accepted)
            onConnectSession(dialog.getSession());
    } else if (command == tr("Open Local Terminal")) {
        onNewLocalTerminal();
    } else if (command == tr("Toggle Split View")) {
        toggleSplitView();
    } else if (command == tr("Toggle 2x2 Grid View")) {
        toggleGridView();
    } else if (command == tr("Move Tab to Other Pane")) {
        moveTabToOtherPane();
    } else if (command == tr("Toggle Multi-Input")) {
        toggleMultiInputBar();
    } else if (command == tr("Open Settings")) {
        onOpenSettings();
    } else if (command == tr("Manage Macros")) {
        onManageMacros();
    } else if (command == tr("Find in All Sessions")) {
        onGlobalSearch();
    } else if (command == tr("Toggle Theme")) {
        toggleTheme();
    }
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

QList<QTabWidget*> MainWindow::allPanes() const {
    return {m_tabWidget, m_tabWidget2, m_tabWidget3, m_tabWidget4};
}

QList<QTabWidget*> MainWindow::visiblePanes() const {
    QList<QTabWidget*> panes = allPanes();
    if (m_paneLayoutMode == 0)
        return {m_tabWidget};
    if (m_paneLayoutMode == 1)
        return {m_tabWidget, m_tabWidget2};
    return panes;
}

QTabWidget* MainWindow::activePane() const {
    if (m_activePane && allPanes().contains(m_activePane) && visiblePanes().contains(m_activePane))
        return m_activePane;
    return m_tabWidget;
}

QTabWidget* MainWindow::otherPane(QTabWidget* pane) const {
    const QList<QTabWidget*> panes = visiblePanes();
    if (panes.size() < 2)
        return m_tabWidget2;
    const int index = panes.indexOf(pane);
    return panes[(index + 1 + panes.size()) % panes.size()];
}

void MainWindow::setPaneLayout(int mode, bool moveCurrentTab) {
    mode = qBound(0, mode, 2);

    // A hidden pane must not retain tabs when collapsing the layout: moving
    // them back keeps sessions reachable and makes the next layout switch
    // predictable.
    if (mode == 0 || mode == 1) {
        const int firstHiddenPane = mode == 0 ? 1 : 2;
        for (int i = firstHiddenPane; i < allPanes().size(); ++i) {
            QTabWidget* from = allPanes()[i];
            while (from->count() > 0) {
                QWidget* widget = from->widget(0);
                const QString title = from->tabText(0);
                from->removeTab(0);
                const int newIndex = m_tabWidget->addTab(widget, title);
                m_tabWidget->setCurrentIndex(newIndex);
            }
        }
    }

    m_paneLayoutMode = mode;
    const QList<QTabWidget*> panes = allPanes();
    for (int i = 0; i < panes.size(); ++i)
        panes[i]->setVisible(visiblePanes().contains(panes[i]));

    // Give only active grid rows/columns stretch. Hidden panes then collapse
    // completely, allowing the primary pane to occupy the full area.
    m_tabGrid->setRowStretch(0, mode == 2 ? 1 : 1);
    m_tabGrid->setRowStretch(1, mode == 2 ? 1 : 0);
    m_tabGrid->setColumnStretch(0, 1);
    m_tabGrid->setColumnStretch(1, mode == 0 ? 0 : 1);

    if (mode == 1 && moveCurrentTab && m_tabWidget2->count() == 0 && m_tabWidget->count() > 0) {
        const int index = m_tabWidget->currentIndex();
        if (index >= 0) {
            QWidget* widget = m_tabWidget->widget(index);
            const QString title = m_tabWidget->tabText(index);
            m_tabWidget->removeTab(index);
            const int newIndex = m_tabWidget2->addTab(widget, title);
            m_tabWidget2->setCurrentIndex(newIndex);
            m_activePane = m_tabWidget2;
        }
    } else if (!visiblePanes().contains(m_activePane)) {
        m_activePane = visiblePanes().first();
    }
}

void MainWindow::saveOpenTabs(QSettings& settings) const {
    QJsonArray tabs;
    const QTabWidget* active = activePane();
    for (int paneIndex = 0; paneIndex < allPanes().size(); ++paneIndex) {
        QTabWidget* pane = allPanes().at(paneIndex);
        for (int tabIndex = 0; tabIndex < pane->count(); ++tabIndex) {
            auto* tab = qobject_cast<TerminalTab*>(pane->widget(tabIndex));
            if (!tab)
                continue;
            QJsonObject entry;
            entry["pane"] = paneIndex;
            entry["active"] = pane == active && tabIndex == pane->currentIndex();
            entry["session"] = tab->getSession().toJson();
            tabs.append(entry);
        }
    }
    settings.setValue("window/openTabs", QJsonDocument(tabs).toJson(QJsonDocument::Compact));
}

void MainWindow::restoreOpenTabs(const QSettings& settings) {
    if (!settings.contains("window/openTabs"))
        return;

    const QJsonDocument document = QJsonDocument::fromJson(settings.value("window/openTabs").toByteArray());
    if (!document.isArray())
        return;

    // setupUi creates one local tab for a first launch. If a saved workspace
    // exists, replace that bootstrap tab with exactly the saved workspace.
    for (QTabWidget* pane : allPanes()) {
        while (pane->count() > 0) {
            QWidget* widget = pane->widget(0);
            pane->removeTab(0);
            delete widget;
        }
    }

    QList<QPair<QTabWidget*, bool>> restored;
    for (const QJsonValue& value : document.array()) {
        if (!value.isObject())
            continue;
        const QJsonObject entry = value.toObject();
        const int paneIndex = entry.value("pane").toInt(0);
        if (paneIndex < 0 || paneIndex >= allPanes().size())
            continue;
        const QJsonObject sessionObject = entry.value("session").toObject();
        if (sessionObject.isEmpty())
            continue;

        QTabWidget* target = allPanes().at(paneIndex);
        if (!visiblePanes().contains(target))
            target = m_tabWidget;
        m_activePane = target;
        onConnectSession(Session::fromJson(sessionObject));
        restored.append({target, entry.value("active").toBool(false)});
    }

    for (const auto& item : restored) {
        if (item.second) {
            m_activePane = item.first;
            item.first->setCurrentIndex(item.first->count() - 1);
            break;
        }
    }
    if (restored.isEmpty())
        m_activePane = visiblePanes().first();

    // Restoring an empty workspace removes the last tab after setupUi() has
    // already initialized the welcome overlay. Refresh it explicitly so the
    // empty initial state is visible on the first application launch too.
    updateWelcomeScreen();
}

void MainWindow::toggleSplitView() {
    if (m_paneLayoutMode == 1)
        setPaneLayout(0);
    else
        setPaneLayout(1, true);
}

void MainWindow::toggleGridView() {
    if (m_paneLayoutMode == 2)
        setPaneLayout(0);
    else
        setPaneLayout(2);
}

void MainWindow::moveTabToOtherPane() {
    QTabWidget* from = activePane();
    QTabWidget* to = otherPane(from);
    if (!visiblePanes().contains(to)) {
        setPaneLayout(1, true);
        to = otherPane(from);
    }
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

void MainWindow::detachCurrentTab() {
    QTabWidget* from = activePane();
    if (!from)
        return;
    const int index = from->currentIndex();
    if (index < 0)
        return;
    auto* tab = qobject_cast<TerminalTab*>(from->widget(index));
    if (!tab || m_detachedTabs.contains(tab))
        return;

    const QString title = from->tabText(index);
    from->removeTab(index);
    auto* window = new DetachedTabWindow(title, this);
    tab->setParent(window);
    window->setCentralWidget(tab);
    window->resize(900, 600);
    m_detachedTabs.insert(tab, window);
    window->closeHandler = [this, tab, title](DetachedTabWindow* detached) {
        if (!m_detachedTabs.remove(tab))
            return;
        QWidget* content = detached->takeCentralWidget();
        QTabWidget* target = activePane();
        if (!content || !target)
            return;
        content->setParent(target);
        const int newIndex = target->addTab(content, title);
        target->setCurrentIndex(newIndex);
        m_activePane = target;
        content->show();
    };
    window->show();
    tab->show();
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
            act->setIcon(QIcon(":/icons/macros.svg"));
            connect(act, &QAction::triggered, this, [this, texts, i]() {
                auto* tab = currentTerminalTab();
                if (tab)
                    tab->sendRaw(texts[i]);
            });
        }
    }

    m_macrosMenu->addSeparator();
    auto* manageAct = m_macrosMenu->addAction(tr("Manage Macros..."));
    manageAct->setIcon(QIcon(":/icons/gear.svg"));
    connect(manageAct, &QAction::triggered, this, &MainWindow::onManageMacros);
    rebuildMacrosRibbon();
}

void MainWindow::rebuildMacrosRibbon() {
    if (!m_macrosRibbonLayout || !m_macrosMenu)
        return;

    while (QLayoutItem* item = m_macrosRibbonLayout->takeAt(0)) {
        if (QWidget* widget = item->widget())
            widget->deleteLater();
        delete item;
    }

    for (QAction* action : m_macrosMenu->actions()) {
        if (action->isSeparator())
            continue;
        auto* button = new QToolButton(m_macrosRibbonPage);
        button->setDefaultAction(action);
        button->setToolButtonStyle(Qt::ToolButtonTextUnderIcon);
        button->setIconSize(QSize(20, 20));
        button->setMinimumSize(76, 61);
        button->setAutoRaise(true);
        m_macrosRibbonLayout->addWidget(button);
    }
    m_macrosRibbonLayout->addStretch();
}

void MainWindow::setRibbonExpanded(bool expanded) {
    if (!m_ribbonTabs || !m_ribbonToolBar || !m_ribbonToggle)
        return;

    m_ribbonTabs->setFixedHeight(expanded ? 94 : 30);
    m_ribbonToolBar->setFixedHeight(expanded ? 98 : 30);
    m_ribbonToggle->setIcon(QIcon(expanded ? ":/icons/chevron-up.svg" : ":/icons/chevron-down.svg"));
    m_ribbonToggle->setToolTip(m_ribbonPinned ? tr("Collapse Ribbon") : tr("Keep Ribbon expanded"));
}

bool MainWindow::eventFilter(QObject* watched, QEvent* event) {
    if (!m_ribbonPinned && m_ribbonToolBar && m_ribbonTabs && event->type() == QEvent::MouseButtonPress) {
        auto* widget = qobject_cast<QWidget*>(watched);
        if (widget && !m_ribbonToolBar->isAncestorOf(widget) && widget != m_ribbonToolBar) {
            setRibbonExpanded(false);
        }
    }
    return QMainWindow::eventFilter(watched, event);
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
    int total = 0;
    for (QTabWidget* pane : allPanes())
        total += pane->count();
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
        for (QTabWidget* pane : allPanes())
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
