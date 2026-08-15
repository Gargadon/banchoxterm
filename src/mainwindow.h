#pragma once
#include <QMainWindow>
#include <QPalette>
#include <QMap>
#include <QHash>
#include <QList>
#include "session.h"

class QTabWidget;
class QStackedWidget;
class QToolButton;
class QFrame;
class QSplitter;
class QGridLayout;
class QWidget;
class QComboBox;
class QLabel;
class QAction;
class QMenu;
class SessionsSidebar;
class SftpSidebar;
class TerminalTab;
class QMainWindow;

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
    void onRemoteStatsUpdated(double cpu, double mem, double disk, double uptimeSecs);
    void onReconnectRequested(const Session& session);
    void toggleSplitView();
    void toggleGridView();
    void moveTabToOtherPane();
    void detachCurrentTab();

    void onCopy();
    void onPaste();
    void showAbout();
    void onManageMacros();
    void onGlobalSearch();

private:
    void setupUi();
    void setupMenuBar();
    void applyThemeMode(const QString& mode);
    void rebuildMacrosMenu();
    TerminalTab* currentTerminalTab() const;
    QTabWidget* activePane() const;
    QTabWidget* otherPane(QTabWidget* pane) const;
    QList<QTabWidget*> allPanes() const;
    QList<QTabWidget*> visiblePanes() const;
    void setPaneLayout(int mode, bool moveCurrentTab = false);
    void updateSessionContext();
    void reattachDetachedTabs();

    QSplitter* m_mainSplitter;

    QFrame* m_sidebarContainer;
    QStackedWidget* m_sidebarStacked;
    SessionsSidebar* m_sessionsSidebar;
    SftpSidebar* m_sftpSidebar;

    QToolButton* m_sessionsTabBtn;
    QToolButton* m_sftpTabBtn;
    QToolButton* m_multiInputBtn = nullptr;

    QWidget* m_tabSplitter = nullptr;
    QGridLayout* m_tabGrid = nullptr;
    QTabWidget* m_tabWidget = nullptr;
    QTabWidget* m_tabWidget2 = nullptr;
    QTabWidget* m_tabWidget3 = nullptr;
    QTabWidget* m_tabWidget4 = nullptr;
    QTabWidget* m_activePane = nullptr;
    int m_paneLayoutMode = 0; // 0 = single, 1 = split, 2 = 2x2 grid

    QWidget* m_multiInputBar;
    QComboBox* m_multiInputEdit;
    QFrame* m_remoteMonitorWidget = nullptr;
    QLabel* m_remoteCpuValue = nullptr;
    QLabel* m_remoteMemValue = nullptr;
    QLabel* m_remoteDiskValue = nullptr;
    QLabel* m_remoteUptimeValue = nullptr;

    QAction* m_copyAction;
    QAction* m_pasteAction;
    QMenu* m_macrosMenu = nullptr;

    QString m_themeMode = "system";
    QPalette m_systemPalette;
    QHash<TerminalTab*, QMainWindow*> m_detachedTabs;
    QFrame* m_sessionContextBar = nullptr;
    QLabel* m_contextProtocolLabel = nullptr;
    QLabel* m_contextSessionLabel = nullptr;
    QLabel* m_contextStateLabel = nullptr;
    QLabel* m_statusConnectionLabel = nullptr;
};
