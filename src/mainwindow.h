#pragma once
#include <QMainWindow>
#include <QMap>
#include "session.h"

class QTabWidget;
class QStackedWidget;
class QToolButton;
class QFrame;
class QSplitter;
class QComboBox;
class QLabel;
class QAction;
class QMenu;
class SessionsSidebar;
class SftpSidebar;
class TerminalTab;

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
    void moveTabToOtherPane();

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

    QString m_themeMode = "system";
    QSplitter* m_mainSplitter;

    QFrame* m_sidebarContainer;
    QStackedWidget* m_sidebarStacked;
    SessionsSidebar* m_sessionsSidebar;
    SftpSidebar* m_sftpSidebar;

    QToolButton* m_sessionsTabBtn;
    QToolButton* m_sftpTabBtn;
    QToolButton* m_multiInputBtn = nullptr;

    QSplitter* m_tabSplitter = nullptr;
    QTabWidget* m_tabWidget = nullptr;
    QTabWidget* m_tabWidget2 = nullptr;
    QTabWidget* m_activePane = nullptr;

    QWidget* m_multiInputBar;
    QComboBox* m_multiInputEdit;
    QLabel* m_remoteMonitorLabel;

    QAction* m_copyAction;
    QAction* m_pasteAction;
    QMenu* m_macrosMenu = nullptr;

    bool m_isDarkTheme = true;
};
