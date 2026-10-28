#include "mainwindow.h"
#include "terminalappearance.h"
#include "sessionssidebar.h"
#include "sftpsidebar.h"
#include "terminaltab.h"
#include "terminalpane.h"
#include <QDragEnterEvent>
#include <QDragMoveEvent>
#include <QDropEvent>
#include <QSignalBlocker>
#include <qtermwidget.h>
#include "sshconnection.h"
#include "sessiondialog.h"
#include "settingsdialog.h"
#include "keyboardshortcuts.h"
#include "updater.h"
#include "masterpasswordmanager.h"
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
#include <QClipboard>
#include <QPalette>
#include <QStyle>
#include <QStyleFactory>
#include <QGuiApplication>
#include <QToolBar>
#include <QMenuBar>
#include <QAction>
#include <QStatusBar>
#include <QFontDialog>
#include <QInputDialog>
#include <QSettings>
#include <QDateTime>
#include <QLineEdit>
#include <QComboBox>
#include <QCloseEvent>
#include <QEvent>
#include <QMouseEvent>
#include <QMessageBox>
#include <QFileDialog>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QFile>
#include <QTimer>
#include <QTextStream>
#include <QTextBrowser>
#include "localizedmessagebox.h"
#include <QMenu>
#include <QSize>
#include <QRegularExpression>
#include <QListWidget>
#include <QDialogButtonBox>
#include <QFileDialog>
#include <QFile>
#include <functional>
#include <QSharedPointer>
#include <QPointer>
#include <QSet>
#include <QUuid>

namespace {
class RibbonStatusContext : public QWidget {
public:
    explicit RibbonStatusContext(QTabBar* tabBar, QWidget* parent) : QWidget(parent), m_tabBar(tabBar) {
    }

    QSize sizeHint() const override {
        QSize size = QWidget::sizeHint();
        // QTabWidget bottom-aligns its corner widget. Match the tab header's
        // height so the session status shares the tabs' vertical alignment.
        size.setHeight(qMax(size.height(), m_tabBar->sizeHint().height()));
        return size;
    }

private:
    QTabBar* m_tabBar;
};

void logMacroEvent(const Session& session, const QString& message) {
    QSettings settings;
    QStringList entries = settings.value(QStringLiteral("macros/log")).toStringList();
    entries.prepend(
        QStringLiteral("%1 | %2 | %3").arg(QDateTime::currentDateTime().toString(Qt::ISODate), session.name, message));
    while (entries.size() > 200)
        entries.removeLast();
    settings.setValue(QStringLiteral("macros/log"), entries);
}

QString expandMacroText(QString text, const Session& session) {
    const QList<QPair<QString, QChar>> controls = {
        {QStringLiteral("{{BREAK}}"), QChar(0x1e)},      {QStringLiteral("{{CTRL+C}}"), QChar(0x03)},
        {QStringLiteral("{{CTRL+D}}"), QChar(0x04)},     {QStringLiteral("{{CTRL+Z}}"), QChar(0x1a)},
        {QStringLiteral("{{ESC}}"), QChar(0x1b)},        {QStringLiteral("{{ENTER}}"), QChar('\r')},
        {QStringLiteral("{{TAB}}"), QChar('\t')},        {QStringLiteral("{{BACKSPACE}}"), QChar(0x7f)},
        {QStringLiteral("{{TELNET-ESC}}"), QChar(0x1d)},
    };
    for (const auto& control : controls)
        text.replace(control.first, control.second);

    const QList<QPair<QString, QString>> variables = {
        {QStringLiteral("{{HOST}}"), session.host},
        {QStringLiteral("{{USER}}"), session.user},
        {QStringLiteral("{{PORT}}"), QString::number(session.port)},
        {QStringLiteral("{{SESSION}}"), session.name},
        {QStringLiteral("{{REMOTE_DIR}}"), session.remoteDirectory},
        {QStringLiteral("{{SERIAL_PORT}}"), session.serialPort},
    };
    for (const auto& variable : variables)
        text.replace(variable.first, variable.second);

    const QRegularExpression environmentPattern(QStringLiteral(R"(\{\{ENV:([A-Za-z_][A-Za-z0-9_]*)\}\})"));
    QRegularExpressionMatch environmentMatch = environmentPattern.match(text);
    while (environmentMatch.hasMatch()) {
        const QString token = environmentMatch.captured(0);
        const QString value = qEnvironmentVariable(environmentMatch.captured(1).toUtf8().constData());
        text.replace(token, value);
        environmentMatch = environmentPattern.match(text);
    }
    return text;
}

void sendMacro(TerminalTab* tab, const QString& macro) {
    if (!tab)
        return;

    const Session session = tab->session();
    logMacroEvent(session, QObject::tr("Macro started"));
    struct MacroStep {
        enum class Type { Text, Pause, Expect, Respond } type = Type::Text;
        QString value;
    };
    QList<MacroStep> steps;
    // Source line breaks format the macro; only explicit tokens send Enter.
    QString script = macro;
    script.remove('\r');
    script.remove('\n');
    const QRegularExpression tokenPattern(
        QStringLiteral(R"(\{\{(PAUSE:[ \t]*\d{1,6}[ \t]*|EXPECT:[^}]*|RESPOND:[^}]*)\}\})"));
    QRegularExpressionMatchIterator matches = tokenPattern.globalMatch(script);
    int offset = 0;
    while (matches.hasNext()) {
        const QRegularExpressionMatch match = matches.next();
        if (match.capturedStart() > offset)
            steps.append({MacroStep::Type::Text, script.mid(offset, match.capturedStart() - offset)});
        const QString token = match.captured(1);
        if (token.startsWith(QStringLiteral("PAUSE:")))
            steps.append({MacroStep::Type::Pause, token.mid(6)});
        else if (token.startsWith(QStringLiteral("EXPECT:")))
            steps.append({MacroStep::Type::Expect, token.mid(7)});
        else
            steps.append({MacroStep::Type::Respond, token.mid(8)});
        offset = match.capturedEnd();
    }
    if (offset < script.size())
        steps.append({MacroStep::Type::Text, script.mid(offset)});

    const auto runner = QSharedPointer<std::function<void(int)>>::create();
    const QWeakPointer<std::function<void(int)>> weakRunner = runner;
    *runner = [tab, session, steps, weakRunner](int index) {
        if (index >= steps.size()) {
            logMacroEvent(session, QObject::tr("Macro completed"));
            return;
        }
        const MacroStep& step = steps.at(index);
        if (step.type == MacroStep::Type::Text) {
            if (!step.value.isEmpty())
                tab->sendRaw(expandMacroText(step.value, session));
            if (const auto next = weakRunner.toStrongRef())
                (*next)(index + 1);
        } else if (step.type == MacroStep::Type::Respond) {
            tab->sendRaw(expandMacroText(step.value, session));
            if (const auto next = weakRunner.toStrongRef())
                (*next)(index + 1);
        } else if (step.type == MacroStep::Type::Pause) {
            // Retain the executor until this asynchronous step resumes. Its
            // own callback keeps only a weak reference, so this creates no cycle.
            const auto next = weakRunner.toStrongRef();
            QTimer::singleShot(step.value.toInt(), tab, [next, index]() {
                if (next)
                    (*next)(index + 1);
            });
        } else {
            const QRegularExpression expression(step.value);
            if (!expression.isValid()) {
                logMacroEvent(session,
                              QObject::tr("Macro error: invalid EXPECT expression: %1").arg(expression.errorString()));
                tab->reportMacroMessage(QObject::tr("Invalid EXPECT expression: %1").arg(expression.errorString()));
                return;
            }
            const auto buffer = QSharedPointer<QByteArray>::create();
            const auto completed = QSharedPointer<bool>::create(false);
            const auto connection = QSharedPointer<QMetaObject::Connection>::create();
            auto* timeout = new QTimer(tab);
            timeout->setSingleShot(true);
            timeout->setInterval(30000);
            const auto finish = QSharedPointer<std::function<void(bool)>>::create();
            const auto next = weakRunner.toStrongRef();
            *finish = [tab, session, next, index, completed, connection, timeout](bool matched) {
                if (*completed)
                    return;
                *completed = true;
                QObject::disconnect(*connection);
                timeout->stop();
                timeout->deleteLater();
                if (matched) {
                    if (next)
                        (*next)(index + 1);
                } else {
                    logMacroEvent(session, QObject::tr("Macro error: EXPECT timed out"));
                    tab->reportMacroMessage(QObject::tr("EXPECT timed out after 30 seconds"));
                }
            };
            *connection = QObject::connect(tab, &TerminalTab::terminalDataReceived, tab,
                                           [buffer, expression, finish](const QByteArray& data) {
                                               buffer->append(data);
                                               if (expression.match(QString::fromUtf8(*buffer)).hasMatch())
                                                   (*finish)(true);
                                               else if (buffer->size() > 65536)
                                                   buffer->remove(0, buffer->size() - 32768);
                                           });
            QObject::connect(timeout, &QTimer::timeout, tab, [finish]() { (*finish)(false); });
            timeout->start();
        }
    };
    (*runner)(0);
}

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
        TerminalAppearance::initializeWindow(this);
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
    TerminalAppearance::initializeWindow(this);
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

    const bool restoredGroups = restorePanelWorkspaces(settings);
    if (!restoredGroups) {
        const auto savedTree = QJsonDocument::fromJson(settings.value("window/paneTree").toByteArray()).object();
        if (!restorePaneTree(savedTree)) {
            const int mode = settings.value("window/tabLayoutMode", 0).toInt();
            if (mode == 2) {
                restorePaneTree(
                    {{"orientation", "vertical"},
                     {"children",
                      QJsonArray{
                          QJsonObject{{"orientation", "horizontal"},
                                      {"children", QJsonArray{QJsonObject{{"pane", 0}}, QJsonObject{{"pane", 1}}}}},
                          QJsonObject{{"orientation", "horizontal"},
                                      {"children", QJsonArray{QJsonObject{{"pane", 2}}, QJsonObject{{"pane", 3}}}}}}}});
            } else {
                setPaneLayout(mode);
            }
        }
    }
    m_sidebarWidth = settings.value("window/sidebarWidth", 350).toInt();
    if (settings.value("window/sidebarHidden", false).toBool())
        m_sidebarContainer->hide();
    if (!restoredGroups) {
        const int savedPane = settings.value("window/activePane", 0).toInt();
        const QList<QTabWidget*> panes = visiblePanes();
        if (savedPane >= 0 && savedPane < panes.size())
            m_activePane = panes[savedPane];
        restoreOpenTabs(settings);
    }

    setWindowTitle("BanchoXterm");
}

MainWindow::~MainWindow() {
    // QWidget destroys its children after this class's members. Stop tab
    // callbacks before the synchronization and diagnostic containers go away.
    for (auto* tab : findChildren<TerminalTab*>())
        disconnect(tab, nullptr, this, nullptr);
}

void MainWindow::closeEvent(QCloseEvent* event) {
    bool hasActive = false;
    for (QTabWidget* pane : allOpenPanes()) {
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

    for (auto* tab : m_detachedTabs.keys())
        hasActive = hasActive || tab->isSessionActive();
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
    settings.setValue("window/paneTree", QJsonDocument(paneTree(m_paneRoot)).toJson(QJsonDocument::Compact));
    settings.setValue("window/sidebarHidden", m_sidebarContainer->isHidden());
    settings.setValue("window/sidebarWidth",
                      m_sidebarContainer->isHidden() ? m_sidebarWidth : m_sidebarContainer->width());
    settings.setValue("window/activePane", qMax(0, visiblePanes().indexOf(activePane())));
    settings.setValue("window/ribbonPinned", m_ribbonPinned);
    saveOpenTabs(settings);
    savePanelWorkspaces(settings);
    QMainWindow::closeEvent(event);
}

void MainWindow::reattachDetachedTabs(const QString& groupId) {
    if (m_detachedTabs.isEmpty())
        return;

    const auto detachedTabs = m_detachedTabs;
    for (auto it = detachedTabs.cbegin(); it != detachedTabs.cend(); ++it) {
        TerminalTab* tab = it.key();
        auto* window = static_cast<DetachedTabWindow*>(it.value());
        if (!tab || !window || (!groupId.isEmpty() && tab->property("panelGroupId").toString() != groupId))
            continue;

        m_detachedTabs.remove(tab);
        QWidget* content = window->takeCentralWidget();
        if (content) {
            QTabWidget* target = activePane();
            const QString owner = tab->property("panelGroupId").toString();
            for (int i = 0; i < m_panelGroups.size(); ++i)
                if (m_panelGroups[i].id == owner && i != m_currentPanelGroup)
                    target = m_panelGroups[i].activePane ? m_panelGroups[i].activePane : m_panelGroups[i].panes.first();
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
    connect(newRemoteAction, &QAction::triggered, this, [this]() { m_sessionsSidebar->onNewSession(); });

    auto* localAction = toolBar->addAction(QIcon(":/icons/terminal.svg"), tr("Local"));
    localAction->setToolTip(tr("Open a local terminal"));
    connect(localAction, &QAction::triggered, this, &MainWindow::onNewLocalTerminal);

    toolBar->addSeparator();

    addToolbarSection(tr("VIEW"));
    auto* splitAction = findChild<QAction*>("shortcut_toggleSplit");
    splitAction->setIcon(QIcon(":/icons/split.svg"));
    splitAction->setIconText(tr("Split left/right"));
    toolBar->addAction(splitAction);

    auto* gridAction = findChild<QAction*>("shortcut_toggleGrid");
    gridAction->setIcon(QIcon(":/icons/grid.svg"));
    gridAction->setIconText(tr("Grid"));
    toolBar->addAction(gridAction);

    auto* moveAction = findChild<QAction*>("shortcut_moveTab");
    moveAction->setIcon(QIcon(":/icons/move.svg"));
    moveAction->setIconText(tr("Move"));
    toolBar->addAction(moveAction);

    toolBar->addSeparator();

    addToolbarSection(tr("TOOLS"));
    auto* multiAction = m_multiInputAction;
    toolBar->addAction(multiAction);
    multiAction->setCheckable(true);
    multiAction->setToolTip(tr("Send input to all terminal sessions"));
    m_multiInputAction = multiAction;

    auto* settingsAction = toolBar->addAction(QIcon(":/icons/gear.svg"), tr("Settings"));
    connect(settingsAction, &QAction::triggered, this, &MainWindow::onOpenSettings);

    const QList<QAction*> ribbonActions = {newRemoteAction, localAction, splitAction,   gridAction,
                                           moveAction,      multiAction, settingsAction};
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
    const QList<QAction*> persistentRibbonActions = {newRemoteAction, localAction, splitAction,   gridAction,
                                                     moveAction,      multiAction, settingsAction};
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
    // The Ribbon supplies its own selected-tab underline. The native base
    // paints extra frame lines that break around tabs and corner widgets.
    ribbonTabs->tabBar()->setDrawBase(false);
    ribbonTabs->tabBar()->setObjectName("ribbonTabBar");

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
        if (action->menu()) {
            // Open the menu directly instead of adding its own menu action
            // as the button's default entry.
            button->setText(action->text());
            button->setIcon(action->icon());
            button->setToolTip(action->toolTip());
            button->setMenu(action->menu());
            button->setPopupMode(QToolButton::InstantPopup);
        } else {
            button->setDefaultAction(action);
        }
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

    auto panelsPage = makeRibbonPage(tr("Panels"));
    auto panelAction = [&](const QString& name, const QString& text, const char* icon, auto slot) {
        auto* action = new QAction(QIcon(icon), text, this);
        action->setObjectName(name);
        connect(action, &QAction::triggered, this, slot);
        addRibbonAction(panelsPage.second, action);
        return action;
    };
    addRibbonAction(panelsPage.second, splitAction);
    panelAction("splitPaneVertically", tr("Split top/bottom"), ":/icons/grid.svg", &MainWindow::splitPaneVertically);
    auto* layoutMenu = new QMenu(this);
    layoutMenu->addAction(gridAction);
    auto layoutAction = [&](const QString& name, const QString& title, const char* icon, auto slot) {
        auto* action = new QAction(QIcon(icon), title, this);
        action->setObjectName(name);
        connect(action, &QAction::triggered, this, slot);
        layoutMenu->addAction(action);
    };
    layoutAction("equalizePaneSizes", tr("Equalize sizes"), ":/icons/move.svg", &MainWindow::equalizePaneSizes);
    layoutAction("restoreSinglePane", tr("Single pane"), ":/icons/terminal.svg", &MainWindow::restoreSinglePane);
    layoutAction("closePane", tr("Close pane"), ":/icons/close.svg", &MainWindow::closePane);
    auto* layoutMenuAction = new QAction(QIcon(":/icons/grid.svg"), tr("Layout"), this);
    layoutMenuAction->setMenu(layoutMenu);
    addRibbonAction(panelsPage.second, layoutMenuAction);

    auto* detachRibbonAction = findChild<QAction*>("shortcut_detachTab");
    detachRibbonAction->setIcon(QIcon(":/icons/detach.svg"));
    detachRibbonAction->setIconText(tr("Detach"));
    auto* tabsMenu = new QMenu(this);
    tabsMenu->addAction(moveAction);
    tabsMenu->addAction(detachRibbonAction);
    auto* tabsMenuAction = new QAction(QIcon(":/icons/move.svg"), tr("Tabs"), this);
    tabsMenuAction->setMenu(tabsMenu);
    addRibbonAction(panelsPage.second, tabsMenuAction);

    m_syncAction = panelAction("synchronizePanels", tr("Synchronize input"), ":/icons/multiinput.svg",
                               &MainWindow::configureSynchronization);
    m_syncAction->setCheckable(true);
    auto* syncTargetsAction = new QAction(QIcon(":/icons/multiinput.svg"), tr("Choose synchronized sessions"), this);
    syncTargetsAction->setObjectName("chooseSynchronizationTargets");
    connect(syncTargetsAction, &QAction::triggered, this, &MainWindow::chooseSynchronizationTargets);
    auto* syncMenu = new QMenu(this);
    syncMenu->addAction(syncTargetsAction);
    syncMenu->addAction(m_syncAction);
    for (auto* button : panelsPage.first->findChildren<QToolButton*>()) {
        if (button->defaultAction() == m_syncAction) {
            button->setMenu(syncMenu);
            button->setPopupMode(QToolButton::InstantPopup);
        }
    }
    m_savedPanelGroupsMenu = new QMenu(this);
    auto* savedGroupsAction = new QAction(QIcon(":/icons/folder.svg"), tr("Open group"), this);
    savedGroupsAction->setObjectName("openSavedPanelGroup");
    savedGroupsAction->setMenu(m_savedPanelGroupsMenu);
    addRibbonAction(panelsPage.second, savedGroupsAction);
    panelAction("newPanelGroup", tr("New group"), ":/icons/add.svg", &MainWindow::createPanelGroup);
    panelAction("managePanelGroups", tr("Manage groups"), ":/icons/gear.svg", &MainWindow::managePanelGroups);
    rebuildPanelGroupsMenu();

    auto viewPage = makeRibbonPage(tr("View"));
    auto* sidebarAction = new QAction(QIcon(":/icons/server.svg"), tr("Toggle sidebar"), this);
    sidebarAction->setObjectName("toggleSidebar");
    connect(sidebarAction, &QAction::triggered, this, &MainWindow::toggleSidebar);

    auto toolsPage = makeRibbonPage(tr("Tools"));
    addRibbonAction(toolsPage.second, multiAction);
    addRibbonAction(toolsPage.second, m_sensitiveMultiInputAction);
    addRibbonAction(toolsPage.second, m_captureShortcutsAction);
    addRibbonAction(toolsPage.second, settingsAction);
    addRibbonAction(toolsPage.second, m_copyAction);
    addRibbonAction(toolsPage.second, m_pasteAction);
    addRibbonAction(toolsPage.second, m_specialCharactersAction);
    auto* clearRibbonAction = new QAction(QIcon(":/icons/delete.svg"), tr("Clear"), this);
    clearRibbonAction->setToolTip(tr("Clear terminal scrollback"));
    connect(clearRibbonAction, &QAction::triggered, this, [this]() {
        if (auto* tab = currentTerminalTab())
            tab->clearTerminal();
    });
    addRibbonAction(toolsPage.second, clearRibbonAction);

    m_globalSearchAction->setIcon(QIcon(":/icons/server.svg"));
    m_globalSearchAction->setIconText(tr("Search all"));
    addRibbonAction(toolsPage.second, m_globalSearchAction);

    auto* themeRibbonAction = new QAction(QIcon(":/icons/palette.svg"), tr("Theme"), this);
    themeRibbonAction->setToolTip(tr("Toggle light and dark theme"));
    connect(themeRibbonAction, &QAction::triggered, this, &MainWindow::toggleTheme);
    addRibbonAction(viewPage.second, themeRibbonAction);

    auto* diagnosticsAction = new QAction(QIcon(":/icons/gear.svg"), tr("Diagnostics"), this);
    diagnosticsAction->setToolTip(tr("Inspect the active session diagnostics"));
    connect(diagnosticsAction, &QAction::triggered, this, &MainWindow::showDiagnostics);
    addRibbonAction(toolsPage.second, diagnosticsAction);

    auto* clearClipboardAction = new QAction(QIcon(":/icons/delete.svg"), tr("Clear Clipboard"), this);
    clearClipboardAction->setToolTip(tr("Remove copied text from the system clipboard"));
    connect(clearClipboardAction, &QAction::triggered, this, [this]() {
        if (auto* clipboard = QApplication::clipboard()) {
            clipboard->clear(QClipboard::Clipboard);
            clipboard->clear(QClipboard::Selection);
        }
        statusBar()->showMessage(tr("Clipboard cleared"), 3000);
    });
    addRibbonAction(toolsPage.second, clearClipboardAction);

    addRibbonAction(toolsPage.second, m_lockAction);

    auto macrosPage = makeRibbonPage(tr("Macros"));
    m_macrosRibbonPage = macrosPage.first;
    m_macrosRibbonLayout = macrosPage.second;
    rebuildMacrosRibbon();

    auto helpPage = makeRibbonPage(tr("Help"));
    auto* aboutRibbonAction = new QAction(QIcon(":/icons/about.svg"), tr("About"), this);
    connect(aboutRibbonAction, &QAction::triggered, this, &MainWindow::showAbout);
    addRibbonAction(helpPage.second, aboutRibbonAction);
    auto* updateRibbonAction = new QAction(QIcon(":/icons/refresh.svg"), tr("Updates"), this);
    connect(updateRibbonAction, &QAction::triggered, this, [this]() { Updater::checkForUpdates(this); });
    addRibbonAction(helpPage.second, updateRibbonAction);

    // The Ribbon is the only visible command surface. Keep the legacy menus
    // alive as an action container for compatibility, but remove the native
    // menu bar from the window layout entirely after all actions are created.
    if (auto* legacyMenuBar = menuBar()) {
        legacyMenuBar->setNativeMenuBar(false);
        legacyMenuBar->hide();
        legacyMenuBar->setFixedHeight(0);
        legacyMenuBar->setMaximumHeight(0);
    }

    ribbonTabs->setFixedHeight(94);

    auto* sessionContext = new QWidget(ribbonTabs);
    sessionContext->setObjectName("sessionContextWidget");
    auto* sessionContextLayout = new QHBoxLayout(sessionContext);
    sessionContextLayout->setContentsMargins(6, 0, 8, 6);
    sessionContextLayout->setSpacing(6);
    m_contextProtocolLabel = new QLabel(tr("NO SESSION"), sessionContext);
    m_contextProtocolLabel->setObjectName("sessionContextProtocol");
    m_contextSessionLabel = new QLabel(tr("Open a session to begin"), sessionContext);
    m_contextSessionLabel->setObjectName("sessionContextSession");
    sessionContextLayout->addWidget(m_contextProtocolLabel);
    sessionContextLayout->addWidget(m_contextSessionLabel);
    auto* statusContext = new RibbonStatusContext(ribbonTabs->tabBar(), ribbonTabs);
    statusContext->setObjectName("statusContextWidget");
    auto* statusContextLayout = new QHBoxLayout(statusContext);
    statusContextLayout->setContentsMargins(8, 0, 2, 0);
    statusContextLayout->setSpacing(14);
    statusContextLayout->addWidget(sessionContext);
    m_contextStateLabel = new QLabel(statusContext);
    m_contextStateLabel->setObjectName("sessionContextState");
    m_contextStateLabel->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    m_contextStateLabel->setContentsMargins(0, 0, 0, 6);
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

    // Main splitter
    m_mainSplitter = new QSplitter(Qt::Horizontal, centralWidget);
    connect(m_mainSplitter, &QSplitter::splitterMoved, this, [this]() {
        const int width = m_mainSplitter->sizes().value(0);
        if (width > 0)
            m_sidebarWidth = width;
    });
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

    // Terminal leaves live in a resizable splitter tree; the grid also hosts
    // the welcome overlay in the same cell.
    m_tabSplitter = new QWidget(m_mainSplitter);
    auto* workspaceLayout = new QVBoxLayout(m_tabSplitter);
    workspaceLayout->setContentsMargins(0, 0, 0, 0);
    workspaceLayout->setSpacing(0);
    m_panelGroupTabs = new QTabBar(m_tabSplitter);
    m_panelGroupTabs->setObjectName("panelGroupTabBar");
    m_panelGroupTabs->setExpanding(false);
    m_panelGroupTabs->setTabsClosable(true);
    m_panelGroupTabs->setAcceptDrops(true);
    m_panelGroupTabs->setElideMode(Qt::ElideRight);
    m_panelGroupTabs->setContextMenuPolicy(Qt::CustomContextMenu);
    m_panelGroupTabs->hide();
    workspaceLayout->addWidget(m_panelGroupTabs);
    auto* paneArea = new QWidget(m_tabSplitter);
    workspaceLayout->addWidget(paneArea, 1);
    m_tabGrid = new QGridLayout(paneArea);
    m_tabGrid->setContentsMargins(0, 0, 0, 0);
    m_tabGrid->setSpacing(1);

    m_paneRoot = new QSplitter(Qt::Horizontal, m_tabSplitter);
    m_paneRoot->setObjectName("terminalPaneRoot");
    m_paneRoot->setChildrenCollapsible(false);
    m_tabGrid->addWidget(m_paneRoot, 0, 0);
    m_tabWidget = createTerminalPane();
    m_tabWidget2 = createTerminalPane();
    m_tabWidget3 = createTerminalPane();
    m_tabWidget4 = createTerminalPane();
    m_paneRoot->addWidget(m_tabWidget);

    m_welcomeWidget = new QWidget(m_tabSplitter);
    m_welcomeWidget->setObjectName("welcomeScreen");
    auto* welcomeLayout = new QVBoxLayout(m_welcomeWidget);
    welcomeLayout->setContentsMargins(32, 28, 32, 32);
    welcomeLayout->setSpacing(0);

    auto* welcomeContent = new QWidget(m_welcomeWidget);
    welcomeContent->setObjectName("welcomeContent");
    welcomeContent->setMaximumWidth(980);
    auto* contentLayout = new QVBoxLayout(welcomeContent);
    contentLayout->setContentsMargins(0, 0, 0, 0);
    contentLayout->setSpacing(24);

    auto* welcomeHeader = new QHBoxLayout();
    welcomeHeader->setSpacing(16);
    auto* welcomeLogo = new QLabel(welcomeContent);
    welcomeLogo->setObjectName("welcomeLogo");
    welcomeLogo->setPixmap(QIcon(":/icons/logo.svg").pixmap(QSize(36, 36)));
    welcomeLogo->setFixedSize(52, 52);
    welcomeHeader->addWidget(welcomeLogo, 0, Qt::AlignTop);

    auto* welcomeHeading = new QVBoxLayout();
    welcomeHeading->setSpacing(3);
    auto* welcomeTitle = new QLabel(tr("Welcome to BanchoXterm"), welcomeContent);
    welcomeTitle->setObjectName("welcomeTitle");
    welcomeHeading->addWidget(welcomeTitle);
    auto* welcomeSubtitle = new QLabel(tr("Your workspace for terminals and remote connections."), welcomeContent);
    welcomeSubtitle->setObjectName("welcomeSubtitle");
    welcomeSubtitle->setWordWrap(true);
    welcomeHeading->addWidget(welcomeSubtitle);
    welcomeHeader->addLayout(welcomeHeading, 1);
    contentLayout->addLayout(welcomeHeader);

    auto* welcomeCards = new QHBoxLayout();
    welcomeCards->setSpacing(18);

    auto* connectCard = new QFrame(welcomeContent);
    connectCard->setObjectName("welcomeCard");
    connectCard->setMinimumWidth(300);
    auto* connectLayout = new QVBoxLayout(connectCard);
    connectLayout->setContentsMargins(24, 22, 24, 22);
    connectLayout->setSpacing(12);
    auto* connectTitle = new QLabel(tr("Connect"), connectCard);
    connectTitle->setObjectName("welcomeCardTitle");
    connectLayout->addWidget(connectTitle);
    auto* connectDescription =
        new QLabel(tr("Open a local shell or configure a connection to another system."), connectCard);
    connectDescription->setObjectName("welcomeCardDescription");
    connectDescription->setWordWrap(true);
    connectLayout->addWidget(connectDescription);
    connectLayout->addSpacing(4);

    auto* welcomeRemoteButton = new QPushButton(QIcon(":/icons/add.svg"), tr("New Session"), connectCard);
    welcomeRemoteButton->setObjectName("primaryButton");
    welcomeRemoteButton->setMinimumHeight(42);
    welcomeRemoteButton->setIconSize(QSize(18, 18));
    connectLayout->addWidget(welcomeRemoteButton);
    auto* welcomeLocalButton = new QPushButton(QIcon(":/icons/terminal.svg"), tr("Local Terminal"), connectCard);
    welcomeLocalButton->setObjectName("welcomeSecondaryButton");
    welcomeLocalButton->setMinimumHeight(42);
    welcomeLocalButton->setIconSize(QSize(18, 18));
    connectLayout->addWidget(welcomeLocalButton);
    connectLayout->addStretch();

    auto* protocolLabel = new QLabel(tr("SSH  ·  Telnet  ·  Serial  ·  RDP  ·  VNC"), connectCard);
    protocolLabel->setObjectName("welcomeProtocols");
    protocolLabel->setWordWrap(true);
    connectLayout->addWidget(protocolLabel);
    welcomeCards->addWidget(connectCard, 2);

    auto* recentCard = new QFrame(welcomeContent);
    recentCard->setObjectName("welcomeCard");
    recentCard->setMinimumWidth(390);
    auto* recentLayout = new QVBoxLayout(recentCard);
    recentLayout->setContentsMargins(24, 22, 24, 22);
    recentLayout->setSpacing(9);

    auto* recentHeader = new QHBoxLayout();
    auto* recentTitle = new QLabel(tr("Recent sessions"), recentCard);
    recentTitle->setObjectName("welcomeCardTitle");
    recentHeader->addWidget(recentTitle);
    recentHeader->addStretch();
    auto* allSessionsButton = new QPushButton(tr("View all"), recentCard);
    allSessionsButton->setObjectName("welcomeLinkButton");
    allSessionsButton->setFlat(true);
    recentHeader->addWidget(allSessionsButton);
    recentLayout->addLayout(recentHeader);

    const QList<Session> savedSessions = SessionManager::loadSessions();
    const QStringList recentNames = QSettings().value("sessions/recent").toStringList();
    int recentCount = 0;
    for (const QString& recentName : recentNames) {
        for (const Session& recentSession : savedSessions) {
            if (recentSession.name != recentName)
                continue;
            QString endpoint;
            if (recentSession.type == SessionType::Local) {
                endpoint = recentSession.shellPath.isEmpty() ? tr("Local shell") : recentSession.shellPath;
            } else {
                endpoint = recentSession.user.isEmpty()
                               ? recentSession.host
                               : QStringLiteral("%1@%2").arg(recentSession.user, recentSession.host);
                if (recentSession.port > 0)
                    endpoint += QStringLiteral(":%1").arg(recentSession.port);
            }
            const QString favorite = recentSession.favorite ? QStringLiteral("★  ") : QString();
            auto* recentButton =
                new QPushButton(QStringLiteral("%1%2\n%3  ·  %4")
                                    .arg(favorite, recentName, sessionTypeName(recentSession.type), endpoint),
                                recentCard);
            recentButton->setObjectName("welcomeRecentButton");
            QString iconPath = QStringLiteral(":/icons/terminal.svg");
            if (recentSession.type == SessionType::SSH)
                iconPath = QStringLiteral(":/icons/server.svg");
            else if (recentSession.type == SessionType::Telnet)
                iconPath = QStringLiteral(":/icons/telnet.svg");
            else if (recentSession.type == SessionType::Serial)
                iconPath = QStringLiteral(":/icons/serial.svg");
            else if (recentSession.type == SessionType::RDP)
                iconPath = QStringLiteral(":/icons/rdp.svg");
            else if (recentSession.type == SessionType::VNC)
                iconPath = QStringLiteral(":/icons/vnc.svg");
            recentButton->setIcon(QIcon(iconPath));
            recentButton->setIconSize(QSize(22, 22));
            recentButton->setToolTip(endpoint);
            recentButton->setMinimumHeight(55);
            recentLayout->addWidget(recentButton);
            connect(recentButton, &QPushButton::clicked, this,
                    [this, recentSession]() { onConnectSession(recentSession); });
            if (++recentCount >= 5)
                break;
        }
        if (recentCount >= 5)
            break;
    }
    if (recentCount == 0) {
        auto* emptyRecent = new QLabel(tr("Your recently opened sessions will appear here."), recentCard);
        emptyRecent->setObjectName("welcomeEmptyState");
        emptyRecent->setAlignment(Qt::AlignCenter);
        emptyRecent->setWordWrap(true);
        emptyRecent->setMinimumHeight(150);
        recentLayout->addWidget(emptyRecent);
    }
    recentLayout->addStretch();
    welcomeCards->addWidget(recentCard, 3);
    contentLayout->addLayout(welcomeCards);

    welcomeLayout->addStretch();
    welcomeLayout->addWidget(welcomeContent, 0, Qt::AlignHCenter);
    welcomeLayout->addStretch();

    m_tabGrid->addWidget(m_welcomeWidget, 0, 0);

    connect(welcomeRemoteButton, &QPushButton::clicked, this, [this]() { m_sessionsSidebar->onNewSession(); });
    connect(welcomeLocalButton, &QPushButton::clicked, this, &MainWindow::onNewLocalTerminal);
    connect(allSessionsButton, &QPushButton::clicked, this, [this]() {
        QDialog dialog(this);
        dialog.setObjectName("allSessionsDialog");
        dialog.setWindowTitle(tr("All sessions"));
        dialog.resize(640, 520);
        auto* layout = new QVBoxLayout(&dialog);
        auto* sessions = new SessionsSidebar(&dialog);
        layout->addWidget(sessions);
        auto* buttons = new QDialogButtonBox(QDialogButtonBox::Close, &dialog);
        layout->addWidget(buttons);
        connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
        Session selectedSession;
        bool connectSelected = false;
        bool openLocal = false;
        connect(sessions, &SessionsSidebar::connectSession, &dialog, [&](const Session& session) {
            selectedSession = session;
            connectSelected = true;
            dialog.accept();
        });
        connect(sessions, &SessionsSidebar::newLocalSessionRequested, &dialog, [&]() {
            openLocal = true;
            dialog.accept();
        });
        connect(sessions, &SessionsSidebar::macrosImported, this, &MainWindow::rebuildMacrosMenu);
        dialog.exec();
        // Edits/imports in the full manager must also refresh the sidebar.
        m_sessionsSidebar->refreshSessions();
        if (connectSelected)
            onConnectSession(selectedSession);
        else if (openLocal)
            onNewLocalTerminal();
    });

    m_tabWidget2->hide();
    m_tabWidget3->hide();
    m_tabWidget4->hide();
    m_activePane = m_tabWidget;
    m_mainSplitter->addWidget(m_tabSplitter);
    updateWelcomeScreen();

    auto* sidebarToggle = new QToolButton(ribbonTabs);
    sidebarToggle->setObjectName("sidebarToggleButton");
    sidebarToggle->setDefaultAction(sidebarAction);
    sidebarToggle->setToolButtonStyle(Qt::ToolButtonIconOnly);
    sidebarToggle->setToolTip(sidebarAction->text());
    sidebarToggle->setAutoRaise(true);
    sidebarToggle->setIconSize(QSize(16, 16));
    // The tab header remains visible when either the sidebar or Ribbon is collapsed.
    ribbonTabs->setCornerWidget(sidebarToggle, Qt::TopLeftCorner);

    auto* syncIndicator = new QToolButton(this);
    syncIndicator->setObjectName("synchronizationIndicator");
    syncIndicator->setDefaultAction(m_syncAction);
    syncIndicator->setMenu(syncMenu);
    syncIndicator->setPopupMode(QToolButton::InstantPopup);
    syncIndicator->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    syncIndicator->hide();
    statusBar()->addPermanentWidget(syncIndicator);
    connect(m_syncAction, &QAction::toggled, syncIndicator, &QWidget::setVisible);

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
    connect(m_sessionsSidebar, &SessionsSidebar::macrosImported, this, &MainWindow::rebuildMacrosMenu);
    connect(m_sftpSidebar, &SftpSidebar::diagnosticMessage, this, [this](const QString& message) {
        SshConnection* connection = m_sftpSidebar->connection();
        if (!connection)
            return;
        for (QTabWidget* pane : allOpenPanes()) {
            for (int i = 0; i < pane->count(); ++i) {
                auto* tab = qobject_cast<TerminalTab*>(pane->widget(i));
                if (tab && tab->connection() == connection) {
                    m_diagnosticLogs[tab].append(message);
                    return;
                }
            }
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

    m_currentPanelGroup = 0;
    PanelGroupState initial;
    initial.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    initial.name = tr("Ungrouped");
    initial.isDefault = true;
    m_panelGroups.append(initial);
    capturePanelGroup();
    {
        const QSignalBlocker blocker(m_panelGroupTabs);
        m_panelGroupTabs->addTab(QIcon(":/icons/folder.svg"), initial.name);
    }
    connect(m_panelGroupTabs, &QTabBar::currentChanged, this, [this](int index) { activatePanelGroup(index); });
    connect(m_panelGroupTabs, &QTabBar::tabCloseRequested, this, &MainWindow::closePanelGroup);
    connect(m_panelGroupTabs, &QWidget::customContextMenuRequested, this, [this](const QPoint& point) {
        const int index = m_panelGroupTabs->tabAt(point);
        if (index < 0)
            return;
        activatePanelGroup(index);
        QMenu menu(this);
        menu.addAction(tr("Save group"), this, &MainWindow::saveActivePanelGroup);
        menu.addAction(tr("Rename group"), this, &MainWindow::renameActivePanelGroup);
        menu.addSeparator();
        menu.addAction(tr("Close group"), this, [this]() { closePanelGroup(m_currentPanelGroup); });
        menu.exec(m_panelGroupTabs->mapToGlobal(point));
    });
    updateSessionContext();
}

void MainWindow::prepareSftpSidebar(SshConnection* connection) {
    if (m_sftpSidebar->connection() == connection && connection) return;
    SftpSidebar* target = nullptr;
    for (auto* sidebar : std::as_const(m_backgroundSftpSidebars)) {
        if (connection && sidebar->connection() == connection) { target = sidebar; break; }
    }
    if (!target && !m_sftpSidebar->hasActiveTransfers()) return;
    const bool visible = m_sidebarStacked->currentWidget() == m_sftpSidebar;
    m_sidebarStacked->removeWidget(m_sftpSidebar);
    m_sftpSidebar->hide();
    m_sftpSidebar->setParent(this);
    m_backgroundSftpSidebars.append(m_sftpSidebar);
    if (target) m_backgroundSftpSidebars.removeOne(target);
    else {
        target = new SftpSidebar(m_sidebarStacked);
        connect(target, &SftpSidebar::diagnosticMessage, this, [this](const QString& message) {
            statusBar()->showMessage(message, 5000);
        });
    }
    m_sftpSidebar = target;
    m_sidebarStacked->insertWidget(1, target);
    if (visible) m_sidebarStacked->setCurrentWidget(target);
}

void MainWindow::switchSidebarTab(int index) {
    m_sidebarStacked->setCurrentIndex(index);
    m_sessionsTabBtn->setChecked(index == 0);
    m_sftpTabBtn->setChecked(index == 1);
}

void MainWindow::onConnectSession(const Session& session) {
    if (session.type == SessionType::FTP) {
        prepareSftpSidebar(nullptr);
        m_sftpSidebar->startSession(session);
        m_sftpTabBtn->setEnabled(true);
        switchSidebarTab(1);
        return;
    }

    QTabWidget* pane = activePane();
    auto* tab = new TerminalTab(session, pane);
    if (m_currentPanelGroup >= 0)
        tab->setProperty("panelGroupId", m_panelGroups[m_currentPanelGroup].id);
    int index = pane->addTab(tab, session.name);
    connect(tab, &QObject::destroyed, this, [this]() { updateSynchronizationUi(); });
    connect(tab, &TerminalTab::tabFinished, this, [this]() { updateSynchronizationUi(); });
    connect(tab, &TerminalTab::titleChanged, this, [this]() { updateSynchronizationUi(); });
    connect(tab, &TerminalTab::userKeyInput, this, [this, tab](QKeyEvent* event, bool paste) {
        if (!m_syncAction->isChecked() || !m_syncTargets.contains(tab) || !tab->isSessionActive() ||
            tab->session().readOnly)
            return;
        for (const auto& target : m_syncTargets)
            if (target && target != tab)
                target->receiveSynchronizedKey(event, paste);
    });
    connect(tab, &TerminalTab::userStringInput, this, [this, tab](const QString& text) {
        if (!m_syncAction->isChecked() || !m_syncTargets.contains(tab) || !tab->isSessionActive() ||
            tab->session().readOnly)
            return;
        for (const auto& target : m_syncTargets)
            if (target && target != tab && target->isSessionActive() && !target->session().readOnly)
                target->sendRaw(text);
    });

    if (session.type == SessionType::SSH) {
        pane->setTabIcon(index, QIcon(":/icons/server.svg"));
        // Share the terminal's SSH connection with the SFTP browser.
        QMetaObject::invokeMethod(tab->connection(), "setTerminalType", Qt::QueuedConnection,
                                  Q_ARG(QString, session.terminalType));
        QMetaObject::invokeMethod(tab->connection(), "setLanguage", Qt::QueuedConnection,
                                  Q_ARG(QString, session.language));
        QMetaObject::invokeMethod(tab->connection(), "setInitialSize", Qt::QueuedConnection,
                                  Q_ARG(int, session.initialRows), Q_ARG(int, session.initialColumns));
        prepareSftpSidebar(tab->connection());
        m_sftpSidebar->setConnection(tab->connection());
        m_sftpSidebar->startSession(session);
        m_sftpTabBtn->setEnabled(true);
        switchSidebarTab(1); // Switch sidebar to SFTP files

        // Keep the SFTP browser in sync with the terminal's working directory
        connect(tab, &TerminalTab::remoteDirChanged, this, [this, tab](const QString& path) {
            tab->setRemoteDirectory(path);
            if (activePane()->currentWidget() == tab && m_sftpSidebar->connection() == tab->connection())
                m_sftpSidebar->navigateTo(path);
        });
        connect(tab->connection(), &SshConnection::remoteStatsUpdated, this,
                [this, tab](double cpu, double mem, double disk, double uptime) {
                    if (activePane()->currentWidget() == tab)
                        onRemoteStatsUpdated(cpu, mem, disk, uptime);
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
    connect(tab, &TerminalTab::shortcutCaptureChanged, this, [this](bool) { updateSessionContext(); });
    connect(tab, &TerminalTab::reconnectRequested, this, &MainWindow::onReconnectRequested);
    connect(tab, &TerminalTab::promptDetected, this, [this, tab](const QString& prompt) {
        m_diagnosticLogs[tab].append(QStringLiteral("[prompt] ") + prompt);
        statusBar()->showMessage(tr("Prompt detected: %1").arg(prompt), 2500);
    });
    if (tab->connection()) {
        connect(tab->connection(), &SshConnection::diagnosticMessage, this,
                [this, tab](const QString& message) { m_diagnosticLogs[tab].append(message); });
        connect(tab->connection(), &SshConnection::tunnelStatus, this,
                [this, tab](const QString& message, bool active) {
                    m_diagnosticLogs[tab].append(
                        (active ? QStringLiteral("[tunnel] ") : QStringLiteral("[tunnel error] ")) + message);
                });
        connect(tab->connection(), &SshConnection::connectionFailed, this, [this, tab](const QString& message) {
            m_diagnosticLogs[tab].append(QStringLiteral("[connection error] ") + message);
        });
    }

    // Alt+F4 on the embedded terminal hits the term host process, which
    // forwards it here; route it through close() so the normal confirmation
    // dialog (closeEvent) decides whether to actually quit.
    connect(tab, &TerminalTab::closeRequested, this, [this]() { close(); });

    connect(tab, &TerminalTab::exitRequested, this, [this, tab]() {
        for (auto* pane : allOpenPanes()) {
            const int index = pane->indexOf(tab);
            if (index >= 0) {
                onTabCloseRequested(pane, index);
                break;
            }
        }
    });

    // Connect title updates
    connect(tab, &TerminalTab::bell, this, [this, tab]() {
        statusBar()->showMessage(tr("Terminal bell: %1").arg(tab->session().name), 3000);
    });
    connect(tab, &TerminalTab::titleChanged, this, [this, tab](const QString& title) {
        QTabWidget* pane = nullptr;
        for (auto* candidate : allOpenPanes())
            if (candidate->indexOf(tab) >= 0) {
                pane = candidate;
                break;
            }
        if (!pane)
            return;
        int idx = pane->indexOf(tab);
        if (idx != -1 && !title.isEmpty()) {
            const bool disconnected = title.startsWith(tr("[Closed]"));
            // Shells often publish a terminal title containing user@host or
            // the remote hostname. Keep the tab identity tied to the saved
            // session name; only surface the closed state when disconnected.
            const QString tabTitle = disconnected ? title : tab->session().name;
            pane->setTabText(idx, tabTitle);
            if (m_statusConnectionLabel)
                m_statusConnectionLabel->setText(tabTitle);
            statusBar()->showMessage(disconnected ? title : tabTitle, 3000);
            if (pane == activePane() && idx == pane->currentIndex()) {
                setWindowTitle(QString("BanchoXterm - %1").arg(tabTitle));
            }
            updateSessionContext();
        }
    });
}

void MainWindow::updateSessionContext() {
    TerminalTab* tab = currentTerminalTab();
    m_captureShortcutsAction->setEnabled(tab && tab->getTerminalWidget() && !tab->session().readOnly);
    m_captureShortcutsAction->setChecked(tab && tab->capturesApplicationShortcuts());
    if (!tab) {
        m_contextProtocolLabel->setText(tr("NO SESSION"));
        m_contextProtocolLabel->setToolTip(QString());
        m_contextSessionLabel->setText(tr("Open a session to begin"));
        m_contextSessionLabel->setToolTip(QString());
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
    const bool viaBastion = session.type == SessionType::SSH && !session.jumpHost.trimmed().isEmpty();
    const QString protocol = viaBastion ? tr("SSH via bastion") : sessionTypeName(session.type);
    QString route = endpoint;
    if (viaBastion) {
        const QString jumpUser = session.jumpUser.trimmed().isEmpty() ? session.user : session.jumpUser.trimmed();
        const QString bastion = QStringLiteral("%1@%2:%3")
                                    .arg(jumpUser, session.jumpHost.trimmed())
                                    .arg(session.jumpPort > 0 ? session.jumpPort : 22);
        route = tr("Bastion: %1\nDestination: %2").arg(bastion, endpoint);
    }
    m_contextProtocolLabel->setText(protocol);
    m_contextProtocolLabel->setToolTip(route);
    m_contextSessionLabel->setToolTip(route);
    m_contextSessionLabel->setText(QStringLiteral("%1  ·  %2").arg(session.name, endpoint));
    m_contextStateLabel->setText(active ? tr("Connected") : tr("Disconnected"));
    m_contextStateLabel->setStyleSheet(active ? QStringLiteral("color: #36b37e; font-weight: 600;")
                                              : QStringLiteral("color: #e06c75; font-weight: 600;"));
    if (m_statusConnectionLabel)
        m_statusConnectionLabel->setText(QStringLiteral("%1  |  %2").arg(protocol, endpoint));
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
        m_diagnosticLogs.remove(tab);
        // Defer destruction until pending signals and timers finish their
        // current event-loop iteration.
        tab->deleteLater();
        updateWelcomeScreen();
        updateSessionContext();
    }
}

void MainWindow::onCurrentTabChanged(QTabWidget* pane, int index) {
    if (!m_panes.contains(pane))
        return;
    m_activePane = pane;
    m_remoteMonitorWidget->hide();
    updateSessionContext();

    if (index < 0) {
        // If this pane has no tabs but another visible pane does, switch to it.
        for (QTabWidget* candidate : visiblePanes()) {
            if (candidate->count() > 0) {
                onCurrentTabChanged(candidate, candidate->currentIndex());
                return;
            }
        }
        prepareSftpSidebar(nullptr);
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
        prepareSftpSidebar(tab->connection());
        m_sftpSidebar->setConnection(tab->connection());
        m_sftpSidebar->startSession(tab->getSession());
        m_sftpTabBtn->setEnabled(true);
    } else {
        prepareSftpSidebar(nullptr);
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
    // Only one surface may receive input. Recreated splitter trees are stacked
    // above the welcome widget even when their empty panes paint no background.
    m_paneRoot->setVisible(hasSession);
    m_welcomeWidget->setVisible(!hasSession);
    if (!hasSession)
        m_welcomeWidget->raise();
    refreshPanelGroupBar();
    QTimer::singleShot(0, this, [this]() { TerminalAppearance::refreshWindow(this); });
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
        palette = QPalette(QColor("#f3f3f3"));
        palette.setColor(QPalette::Window, QColor("#f3f3f3"));
        palette.setColor(QPalette::Base, Qt::white);
        palette.setColor(QPalette::AlternateBase, QColor("#f3f3f3"));
        palette.setColor(QPalette::Button, QColor("#f7f7f7"));
        palette.setColor(QPalette::Text, QColor("#1a1a1a"));
        palette.setColor(QPalette::WindowText, QColor("#1a1a1a"));
        palette.setColor(QPalette::ButtonText, QColor("#1a1a1a"));
        palette.setColor(QPalette::PlaceholderText, QColor("#707070"));
        palette.setColor(QPalette::Disabled, QPalette::Text, QColor("#858585"));
        palette.setColor(QPalette::Disabled, QPalette::WindowText, QColor("#858585"));
        palette.setColor(QPalette::Disabled, QPalette::ButtonText, QColor("#858585"));
        palette.setColor(QPalette::Disabled, QPalette::PlaceholderText, QColor("#a0a0a0"));
        palette.setColor(QPalette::Highlight, QColor("#0078d4"));
        palette.setColor(QPalette::HighlightedText, Qt::white);
    } else if (mode == "dark") {
        palette = QPalette(QColor("#202020"));
        palette.setColor(QPalette::Window, QColor("#202020"));
        palette.setColor(QPalette::Base, QColor("#181818"));
        palette.setColor(QPalette::AlternateBase, QColor("#181818"));
        palette.setColor(QPalette::Button, QColor("#2b2b2b"));
        palette.setColor(QPalette::Text, QColor("#f5f5f5"));
        palette.setColor(QPalette::WindowText, QColor("#f5f5f5"));
        palette.setColor(QPalette::ButtonText, QColor("#f5f5f5"));
        palette.setColor(QPalette::PlaceholderText, QColor("#a0a0a0"));
        palette.setColor(QPalette::Disabled, QPalette::Text, QColor("#858585"));
        palette.setColor(QPalette::Disabled, QPalette::WindowText, QColor("#858585"));
        palette.setColor(QPalette::Disabled, QPalette::ButtonText, QColor("#858585"));
        palette.setColor(QPalette::Disabled, QPalette::PlaceholderText, QColor("#707070"));
        palette.setColor(QPalette::Highlight, QColor("#60cdff"));
        palette.setColor(QPalette::HighlightedText, Qt::white);
    }
    qApp->setPalette(mode == "system" ? m_systemPalette : palette);

    const bool darkTheme = qApp->palette().color(QPalette::Window).lightness() < 128;
    const QString canvas = darkTheme ? QStringLiteral("#202020") : QStringLiteral("#f3f3f3");
    const QString surface = darkTheme ? QStringLiteral("#292929") : QStringLiteral("#ffffff");
    const QString surfaceHover = darkTheme ? QStringLiteral("#383838") : QStringLiteral("#f5f5f5");
    const QString surfacePressed = darkTheme ? QStringLiteral("#414141") : QStringLiteral("#ebebeb");
    const QString border = darkTheme ? QStringLiteral("#454545") : QStringLiteral("#e5e5e5");
    const QString text = darkTheme ? QStringLiteral("#f5f5f5") : QStringLiteral("#1a1a1a");
    const QString muted = darkTheme ? QStringLiteral("#b3b3b3") : QStringLiteral("#616161");
    const QString accent = darkTheme ? QStringLiteral("#60cdff") : QStringLiteral("#0078d4");
    const QString accentHover = darkTheme ? QStringLiteral("#80d8ff") : QStringLiteral("#106ebe");
    const QString accentText = darkTheme ? QStringLiteral("#00344d") : QStringLiteral("#ffffff");
    const QString selectionBackground = darkTheme ? QStringLiteral("#263b4d") : QStringLiteral("#dceafa");
    const QString selectionText = text;
    const QString checkboxMark =
        darkTheme ? QStringLiteral(":/icons/check-dark.svg") : QStringLiteral(":/icons/check-white.svg");
    const QString radioMark =
        darkTheme ? QStringLiteral(":/icons/radio-checked-dark.svg") : QStringLiteral(":/icons/radio-checked.svg");
    qApp->setStyleSheet(
        QStringLiteral(
            "QMainWindow, QDialog { background: %1; color: %2; }"
            "QToolBar#ribbonToolBar { background: %3; border: 0; border-bottom: 1px solid %4; spacing: 4px; }"
            "QTabWidget#ribbonTabs::pane { border: 0; background: %3; }"
            "QTabBar#ribbonTabBar { background: transparent; border: 0; }"
            "QTabBar#ribbonTabBar::tab { background: transparent; color: %5; padding: 8px 15px 9px;"
            " border: 0; border-bottom: 3px solid transparent; margin: 0 2px; }"
            "QTabBar#ribbonTabBar::tab:hover { background: %6; color: %2; border-radius: 4px; }"
            "QTabBar#ribbonTabBar::tab:selected { background: transparent; color: %2;"
            " border-bottom: 3px solid %7; font-weight: 600; }"
            "QToolButton { color: %2; border: 1px solid transparent; border-radius: 5px; padding: 5px; }"
            "QToolButton:hover { background: %6; border-color: %4; }"
            "QToolButton:pressed, QToolButton:checked { background: %8; border-color: %4; }"
            "QToolButton#sidebarToggleButton { background: transparent; color: %5;"
            " padding: 8px 15px 9px; border: 0; border-bottom: 3px solid transparent;"
            " border-radius: 0; margin: 0 2px; }"
            "QToolButton#sidebarToggleButton:hover { background: %6; color: %2; border-radius: 4px; }"
            "QToolButton#sidebarToggleButton:pressed { background: %8; color: %2; }"
            "QPushButton, QComboBox, QLineEdit, QSpinBox, QDoubleSpinBox { min-height: 30px;"
            " background: %3; color: %2; border: 1px solid %4; border-radius: 5px; padding: 3px 9px; }"
            "QPushButton:hover, QComboBox:hover { background: %6; }"
            "QPushButton:pressed { background: %8; }"
            "QPushButton[localAction=\"true\"], QPushButton#remoteActionButton, QPushButton#sessionActionButton {"
            " background: transparent;"
            " border: 1px solid transparent;"
            " border-radius: 6px; padding: 4px; min-width: 32px; max-width: 32px;"
            " min-height: 32px; max-height: 32px; }"
            "QPushButton[localAction=\"true\"]:hover, QPushButton#remoteActionButton:hover, "
            "QPushButton#sessionActionButton:hover {"
            " background: %6; border-color: %4; }"
            "QPushButton[localAction=\"true\"]:pressed, QPushButton#remoteActionButton:pressed, "
            "QPushButton#sessionActionButton:pressed {"
            " background: %8; border-color: %7; }"
            "QPushButton[localAction=\"true\"]:disabled, QPushButton#remoteActionButton:disabled, "
            "QPushButton#sessionActionButton:disabled {"
            " background: transparent; border-color: transparent; }"
            "QPushButton:focus, QComboBox:focus, QLineEdit:focus, QSpinBox:focus, QDoubleSpinBox:focus {"
            " border: 2px solid %7; padding: 2px 8px; }"
            "QPushButton#primaryButton { background: %7; color: %9; border: 1px solid %7; font-weight: 600; }"
            "QPushButton#primaryButton:hover { background: %10; border-color: %10; }"
            "QPushButton#primaryButton:pressed { background: %11; border-color: %11; }"
            "QPushButton:disabled, QToolButton:disabled { color: %5; background: %1; border-color: %4; }"
            "QPushButton:default { background: %7; color: %9; border-color: %7; font-weight: 600; }"
            "QPushButton:default:hover { background: %10; border-color: %10; }"
            "QComboBox::drop-down { border: 0; width: 28px; subcontrol-origin: padding;"
            " subcontrol-position: top right; }"
            "QComboBox::drop-down:hover, QComboBox::drop-down:on { background: %8; border-radius: 4px; }"
            "QComboBox::down-arrow { image: url(:/icons/chevron-down.svg); width: 12px; height: 12px; border: 0; }"
            "QComboBox::down-arrow:on { top: 1px; }"
            "QSpinBox::up-button, QDoubleSpinBox::up-button, QSpinBox::down-button, QDoubleSpinBox::down-button {"
            " subcontrol-origin: border; width: 24px; border: 0; border-left: 1px solid %4;"
            " border-top-right-radius: 4px; border-bottom-right-radius: 4px; }"
            "QSpinBox::up-button, QDoubleSpinBox::up-button { subcontrol-position: top right; }"
            "QSpinBox::down-button, QDoubleSpinBox::down-button { subcontrol-position: bottom right;"
            " border-top: 1px solid %4; }"
            "QSpinBox::up-button:hover, QDoubleSpinBox::up-button:hover,"
            " QSpinBox::down-button:hover, QDoubleSpinBox::down-button:hover { background: %6; }"
            "QSpinBox::up-arrow, QDoubleSpinBox::up-arrow { image: url(:/icons/chevron-up.svg);"
            " width: 11px; height: 11px; }"
            "QSpinBox::down-arrow, QDoubleSpinBox::down-arrow { image: url(:/icons/chevron-down.svg);"
            " width: 11px; height: 11px; }"
            "QComboBox QAbstractItemView { background: %3; color: %2; selection-background-color: %14;"
            " selection-color: %15; border: 1px solid %4; outline: 0; }"
            "QComboBox QAbstractItemView::item { min-height: 20px; padding: 2px 8px; border-radius: 3px; }"
            "QMenu { background: %3; color: %2; border: 1px solid %4; padding: 5px; }"
            "QMenu::item { padding: 6px 28px 6px 10px; border-radius: 4px; }"
            "QMenu::item:selected { background: %6; }"
            "QMenu::separator { height: 1px; background: %4; margin: 5px 8px; }"
            "QTabWidget::pane { border: 1px solid %4; background: %1; top: -1px; }"
            "QTabWidget#terminalTabs[tabDropTarget=\"true\"]::pane { border: 2px solid %7; }"
            "QTabBar { background: transparent; border-bottom: 1px solid %4; }"
            "QTabBar::tab { background: transparent; color: %5; border: 0;"
            " border-bottom: 3px solid transparent; padding: 8px 12px 9px; margin-right: 2px; }"
            "QTabBar::tab:hover { background: %6; color: %2; border-radius: 4px; }"
            "QTabBar::tab:selected { background: transparent; color: %2;"
            " border-bottom: 3px solid %7; font-weight: 600; }"
            "QTabBar::tab:disabled { color: %5; }"
            "QTabBar::close-button { image: url(:/icons/close.svg); width: 14px; height: 14px;"
            " border: 0; border-radius: 4px; padding: 2px; }"
            "QTabBar::close-button:hover { background: %8; }"
            "QTreeView, QTreeWidget, QListWidget, QTableWidget, QPlainTextEdit, QTextEdit { background: %3;"
            " alternate-background-color: %1; color: %2; border: 1px solid %4; border-radius: 5px;"
            " selection-background-color: %14; selection-color: %15; outline: 0; }"
            "QHeaderView::section { background: %1; color: %5; border: 0; border-bottom: 1px solid %4;"
            " padding: 7px 8px; font-weight: 600; }"
            "QTreeWidget::item { padding: 5px 3px; border-radius: 3px; }"
            "QTreeWidget::item:hover { background: %6; }"
            "QTreeView::item:selected, QTreeWidget::item:selected { background: %14; color: %15;"
            " border-left: 3px solid %7; border-radius: 3px; }"
            "QTreeView::item:selected:active, QTreeWidget::item:selected:active {"
            " background: %14; color: %15; }"
            "QRadioButton { spacing: 8px; color: %2; }"
            "QRadioButton::indicator { width: 18px; height: 18px; border: 1px solid %5;"
            " border-radius: 9px; background: %3; }"
            "QRadioButton::indicator:hover { border: 2px solid %7; }"
            "QRadioButton::indicator:checked { image: url(%13); border: 0; background: transparent; }"
            "QRadioButton::indicator:disabled { opacity: 0.55; }"
            "QCheckBox { spacing: 8px; color: %2; }"
            "QGroupBox { border: 1px solid %4; border-radius: 6px; margin-top: 12px; padding: 12px 10px 8px;"
            " color: %2; font-weight: 600; }"
            "QGroupBox::title { subcontrol-origin: margin; left: 10px; padding: 0 5px; color: %5; }"
            "QProgressBar { background: %1; color: %2; border: 1px solid %4; border-radius: 5px;"
            " min-height: 14px; text-align: center; }"
            "QProgressBar::chunk { background: %7; border-radius: 4px; margin: 1px; }"
            "QSplitter::handle { background: %4; }"
            "QSplitter::handle:horizontal { width: 5px; }"
            "QSplitter::handle:vertical { height: 5px; }"
            "QSplitter::handle:hover { background: %7; }"
            "QStatusBar { background: %3; color: %5; border-top: 1px solid %4; }"
            "QStatusBar::item { border: 0; }"
            "QFrame#sessionContextWidget, QWidget#multiInputBar { background: %3; border-bottom: 1px solid %4; }"
            "QWidget#statusContextWidget { background: transparent; }"
            "QFrame#sidebarContainer { background: %3; border-right: 1px solid %4; }"
            "QFrame#verticalTabStrip { background: %1; border-right: 1px solid %4; }"
            "QFrame#verticalTabStrip QToolButton { border-radius: 0; border-left: 3px solid transparent; }"
            "QFrame#verticalTabStrip QToolButton:checked { background: %3; border-left: 3px solid %7; }"
            "QFrame#verticalTabStrip QToolButton:hover { background: %6; }"
            "QWidget#welcomeScreen { background: %1; }"
            "QFrame#welcomeCard { background: %3; border: 1px solid %4; border-radius: 12px; }"
            "QLabel#welcomeLogo { background: %3; border: 1px solid %4; border-radius: 12px; padding: 7px; }"
            "QLabel#welcomeCardTitle { color: %2; font-size: 16px; font-weight: 650; }"
            "QLabel#welcomeCardDescription, QLabel#welcomeProtocols, QLabel#welcomeEmptyState { color: %5; }"
            "QLabel#welcomeProtocols { border-top: 1px solid %4; padding-top: 12px; }"
            "QPushButton#welcomeSecondaryButton { background: %3; text-align: left; padding: 8px 14px; }"
            "QPushButton#welcomeSecondaryButton:hover { background: %6; }"
            "QPushButton#welcomeRecentButton { background: %1; text-align: left; padding: 8px 12px;"
            " border: 1px solid %4; border-radius: 7px; }"
            "QPushButton#welcomeRecentButton:hover { background: %6; border-color: %7; }"
            "QPushButton#welcomeLinkButton { color: %7; background: transparent; border: 0; padding: 4px 6px; }"
            "QPushButton#welcomeLinkButton:hover { color: %10; text-decoration: underline; }"
            "QLabel#sftpPaneTitle { color: %2; font-weight: 600; padding: 5px 0; }"
            "QWidget#sftpRemotePanel, QWidget#sftpLocalPanel { background: %3; border: 1px solid %4;"
            " border-radius: 8px; }"
            "QTreeWidget#sessionTree, QTreeWidget#sftpTree, QTreeWidget#localFileTree {"
            " border-radius: 7px; }"
            "QTreeWidget#sessionTree QHeaderView::section, QTreeWidget#sftpTree QHeaderView::section,"
            " QTreeWidget#localFileTree QHeaderView::section { background: %1; }"
            "QTreeWidget#sessionTree::item { padding: 6px 5px; }"
            "QLabel#sftpStatus { color: %5; background: %1; border: 1px solid %4;"
            " border-radius: 6px; padding: 6px 9px; }"
            "QLabel#sftpTransferLabel { color: %2; font-weight: 600; padding-top: 3px; }"
            "QProgressBar#sftpTransferProgress { border: 0; background: %1; border-radius: 3px; }"
            "QProgressBar#sftpTransferProgress::chunk { background: %7; border-radius: 3px; }"
            "QPushButton#sftpTransferAction { min-height: 28px; }"
            "QWidget#remoteMonitorWidget { background: transparent; }"
            "QDialog#sessionDialog { background: %1; }"
            "QDialog#sessionDialog QLabel { color: %2; }"
            "QWidget#sessionIdentityPanel { background: %3; border: 1px solid %4; border-radius: 8px; }"
            "QDialog#sessionDialog QTabWidget::pane { background: %3; border-radius: 0 0 6px 6px; }"
            "QDialog#sessionDialog QTabBar { background: transparent; }"
            "QDialog#sftpUtilityDialog { background: %1; }"
            "QDialog#sftpUtilityDialog QListWidget { padding: 5px; }"
            "QDialogButtonBox QPushButton { min-width: 76px; }"
            "QLabel#sessionContextProtocol { color: %7; font-weight: 700; }"
            "QLabel#sessionContextSession { color: %2; font-weight: 600; }"
            "QLabel#sessionContextState { color: %5; }"
            "QTabWidget#terminalTabs::pane { border: 0; background: %1; }"
            "QTabBar#terminalTabBar { background: %1; border-bottom: 1px solid %4; }"
            "QTabBar#terminalTabBar::tab { background: %1; padding: 8px 12px 9px;"
            " border: 1px solid %4; border-bottom: 3px solid transparent;"
            " border-top-left-radius: 5px; border-top-right-radius: 5px; margin: 2px 2px 0; }"
            "QTabBar#terminalTabBar::tab:hover { background: %6; }"
            "QTabBar#terminalTabBar::tab:selected { background: %3; border: 1px solid %4;"
            " border-bottom: 3px solid %7; font-weight: 600; }"
            "QLabel#sidebarSectionTitle, QLabel#welcomeSectionTitle { color: %5; font-weight: 600; }"
            "QLabel#sidebarSectionMeta, QLabel#welcomeSubtitle { color: %5; }"
            "QLabel#welcomeTitle { color: %2; font-size: 26px; font-weight: 650; }"
            "QLabel#toolbarSectionLabel { color: %5; }"
            "QFrame#remoteStatsCard { background: %1; border: 1px solid %4; border-radius: 7px; }"
            "QScrollBar:vertical { background: transparent; width: 12px; margin: 2px; }"
            "QScrollBar::handle:vertical { background: %4; min-height: 28px; border-radius: 5px; }"
            "QScrollBar::handle:vertical:hover { background: %5; }"
            "QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical { height: 0; }"
            "QScrollBar:horizontal { background: transparent; height: 12px; margin: 2px; }"
            "QScrollBar::handle:horizontal { background: %4; min-width: 28px; border-radius: 5px; }"
            "QScrollBar::handle:horizontal:hover { background: %5; }"
            "QScrollBar::add-line:horizontal, QScrollBar::sub-line:horizontal { width: 0; }"
            "QToolTip { background: %3; color: %2; border: 1px solid %4; padding: 5px 7px;"
            " border-radius: 4px; }"
            "QCheckBox::indicator { width: 18px; height: 18px; border: 1px solid %5;"
            " border-radius: 4px; background: %3; }"
            "QCheckBox::indicator:hover { border: 2px solid %7; }"
            "QCheckBox::indicator:pressed { background: %8; }"
            "QCheckBox::indicator:checked { background: %7; border: 1px solid %7;"
            " image: url(%12); }"
            "QCheckBox::indicator:indeterminate { background: %7; border: 1px solid %7;"
            " image: url(%12); }"
            "QCheckBox::indicator:disabled { opacity: 0.55; }")
            .arg(canvas, text, surface, border, muted, surfaceHover, accent, surfacePressed, accentText)
            .arg(accentHover, darkTheme ? QStringLiteral("#45b6e6") : QStringLiteral("#005a9e"))
            .arg(checkboxMark, radioMark)
            .arg(selectionBackground, selectionText));

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
        if (!QSettings().value("ssh/monitoringEnabled", true).toBool())
            m_remoteMonitorWidget->hide();
        for (QWidget* window : QApplication::topLevelWidgets())
            TerminalAppearance::refreshWindow(window);
        if (dialog.themeMode() != m_themeMode)
            applyThemeMode(dialog.themeMode());
        if (m_lockAction)
            m_lockAction->setEnabled(MasterPasswordManager::instance().isEnabled());

        // Typography Configuration
        for (QTabWidget* pane : allOpenPanes()) {
            for (int i = 0; i < pane->count(); ++i) {
                auto* tab = qobject_cast<TerminalTab*>(pane->widget(i));
                if (tab) {
                    tab->updateFontFromSettings();
                }
            }
        }
        // Detached terminals also keep live SSH connections.
        for (QWidget* widget : QApplication::allWidgets()) {
            auto* tab = qobject_cast<TerminalTab*>(widget);
            if (tab && tab->connection())
                QMetaObject::invokeMethod(tab->connection(), "reloadMonitoringSettings", Qt::QueuedConnection);
        }
    }
}

void MainWindow::showDiagnostics() {
    TerminalTab* tab = currentTerminalTab();
    if (!tab) {
        QMessageBox::information(this, tr("Diagnostics"), tr("No active terminal session."));
        return;
    }

    const Session& session = tab->session();
    QDialog dialog(this);
    dialog.setWindowTitle(tr("Session Diagnostics"));
    dialog.resize(620, 480);
    auto* layout = new QVBoxLayout(&dialog);
    auto* summary = new QLabel(&dialog);
    summary->setWordWrap(true);
    summary->setText(tr("Session: %1\nProtocol: %2\nState: %3\nReconnects: %4")
                         .arg(session.name)
                         .arg(sessionTypeName(session.type))
                         .arg(tab->isSessionActive() ? tr("Connected") : tr("Disconnected"))
                         .arg(m_reconnectCounts.value(session.id, 0)));
    layout->addWidget(summary);

    auto* metrics = new QLabel(&dialog);
    metrics->setText(tr("Remote metrics: CPU %1, RAM %2, Disk %3, Uptime %4")
                         .arg(m_remoteCpuValue->text(), m_remoteMemValue->text(), m_remoteDiskValue->text(),
                              m_remoteUptimeValue->text()));
    layout->addWidget(metrics);

    auto* logList = new QListWidget(&dialog);
    const QStringList logs = m_diagnosticLogs.value(tab);
    if (logs.isEmpty())
        logList->addItem(tr("No diagnostic events recorded for this session."));
    else
        logList->addItems(logs);
    layout->addWidget(logList, 1);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Close, &dialog);
    auto* exportButton = buttons->addButton(tr("Export..."), QDialogButtonBox::ActionRole);
    layout->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    connect(exportButton, &QPushButton::clicked, &dialog, [this, &dialog, session, tab, logs]() {
        const QString path =
            QFileDialog::getSaveFileName(&dialog, tr("Export Diagnostics"), QString(), tr("Text files (*.txt)"));
        if (path.isEmpty())
            return;
        QFile file(path);
        if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
            QMessageBox::warning(&dialog, tr("Export Diagnostics"), tr("Could not write the selected file."));
            return;
        }
        QTextStream stream(&file);
        stream << "BanchoXterm diagnostics\n"
               << "Session: " << session.name << "\n"
               << "Protocol: " << sessionTypeName(session.type) << "\n"
               << "State: " << (tab->isSessionActive() ? "Connected" : "Disconnected") << "\n"
               << "Reconnects: " << m_reconnectCounts.value(session.id, 0) << "\n\n";
        for (const QString& log : logs)
            stream << log << '\n';
    });
    dialog.exec();
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

    if (!broadcastInput(text, false))
        return;

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

bool MainWindow::broadcastInput(const QString& text, bool sensitive) {
    QList<QPointer<TerminalTab>> targets;
    QStringList targetNames;
    for (QTabWidget* pane : allOpenPanes()) {
        for (int i = 0; i < pane->count(); ++i) {
            auto* tab = qobject_cast<TerminalTab*>(pane->widget(i));
            if (!tab || (sensitive && (!tab->isSessionActive() || tab->isRdpOrVnc() || tab->session().readOnly)))
                continue;
            targets.append(tab);
            targetNames.append(pane->tabText(i));
        }
    }
    if (targets.isEmpty()) {
        if (sensitive)
            QMessageBox::information(this, tr("Sensitive Multi-Input"),
                                     tr("No active writable terminal sessions are available."));
        return false;
    }

    const QRegularExpression destructivePattern(
        QStringLiteral(
            R"((^|[;&|]\s*)(rm\s+-[a-z]*r[a-z]*f|mkfs(?:\.|\s)|shutdown\b|reboot\b|poweroff\b|format\s|drop\s+database\b|truncate\s+))"),
        QRegularExpression::CaseInsensitiveOption);
    if (destructivePattern.match(text).hasMatch()) {
        const auto decision = localizedQuestion(
            this, tr("Potentially destructive command"),
            sensitive ? tr("This sensitive entry may modify or destroy data. Do you want to continue?")
                      : tr("This command may modify or destroy data:\n\n%1\n\nDo you want to continue?").arg(text),
            QMessageBox::Yes | QMessageBox::No, QMessageBox::No, QMessageBox::Warning);
        if (decision != QMessageBox::Yes)
            return false;
    }

    QDialog targetDialog(this);
    targetDialog.setWindowTitle(sensitive ? tr("Confirm Sensitive Multi-Input") : tr("Confirm Multi-Input"));
    targetDialog.setObjectName("multiInputTargetDialog");
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
    auto* commandLabel =
        new QLabel(sensitive ? tr("Sensitive value hidden. It will be sent with Enter.") : tr("Command: %1").arg(text),
                   &targetDialog);
    commandLabel->setWordWrap(true);
    targetLayout->addWidget(commandLabel);
    auto* targetButtons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &targetDialog);
    targetButtons->button(QDialogButtonBox::Ok)->setIcon(QIcon(QStringLiteral(":/icons/check.svg")));
    targetButtons->button(QDialogButtonBox::Cancel)->setIcon(QIcon(QStringLiteral(":/icons/close.svg")));
    targetLayout->addWidget(targetButtons);
    connect(targetButtons, &QDialogButtonBox::accepted, &targetDialog, &QDialog::accept);
    connect(targetButtons, &QDialogButtonBox::rejected, &targetDialog, &QDialog::reject);

    if (targetDialog.exec() != QDialog::Accepted)
        return false;

    QList<QPointer<TerminalTab>> selectedTargets;
    for (int i = 0; i < targets.size(); ++i) {
        if (targets.at(i) && targetList->item(i)->checkState() == Qt::Checked) {
            selectedTargets.append(targets.at(i));
        }
    }
    if (selectedTargets.isEmpty())
        return false;

    // Stagger writes to avoid flooding slow devices or WAN links when a
    // broadcast targets many sessions.  The tab is the context object, so a
    // closed session cancels its pending write automatically.
    constexpr int broadcastIntervalMs = 75;
    for (int i = 0; i < selectedTargets.size(); ++i) {
        QPointer<TerminalTab> tab = selectedTargets.at(i);
        QTimer::singleShot(i * broadcastIntervalMs, tab, [tab, text, sensitive]() {
            if (!tab)
                return;
            if (sensitive)
                tab->sendSensitiveInputText(text);
            else
                tab->sendInputText(text);
        });
    }

    return true;
}

void MainWindow::onSensitiveMultiInput() {
    QDialog dialog(this);
    dialog.setObjectName("sensitiveMultiInputDialog");
    dialog.setWindowTitle(tr("Sensitive Multi-Input"));
    dialog.resize(600, 420);
    auto* layout = new QVBoxLayout(&dialog);
    auto* help = new QLabel(tr("Entries are kept only until BanchoXterm closes and are excluded from input histories. "
                               "Remote shell history and server-side logging remain under the server's control."),
                            &dialog);
    help->setWordWrap(true);
    layout->addWidget(help);
    auto* list = new QListWidget(&dialog);
    list->setObjectName("sensitiveInputList");
    list->addItems(m_sensitiveInputNames);
    layout->addWidget(list);
    auto* name = new QLineEdit(&dialog);
    name->setObjectName("sensitiveInputName");
    name->setPlaceholderText(tr("Entry name"));
    layout->addWidget(name);
    auto* value = new QLineEdit(&dialog);
    value->setObjectName("sensitiveInputValue");
    value->setEchoMode(QLineEdit::Password);
    value->setInputMethodHints(Qt::ImhHiddenText | Qt::ImhSensitiveData | Qt::ImhNoPredictiveText);
    value->setPlaceholderText(tr("Sensitive value"));
    layout->addWidget(value);
    auto* row = new QHBoxLayout();
    auto* add = new QPushButton(tr("Add / Update"), &dialog);
    add->setObjectName("sensitiveInputAddButton");
    auto* remove = new QPushButton(tr("Delete"), &dialog);
    remove->setObjectName("sensitiveInputDeleteButton");
    auto* send = new QPushButton(tr("Send"), &dialog);
    send->setObjectName("sensitiveInputSendButton");
    row->addWidget(add);
    row->addWidget(remove);
    row->addWidget(send);
    layout->addLayout(row);
    auto* close = new QDialogButtonBox(QDialogButtonBox::Close, &dialog);
    layout->addWidget(close);
    connect(close, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    connect(list, &QListWidget::currentRowChanged, &dialog, [this, name, value](int index) {
        name->setText(index >= 0 ? m_sensitiveInputNames.value(index) : QString());
        value->setText(index >= 0 ? m_sensitiveInputValues.value(index) : QString());
    });
    connect(add, &QPushButton::clicked, &dialog, [this, list, name, value]() {
        const QString label = name->text().trimmed();
        if (label.isEmpty() || value->text().isEmpty())
            return;
        int index = m_sensitiveInputNames.indexOf(label);
        if (index < 0) {
            index = m_sensitiveInputNames.size();
            m_sensitiveInputNames.append(label);
            m_sensitiveInputValues.append(value->text());
        } else {
            m_sensitiveInputValues[index] = value->text();
        }
        list->clear();
        list->addItems(m_sensitiveInputNames);
        list->setCurrentRow(index);
    });
    connect(remove, &QPushButton::clicked, &dialog, [this, list]() {
        const int index = list->currentRow();
        if (index < 0)
            return;
        m_sensitiveInputNames.removeAt(index);
        m_sensitiveInputValues.removeAt(index);
        list->clear();
        list->addItems(m_sensitiveInputNames);
    });
    connect(send, &QPushButton::clicked, &dialog, [this, list]() {
        const int index = list->currentRow();
        if (index >= 0)
            broadcastInput(m_sensitiveInputValues.at(index), true);
    });
    dialog.exec();
}

void MainWindow::onReconnectRequested(const Session& session) {
    const int previousGroup = m_currentPanelGroup;
    auto* reconnecting = qobject_cast<TerminalTab*>(sender());
    if (reconnecting) {
        for (auto* pane : allOpenPanes())
            if (pane->indexOf(reconnecting) >= 0) {
                activatePanelGroup(panelGroupForPane(pane), false);
                m_activePane = pane;
                break;
            }
    }
    ++m_reconnectCounts[session.id];
    auto* tab = qobject_cast<TerminalTab*>(sender());
    if (tab) {
        QTabWidget* pane = nullptr;
        if (m_tabWidget->indexOf(tab) != -1)
            pane = m_tabWidget;
        else {
            for (QTabWidget* candidate : allOpenPanes()) {
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
            m_diagnosticLogs.remove(tab);
            tab->deleteLater();
        }
    }
    onConnectSession(session);
    activatePanelGroup(previousGroup, false);
}

void MainWindow::onRemoteStatsUpdated(double cpu, double mem, double disk, double uptimeSecs) {
    auto* tab = qobject_cast<TerminalTab*>(activePane()->currentWidget());
    if (!tab || !tab->isSsh()) {
        m_remoteMonitorWidget->hide();
        return;
    }
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
    m_remoteMonitorWidget->setVisible(QSettings().value("ssh/monitoringEnabled", true).toBool());
}

void MainWindow::setupMenuBar() {
    auto* fileMenu = menuBar()->addMenu(tr("&File"));

    auto* newTabAction = fileMenu->addAction(tr("&New Remote Session..."));
    connect(newTabAction, &QAction::triggered, this, [this]() { m_sessionsSidebar->onNewSession(); });

    auto* closeTabAction = fileMenu->addAction(tr("Close session"));
    KeyboardShortcuts::bindAction(closeTabAction, "closeTab");
    connect(closeTabAction, &QAction::triggered, this, [this]() {
        QTabWidget* pane = activePane();
        const int index = pane ? pane->currentIndex() : -1;
        if (index >= 0)
            onTabCloseRequested(pane, index);
    });
    fileMenu->addSeparator();

    auto* exitAction = fileMenu->addAction(tr("E&xit"));
    KeyboardShortcuts::bindAction(exitAction, "quit");
    connect(exitAction, &QAction::triggered, this, &QWidget::close);

    auto* editMenu = menuBar()->addMenu(tr("&Edit"));

    m_copyAction = editMenu->addAction(tr("&Copy"));
    m_copyAction->setIcon(QIcon(":/icons/copy.svg"));
    // The terminal owns keyboard activation. Widget context keeps menu/ribbon
    // labels in sync without registering a second window-wide clipboard path.
    m_copyAction->setToolTip(tr("Copy selection"));
    KeyboardShortcuts::bindAction(m_copyAction, "copy", Qt::WidgetShortcut);
    connect(m_copyAction, &QAction::triggered, this, &MainWindow::onCopy);

    m_pasteAction = editMenu->addAction(tr("&Paste"));
    m_pasteAction->setIcon(QIcon(":/icons/paste.svg"));
    KeyboardShortcuts::bindAction(m_pasteAction, "paste", Qt::WidgetShortcut);
    connect(m_pasteAction, &QAction::triggered, this, &MainWindow::onPaste);

    editMenu->addSeparator();

    auto* specialMenu = editMenu->addMenu(tr("Special Characters"));
    m_specialCharactersAction = specialMenu->menuAction();
    m_specialCharactersAction->setIcon(QIcon(":/icons/terminal.svg"));
    const QList<QPair<QString, QByteArray>> specialCharacters = {
        {tr("Break"), QByteArray()},
        {tr("Cisco escape (Ctrl+^)"), QByteArray("\x1e")},
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
        action->setProperty("physicalBreak", special.first == tr("Break"));
        action->setProperty("ciscoEscape", special.second == QByteArrayLiteral("\x1e"));
        action->setProperty("telnetEscape", special.second == QByteArrayLiteral("\x1d"));
        connect(action, &QAction::triggered, this, [this, action]() {
            if (auto* tab = currentTerminalTab()) {
                if (action->property("physicalBreak").toBool())
                    tab->sendBreak();
                else if (action->property("ciscoEscape").toBool()) tab->sendCiscoEscape();
                else if (action->property("telnetEscape").toBool()) tab->sendTelnetEscape();
                else tab->sendSpecialCharacter(action->data().toByteArray());
            }
        });
    }

    connect(specialMenu, &QMenu::aboutToShow, this, [this, specialMenu] {
        auto* tab = currentTerminalTab();
        for (auto* action : specialMenu->actions()) {
            if (!action->data().isValid() && !action->property("physicalBreak").isValid()) continue;
            bool enabled = tab && tab->isSessionActive() && !tab->session().readOnly;
            if (enabled && action->property("telnetEscape").toBool()) enabled = tab->session().type == SessionType::Telnet;
            if (enabled && action->property("physicalBreak").toBool()) enabled = tab->isSsh() || tab->session().type == SessionType::Telnet || tab->session().type == SessionType::Serial;
            action->setEnabled(enabled);
        }
    });
    specialMenu->addSeparator();
    auto* customMenu = specialMenu->addMenu(tr("Custom"));
    auto* manageCustomAction = specialMenu->addAction(tr("Manage custom characters..."));
    const auto rebuildCustomCharacters = [customMenu, this]() {
        customMenu->clear();
        QSettings settings;
        const QStringList names = settings.value(QStringLiteral("specialCharacters/names")).toStringList();
        const QStringList values = settings.value(QStringLiteral("specialCharacters/values")).toStringList();
        for (int i = 0; i < names.size() && i < values.size(); ++i) {
            const QByteArray bytes = QByteArray::fromHex(values.at(i).toLatin1());
            if (bytes.isEmpty())
                continue;
            auto* action = customMenu->addAction(names.at(i));
            connect(action, &QAction::triggered, this, [this, bytes]() {
                if (auto* tab = currentTerminalTab())
                    tab->sendBytes(bytes);
            });
        }
        if (customMenu->isEmpty())
            customMenu->addAction(tr("No custom characters configured"))->setEnabled(false);
    };
    rebuildCustomCharacters();
    connect(manageCustomAction, &QAction::triggered, this, [this, rebuildCustomCharacters]() {
        QSettings settings;
        QStringList names = settings.value(QStringLiteral("specialCharacters/names")).toStringList();
        QStringList values = settings.value(QStringLiteral("specialCharacters/values")).toStringList();

        QDialog dialog(this);
        dialog.setWindowTitle(tr("Custom special characters"));
        dialog.resize(520, 360);
        auto* layout = new QVBoxLayout(&dialog);
        auto* list = new QListWidget(&dialog);
        layout->addWidget(list);
        auto refresh = [&]() {
            list->clear();
            for (int i = 0; i < names.size() && i < values.size(); ++i)
                list->addItem(QStringLiteral("%1 — %2").arg(names.at(i), values.at(i)));
        };
        refresh();

        auto* buttons = new QDialogButtonBox(&dialog);
        auto* addButton = buttons->addButton(tr("Add"), QDialogButtonBox::ActionRole);
        auto* removeButton = buttons->addButton(tr("Remove"), QDialogButtonBox::DestructiveRole);
        auto* closeButton = buttons->addButton(QDialogButtonBox::Close);
        layout->addWidget(buttons);
        connect(addButton, &QPushButton::clicked, &dialog, [&]() {
            bool nameOk = false;
            const QString name = QInputDialog::getText(&dialog, tr("Custom character"), tr("Name:"), QLineEdit::Normal,
                                                       QString(), &nameOk)
                                     .trimmed();
            if (!nameOk || name.isEmpty())
                return;
            bool valueOk = false;
            QString hex = QInputDialog::getText(&dialog, tr("Custom character"), tr("Hex bytes (for example 1B5B):"),
                                                QLineEdit::Normal, QString(), &valueOk)
                              .remove(' ')
                              .trimmed()
                              .toUpper();
            if (!valueOk || hex.isEmpty() || hex.size() % 2 != 0 ||
                !QRegularExpression(QStringLiteral("^[0-9A-F]+$")).match(hex).hasMatch()) {
                QMessageBox::warning(&dialog, tr("Invalid sequence"),
                                     tr("Enter an even number of hexadecimal digits."));
                return;
            }
            const int existing = names.indexOf(name);
            if (existing >= 0) {
                values[existing] = hex;
            } else {
                names.append(name);
                values.append(hex);
            }
            refresh();
        });
        connect(removeButton, &QPushButton::clicked, &dialog, [&]() {
            const int row = list->currentRow();
            if (row < 0 || row >= names.size() || row >= values.size())
                return;
            names.removeAt(row);
            values.removeAt(row);
            refresh();
        });
        connect(closeButton, &QPushButton::clicked, &dialog, &QDialog::accept);
        dialog.exec();
        settings.setValue(QStringLiteral("specialCharacters/names"), names);
        settings.setValue(QStringLiteral("specialCharacters/values"), values);
        rebuildCustomCharacters();
    });

    auto* clearAction = editMenu->addAction(tr("Clear Scrollback"));
    connect(clearAction, &QAction::triggered, this, [this]() {
        auto* tab = currentTerminalTab();
        if (tab) {
            tab->clearTerminal();
        }
    });

    editMenu->addSeparator();

    auto* findTerminalAction = editMenu->addAction(tr("Find in current terminal..."));
    KeyboardShortcuts::bindAction(findTerminalAction, "findTerminal", Qt::WidgetShortcut);
    connect(findTerminalAction, &QAction::triggered, this, [this]() {
        if (auto* tab = currentTerminalTab())
            tab->showTerminalSearch();
    });

    auto* globalSearchAction = editMenu->addAction(tr("Find in &All Sessions..."));
    m_globalSearchAction = globalSearchAction;
    KeyboardShortcuts::bindAction(globalSearchAction, "findAll");
    connect(globalSearchAction, &QAction::triggered, this, &MainWindow::onGlobalSearch);

    editMenu->addSeparator();

    auto* settingsAction = editMenu->addAction(tr("C&onfiguration..."));
    connect(settingsAction, &QAction::triggered, this, &MainWindow::onOpenSettings);

    m_lockAction = new QAction(QIcon(":/icons/gear.svg"), tr("Lock Application"), this);
    m_lockAction->setToolTip(tr("Lock the application using the Master Password"));
    KeyboardShortcuts::bindAction(m_lockAction, "lockApplication");
    m_lockAction->setEnabled(MasterPasswordManager::instance().isEnabled());
    connect(m_lockAction, &QAction::triggered, this, &MainWindow::lockApplication);

    auto* viewMenu = menuBar()->addMenu(tr("&View"));

    auto* splitAction = viewMenu->addAction(tr("Split left/right"));
    KeyboardShortcuts::bindAction(splitAction, "toggleSplit");
    connect(splitAction, &QAction::triggered, this, &MainWindow::toggleSplitView);

    auto* gridAction = viewMenu->addAction(tr("Arrange Tabs in a &Grid"));
    KeyboardShortcuts::bindAction(gridAction, "toggleGrid");
    connect(gridAction, &QAction::triggered, this, &MainWindow::toggleGridView);

    auto* moveTabAction = viewMenu->addAction(tr("Move Tab to &Other Pane"));
    KeyboardShortcuts::bindAction(moveTabAction, "moveTab");
    connect(moveTabAction, &QAction::triggered, this, &MainWindow::moveTabToOtherPane);

    auto* detachTabAction = viewMenu->addAction(tr("&Detach Current Tab"));
    KeyboardShortcuts::bindAction(detachTabAction, "detachTab");
    connect(detachTabAction, &QAction::triggered, this, &MainWindow::detachCurrentTab);

    m_multiInputAction = new QAction(QIcon(":/icons/multiinput.svg"), tr("Multi-Input"), this);
    connect(m_multiInputAction, &QAction::triggered, this, &MainWindow::toggleMultiInputBar);
    m_sensitiveMultiInputAction = new QAction(QIcon(":/icons/multiinput.svg"), tr("Sensitive Multi-Input"), this);
    m_sensitiveMultiInputAction->setObjectName("sensitiveMultiInputAction");
    connect(m_sensitiveMultiInputAction, &QAction::triggered, this, &MainWindow::onSensitiveMultiInput);
    auto* toolsMenu = menuBar()->addMenu(tr("&Tools"));
    toolsMenu->addAction(m_multiInputAction);
    toolsMenu->addAction(m_sensitiveMultiInputAction);
    auto* paletteAction = toolsMenu->addAction(tr("Command palette..."));
    KeyboardShortcuts::bindAction(paletteAction, "commandPalette");
    connect(paletteAction, &QAction::triggered, this, &MainWindow::showCommandPalette);
    m_captureShortcutsAction = new QAction(QIcon(":/icons/terminal.svg"), tr("Send shortcuts to terminal"), this);
    m_captureShortcutsAction->setObjectName("captureShortcutsAction");
    m_captureShortcutsAction->setToolTip(tr("Give this terminal priority over application keyboard shortcuts."));
    m_captureShortcutsAction->setCheckable(true);
    m_captureShortcutsAction->setEnabled(false);
    connect(m_captureShortcutsAction, &QAction::triggered, this, [this](bool enabled) {
        if (auto* tab = currentTerminalTab())
            tab->setCaptureApplicationShortcuts(enabled);
    });
    toolsMenu->addAction(m_captureShortcutsAction);

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
        tr("New Remote Session"),     tr("Open Local Terminal"), tr("Split left/right"), tr("Arrange Tabs in a Grid"),
        tr("Move Tab to Other Pane"), tr("Toggle Multi-Input"),  tr("Open Settings"),    tr("Manage Macros"),
        tr("Find in All Sessions"),   tr("Open Diagnostics"),    tr("Clear Clipboard"),  tr("Toggle Theme"),
        tr("Lock Application"),
    };

    bool accepted = false;
    const QString command =
        QInputDialog::getItem(this, tr("Command Palette"), tr("Action:"), commands, 0, true, &accepted);
    if (!accepted || command.isEmpty())
        return;

    if (command == tr("New Remote Session")) {
        m_sessionsSidebar->onNewSession();
    } else if (command == tr("Open Local Terminal")) {
        onNewLocalTerminal();
    } else if (command == tr("Split left/right")) {
        toggleSplitView();
    } else if (command == tr("Arrange Tabs in a Grid")) {
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
    } else if (command == tr("Open Diagnostics")) {
        showDiagnostics();
    } else if (command == tr("Clear Clipboard")) {
        if (auto* clipboard = QApplication::clipboard()) {
            clipboard->clear(QClipboard::Clipboard);
            clipboard->clear(QClipboard::Selection);
        }
        statusBar()->showMessage(tr("Clipboard cleared"), 3000);
    } else if (command == tr("Toggle Theme")) {
        toggleTheme();
    } else if (command == tr("Lock Application")) {
        lockApplication();
    }
}

void MainWindow::lockApplication() {
    auto& masterPassword = MasterPasswordManager::instance();
    if (!masterPassword.isEnabled()) {
        QMessageBox::information(this, tr("Application Lock"),
                                 tr("Enable the Master Password in Settings before locking the application."));
        return;
    }

    masterPassword.lock();

    QDialog dialog(this);
    dialog.setWindowTitle(tr("BanchoXterm Locked"));
    dialog.setModal(true);
    dialog.setWindowFlags(Qt::Dialog | Qt::CustomizeWindowHint | Qt::WindowTitleHint);
    dialog.setMinimumWidth(360);
    auto* layout = new QVBoxLayout(&dialog);
    auto* message = new QLabel(tr("Enter the Master Password to continue."), &dialog);
    message->setWordWrap(true);
    layout->addWidget(message);
    auto* password = new QLineEdit(&dialog);
    password->setEchoMode(QLineEdit::Password);
    password->setPlaceholderText(tr("Master Password"));
    layout->addWidget(password);
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    buttons->button(QDialogButtonBox::Ok)->setIcon(QIcon(QStringLiteral(":/icons/check.svg")));
    buttons->button(QDialogButtonBox::Cancel)->setIcon(QIcon(QStringLiteral(":/icons/close.svg")));
    buttons->button(QDialogButtonBox::Ok)->setText(tr("Unlock"));
    layout->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    password->setFocus();

    constexpr int maxAttempts = 5;
    for (int attempt = 1; attempt <= maxAttempts; ++attempt) {
        if (dialog.exec() != QDialog::Accepted) {
            close();
            return;
        }
        if (masterPassword.unlock(password->text()))
            return;
        if (attempt == maxAttempts) {
            QMessageBox::critical(this, tr("Application Lock"),
                                  tr("Too many incorrect attempts. BanchoXterm will close."));
            close();
            return;
        }
        QMessageBox::warning(&dialog, tr("Application Lock"),
                             tr("The Master Password is incorrect. Attempt %1 of %2.").arg(attempt).arg(maxAttempts));
        password->clear();
        password->setFocus();
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
    return m_panes;
}

QList<QTabWidget*> MainWindow::visiblePanes() const {
    QList<QTabWidget*> panes;
    for (auto* pane : m_panes)
        if (pane->parentWidget() != m_tabSplitter)
            panes.append(pane);
    return panes;
}

QTabWidget* MainWindow::activePane() const {
    if (m_activePane && allPanes().contains(m_activePane) && visiblePanes().contains(m_activePane))
        return m_activePane;
    return visiblePanes().isEmpty() ? m_tabWidget : visiblePanes().first();
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
    if (mode == 2) {
        arrangeGrid();
        return;
    }

    // A hidden pane must not retain tabs when collapsing the layout: moving
    // them back keeps sessions reachable and makes the next layout switch
    // predictable.
    {
        const int firstHiddenPane = mode == 0 ? 1 : mode == 1 ? 2 : 4;
        for (int i = firstHiddenPane; i < allPanes().size(); ++i) {
            QTabWidget* from = allPanes()[i];
            while (from->count() > 0) {
                QWidget* widget = from->widget(0);
                const QString title = from->tabText(0);
                const QIcon icon = from->tabIcon(0);
                const QString tooltip = from->tabToolTip(0);
                from->removeTab(0);
                const int newIndex = m_tabWidget->addTab(widget, icon, title);
                m_tabWidget->setTabToolTip(newIndex, tooltip);
                m_tabWidget->setCurrentIndex(newIndex);
            }
        }
    }

    // Detach the leaves before deleting their old splitter tree.
    for (auto* pane : m_panes) {
        pane->setParent(m_tabSplitter);
        pane->hide();
    }
    delete m_paneRoot;
    m_paneRoot = new QSplitter(Qt::Horizontal, m_tabSplitter);
    m_paneRoot->setObjectName("terminalPaneRoot");
    m_paneRoot->setChildrenCollapsible(false);
    m_tabGrid->addWidget(m_paneRoot, 0, 0);
    m_paneRoot->addWidget(m_tabWidget);
    if (mode == 1)
        m_paneRoot->addWidget(m_tabWidget2);
    m_paneRoot->setSizes({1, 1});
    m_paneLayoutMode = mode;
    for (auto* pane : visiblePanes())
        pane->show();
    m_paneRoot->show();
    refreshPanes();

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

    QList<QPair<QTabWidget*, int>> restored;
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
            target = visiblePanes().first();
        m_activePane = target;
        onConnectSession(Session::fromJson(sessionObject));
        restored.append({target, entry.value("active").toBool(false) ? target->currentIndex() : -1});
    }

    for (const auto& item : restored) {
        if (item.second >= 0) {
            m_activePane = item.first;
            item.first->setCurrentIndex(item.second);
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
    splitPaneHorizontally();
}

void MainWindow::toggleGridView() {
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
                if (!tab)
                    return;

                QList<TerminalTab*> groupTargets;
                const QString group = tab->session().group.trimmed();
                if (!group.isEmpty()) {
                    for (QTabWidget* pane : allOpenPanes()) {
                        for (int tabIndex = 0; tabIndex < pane->count(); ++tabIndex) {
                            auto* candidate = qobject_cast<TerminalTab*>(pane->widget(tabIndex));
                            if (candidate && candidate->session().group.trimmed() == group)
                                groupTargets.append(candidate);
                        }
                    }
                }

                QList<TerminalTab*> targets = {tab};
                if (groupTargets.size() > 1) {
                    const auto choice =
                        localizedQuestion(this, tr("Run Macro"),
                                          tr("The current session belongs to group '%1'. Send this macro to all %2 "
                                             "sessions in the group?")
                                              .arg(group)
                                              .arg(groupTargets.size()),
                                          QMessageBox::Yes | QMessageBox::No | QMessageBox::Cancel, QMessageBox::Yes);
                    if (choice == QMessageBox::Cancel)
                        return;
                    if (choice == QMessageBox::Yes)
                        targets = groupTargets;
                }

                for (int targetIndex = 0; targetIndex < targets.size(); ++targetIndex) {
                    TerminalTab* target = targets.at(targetIndex);
                    QTimer::singleShot(targetIndex * 75, target,
                                       [target, text = texts[i]]() { sendMacro(target, text); });
                }
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

    // Hide the command page instead of relying on clipping its height, which
    // can leave the buttons' top borders visible below the tab header.
    if (auto* page = m_ribbonTabs->currentWidget())
        page->setVisible(expanded);

    // Keep the entire styled header visible with larger fonts or DPI scaling.
    int headerHeight = m_ribbonTabs->tabBar()->sizeHint().height();
    if (auto* corner = m_ribbonTabs->cornerWidget(Qt::TopRightCorner))
        headerHeight = qMax(headerHeight, corner->sizeHint().height());
    const int tabsHeight = expanded ? 94 : headerHeight;
    m_ribbonTabs->setFixedHeight(tabsHeight);
    m_ribbonToolBar->setFixedHeight(tabsHeight + 4);
    m_ribbonToggle->setIcon(QIcon(expanded ? ":/icons/chevron-up.svg" : ":/icons/chevron-down.svg"));
    m_ribbonToggle->setToolTip(m_ribbonPinned ? tr("Collapse Ribbon") : tr("Keep Ribbon expanded"));
}

bool MainWindow::eventFilter(QObject* watched, QEvent* event) {
    if (event->type() == QEvent::DragLeave) {
        setTabDropTarget(nullptr);
    } else if (event->type() == QEvent::DragEnter || event->type() == QEvent::DragMove ||
               event->type() == QEvent::Drop) {
        auto* dragEvent = static_cast<QDropEvent*>(event);
        auto* payload = PaneTabMimeData::fromMimeData(dragEvent->mimeData());
        auto* widget = qobject_cast<QWidget*>(watched);
        if (payload && payload->pane && payload->tab && allOpenPanes().contains(payload->pane) && widget) {
            if (widget == m_panelGroupTabs) {
                const int index = m_panelGroupTabs->tabAt(dragEvent->position().toPoint());
                if (index >= 0) {
                    activatePanelGroup(index);
                    if (event->type() == QEvent::Drop)
                        moveTabToPane(payload->tab, payload->pane, activePane());
                    dragEvent->setDropAction(Qt::MoveAction);
                    dragEvent->accept();
                    return true;
                }
            }
            for (auto* pane : visiblePanes()) {
                if (widget != pane && !pane->isAncestorOf(widget))
                    continue;
                if (payload->pane->indexOf(payload->tab) < 0)
                    break;
                if (event->type() == QEvent::Drop) {
                    const QPoint point =
                        pane->tabBar()->mapFromGlobal(widget->mapToGlobal(dragEvent->position().toPoint()));
                    const int position = pane->tabBar()->rect().contains(point) ? pane->tabBar()->tabAt(point) : -1;
                    const bool moved = moveTabToPane(payload->tab, payload->pane, pane, position);
                    setTabDropTarget(nullptr);
                    if (!moved)
                        break;
                } else {
                    setTabDropTarget(pane);
                }
                dragEvent->setDropAction(Qt::MoveAction);
                dragEvent->accept();
                return true;
            }
        }
    }
    if (event->type() == QEvent::MouseButtonPress) {
        auto* widget = qobject_cast<QWidget*>(watched);
        for (auto* pane : visiblePanes())
            if (widget && (pane == widget || pane->isAncestorOf(widget))) {
                m_activePane = pane;
                updateSessionContext();
                break;
            }
    }
    if (event->type() == QEvent::FocusIn) {
        auto* widget = qobject_cast<QWidget*>(watched);
        for (QTabWidget* pane : visiblePanes()) {
            QWidget* current = pane->currentWidget();
            if (current && widget && (current == widget || current->isAncestorOf(widget)) && activePane() != pane) {
                onCurrentTabChanged(pane, pane->currentIndex());
                break;
            }
        }
    }
    if (event->type() == QEvent::ApplicationPaletteChange && m_themeMode == "system") {
        m_systemPalette = qApp->palette();
        applyThemeMode("system");
    }

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
    textEdit->setPlaceholderText(tr("Text to send (use {{PAUSE:500}} between steps)"));
    lay->addWidget(textEdit);
    auto* referenceBtn = new QPushButton(tr("Quick reference"), &dlg);
    connect(referenceBtn, &QPushButton::clicked, &dlg, [&]() {
        QDialog reference(&dlg);
        reference.setWindowTitle(tr("Macro quick reference"));
        reference.resize(720, 620);
        auto* referenceLayout = new QVBoxLayout(&reference);
        auto* browser = new QTextBrowser(&reference);
        QString html = "<html><body>";
        html += "<p>" +
                tr("Shell commands are sent as text and depend on the active terminal. The following macro tokens are "
                   "supported and are case-sensitive.")
                    .toHtmlEscaped() +
                "</p>";
        auto section = [&](const QString& title) {
            html += "<h3>" + title.toHtmlEscaped() + "</h3><table cellspacing='6' width='100%'><tr><th align='left'>" +
                    tr("Token").toHtmlEscaped() + "</th><th align='left'>" + tr("Effect").toHtmlEscaped() +
                    "</th></tr>";
        };
        auto row = [&](const QString& token, const QString& description) {
            html += "<tr><td valign='top'><code>" + token.toHtmlEscaped().replace(", ", "<br>") + "</code></td><td>" +
                    description.toHtmlEscaped() + "</td></tr>";
        };
        section(tr("Keys"));
        row("{{ENTER}}", tr("Execute the command (Enter)."));
        row("{{TAB}}, {{ESC}}, {{BACKSPACE}}", tr("Send Tab, Escape or Backspace."));
        row("{{CTRL+C}}, {{CTRL+D}}, {{CTRL+Z}}", tr("Send the corresponding control character."));
        row("{{BREAK}}", tr("Send Ctrl+^ (0x1E); this is not a protocol Break signal."));
        row("{{TELNET-ESC}}", tr("Send Ctrl+] (Telnet escape)."));
        html += "</table>";
        section(tr("Sequence"));
        row("{{PAUSE: 500}}", tr("Wait 500 milliseconds. Accepts 0–999999; spaces around the number are allowed."));
        row("{{EXPECT:regex}}", tr("Wait up to 30 seconds for a regular expression in new terminal output."));
        row("{{RESPOND:text}}", tr("Send text immediately; does not add Enter."));
        html += "</table>";
        section(tr("Variables"));
        row("{{HOST}}, {{USER}}, {{PORT}}, {{SESSION}}", tr("Session host, user, port and name."));
        row("{{REMOTE_DIR}}", tr("Configured remote directory, not the current shell directory."));
        row("{{SERIAL_PORT}}", tr("Configured serial port."));
        row("{{ENV:USER}}",
            tr("Local environment variable (where BanchoXterm runs). Unset variables expand to empty text."));
        html += "</table><h3>" + tr("Example").toHtmlEscaped() + "</h3><pre>";
        html += QStringLiteral("echo \"Hello\"{{ENTER}}\n{{PAUSE: 500}}\necho \"Done\"{{ENTER}}").toHtmlEscaped();
        html += "</pre><p>" +
                tr("Line breaks only format the macro. Each command needs an explicit {{ENTER}}.").toHtmlEscaped() +
                "</p>";
        html += "<p>" +
                tr("EXPECT and RESPOND cannot contain a closing brace or nested tokens. To send a response with Enter, "
                   "write plain text followed by {{ENTER}} after EXPECT.")
                    .toHtmlEscaped() +
                "</p></body></html>";
        browser->setHtml(html);
        referenceLayout->addWidget(browser);
        auto* buttons = new QDialogButtonBox(QDialogButtonBox::Close, &reference);
        referenceLayout->addWidget(buttons);
        connect(buttons, &QDialogButtonBox::rejected, &reference, &QDialog::reject);
        reference.exec();
    });

    auto* btnRow = new QHBoxLayout();
    auto* addBtn = new QPushButton(tr("Add / Update"), &dlg);
    auto* delBtn = new QPushButton(tr("Delete"), &dlg);
    auto* importBtn = new QPushButton(tr("Import"), &dlg);
    auto* exportBtn = new QPushButton(tr("Export"), &dlg);
    auto* logBtn = new QPushButton(tr("View log"), &dlg);
    btnRow->addWidget(addBtn);
    btnRow->addWidget(delBtn);
    btnRow->addWidget(importBtn);
    btnRow->addWidget(exportBtn);
    btnRow->addWidget(logBtn);
    btnRow->addWidget(referenceBtn);
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

    connect(exportBtn, &QPushButton::clicked, &dlg, [&]() {
        const QString path =
            QFileDialog::getSaveFileName(&dlg, tr("Export Macros"), QString(), tr("JSON files (*.json)"));
        if (path.isEmpty())
            return;

        QJsonArray macros;
        for (int i = 0; i < names.size() && i < texts.size(); ++i) {
            QJsonObject macro;
            macro.insert(QStringLiteral("name"), names.at(i));
            macro.insert(QStringLiteral("text"), texts.at(i));
            macros.append(macro);
        }
        QFile file(path);
        if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
            QMessageBox::warning(&dlg, tr("Export Macros"), tr("Could not write the selected file."));
            return;
        }
        file.write(QJsonDocument(macros).toJson(QJsonDocument::Indented));
    });

    connect(importBtn, &QPushButton::clicked, &dlg, [&]() {
        const QString path =
            QFileDialog::getOpenFileName(&dlg, tr("Import Macros"), QString(), tr("JSON files (*.json)"));
        if (path.isEmpty())
            return;

        QFile file(path);
        if (!file.open(QIODevice::ReadOnly)) {
            QMessageBox::warning(&dlg, tr("Import Macros"), tr("Could not read the selected file."));
            return;
        }
        QJsonParseError parseError;
        const QJsonDocument document = QJsonDocument::fromJson(file.readAll(), &parseError);
        if (!document.isArray() || parseError.error != QJsonParseError::NoError) {
            QMessageBox::warning(&dlg, tr("Import Macros"), tr("The selected file is not a valid macro list."));
            return;
        }

        int imported = 0;
        for (const QJsonValue& value : document.array()) {
            if (!value.isObject())
                continue;
            const QJsonObject macro = value.toObject();
            const QString name = macro.value(QStringLiteral("name")).toString().trimmed();
            if (name.isEmpty() || !macro.value(QStringLiteral("text")).isString())
                continue;
            const QString text = macro.value(QStringLiteral("text")).toString();
            const int index = names.indexOf(name);
            if (index >= 0)
                texts[index] = text;
            else {
                names.append(name);
                texts.append(text);
            }
            ++imported;
        }
        list->clear();
        list->addItems(names);
        if (imported == 0)
            QMessageBox::warning(&dlg, tr("Import Macros"), tr("No valid macros were found in the selected file."));
    });

    connect(logBtn, &QPushButton::clicked, &dlg, [&]() {
        QDialog logDialog(&dlg);
        logDialog.setWindowTitle(tr("Macro execution log"));
        logDialog.resize(720, 420);
        auto* logLayout = new QVBoxLayout(&logDialog);
        auto* logList = new QListWidget(&logDialog);
        logList->addItems(QSettings().value(QStringLiteral("macros/log")).toStringList());
        logLayout->addWidget(logList);
        auto* closeLog = new QDialogButtonBox(QDialogButtonBox::Close, &logDialog);
        logLayout->addWidget(closeLog);
        connect(closeLog, &QDialogButtonBox::rejected, &logDialog, &QDialog::reject);
        logDialog.exec();
    });

    dlg.exec();

    settings.setValue("macros/names", names);
    settings.setValue("macros/texts", texts);
    rebuildMacrosMenu();
}

void MainWindow::onGlobalSearch() {
    int total = 0;
    for (QTabWidget* pane : allOpenPanes())
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
        for (QTabWidget* pane : allOpenPanes())
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
                activatePanelGroup(panelGroupForPane(pane));
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

QTabWidget* MainWindow::createTerminalPane() {
    auto* pane = new TerminalPane(m_tabSplitter);
    pane->setObjectName("terminalTabs");
    pane->tabBar()->setObjectName("terminalTabBar");
    pane->setTabsClosable(true);
    pane->setMovable(true);
    pane->setDocumentMode(true);
    pane->setMinimumSize(100, 80);
    pane->tabBar()->setExpanding(false);
    pane->tabBar()->setElideMode(Qt::ElideRight);
    connect(pane, &QTabWidget::tabCloseRequested, this, [this, pane](int index) { onTabCloseRequested(pane, index); });
    connect(pane, &QTabWidget::currentChanged, this, [this, pane](int index) { onCurrentTabChanged(pane, index); });
    m_panes.append(pane);
    return pane;
}

void MainWindow::refreshPanes() {
    QTimer::singleShot(0, this, [this]() {
        for (auto* pane : visiblePanes()) {
            pane->update();
            if (auto* tab = qobject_cast<TerminalTab*>(pane->currentWidget()))
                tab->syncTerminalSize();
        }
        updateWelcomeScreen();
        updateSessionContext();
        updateSynchronizationUi();
    });
}

void MainWindow::splitPaneHorizontally() {
    splitActivePane(Qt::Horizontal);
}

void MainWindow::splitPaneVertically() {
    splitActivePane(Qt::Vertical);
}

void MainWindow::splitActivePane(Qt::Orientation orientation) {
    auto* from = activePane();
    auto* parent = qobject_cast<QSplitter*>(from->parentWidget());
    if (!parent)
        return;
    auto* to = static_cast<QTabWidget*>(nullptr);
    for (auto* pane : m_panes)
        if (pane->parentWidget() == m_tabSplitter) {
            to = pane;
            break;
        }
    if (!to)
        to = createTerminalPane();
    // Prefer an existing background tab. Repeated splits can then expose
    // three or more open sessions without leaving intermediate panes empty.
    QTabWidget* donor = from->count() > 1 ? from : nullptr;
    if (!donor)
        for (auto* pane : visiblePanes())
            if (pane->count() > 1) {
                donor = pane;
                break;
            }

    auto sizes = parent->sizes();
    const int position = parent->indexOf(from);
    if (parent->orientation() == orientation || parent->count() == 1) {
        parent->setOrientation(orientation);
        parent->insertWidget(position + 1, to);
        from->show();
        to->show();
        const int half = qMax(1, sizes.value(position) / 2);
        sizes[position] = half;
        sizes.insert(position + 1, half);
        parent->setSizes(sizes);
    } else {
        auto* split = new QSplitter(orientation);
        split->setChildrenCollapsible(false);
        parent->replaceWidget(position, split);
        split->addWidget(from);
        split->addWidget(to);
        from->show();
        to->show();
        split->show();
        split->setSizes({1, 1});
        parent->setSizes(sizes);
    }
    from->show();
    to->show();
    if (donor) {
        const int index = donor->currentIndex();
        QWidget* tab = donor->widget(index);
        const QString title = donor->tabText(index);
        const QIcon icon = donor->tabIcon(index);
        const QString tooltip = donor->tabToolTip(index);
        donor->removeTab(index);
        to->setCurrentIndex(to->addTab(tab, icon, title));
        to->setTabToolTip(0, tooltip);
    }
    m_activePane = to;
    m_paneLayoutMode = 3;
    refreshPanes();
}

void MainWindow::equalizePaneSizes() {
    auto splits = m_paneRoot->findChildren<QSplitter*>();
    splits.prepend(m_paneRoot);
    for (auto* split : splits) {
        QList<int> sizes;
        for (int i = 0; i < split->count(); ++i)
            sizes.append(1);
        split->setSizes(sizes);
    }
    refreshPanes();
}

void MainWindow::closePane() {
    auto* pane = activePane();
    const auto panes = visiblePanes();
    if (panes.size() < 2)
        return;
    auto* target = panes.first() == pane ? panes.at(1) : panes.first();
    while (pane->count()) {
        QWidget* tab = pane->widget(0);
        const QString title = pane->tabText(0);
        const QIcon icon = pane->tabIcon(0);
        pane->removeTab(0);
        target->addTab(tab, icon, title);
    }
    auto* parent = qobject_cast<QSplitter*>(pane->parentWidget());
    pane->setParent(m_tabSplitter);
    pane->hide();
    // Eliminate redundant splitters while preserving their parent's ratios.
    while (parent && parent != m_paneRoot && parent->count() <= 1) {
        auto* grand = qobject_cast<QSplitter*>(parent->parentWidget());
        const auto sizes = grand->sizes();
        if (parent->count() == 1) {
            auto* child = parent->widget(0);
            child->setParent(m_tabSplitter);
            grand->replaceWidget(grand->indexOf(parent), child);
            child->show();
        }
        delete parent;
        grand->setSizes(sizes);
        parent = grand;
    }
    m_activePane = target;
    m_paneLayoutMode = 3;
    refreshPanes();
}

void MainWindow::toggleSidebar() {
    if (m_sidebarContainer->isHidden() || m_mainSplitter->sizes().value(0) == 0) {
        m_sidebarContainer->show();
        m_mainSplitter->setSizes({qMax(100, m_sidebarWidth), qMax(100, m_mainSplitter->width() - m_sidebarWidth)});
    } else {
        m_sidebarWidth = m_sidebarContainer->width();
        m_sidebarContainer->hide();
    }
    refreshPanes();
}

QJsonObject MainWindow::paneTree(QWidget* widget) const {
    if (auto* pane = qobject_cast<QTabWidget*>(widget))
        return {{"pane", m_panes.indexOf(pane)}};
    auto* split = qobject_cast<QSplitter*>(widget);
    if (!split)
        return {};
    QJsonArray children, sizes;
    for (int i = 0; i < split->count(); ++i) {
        children.append(paneTree(split->widget(i)));
        sizes.append(split->sizes().value(i));
    }
    return {{"orientation", split->orientation() == Qt::Horizontal ? "horizontal" : "vertical"},
            {"children", children},
            {"sizes", sizes}};
}

bool MainWindow::restorePaneTree(const QJsonObject& tree) {
    // Validate the entire tree before changing the live workspace.
    QSet<int> used;
    std::function<bool(const QJsonObject&, int)> valid = [&](const QJsonObject& node, int depth) {
        if (depth > 64)
            return false;
        if (node.contains("pane")) {
            const int index = node.value("pane").toInt(-1);
            if (index < 0 || index > 255 || used.contains(index))
                return false;
            used.insert(index);
            return true;
        }
        const auto children = node.value("children").toArray();
        if (children.isEmpty() || children.size() > 256)
            return false;
        const QString orientation = node.value("orientation").toString();
        if (orientation != "horizontal" && orientation != "vertical")
            return false;
        for (const auto& child : children)
            if (!child.isObject() || !valid(child.toObject(), depth + 1))
                return false;
        return true;
    };
    if (!tree.contains("children") || !valid(tree, 0))
        return false;
    for (int index : used)
        while (m_panes.size() <= index)
            createTerminalPane();
    // Keep sessions reachable if this tree does not use all existing leaves.
    for (int i = 0; i < m_panes.size(); ++i) {
        auto* pane = m_panes[i];
        if (!used.contains(i))
            while (pane->count()) {
                auto* tab = pane->widget(0);
                const QString title = pane->tabText(0);
                const QIcon icon = pane->tabIcon(0);
                pane->removeTab(0);
                m_panes[*used.cbegin()]->addTab(tab, icon, title);
            }
        pane->setParent(m_tabSplitter);
        pane->hide();
    }
    delete m_paneRoot;
    std::function<QWidget*(const QJsonObject&)> build = [&](const QJsonObject& node) -> QWidget* {
        if (node.contains("pane"))
            return m_panes[node.value("pane").toInt()];
        auto* split = new QSplitter(node.value("orientation") == "horizontal" ? Qt::Horizontal : Qt::Vertical);
        split->setChildrenCollapsible(false);
        for (const auto& child : node.value("children").toArray()) {
            auto* widget = build(child.toObject());
            split->addWidget(widget);
            widget->show();
        }
        QList<int> sizes;
        for (const auto& size : node.value("sizes").toArray())
            sizes.append(qMax(1, size.toInt(1)));
        if (sizes.size() == split->count())
            split->setSizes(sizes);
        connect(split, &QSplitter::splitterMoved, this, [this]() { refreshPanes(); });
        return split;
    };
    m_paneRoot = qobject_cast<QSplitter*>(build(tree));
    m_paneRoot->setObjectName("terminalPaneRoot");
    m_tabGrid->addWidget(m_paneRoot, 0, 0);
    m_paneRoot->show();
    m_paneLayoutMode = 3;
    m_activePane = visiblePanes().first();
    refreshPanes();
    return true;
}

void MainWindow::configureSynchronization() {
    if (!m_syncAction->isChecked()) {
        m_syncTargets.clear();
        updateSynchronizationUi();
        return;
    }
    chooseSynchronizationTargets();
}

void MainWindow::chooseSynchronizationTargets() {
    const bool alreadyActive = m_syncTargets.size() >= 2;
    QDialog dialog(this);
    dialog.setObjectName("synchronizePanelsDialog");
    dialog.setWindowTitle(tr("Synchronize input"));
    auto* layout = new QVBoxLayout(&dialog);
    auto* explanation = new QLabel(tr("Select at least two sessions. Typing and pasting in any selected session will "
                                      "be sent to all selected sessions. Click Synchronize input again to stop."),
                                   &dialog);
    explanation->setWordWrap(true);
    layout->addWidget(explanation);
    auto* list = new QListWidget(&dialog);
    QList<QPointer<TerminalTab>> targets;
    for (auto* pane : visiblePanes())
        for (int i = 0; i < pane->count(); ++i) {
            auto* tab = qobject_cast<TerminalTab*>(pane->widget(i));
            if (!tab || !tab->isSessionActive() || tab->isRdpOrVnc() || tab->session().readOnly)
                continue;
            targets.append(tab);
            auto* item =
                new QListWidgetItem(tr("Pane %1: %2").arg(m_panes.indexOf(pane) + 1).arg(pane->tabText(i)), list);
            item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
            item->setCheckState((alreadyActive ? m_syncTargets.contains(tab) : tab == pane->currentWidget())
                                    ? Qt::Checked
                                    : Qt::Unchecked);
        }
    layout->addWidget(list);
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    layout->addWidget(buttons);
    auto update = [list, buttons]() {
        int count = 0;
        for (int i = 0; i < list->count(); ++i)
            count += list->item(i)->checkState() == Qt::Checked;
        buttons->button(QDialogButtonBox::Ok)->setEnabled(count >= 2);
    };
    connect(list, &QListWidget::itemChanged, &dialog, [update]() { update(); });
    update();
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    if (dialog.exec() == QDialog::Accepted) {
        QList<QPointer<TerminalTab>> selected;
        for (int i = 0; i < targets.size(); ++i)
            if (targets[i] && list->item(i)->checkState() == Qt::Checked)
                selected.append(targets[i]);
        if (selected.size() >= 2)
            m_syncTargets = selected;
    }
    updateSynchronizationUi();
}

QJsonObject MainWindow::workspace() const {
    QJsonArray tabs;
    for (int i = 0; i < m_panes.size(); ++i)
        for (int j = 0; j < m_panes[i]->count(); ++j)
            if (auto* tab = qobject_cast<TerminalTab*>(m_panes[i]->widget(j)))
                tabs.append(QJsonObject{{"pane", i}, {"session", tab->session().toJson()}});
    return {{"tree", paneTree(m_paneRoot)}, {"tabs", tabs}};
}

void MainWindow::openPanelGroup(const QJsonObject& group, const QString& name) {
    const QString title = name.isEmpty() ? tr("Panel group") : name;
    for (int i = 0; i < m_panelGroups.size(); ++i)
        if (!name.isEmpty() && m_panelGroups[i].savedName == name) {
            activatePanelGroup(i);
            return;
        }
    const int previous = m_currentPanelGroup;
    addPanelGroup(title, name);
    if (!restorePaneTree(group.value("tree").toObject())) {
        closePanelGroup(m_currentPanelGroup);
        return;
    }
    restoreWorkspaceTabs(group);
    discardEmptyDefaultGroup(previous);
}

void MainWindow::managePanelGroups() {
    QSettings settings;
    QJsonObject groups = QJsonDocument::fromJson(settings.value("panels/groups").toByteArray()).object();
    QDialog dialog(this);
    dialog.setObjectName("panelGroupsDialog");
    dialog.setWindowTitle(tr("Panel groups"));
    dialog.resize(760, 560);
    auto* layout = new QVBoxLayout(&dialog);
    auto* list = new QListWidget(&dialog);
    list->setObjectName("panelGroupsList");
    list->addItems(groups.keys());
    layout->addWidget(new QLabel(tr("Saved groups"), &dialog));
    auto* savedRow = new QHBoxLayout;
    savedRow->addWidget(list, 1);
    auto* preview = new QListWidget(&dialog);
    preview->setObjectName("savedPanelGroupSessions");
    preview->setSelectionMode(QAbstractItemView::NoSelection);
    preview->setToolTip(tr("Sessions in the saved group"));
    savedRow->addWidget(preview, 2);
    layout->addLayout(savedRow);
    auto* name = new QLineEdit(&dialog);
    name->setObjectName("panelGroupName");
    name->setPlaceholderText(tr("Group name"));
    layout->addWidget(name);
    layout->addWidget(new QLabel(tr("Choose the open sessions to save with this panel layout:"), &dialog));
    auto* sessions = new QListWidget(&dialog);
    sessions->setObjectName("panelGroupSessions");
    QJsonObject current = workspace();
    for (const auto& value : current.value("tabs").toArray()) {
        const auto entry = value.toObject();
        auto* item = new QListWidgetItem(tr("Pane %1: %2")
                                             .arg(entry.value("pane").toInt() + 1)
                                             .arg(entry.value("session").toObject().value("name").toString()),
                                         sessions);
        item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
        item->setCheckState(Qt::Checked);
    }
    layout->addWidget(sessions);
    auto* row = new QHBoxLayout;
    auto* save = new QPushButton(tr("Save current selection"), &dialog);
    save->setObjectName("savePanelGroup");
    auto* open = new QPushButton(tr("Open group"), &dialog);
    open->setObjectName("openPanelGroup");
    auto* remove = new QPushButton(tr("Delete group"), &dialog);
    remove->setObjectName("deletePanelGroup");
    auto* rename = new QPushButton(tr("Rename group"), &dialog);
    rename->setObjectName("renamePanelGroup");
    row->addWidget(save);
    row->addWidget(open);
    row->addWidget(rename);
    row->addWidget(remove);
    layout->addLayout(row);
    auto persist = [&]() {
        settings.setValue("panels/groups", QJsonDocument(groups).toJson(QJsonDocument::Compact));
        rebuildPanelGroupsMenu();
    };
    connect(list, &QListWidget::currentTextChanged, &dialog, [&](const QString& key) {
        name->setText(key);
        preview->clear();
        for (const auto& value : groups.value(key).toObject().value("tabs").toArray()) {
            const auto profile = value.toObject().value("session").toObject();
            preview->addItem(profile.value("name").toString());
        }
    });
    connect(rename, &QPushButton::clicked, &dialog, [&]() {
        if (!list->currentItem() || name->text().trimmed().isEmpty())
            return;
        const QString old = list->currentItem()->text();
        const QString next = name->text().trimmed();
        if (old == next)
            return;
        if (groups.contains(next)) {
            QMessageBox::information(&dialog, tr("Panel groups"), tr("A saved group with this name already exists."));
            return;
        }
        groups[next] = groups.take(old);
        for (auto& group : m_panelGroups)
            if (group.savedName == old) {
                group.name = next;
                group.savedName = next;
            }
        persist();
        refreshPanelGroupBar();
        list->clear();
        list->addItems(groups.keys());
    });
    connect(save, &QPushButton::clicked, &dialog, [&]() {
        const QString key = name->text().trimmed();
        if (key.isEmpty())
            return;
        QJsonArray selected;
        const auto tabs = current.value("tabs").toArray();
        for (int i = 0; i < tabs.size(); ++i)
            if (sessions->item(i)->checkState() == Qt::Checked)
                selected.append(tabs[i]);
        if (selected.isEmpty())
            return;
        if (groups.contains(key) &&
            localizedQuestion(&dialog, tr("Panel groups"), tr("Replace the saved group %1?").arg(key),
                              QMessageBox::Yes | QMessageBox::No) != QMessageBox::Yes)
            return;
        auto saved = current;
        saved["tabs"] = selected;
        groups[key] = saved;
        persist();
        list->clear();
        list->addItems(groups.keys());
    });
    connect(open, &QPushButton::clicked, &dialog, [&]() {
        if (!list->currentItem())
            return;
        const QString key = list->currentItem()->text();
        const auto group = groups.value(key).toObject();
        dialog.accept();
        openPanelGroup(group, key);
    });
    connect(remove, &QPushButton::clicked, &dialog, [&]() {
        if (!list->currentItem())
            return;
        const QString key = list->currentItem()->text();
        groups.remove(key);
        for (auto& group : m_panelGroups)
            if (group.savedName == key)
                group.savedName.clear();
        delete list->takeItem(list->currentRow());
        persist();
    });
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Close, &dialog);
    layout->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    dialog.exec();
}

void MainWindow::arrangeGrid() {
    struct Tab {
        QWidget* widget;
        QString title;
        QIcon icon;
        QString tooltip;
    };
    QWidget* selected = activePane()->currentWidget();
    QList<Tab> tabs;
    // Suppress currentChanged while moving tabs: session context is refreshed
    // once after every live terminal has reached its destination.
    QList<bool> blocked;
    for (auto* pane : m_panes) {
        blocked.append(pane->blockSignals(true));
        while (pane->count()) {
            tabs.append({pane->widget(0), pane->tabText(0), pane->tabIcon(0), pane->tabToolTip(0)});
            pane->removeTab(0);
        }
        pane->setParent(m_tabSplitter);
        pane->hide();
    }
    const int count = qMax(1, int(tabs.size()));
    while (m_panes.size() < count) {
        auto* pane = createTerminalPane();
        blocked.append(pane->blockSignals(true));
    }
    delete m_paneRoot;
    m_paneRoot = new QSplitter(Qt::Vertical, m_tabSplitter);
    m_paneRoot->setObjectName("terminalPaneRoot");
    m_paneRoot->setChildrenCollapsible(false);
    m_tabGrid->addWidget(m_paneRoot, 0, 0);
    int columns = 1;
    while (columns * columns < count)
        ++columns;
    QList<int> rowSizes;
    for (int first = 0; first < count; first += columns) {
        auto* row = new QSplitter(Qt::Horizontal, m_paneRoot);
        row->setChildrenCollapsible(false);
        QList<int> sizes;
        for (int i = first; i < qMin(first + columns, count); ++i) {
            auto* pane = m_panes[i];
            row->addWidget(pane);
            if (i < tabs.size()) {
                const auto& tab = tabs[i];
                pane->addTab(tab.widget, tab.icon, tab.title);
                pane->setTabToolTip(0, tab.tooltip);
                if (tab.widget == selected)
                    m_activePane = pane;
            }
            pane->show();
            sizes.append(1);
        }
        row->setSizes(sizes);
        rowSizes.append(1);
    }
    m_paneRoot->setSizes(rowSizes);
    for (int i = 0; i < m_panes.size(); ++i)
        m_panes[i]->blockSignals(blocked[i]);
    m_paneLayoutMode = 2;
    if (!selected)
        m_activePane = m_panes.first();
    updateWelcomeScreen();
    refreshPanes();
}

void MainWindow::setTabDropTarget(QTabWidget* pane) {
    if (m_tabDropTarget == pane)
        return;
    auto* previous = m_tabDropTarget.data();
    m_tabDropTarget = pane;
    for (auto* target : {previous, pane}) {
        if (!target)
            continue;
        target->setProperty("tabDropTarget", target == pane);
        target->style()->unpolish(target);
        target->style()->polish(target);
        target->update();
    }
}

bool MainWindow::moveTabToPane(QWidget* tab, QTabWidget* source, QTabWidget* target, int position) {
    if (!tab || !allOpenPanes().contains(source) || !visiblePanes().contains(target))
        return false;
    const int index = source->indexOf(tab);
    if (index < 0)
        return false;
    if (source == target) {
        target->setCurrentWidget(tab);
        return true;
    }
    const QString title = source->tabText(index);
    const QIcon icon = source->tabIcon(index);
    const QString tooltip = source->tabToolTip(index);
    const QVariant data = source->tabBar()->tabData(index);
    const QColor color = source->tabBar()->tabTextColor(index);
    {
        const QSignalBlocker blockSource(source), blockTarget(target);
        source->removeTab(index);
        const int destination = target->insertTab(position, tab, icon, title);
        target->setTabToolTip(destination, tooltip);
        target->tabBar()->setTabData(destination, data);
        target->tabBar()->setTabTextColor(destination, color);
        target->setCurrentIndex(destination);
    }
    if (m_currentPanelGroup >= 0)
        tab->setProperty("panelGroupId", m_panelGroups[m_currentPanelGroup].id);
    m_activePane = target;
    onCurrentTabChanged(target, target->currentIndex());
    if (auto* terminal = qobject_cast<TerminalTab*>(tab))
        if (auto* display = terminal->getTerminalWidget())
            display->setFocus(Qt::OtherFocusReason);
    updateWelcomeScreen();
    refreshPanes();
    return true;
}

void MainWindow::restoreSinglePane() {
    QWidget* selected = activePane()->currentWidget();
    reattachDetachedTabs(m_panelGroups[m_currentPanelGroup].id);
    setPaneLayout(0);
    m_activePane = m_tabWidget;
    if (selected && m_tabWidget->indexOf(selected) >= 0)
        m_tabWidget->setCurrentWidget(selected);
    onCurrentTabChanged(m_tabWidget, m_tabWidget->currentIndex());
    refreshPanes();
}

void MainWindow::updateSynchronizationUi() {
    if (!m_syncAction)
        return;
    m_syncTargets.removeIf([](const auto& tab) {
        return !tab || !tab->isSessionActive() || tab->isRdpOrVnc() || tab->session().readOnly;
    });
    if (m_syncTargets.size() < 2)
        m_syncTargets.clear();
    const bool enabled = m_syncTargets.size() >= 2;
    m_syncAction->setChecked(enabled);
    m_syncAction->setText(enabled ? tr("Stop synchronization (%1)").arg(m_syncTargets.size())
                                  : tr("Synchronize input"));
    for (auto* pane : allOpenPanes()) {
        for (int i = 0; i < pane->count(); ++i) {
            auto* tab = qobject_cast<TerminalTab*>(pane->widget(i));
            QWidget* badge = pane->tabBar()->tabButton(i, QTabBar::LeftSide);
            const bool selected = enabled && tab && m_syncTargets.contains(tab);
            if (selected && !badge) {
                auto* label = new QLabel(QStringLiteral("\u21c4"), pane->tabBar());
                label->setObjectName("synchronizedTabBadge");
                label->setToolTip(tr("Input synchronized"));
                label->setStyleSheet(QStringLiteral("color: #e69c35; font-weight: bold; background: transparent;"));
                label->setMargin(2);
                label->setAttribute(Qt::WA_TransparentForMouseEvents);
                pane->tabBar()->setTabButton(i, QTabBar::LeftSide, label);
            } else if (!selected && badge && badge->objectName() == "synchronizedTabBadge") {
                pane->tabBar()->setTabButton(i, QTabBar::LeftSide, nullptr);
                badge->deleteLater();
            }
        }
    }
}

QList<QTabWidget*> MainWindow::allOpenPanes() const {
    QList<QTabWidget*> result = m_panes;
    for (int i = 0; i < m_panelGroups.size(); ++i)
        if (i != m_currentPanelGroup)
            result.append(m_panelGroups[i].panes);
    return result;
}

int MainWindow::panelGroupForPane(QTabWidget* pane) const {
    if (m_panes.contains(pane))
        return m_currentPanelGroup;
    for (int i = 0; i < m_panelGroups.size(); ++i)
        if (i != m_currentPanelGroup && m_panelGroups[i].panes.contains(pane))
            return i;
    return -1;
}

void MainWindow::capturePanelGroup() {
    if (m_currentPanelGroup < 0 || m_currentPanelGroup >= m_panelGroups.size())
        return;
    auto& group = m_panelGroups[m_currentPanelGroup];
    group.panes = m_panes;
    group.root = m_paneRoot;
    group.activePane = activePane();
    group.layoutMode = m_paneLayoutMode;
}

void MainWindow::refreshPanelGroupBar() {
    if (!m_panelGroupTabs)
        return;
    const QSignalBlocker blocker(m_panelGroupTabs);
    for (int i = 0; i < m_panelGroups.size(); ++i) {
        m_panelGroupTabs->setTabText(i, m_panelGroups[i].name);
        int count = 0;
        for (auto* pane : (i == m_currentPanelGroup ? m_panes : m_panelGroups[i].panes))
            count += pane->count();
        for (auto* tab : m_detachedTabs.keys())
            count += tab->property("panelGroupId").toString() == m_panelGroups[i].id;
        m_panelGroupTabs->setTabToolTip(i, tr("%1 sessions").arg(count));
    }
    m_panelGroupTabs->setCurrentIndex(m_currentPanelGroup);
    m_panelGroupTabs->setVisible(m_panelGroups.size() > 1 ||
                                 (!m_panelGroups.isEmpty() && !m_panelGroups.first().isDefault));
}

void MainWindow::activatePanelGroup(int index, bool stopSynchronization) {
    if (index < 0 || index >= m_panelGroups.size() || index == m_currentPanelGroup)
        return;
    capturePanelGroup();
    setTabDropTarget(nullptr);
    if (m_paneRoot) {
        m_tabGrid->removeWidget(m_paneRoot);
        m_paneRoot->setParent(m_tabSplitter);
        m_paneRoot->hide();
    }
    if (stopSynchronization) {
        m_syncTargets.clear();
        updateSynchronizationUi();
    }
    m_currentPanelGroup = index;
    const auto& group = m_panelGroups[index];
    m_panes = group.panes;
    m_paneRoot = group.root;
    m_tabWidget = m_panes.value(0);
    m_tabWidget2 = m_panes.value(1);
    m_tabWidget3 = m_panes.value(2);
    m_tabWidget4 = m_panes.value(3);
    m_activePane = group.activePane;
    m_paneLayoutMode = group.layoutMode;
    m_tabGrid->addWidget(m_paneRoot, 0, 0);
    refreshPanelGroupBar();
    onCurrentTabChanged(activePane(), activePane()->currentIndex());
    updateWelcomeScreen();
    refreshPanes();
}

void MainWindow::addPanelGroup(const QString& name, const QString& savedName, bool isDefault) {
    capturePanelGroup();
    if (m_paneRoot) {
        m_tabGrid->removeWidget(m_paneRoot);
        m_paneRoot->setParent(m_tabSplitter);
        m_paneRoot->hide();
    }
    m_syncTargets.clear();
    updateSynchronizationUi();
    PanelGroupState group;
    group.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    group.name = name;
    group.savedName = savedName;
    group.isDefault = isDefault;
    m_panelGroups.append(group);
    m_currentPanelGroup = m_panelGroups.size() - 1;
    m_panes.clear();
    m_paneRoot = new QSplitter(Qt::Horizontal, m_tabSplitter);
    m_paneRoot->setObjectName("terminalPaneRoot");
    m_paneRoot->setChildrenCollapsible(false);
    m_tabGrid->addWidget(m_paneRoot, 0, 0);
    m_tabWidget = createTerminalPane();
    m_tabWidget2 = createTerminalPane();
    m_tabWidget3 = createTerminalPane();
    m_tabWidget4 = createTerminalPane();
    for (auto* pane : m_panes)
        pane->hide();
    m_paneRoot->addWidget(m_tabWidget);
    m_tabWidget->show();
    m_activePane = m_tabWidget;
    m_paneLayoutMode = 0;
    {
        const QSignalBlocker blocker(m_panelGroupTabs);
        m_panelGroupTabs->addTab(QIcon(":/icons/folder.svg"), name);
    }
    capturePanelGroup();
    refreshPanelGroupBar();
    onCurrentTabChanged(m_tabWidget, -1);
    updateWelcomeScreen();
}

void MainWindow::rebuildPanelGroupsMenu() {
    if (!m_savedPanelGroupsMenu)
        return;
    m_savedPanelGroupsMenu->clear();
    const auto groups = QJsonDocument::fromJson(QSettings().value("panels/groups").toByteArray()).object();
    if (groups.isEmpty()) {
        auto* empty = m_savedPanelGroupsMenu->addAction(tr("No saved groups"));
        empty->setEnabled(false);
    }
    for (const auto& name : groups.keys()) {
        auto* action = m_savedPanelGroupsMenu->addAction(QIcon(":/icons/folder.svg"), name);
        const auto definition = groups[name].toObject();
        connect(action, &QAction::triggered, this, [this, definition, name]() { openPanelGroup(definition, name); });
    }
}

void MainWindow::restoreWorkspaceTabs(const QJsonObject& definition) {
    QTabWidget* focused = nullptr;
    int focusedIndex = -1;
    for (const auto& value : definition.value("tabs").toArray()) {
        const auto entry = value.toObject();
        const int pane = entry.value("pane").toInt(-1);
        const auto profile = entry.value("session").toObject();
        if (pane < 0 || pane >= m_panes.size() || profile.isEmpty())
            continue;
        m_activePane = m_panes[pane];
        onConnectSession(Session::fromJson(profile));
        if (entry.value("active").toBool()) {
            focused = m_activePane;
            focusedIndex = focused->currentIndex();
        }
    }
    if (focused) {
        m_activePane = focused;
        focused->setCurrentIndex(focusedIndex);
    }
    capturePanelGroup();
    updateWelcomeScreen();
    refreshPanes();
}

void MainWindow::savePanelWorkspaces(QSettings& settings) {
    capturePanelGroup();
    QJsonArray workspaces;
    for (const auto& group : m_panelGroups) {
        QJsonArray tabs;
        for (int i = 0; i < group.panes.size(); ++i) {
            auto* pane = group.panes[i];
            for (int j = 0; j < pane->count(); ++j) {
                auto* tab = qobject_cast<TerminalTab*>(pane->widget(j));
                if (tab)
                    tabs.append(QJsonObject{{"pane", i},
                                            {"session", tab->session().toJson()},
                                            {"active", pane == group.activePane && j == pane->currentIndex()}});
            }
        }
        // paneTree uses current indices; other live groups have their own index space.
        std::function<QJsonObject(QWidget*)> tree = [&](QWidget* widget) -> QJsonObject {
            if (auto* pane = qobject_cast<QTabWidget*>(widget))
                return {{"pane", group.panes.indexOf(pane)}};
            auto* splitter = qobject_cast<QSplitter*>(widget);
            QJsonArray children, sizes;
            for (int i = 0; splitter && i < splitter->count(); ++i) {
                children.append(tree(splitter->widget(i)));
                sizes.append(splitter->sizes().value(i));
            }
            return {{"orientation", splitter && splitter->orientation() == Qt::Vertical ? "vertical" : "horizontal"},
                    {"children", children},
                    {"sizes", sizes}};
        };
        workspaces.append(QJsonObject{{"id", group.id},
                                      {"name", group.name},
                                      {"savedName", group.savedName},
                                      {"default", group.isDefault},
                                      {"tree", tree(group.root)},
                                      {"tabs", tabs}});
    }
    settings.setValue("window/panelWorkspaces", QJsonDocument(workspaces).toJson(QJsonDocument::Compact));
    settings.setValue("window/activePanelGroup", m_panelGroups[m_currentPanelGroup].id);
}

bool MainWindow::restorePanelWorkspaces(const QSettings& settings) {
    const auto document = QJsonDocument::fromJson(settings.value("window/panelWorkspaces").toByteArray());
    if (!document.isArray() || document.array().isEmpty())
        return false;
    const auto entries = document.array();
    // Reject malformed trees before opening any saved network sessions.
    std::function<bool(const QJsonObject&, QSet<int>&, int)> valid = [&](const QJsonObject& node, QSet<int>& used,
                                                                         int depth) {
        if (depth > 64)
            return false;
        if (node.contains("pane")) {
            const int index = node.value("pane").toInt(-1);
            if (index < 0 || index > 255 || used.contains(index))
                return false;
            used.insert(index);
            return true;
        }
        const auto children = node.value("children").toArray();
        if (children.isEmpty() || children.size() > 256)
            return false;
        const auto orientation = node.value("orientation").toString();
        if (orientation != "horizontal" && orientation != "vertical")
            return false;
        for (const auto& child : children)
            if (!valid(child.toObject(), used, depth + 1))
                return false;
        return true;
    };
    for (const auto& entry : entries) {
        QSet<int> used;
        const auto tree = entry.toObject().value("tree").toObject();
        if (!tree.contains("children") || !valid(tree, used, 0))
            return false;
    }
    int active = 0;
    for (int i = 0; i < entries.size(); ++i) {
        const auto entry = entries[i].toObject();
        if (i > 0)
            addPanelGroup(entry.value("name").toString(tr("Panel group")), entry.value("savedName").toString());
        auto& group = m_panelGroups[m_currentPanelGroup];
        group.id = entry.value("id").toString(QUuid::createUuid().toString(QUuid::WithoutBraces));
        group.name = entry.value("name").toString(tr("Panel group"));
        group.savedName = entry.value("savedName").toString();
        group.isDefault = entry.value("default").toBool();
        restorePaneTree(entry.value("tree").toObject());
        restoreWorkspaceTabs(entry);
        if (group.id == settings.value("window/activePanelGroup").toString())
            active = i;
    }
    activatePanelGroup(active);
    refreshPanelGroupBar();
    return true;
}

void MainWindow::closePanelGroup(int index) {
    if (index < 0 || index >= m_panelGroups.size())
        return;
    const auto panes = index == m_currentPanelGroup ? m_panes : m_panelGroups[index].panes;
    bool connected = false;
    for (auto* pane : panes)
        for (int i = 0; i < pane->count(); ++i)
            if (auto* tab = qobject_cast<TerminalTab*>(pane->widget(i)))
                connected = connected || tab->isSessionActive();
    for (auto* tab : m_detachedTabs.keys())
        if (tab->property("panelGroupId").toString() == m_panelGroups[index].id)
            connected = connected || tab->isSessionActive();
    if (connected &&
        localizedQuestion(this, tr("Close panel group"),
                          tr("Disconnect the sessions in group %1 and close it?").arg(m_panelGroups[index].name),
                          QMessageBox::Yes | QMessageBox::No, QMessageBox::No) != QMessageBox::Yes)
        return;
    const int previous = m_currentPanelGroup;
    activatePanelGroup(index);
    reattachDetachedTabs(m_panelGroups[index].id);
    m_sftpSidebar->detachConnection();
    m_sftpSidebar->stopSession();
    const auto removed = m_panes;
    m_panes.clear();
    for (auto* pane : removed) {
        for (int i = 0; i < pane->count(); ++i)
            if (auto* tab = qobject_cast<TerminalTab*>(pane->widget(i))) {
                disconnect(tab, nullptr, this, nullptr);
                m_diagnosticLogs.remove(tab);
            }
        pane->setParent(m_tabSplitter);
        delete pane;
    }
    m_tabGrid->removeWidget(m_paneRoot);
    delete m_paneRoot;
    m_paneRoot = nullptr;
    m_activePane = nullptr;
    m_panelGroups.removeAt(index);
    m_currentPanelGroup = -1;
    {
        const QSignalBlocker blocker(m_panelGroupTabs);
        m_panelGroupTabs->removeTab(index);
    }
    if (m_panelGroups.isEmpty())
        addPanelGroup(tr("Ungrouped"), QString(), true);
    else
        activatePanelGroup(previous == index ? qMin(index, int(m_panelGroups.size()) - 1)
                                             : previous - (previous > index));
    refreshPanelGroupBar();
}

void MainWindow::saveActivePanelGroup() {
    if (m_currentPanelGroup < 0)
        return;
    auto& group = m_panelGroups[m_currentPanelGroup];
    if (group.isDefault) {
        createPanelGroup();
        return;
    }
    QSettings settings;
    auto groups = QJsonDocument::fromJson(settings.value("panels/groups").toByteArray()).object();
    groups[group.name] = workspace();
    group.savedName = group.name;
    settings.setValue("panels/groups", QJsonDocument(groups).toJson(QJsonDocument::Compact));
    rebuildPanelGroupsMenu();
}

void MainWindow::renameActivePanelGroup() {
    if (m_currentPanelGroup < 0)
        return;
    bool accepted = false;
    const QString name = QInputDialog::getText(this, tr("Rename group"), tr("Group name"), QLineEdit::Normal,
                                               m_panelGroups[m_currentPanelGroup].name, &accepted)
                             .trimmed();
    if (!accepted || name.isEmpty())
        return;
    QSettings settings;
    auto groups = QJsonDocument::fromJson(settings.value("panels/groups").toByteArray()).object();
    auto& group = m_panelGroups[m_currentPanelGroup];
    if (name != group.savedName && groups.contains(name)) {
        QMessageBox::information(this, tr("Panel groups"), tr("A saved group with this name already exists."));
        return;
    }
    if (!group.savedName.isEmpty() && groups.contains(group.savedName)) {
        const auto definition = groups.take(group.savedName);
        groups[name] = definition;
        group.savedName = name;
        settings.setValue("panels/groups", QJsonDocument(groups).toJson(QJsonDocument::Compact));
    }
    group.name = name;
    group.isDefault = false;
    refreshPanelGroupBar();
    rebuildPanelGroupsMenu();
}

void MainWindow::createPanelGroup() {
    struct Selection {
        QPointer<TerminalTab> tab;
        QPointer<QTabWidget> pane;
        int index;
    };
    QList<Selection> candidates;
    QDialog dialog(this);
    dialog.setObjectName("createPanelGroupDialog");
    dialog.setWindowTitle(tr("New group"));
    dialog.resize(560, 430);
    auto* layout = new QVBoxLayout(&dialog);
    auto* name = new QLineEdit(&dialog);
    name->setObjectName("newPanelGroupName");
    name->setPlaceholderText(tr("Group name"));
    layout->addWidget(name);
    auto* explanation =
        new QLabel(tr("Choose the open tabs to move into the new group. Their sessions stay connected."), &dialog);
    explanation->setWordWrap(true);
    layout->addWidget(explanation);
    auto* list = new QListWidget(&dialog);
    list->setObjectName("newPanelGroupSessions");
    for (int i = 0; i < m_panes.size(); ++i)
        for (int j = 0; j < m_panes[i]->count(); ++j)
            if (auto* tab = qobject_cast<TerminalTab*>(m_panes[i]->widget(j))) {
                candidates.append({tab, m_panes[i], i});
                auto* item = new QListWidgetItem(m_panes[i]->tabText(j), list);
                item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
                item->setCheckState(Qt::Checked);
            }
    layout->addWidget(list);
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Save | QDialogButtonBox::Cancel, &dialog);
    buttons->button(QDialogButtonBox::Save)->setObjectName("createPanelGroupSave");
    layout->addWidget(buttons);
    auto update = [=]() {
        int selected = 0;
        for (int i = 0; i < list->count(); ++i)
            selected += list->item(i)->checkState() == Qt::Checked;
        buttons->button(QDialogButtonBox::Save)->setEnabled(selected > 0 && !name->text().trimmed().isEmpty());
    };
    connect(name, &QLineEdit::textChanged, &dialog, update);
    connect(list, &QListWidget::itemChanged, &dialog, [update]() { update(); });
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, [&]() {
        const QString key = name->text().trimmed();
        for (const auto& group : m_panelGroups)
            if (!group.isDefault && group.name == key) {
                QMessageBox::information(&dialog, tr("Panel groups"),
                                         tr("An open group with this name already exists."));
                return;
            }
        const auto saved = QJsonDocument::fromJson(QSettings().value("panels/groups").toByteArray()).object();
        if (saved.contains(key) &&
            localizedQuestion(&dialog, tr("Panel groups"), tr("Replace the saved group %1?").arg(key),
                              QMessageBox::Yes | QMessageBox::No) != QMessageBox::Yes)
            return;
        dialog.accept();
    });
    update();
    if (dialog.exec() != QDialog::Accepted)
        return;
    QList<Selection> selected;
    QSet<int> included;
    for (int i = 0; i < candidates.size(); ++i)
        if (candidates[i].tab && candidates[i].pane && list->item(i)->checkState() == Qt::Checked) {
            selected.append(candidates[i]);
            included.insert(candidates[i].index);
        }
    if (selected.isEmpty())
        return;
    QMap<int, int> mapping;
    std::function<QJsonObject(const QJsonObject&)> prune = [&](const QJsonObject& node) -> QJsonObject {
        if (node.contains("pane")) {
            const int index = node.value("pane").toInt();
            if (!included.contains(index))
                return {};
            if (!mapping.contains(index))
                mapping[index] = mapping.size();
            return {{"pane", mapping[index]}};
        }
        QJsonArray children, sizes;
        const auto original = node.value("children").toArray();
        const auto weights = node.value("sizes").toArray();
        for (int i = 0; i < original.size(); ++i) {
            const auto child = prune(original[i].toObject());
            if (!child.isEmpty()) {
                children.append(child);
                sizes.append(i < weights.size() ? weights[i] : QJsonValue(1));
            }
        }
        if (children.isEmpty())
            return {};
        if (children.size() == 1)
            return children[0].toObject();
        return {{"orientation", node.value("orientation")}, {"children", children}, {"sizes", sizes}};
    };
    auto tree = prune(paneTree(m_paneRoot));
    if (tree.contains("pane"))
        tree = {{"orientation", "horizontal"}, {"children", QJsonArray{tree}}, {"sizes", QJsonArray{1}}};
    const QString key = name->text().trimmed();
    const int originalGroup = m_currentPanelGroup;
    QPointer<QWidget> focused = activePane()->currentWidget();
    addPanelGroup(key, key);
    restorePaneTree(tree);
    for (const auto& item : selected)
        if (item.tab && item.pane)
            moveTabToPane(item.tab, item.pane, m_panes[mapping.value(item.index)]);
    if (focused)
        for (auto* pane : m_panes)
            if (pane->indexOf(focused) >= 0) {
                pane->setCurrentWidget(focused);
                m_activePane = pane;
                break;
            }
    saveActivePanelGroup();
    capturePanelGroup();
    discardEmptyDefaultGroup(originalGroup);
    refreshPanelGroupBar();
    refreshPanes();
}

void MainWindow::discardEmptyDefaultGroup(int index) {
    if (index < 0 || index == m_currentPanelGroup || index >= m_panelGroups.size() || !m_panelGroups[index].isDefault)
        return;
    for (auto* pane : m_panelGroups[index].panes)
        if (pane->count() != 0)
            return;
    for (auto* tab : m_detachedTabs.keys())
        if (tab->property("panelGroupId").toString() == m_panelGroups[index].id)
            return;
    closePanelGroup(index);
}
