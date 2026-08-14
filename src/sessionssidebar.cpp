#include "sessionssidebar.h"
#include "sessiondialog.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QPushButton>
#include <QTreeWidget>
#include <QMenu>
#include <QMessageBox>
#include <QLabel>
#include <QIcon>
#include <QFileDialog>
#include <QHeaderView>
#include <QDateTime>
#include <QDir>
#include <QUuid>
#include <QSet>
#include <QAbstractItemModel>

namespace {
constexpr int kSessionIdRole = Qt::UserRole;
constexpr int kIsGroupRole = Qt::UserRole + 1;
} // namespace

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

    auto* ioLayout = new QHBoxLayout();
    auto* importBtn = new QPushButton(QIcon(":/icons/upload.svg"), tr("Import"), this);
    auto* exportBtn = new QPushButton(QIcon(":/icons/download.svg"), tr("Export"), this);
    ioLayout->addWidget(importBtn);
    ioLayout->addWidget(exportBtn);
    layout->addLayout(ioLayout);

    m_treeWidget = new QTreeWidget(this);
    m_treeWidget->setHeaderHidden(true);
    m_treeWidget->setRootIsDecorated(true);
    m_treeWidget->setContextMenuPolicy(Qt::CustomContextMenu);
    m_treeWidget->setDragEnabled(true);
    m_treeWidget->setAcceptDrops(true);
    m_treeWidget->setDropIndicatorShown(true);
    m_treeWidget->setDragDropMode(QAbstractItemView::InternalMove);
    layout->addWidget(m_treeWidget);

    connect(newRemoteBtn, &QPushButton::clicked, this, &SessionsSidebar::onNewSession);
    connect(newLocalBtn, &QPushButton::clicked, this, &SessionsSidebar::newLocalSessionRequested);
    connect(importBtn, &QPushButton::clicked, this, &SessionsSidebar::onImportSessions);
    connect(exportBtn, &QPushButton::clicked, this, &SessionsSidebar::onExportSessions);
    connect(m_treeWidget, &QTreeWidget::itemDoubleClicked, this, &SessionsSidebar::onItemDoubleClicked);
    connect(m_treeWidget, &QTreeWidget::customContextMenuRequested, this, &SessionsSidebar::showContextMenu);
    connect(m_treeWidget->model(), &QAbstractItemModel::rowsMoved, this,
            [this](const QModelIndex&, int, int, const QModelIndex&, int) { onSessionsReordered(); });

    loadSessions();
}

void SessionsSidebar::loadSessions() {
    m_treeWidget->clear();
    m_sessions = SessionManager::loadSessions();

    // Map group -> tree item, preserving first-seen order.
    QMap<QString, QTreeWidgetItem*> groupItems;

    for (const Session& session : m_sessions) {
        QTreeWidgetItem* parent = nullptr;
        if (!session.group.isEmpty()) {
            auto it = groupItems.find(session.group);
            if (it == groupItems.end()) {
                auto* groupItem = new QTreeWidgetItem();
                groupItem->setText(0, session.group);
                groupItem->setIcon(0, QIcon(":/icons/folder.svg"));
                groupItem->setData(0, kIsGroupRole, true);
                groupItem->setData(0, kSessionIdRole, QString());
                groupItem->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable | Qt::ItemIsDropEnabled);
                m_treeWidget->addTopLevelItem(groupItem);
                groupItems.insert(session.group, groupItem);
                parent = groupItem;
            } else {
                parent = it.value();
            }
        }

        auto* item = new QTreeWidgetItem();
        item->setText(0, session.name);
        item->setData(0, kSessionIdRole, session.id);
        item->setData(0, kIsGroupRole, false);

        switch (session.type) {
        case SessionType::SSH:
            item->setIcon(0, QIcon(":/icons/server.svg"));
            item->setToolTip(0, QString("%1@%2:%3").arg(session.user, session.host).arg(session.port));
            break;
        case SessionType::Telnet:
            item->setIcon(0, QIcon(":/icons/telnet.svg"));
            item->setToolTip(0, QString("telnet://%1:%2").arg(session.host).arg(session.port));
            break;
        case SessionType::RDP:
            item->setIcon(0, QIcon(":/icons/rdp.svg"));
            item->setToolTip(0, QString("rdp://%1:%2").arg(session.host).arg(session.port));
            break;
        case SessionType::VNC:
            item->setIcon(0, QIcon(":/icons/vnc.svg"));
            item->setToolTip(0, QString("vnc://%1:%2").arg(session.host).arg(session.port));
            break;
        case SessionType::Serial:
            item->setIcon(0, QIcon(":/icons/serial.svg"));
            item->setToolTip(0, tr("serial://%1 (%2 baud via %3)")
                                   .arg(session.serialPort)
                                   .arg(session.baudRate)
                                   .arg(session.serialCmd));
            break;
        case SessionType::FTP:
            item->setIcon(0, QIcon(":/icons/folder.svg"));
            item->setToolTip(0, QString("ftp://%1:%2").arg(session.host).arg(session.port));
            break;
        default:
            item->setIcon(0, QIcon(":/icons/terminal.svg"));
            item->setToolTip(0, session.shellPath);
            break;
        }

        if (parent) {
            parent->addChild(item);
        } else {
            m_treeWidget->addTopLevelItem(item);
        }
    }

    m_treeWidget->expandAll();
}

void SessionsSidebar::saveSessions() {
    SessionManager::saveSessions(m_sessions);
}

QTreeWidgetItem* SessionsSidebar::findSessionItem(const QString& id) const {
    const int top = m_treeWidget->topLevelItemCount();
    for (int i = 0; i < top; ++i) {
        QTreeWidgetItem* item = m_treeWidget->topLevelItem(i);
        if (!item->data(0, kIsGroupRole).toBool() && item->data(0, kSessionIdRole).toString() == id)
            return item;
        const int childCount = item->childCount();
        for (int j = 0; j < childCount; ++j) {
            QTreeWidgetItem* child = item->child(j);
            if (child->data(0, kSessionIdRole).toString() == id)
                return child;
        }
    }
    return nullptr;
}

QString SessionsSidebar::sessionIdForItem(QTreeWidgetItem* item) const {
    if (!item || item->data(0, kIsGroupRole).toBool())
        return QString();
    return item->data(0, kSessionIdRole).toString();
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
    auto* item = m_treeWidget->currentItem();
    QString id = sessionIdForItem(item);
    if (id.isEmpty())
        return;

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
    auto* item = m_treeWidget->currentItem();
    QString id = sessionIdForItem(item);
    if (id.isEmpty())
        return;

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

void SessionsSidebar::onImportSessions() {
    QString path = QFileDialog::getOpenFileName(this, tr("Import Sessions"), QDir::homePath(),
                                                tr("BanchoXterm Sessions (*.json);;All Files (*)"));
    if (path.isEmpty())
        return;

    bool ok = false;
    QList<Session> imported = SessionManager::importSessions(path, &ok);
    if (!ok) {
        QMessageBox::critical(this, tr("Import Failed"), tr("Could not read sessions from the selected file."));
        return;
    }

    int added = 0;
    for (const Session& s : imported) {
        bool exists = false;
        for (const Session& existing : m_sessions) {
            if (existing.id == s.id) {
                exists = true;
                break;
            }
        }
        if (!exists) {
            m_sessions.append(s);
            ++added;
        }
    }

    saveSessions();
    loadSessions();
    QMessageBox::information(this, tr("Import Complete"),
                             tr("Imported %1 session(s) (%2 skipped as duplicates).")
                                 .arg(added)
                                 .arg(imported.size() - added));
}

void SessionsSidebar::onExportSessions() {
    QString defaultName = QString("banchoxterm_sessions_%1.json")
                              .arg(QDateTime::currentDateTime().toString("yyyyMMdd_HHmmss"));
    QString path = QFileDialog::getSaveFileName(this, tr("Export Sessions"), QDir::homePath() + "/" + defaultName,
                                                tr("BanchoXterm Sessions (*.json)"));
    if (path.isEmpty())
        return;

    if (SessionManager::exportSessions(m_sessions, path)) {
        QMessageBox::information(this, tr("Export Complete"),
                                 tr("Exported %1 session(s).").arg(m_sessions.size()));
    } else {
        QMessageBox::critical(this, tr("Export Failed"), tr("Could not write sessions to the selected file."));
    }
}

void SessionsSidebar::onItemDoubleClicked(QTreeWidgetItem* item, int column) {
    Q_UNUSED(column);
    QString id = sessionIdForItem(item);
    if (id.isEmpty())
        return;

    for (const Session& session : m_sessions) {
        if (session.id == id) {
            emit connectSession(session);
            break;
        }
    }
}

void SessionsSidebar::showContextMenu(const QPoint& pos) {
    auto* item = m_treeWidget->itemAt(pos);
    QString id = sessionIdForItem(item);
    if (id.isEmpty())
        return;

    QMenu menu(this);
    auto* connectAction = menu.addAction(QIcon(":/icons/terminal.svg"), tr("Connect"));
    menu.addSeparator();
    auto* editAction = menu.addAction(QIcon(":/icons/edit.svg"), tr("Edit"));
    auto* duplicateAction = menu.addAction(QIcon(":/icons/terminal.svg"), tr("Duplicate"));
    auto* deleteAction = menu.addAction(QIcon(":/icons/delete.svg"), tr("Delete"));

    auto* selectedAction = menu.exec(m_treeWidget->mapToGlobal(pos));
    if (selectedAction == connectAction) {
        onItemDoubleClicked(item, 0);
    } else if (selectedAction == editAction) {
        onEditSession();
    } else if (selectedAction == duplicateAction) {
        onDuplicateSession();
    } else if (selectedAction == deleteAction) {
        onDeleteSession();
    }
}

void SessionsSidebar::onDuplicateSession() {
    auto* item = m_treeWidget->currentItem();
    const QString id = sessionIdForItem(item);
    if (id.isEmpty())
        return;

    for (const Session& s : m_sessions) {
        if (s.id == id) {
            Session copy = s;
            copy.id = QUuid::createUuid().toString();
            copy.name = s.name + tr(" (copy)");
            m_sessions.append(copy);
            saveSessions();
            loadSessions();
            break;
        }
    }
}

void SessionsSidebar::onSessionsReordered() {
    QList<Session> reordered;
    QSet<QString> seen;

    auto appendById = [&](const QString& id) {
        for (const Session& s : m_sessions) {
            if (s.id == id) {
                Session copy = s;
                copy.group.clear();
                reordered.append(copy);
                seen.insert(id);
                break;
            }
        }
    };

    const int top = m_treeWidget->topLevelItemCount();
    for (int i = 0; i < top; ++i) {
        QTreeWidgetItem* item = m_treeWidget->topLevelItem(i);
        if (item->data(0, kIsGroupRole).toBool()) {
            const QString group = item->text(0);
            for (int j = 0; j < item->childCount(); ++j) {
                const QString id = item->child(j)->data(0, kSessionIdRole).toString();
                for (const Session& s : m_sessions) {
                    if (s.id == id) {
                        Session copy = s;
                        copy.group = group;
                        reordered.append(copy);
                        seen.insert(id);
                        break;
                    }
                }
            }
        } else {
            appendById(item->data(0, kSessionIdRole).toString());
        }
    }

    // Preserve any session that is not visible in the tree (safety net).
    for (const Session& s : m_sessions) {
        if (!seen.contains(s.id))
            reordered.append(s);
    }

    m_sessions = reordered;
    saveSessions();
}
