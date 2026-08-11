#include "sessionssidebar.h"
#include "sessiondialog.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QPushButton>
#include <QListWidget>
#include <QMenu>
#include <QMessageBox>
#include <QLabel>
#include <QIcon>

SessionsSidebar::SessionsSidebar(QWidget* parent) : QWidget(parent) {
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(10, 10, 10, 10);
    layout->setSpacing(8);

    auto* titleLabel = new QLabel(tr("SESSIONS"), this);
    titleLabel->setStyleSheet(
        "font-weight: bold; color: #787c99; font-size: 11px; letter-spacing: 1px; margin-bottom: 4px;");
    layout->addWidget(titleLabel);

    auto* actionsLayout = new QHBoxLayout();
    auto* newRemoteBtn = new QPushButton(QIcon(":/icons/add.svg"), tr("New Remote Session"), this);
    newRemoteBtn->setObjectName("primaryButton");
    actionsLayout->addWidget(newRemoteBtn);

    auto* newLocalBtn = new QPushButton(QIcon(":/icons/terminal.svg"), tr("Local Session"), this);
    actionsLayout->addWidget(newLocalBtn);

    layout->addLayout(actionsLayout);

    m_listWidget = new QListWidget(this);
    m_listWidget->setContextMenuPolicy(Qt::CustomContextMenu);
    layout->addWidget(m_listWidget);

    connect(newRemoteBtn, &QPushButton::clicked, this, &SessionsSidebar::onNewSession);
    connect(newLocalBtn, &QPushButton::clicked, this, &SessionsSidebar::newLocalSessionRequested);
    connect(m_listWidget, &QListWidget::itemDoubleClicked, this, &SessionsSidebar::onItemDoubleClicked);
    connect(m_listWidget, &QListWidget::customContextMenuRequested, this, &SessionsSidebar::showContextMenu);

    loadSessions();
}

void SessionsSidebar::loadSessions() {
    m_listWidget->clear();
    m_sessions = SessionManager::loadSessions();

    for (const Session& session : m_sessions) {
        auto* item = new QListWidgetItem(m_listWidget);
        item->setText(session.name);
        item->setData(Qt::UserRole, session.id);

        if (session.type == SessionType::SSH) {
            item->setIcon(QIcon(":/icons/server.svg"));
            item->setToolTip(QString("%1@%2:%3").arg(session.user, session.host).arg(session.port));
        } else if (session.type == SessionType::Telnet) {
            item->setIcon(QIcon(":/icons/telnet.svg"));
            item->setToolTip(QString("telnet://%1:%2").arg(session.host).arg(session.port));
        } else if (session.type == SessionType::Serial) {
            item->setIcon(QIcon(":/icons/serial.svg"));
            item->setToolTip(QString("serial://%1 (%2 baud via %3)")
                                 .arg(session.serialPort)
                                 .arg(session.baudRate)
                                 .arg(session.serialCmd));
        } else {
            item->setIcon(QIcon(":/icons/terminal.svg"));
            item->setToolTip(session.shellPath);
        }
        m_listWidget->addItem(item);
    }
}

void SessionsSidebar::saveSessions() {
    SessionManager::saveSessions(m_sessions);
}

void SessionsSidebar::onNewSession() {
    SessionDialog dialog(this);
    if (dialog.exec() == QDialog::Accepted) {
        Session newSession = dialog.getSession();
        m_sessions.append(newSession);
        saveSessions();
        loadSessions();
    }
}

void SessionsSidebar::onEditSession() {
    auto* item = m_listWidget->currentItem();
    if (!item)
        return;

    QString id = item->data(Qt::UserRole).toString();
    for (int i = 0; i < m_sessions.size(); ++i) {
        if (m_sessions[i].id == id) {
            SessionDialog dialog(m_sessions[i], this);
            if (dialog.exec() == QDialog::Accepted) {
                m_sessions[i] = dialog.getSession();
                saveSessions();
                loadSessions();
            }
            break;
        }
    }
}

void SessionsSidebar::onDeleteSession() {
    auto* item = m_listWidget->currentItem();
    if (!item)
        return;

    QString id = item->data(Qt::UserRole).toString();
    auto result = QMessageBox::question(this, tr("Delete Session"), tr("Are you sure you want to delete this session?"),
                                        QMessageBox::Yes | QMessageBox::No);
    if (result == QMessageBox::Yes) {
        for (int i = 0; i < m_sessions.size(); ++i) {
            if (m_sessions[i].id == id) {
                m_sessions.removeAt(i);
                saveSessions();
                loadSessions();
                break;
            }
        }
    }
}

void SessionsSidebar::onItemDoubleClicked(QListWidgetItem* item) {
    if (!item)
        return;
    QString id = item->data(Qt::UserRole).toString();
    for (const Session& session : m_sessions) {
        if (session.id == id) {
            emit connectSession(session);
            break;
        }
    }
}

void SessionsSidebar::showContextMenu(const QPoint& pos) {
    auto* item = m_listWidget->itemAt(pos);
    if (!item)
        return;

    QMenu menu(this);
    auto* connectAction = menu.addAction(QIcon(":/icons/terminal.svg"), tr("Connect"));
    menu.addSeparator();
    auto* editAction = menu.addAction(QIcon(":/icons/edit.svg"), tr("Edit"));
    auto* deleteAction = menu.addAction(QIcon(":/icons/delete.svg"), tr("Delete"));

    auto* selectedAction = menu.exec(m_listWidget->mapToGlobal(pos));
    if (selectedAction == connectAction) {
        onItemDoubleClicked(item);
    } else if (selectedAction == editAction) {
        onEditSession();
    } else if (selectedAction == deleteAction) {
        onDeleteSession();
    }
}
