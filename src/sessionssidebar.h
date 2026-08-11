#pragma once
#include <QWidget>
#include <QList>
#include "session.h"

class QListWidget;
class QListWidgetItem;

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
    void onEditSession();
    void onDeleteSession();
    void onItemDoubleClicked(QListWidgetItem* item);
    void showContextMenu(const QPoint& pos);

private:
    void loadSessions();
    void saveSessions();

    QListWidget* m_listWidget;
    QList<Session> m_sessions;
};
