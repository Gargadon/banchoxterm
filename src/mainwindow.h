#pragma once
#include <QMainWindow>
#include <QPalette>
#include <QMap>
#include <QHash>
#include <QList>
#include <QJsonObject>
#include <QPointer>
#include "session.h"

class QTabWidget;
class QTabBar;
class QStackedWidget;
class QToolButton;
class QFrame;
class QSplitter;
class QGridLayout;
class QHBoxLayout;
class QWidget;
class QComboBox;
class QLabel;
class QAction;
class QMenu;
class QSettings;
class QToolBar;
class SessionsSidebar;
class SftpSidebar;
class SshConnection;
class TerminalTab;
class QMainWindow;
class QEvent;

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(QWidget* parent = nullptr);
    ~MainWindow() override;

protected:
    void closeEvent(QCloseEvent* event) override;

private slots:
    void onConnectSession(const Session& session);
    void onTabCloseRequested(QTabWidget* pane, int index);
    void onCurrentTabChanged(QTabWidget* pane, int index);
    void onNewLocalTerminal();
    void toggleTheme();
    void switchSidebarTab(int index);
    void onOpenSettings();
    void toggleMultiInputBar();
    void onSendMultiInput();
    void onSensitiveMultiInput();
    void onRemoteStatsUpdated(double cpu, double mem, double disk, double uptimeSecs);
    void onReconnectRequested(const Session& session);
    void toggleSplitView();
    void toggleGridView();
    void moveTabToOtherPane();
    void detachCurrentTab();
    void splitPaneHorizontally();
    void splitPaneVertically();
    void closePane();
    void equalizePaneSizes();
    void restoreSinglePane();
    void chooseSynchronizationTargets();
    void configureSynchronization();
    void managePanelGroups();
    void createPanelGroup();
    void saveActivePanelGroup();
    void renameActivePanelGroup();
    void closePanelGroup(int index);
    void toggleSidebar();

    void onCopy();
    void onPaste();
    void showAbout();
    void onManageMacros();
    void onGlobalSearch();
    void showDiagnostics();
    void showCommandPalette();
    void lockApplication();

private:
    bool broadcastInput(const QString& text, bool sensitive);
    void setupUi();
    void setupMenuBar();
    void applyThemeMode(const QString& mode);
    void rebuildMacrosMenu();
    void rebuildMacrosRibbon();
    TerminalTab* currentTerminalTab() const;
    QTabWidget* activePane() const;
    QTabWidget* otherPane(QTabWidget* pane) const;
    QList<QTabWidget*> allPanes() const;
    QList<QTabWidget*> allOpenPanes() const;
    QList<QTabWidget*> visiblePanes() const;
    void setPaneLayout(int mode, bool moveCurrentTab = false);
    QTabWidget* createTerminalPane();
    void splitActivePane(Qt::Orientation orientation);
    void refreshPanes();
    void arrangeGrid();
    bool moveTabToPane(QWidget* tab, QTabWidget* source, QTabWidget* target, int position = -1);
    void setTabDropTarget(QTabWidget* pane);
    void updateSynchronizationUi();
    QJsonObject paneTree(QWidget* widget) const;
    bool restorePaneTree(const QJsonObject& tree);
    QJsonObject workspace() const;
    void openPanelGroup(const QJsonObject& group, const QString& name = QString());
    void capturePanelGroup();
    void discardEmptyDefaultGroup(int index);
    void activatePanelGroup(int index, bool stopSynchronization = true);
    void addPanelGroup(const QString& name, const QString& savedName = QString(), bool isDefault = false);
    void refreshPanelGroupBar();
    void rebuildPanelGroupsMenu();
    int panelGroupForPane(QTabWidget* pane) const;
    bool restorePanelWorkspaces(const QSettings& settings);
    void savePanelWorkspaces(QSettings& settings);
    void restoreWorkspaceTabs(const QJsonObject& definition);
    void saveOpenTabs(QSettings& settings) const;
    void restoreOpenTabs(const QSettings& settings);
    void updateSessionContext();
    void updateWelcomeScreen();
    void reattachDetachedTabs(const QString& groupId = QString());
    void setRibbonExpanded(bool expanded);
    bool eventFilter(QObject* watched, QEvent* event) override;

    QSplitter* m_mainSplitter;

    QFrame* m_sidebarContainer;
    QStackedWidget* m_sidebarStacked;
    SessionsSidebar* m_sessionsSidebar;
    SftpSidebar* m_sftpSidebar;
    void prepareSftpSidebar(SshConnection* connection);
    QList<SftpSidebar*> m_backgroundSftpSidebars;

    QToolButton* m_sessionsTabBtn;
    QToolButton* m_sftpTabBtn;
    QAction* m_multiInputAction = nullptr;
    QAction* m_sensitiveMultiInputAction = nullptr;
    QAction* m_captureShortcutsAction = nullptr;
    QStringList m_sensitiveInputNames;
    QStringList m_sensitiveInputValues;

    struct PanelGroupState {
        QString id;
        QString name;
        QString savedName;
        bool isDefault = false;
        QList<QTabWidget*> panes;
        QSplitter* root = nullptr;
        QTabWidget* activePane = nullptr;
        int layoutMode = 0;
    };
    QList<PanelGroupState> m_panelGroups;
    int m_currentPanelGroup = -1;
    QTabBar* m_panelGroupTabs = nullptr;
    QMenu* m_savedPanelGroupsMenu = nullptr;
    QWidget* m_tabSplitter = nullptr;
    QGridLayout* m_tabGrid = nullptr;
    QSplitter* m_paneRoot = nullptr;
    QList<QTabWidget*> m_panes;
    QAction* m_syncAction = nullptr;
    QPointer<QTabWidget> m_tabDropTarget;
    QList<QPointer<TerminalTab>> m_syncTargets;
    int m_sidebarWidth = 350;
    QTabWidget* m_tabWidget = nullptr;
    QTabWidget* m_tabWidget2 = nullptr;
    QTabWidget* m_tabWidget3 = nullptr;
    QTabWidget* m_tabWidget4 = nullptr;
    QTabWidget* m_activePane = nullptr;
    QWidget* m_welcomeWidget = nullptr;
    QTabWidget* m_ribbonTabs = nullptr;
    QToolBar* m_ribbonToolBar = nullptr;
    QToolButton* m_ribbonToggle = nullptr;
    bool m_ribbonPinned = true;
    QWidget* m_macrosRibbonPage = nullptr;
    QHBoxLayout* m_macrosRibbonLayout = nullptr;
    int m_paneLayoutMode = 0; // 0 = single, 1 = split, 2 = grid, 3 = custom

    QWidget* m_multiInputBar;
    QComboBox* m_multiInputEdit;
    QFrame* m_remoteMonitorWidget = nullptr;
    QLabel* m_remoteCpuValue = nullptr;
    QLabel* m_remoteMemValue = nullptr;
    QLabel* m_remoteDiskValue = nullptr;
    QLabel* m_remoteUptimeValue = nullptr;

    QAction* m_globalSearchAction = nullptr;
    QAction* m_copyAction;
    QAction* m_pasteAction;
    QAction* m_specialCharactersAction = nullptr;
    QAction* m_lockAction = nullptr;
    QMenu* m_macrosMenu = nullptr;

    QString m_themeMode = "system";
    QPalette m_systemPalette;
    QHash<TerminalTab*, QMainWindow*> m_detachedTabs;
    QFrame* m_sessionContextBar = nullptr;
    QLabel* m_contextProtocolLabel = nullptr;
    QLabel* m_contextSessionLabel = nullptr;
    QLabel* m_contextStateLabel = nullptr;
    QLabel* m_statusConnectionLabel = nullptr;
    QHash<TerminalTab*, QStringList> m_diagnosticLogs;
    QHash<QString, int> m_reconnectCounts;
};
