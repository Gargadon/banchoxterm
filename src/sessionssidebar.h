#pragma once
#include <QWidget>
#include <QList>
#include "session.h"

class QTreeWidget;
class QTreeWidgetItem;
class QLineEdit;
class QCompleter;
class QLabel;

class SessionsSidebar : public QWidget {
    Q_OBJECT
public:
    explicit SessionsSidebar(QWidget* parent = nullptr);

signals:
    void connectSession(const Session& session);
    void newLocalSessionRequested();

public slots:
    void onNewSession();

private slots:
    void onQuickConnect();
    void onEditSession();
    void onDeleteSession();
    void onDuplicateSession();
    void onToggleFavorite();
    void onImportSessions();
    void onExportSessions();
    void onItemDoubleClicked(QTreeWidgetItem* item, int column);
    void showContextMenu(const QPoint& pos);

private:
    void loadSessions();
    void saveSessions();
    void onSessionsReordered();
    void recordRecent(const Session& session);
    QTreeWidgetItem* findSessionItem(const QString& id) const;
    QString sessionIdForItem(QTreeWidgetItem* item) const;

    QTreeWidget* m_treeWidget;
    QLineEdit* m_quickConnectEdit = nullptr;
    QCompleter* m_quickConnectCompleter = nullptr;
    QLabel* m_sessionSummaryLabel = nullptr;
    QList<Session> m_sessions;
};
