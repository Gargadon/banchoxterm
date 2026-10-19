#include <algorithm>
#include <QDropEvent>
#include "sessiontags.h"
#include <QInputDialog>
#include <QJsonArray>
#include <QJsonDocument>
#include <QTreeWidgetItemIterator>
#include <functional>
#include <QTimer>
#include "sessionssidebar.h"
#include "sessiondialog.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QPushButton>
#include <QTreeWidget>
#include <QMenu>
#include <QMessageBox>
#include "localizedmessagebox.h"
#include <QLabel>
#include <QIcon>
#include <QFileDialog>
#include <QHeaderView>
#include <QDateTime>
#include <QDir>
#include <QUuid>
#include <QSet>
#include <QAbstractItemModel>
#include <QLineEdit>
#include <QCompleter>
#include <QSettings>

namespace {
constexpr int kSessionIdRole = Qt::UserRole;
constexpr int kIsGroupRole = Qt::UserRole + 1;
constexpr int kGroupPathRole = Qt::UserRole + 2;
QString itemOrderKey(const QTreeWidgetItem* item) {
    return item->data(0, kIsGroupRole).toBool() ? QStringLiteral("g:") + item->data(0, kGroupPathRole).toString()
                                                : QStringLiteral("s:") + item->data(0, kSessionIdRole).toString();
}
// Reject duplicate sibling groups rather than merging their paths on reload.
class SessionTree : public QTreeWidget {
public:
    explicit SessionTree(QWidget* parent) : QTreeWidget(parent) {
    }

protected:
    void dropEvent(QDropEvent* event) override {
        auto* target = itemAt(event->position().toPoint());
        QTreeWidgetItem* parent = nullptr;
        if (target)
            parent = dropIndicatorPosition() == OnItem ? target : target->parent();
        for (auto* moving : selectedItems()) {
            if (!moving->data(0, kIsGroupRole).toBool())
                continue;
            for (auto* ancestor = parent; ancestor; ancestor = ancestor->parent()) {
                if (ancestor == moving) {
                    event->ignore();
                    return;
                }
            }
            const int count = parent ? parent->childCount() : topLevelItemCount();
            for (int i = 0; i < count; ++i) {
                auto* sibling = parent ? parent->child(i) : topLevelItem(i);
                if (sibling != moving && sibling->data(0, kIsGroupRole).toBool() &&
                    sibling->text(0) == moving->text(0)) {
                    event->ignore();
                    return;
                }
            }
        }
        QTreeWidget::dropEvent(event);
    }
};
} // namespace

SessionsSidebar::SessionsSidebar(QWidget* parent) : QWidget(parent) {
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(10, 10, 10, 10);
    layout->setSpacing(8);

    auto* titleRow = new QHBoxLayout();
    auto* titleLabel = new QLabel(tr("SESSIONS"), this);
    titleLabel->setObjectName("sidebarSectionTitle");
    titleRow->addWidget(titleLabel);
    m_sessionSummaryLabel = new QLabel(this);
    m_sessionSummaryLabel->setObjectName("sidebarSectionMeta");
    m_sessionSummaryLabel->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    titleRow->addWidget(m_sessionSummaryLabel, 1);
    layout->addLayout(titleRow);

    auto* quickConnectRow = new QHBoxLayout();
    quickConnectRow->setSpacing(6);
    auto* quickConnectIcon = new QLabel(this);
    quickConnectIcon->setPixmap(QIcon(":/icons/server.svg").pixmap(16, 16));
    quickConnectIcon->setToolTip(tr("QuickConnect"));
    quickConnectRow->addWidget(quickConnectIcon);
    m_quickConnectEdit = new QLineEdit(this);
    m_quickConnectEdit->setPlaceholderText(tr("QuickConnect: user@host[:port] or saved session"));
    m_quickConnectEdit->setClearButtonEnabled(true);
    m_quickConnectEdit->setToolTip(tr("Press Enter to connect directly or open a saved session."));
    m_quickConnectEdit->setObjectName("quickConnectEdit");
    quickConnectRow->addWidget(m_quickConnectEdit, 1);
    layout->addLayout(quickConnectRow);

    m_filterEdit = new QLineEdit(this);
    m_filterEdit->setObjectName("sessionFilterEdit");
    m_filterEdit->setPlaceholderText(tr("Filter sessions, groups or key:value tags"));
    m_filterEdit->setClearButtonEnabled(true);
    layout->addWidget(m_filterEdit);
    connect(m_filterEdit, &QLineEdit::textChanged, this, &SessionsSidebar::applyFilter);

    auto* actionsLayout = new QHBoxLayout();
    actionsLayout->setSpacing(4);
    actionsLayout->setAlignment(Qt::AlignLeft);
    auto* newRemoteBtn = new QPushButton(QIcon(":/icons/add.svg"), QString(), this);
    newRemoteBtn->setObjectName("sessionActionButton");
    newRemoteBtn->setToolTip(tr("Create a new SSH or remote session"));
    newRemoteBtn->setAccessibleName(tr("New Remote Session"));
    actionsLayout->addWidget(newRemoteBtn);

    auto* newLocalBtn = new QPushButton(QIcon(":/icons/terminal.svg"), QString(), this);
    newLocalBtn->setObjectName("sessionActionButton");
    newLocalBtn->setToolTip(tr("Open a local terminal session"));
    newLocalBtn->setAccessibleName(tr("Local Session"));
    actionsLayout->addWidget(newLocalBtn);

    auto* importBtn = new QPushButton(QIcon(":/icons/upload.svg"), QString(), this);
    importBtn->setObjectName("sessionActionButton");
    importBtn->setToolTip(tr("Import JSON, OpenSSH, PuTTY, MobaXterm, SecureCRT or Royal TS sessions"));
    importBtn->setAccessibleName(tr("Import"));
    actionsLayout->addWidget(importBtn);

    auto* exportBtn = new QPushButton(QIcon(":/icons/download.svg"), QString(), this);
    exportBtn->setObjectName("sessionActionButton");
    exportBtn->setToolTip(tr("Export saved sessions"));
    exportBtn->setAccessibleName(tr("Export"));
    actionsLayout->addWidget(exportBtn);

    const QList<QPushButton*> sessionActionButtons = {newRemoteBtn, newLocalBtn, importBtn, exportBtn};
    for (QPushButton* button : sessionActionButtons) {
        button->setIconSize(QSize(16, 16));
        button->setFixedSize(32, 32);
    }
    layout->addLayout(actionsLayout);

    m_treeWidget = new SessionTree(this);
    m_treeWidget->setColumnCount(2);
    m_treeWidget->setHeaderLabels({tr("Session"), tr("Endpoint")});
    m_treeWidget->header()->setStretchLastSection(true);
    m_treeWidget->header()->setSectionResizeMode(0, QHeaderView::Stretch);
    m_treeWidget->header()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    m_treeWidget->setObjectName("sessionTree");
    m_treeWidget->setIconSize(QSize(18, 18));
    m_treeWidget->setIndentation(18);
    // Keep every row on the same background.  Alternating rows are especially
    // distracting with the application's dark palette on Windows.
    m_treeWidget->setAlternatingRowColors(false);
    m_treeWidget->setUniformRowHeights(true);
    m_treeWidget->setTextElideMode(Qt::ElideRight);
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
    connect(m_treeWidget->model(), &QAbstractItemModel::rowsRemoved, this, [this]() {
        if (!m_loading)
            QTimer::singleShot(0, this, [this]() {
                onSessionsReordered();
                loadSessions();
            });
    });
    connect(m_quickConnectEdit, &QLineEdit::returnPressed, this, &SessionsSidebar::onQuickConnect);

    connect(m_treeWidget, &QTreeWidget::itemCollapsed, this, [this]() { saveGroupState(); });
    connect(m_treeWidget, &QTreeWidget::itemExpanded, this, [this]() { saveGroupState(); });
    loadSessions();
}

void SessionsSidebar::onQuickConnect() {
    const QString input = m_quickConnectEdit->text().trimmed();
    if (input.isEmpty())
        return;

    for (const Session& session : m_sessions) {
        if (session.name.compare(input, Qt::CaseInsensitive) == 0 ||
            QStringLiteral("%1@%2").arg(session.user, session.host).compare(input, Qt::CaseInsensitive) == 0) {
            recordRecent(session);
            emit connectSession(session);
            return;
        }
    }

    Session session;
    session.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    session.name = input;
    session.type = SessionType::SSH;
    session.port = 22;

    QString endpoint = input;
    const int at = endpoint.lastIndexOf('@');
    if (at >= 0) {
        session.user = endpoint.left(at).trimmed();
        endpoint = endpoint.mid(at + 1).trimmed();
    }

    const int colon = endpoint.lastIndexOf(':');
    if (colon > 0) {
        bool ok = false;
        const int port = endpoint.mid(colon + 1).toInt(&ok);
        if (ok && port > 0 && port <= 65535) {
            session.port = port;
            endpoint = endpoint.left(colon);
        }
    }
    session.host = endpoint;
    if (session.host.isEmpty())
        return;

    emit connectSession(session);
    m_quickConnectEdit->clear();
}

void SessionsSidebar::refreshSessions() {
    loadSessions();
}

void SessionsSidebar::loadSessions() {
    m_loading = true;
    m_treeWidget->clear();
    m_sessions = SessionManager::loadSessions();
    if (m_sessionSummaryLabel)
        m_sessionSummaryLabel->setText(tr("%1 profiles").arg(m_sessions.size()));

    QStringList completionItems;
    for (const Session& session : m_sessions) {
        completionItems.append(session.name);
        if (!session.user.isEmpty() && !session.host.isEmpty())
            completionItems.append(QStringLiteral("%1@%2").arg(session.user, session.host));
    }
    const QSettings settings;
    completionItems.append(settings.value("sessions/recent").toStringList());
    completionItems.removeDuplicates();
    if (m_quickConnectCompleter)
        m_quickConnectCompleter->deleteLater();
    m_quickConnectCompleter = new QCompleter(completionItems, m_quickConnectEdit);
    m_quickConnectCompleter->setCaseSensitivity(Qt::CaseInsensitive);
    m_quickConnectCompleter->setFilterMode(Qt::MatchContains);
    m_quickConnectEdit->setCompleter(m_quickConnectCompleter);

    QMap<QString, QTreeWidgetItem*> groupItems;
    std::function<QTreeWidgetItem*(const QString&)> ensureGroup = [&](const QString& path) {
        if (path.isEmpty())
            return static_cast<QTreeWidgetItem*>(nullptr);
        if (groupItems.contains(path))
            return groupItems.value(path);
        const int slash = path.lastIndexOf('/');
        auto* parent = slash >= 0 ? ensureGroup(path.left(slash)) : nullptr;
        auto* item = new QTreeWidgetItem();
        item->setText(0, path.mid(slash + 1));
        item->setFirstColumnSpanned(true);
        item->setIcon(0, QIcon(":/icons/folder.svg"));
        QFont font = m_treeWidget->font();
        font.setWeight(QFont::DemiBold);
        item->setFont(0, font);
        const auto notes = settings.value("sessions/groupMetadata").toMap().value(path).toMap().value("notes").toString();
        item->setToolTip(0, tr("Session group: %1").arg(path) + (notes.isEmpty() ? QString() : '\n' + notes));
        item->setData(0, kIsGroupRole, true);
        item->setData(0, kGroupPathRole, path);
        item->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable | Qt::ItemIsDragEnabled | Qt::ItemIsDropEnabled);
        if (parent)
            parent->addChild(item);
        else
            m_treeWidget->addTopLevelItem(item);
        groupItems.insert(path, item);
        return item;
    };
    for (const QString& group : settings.value("sessions/groups").toStringList())
        ensureGroup(group);
    for (const Session& session : m_sessions) {
        QTreeWidgetItem* parent = ensureGroup(session.group);

        auto* item = new QTreeWidgetItem();
        item->setText(0, session.favorite ? QStringLiteral("★ %1").arg(session.name) : session.name);
        item->setData(0, kSessionIdRole, session.id);
        item->setData(0, kIsGroupRole, false);
        item->setFlags(item->flags() & ~Qt::ItemIsDropEnabled);

        switch (session.type) {
        case SessionType::SSH:
            item->setIcon(0, QIcon(":/icons/server.svg"));
            item->setText(1, QStringLiteral("SSH  %1@%2:%3").arg(session.user, session.host).arg(session.port));
            item->setToolTip(0, QString("%1@%2:%3").arg(session.user, session.host).arg(session.port));
            break;
        case SessionType::Telnet:
            item->setIcon(0, QIcon(":/icons/telnet.svg"));
            item->setText(1, QStringLiteral("TELNET  %1:%2").arg(session.host).arg(session.port));
            item->setToolTip(0, QString("telnet://%1:%2").arg(session.host).arg(session.port));
            break;
        case SessionType::RDP:
            item->setIcon(0, QIcon(":/icons/rdp.svg"));
            item->setText(1, QStringLiteral("RDP  %1:%2").arg(session.host).arg(session.port));
            item->setToolTip(0, QString("rdp://%1:%2").arg(session.host).arg(session.port));
            break;
        case SessionType::VNC:
            item->setIcon(0, QIcon(":/icons/vnc.svg"));
            item->setText(1, QStringLiteral("VNC  %1:%2").arg(session.host).arg(session.port));
            item->setToolTip(0, QString("vnc://%1:%2").arg(session.host).arg(session.port));
            break;
        case SessionType::Serial:
            item->setIcon(0, QIcon(":/icons/serial.svg"));
            item->setText(1, QStringLiteral("SERIAL  %1  %2 baud").arg(session.serialPort).arg(session.baudRate));
            item->setToolTip(0, tr("serial://%1 (%2 baud via %3)")
                                    .arg(session.serialPort)
                                    .arg(session.baudRate)
                                    .arg(session.serialCmd));
            break;
        case SessionType::FTP:
            item->setIcon(0, QIcon(":/icons/folder.svg"));
            item->setText(1, QStringLiteral("FTP  %1:%2").arg(session.host).arg(session.port));
            item->setToolTip(0, QString("ftp://%1:%2").arg(session.host).arg(session.port));
            break;
        default:
            item->setIcon(0, QIcon(":/icons/terminal.svg"));
            item->setText(1, tr("LOCAL TERMINAL"));
            item->setToolTip(0, session.shellPath);
            break;
        }

        if (!session.notes.isEmpty()) item->setToolTip(0, item->toolTip(0) + '\n' + session.notes);
        if (parent) {
            parent->addChild(item);
        } else {
            m_treeWidget->addTopLevelItem(item);
        }
    }

    // Restore each parent's mixed list of groups and sessions. Unknown/new
    // children retain their relative order and are appended after saved entries.
    const auto order = settings.value("sessions/treeOrder").toMap();
    std::function<void(QTreeWidgetItem*, const QString&)> restoreOrder = [&](QTreeWidgetItem* parent,
                                                                             const QString& path) {
        const auto saved = order.value(path).toStringList();
        auto children = parent->takeChildren();
        auto rank = [&](QTreeWidgetItem* child) {
            const int index = saved.indexOf(itemOrderKey(child));
            return index < 0 ? saved.size() : index;
        };
        std::stable_sort(children.begin(), children.end(), [&](auto* a, auto* b) { return rank(a) < rank(b); });
        parent->addChildren(children);
        for (auto* child : children)
            if (child->data(0, kIsGroupRole).toBool())
                restoreOrder(child, child->data(0, kGroupPathRole).toString());
    };
    restoreOrder(m_treeWidget->invisibleRootItem(), {});
    saveTreeOrder();

    const QStringList collapsed = settings.value("sessions/collapsedGroups").toStringList();
    for (auto it = groupItems.cbegin(); it != groupItems.cend(); ++it)
        it.value()->setExpanded(!collapsed.contains(it.key()));
    m_loading = false;
    applyFilter();
}

void SessionsSidebar::saveSessions() {
    SessionManager::saveSessions(m_sessions);
}

void SessionsSidebar::saveCurrentOrder() {
    onSessionsReordered();
}

QTreeWidgetItem* SessionsSidebar::findSessionItem(const QString& id) const {
    for (QTreeWidgetItemIterator it(m_treeWidget); *it; ++it)
        if (sessionIdForItem(*it) == id)
            return *it;
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
        loadSessions();
        emit connectSession(dialog.getSession());
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

    auto result = localizedQuestion(this, tr("Delete Session"), tr("Are you sure you want to delete this session?"),
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
    QString path = QFileDialog::getOpenFileName(
        this, tr("Import Sessions"), QDir::homePath(),
        tr("Session files (*.json *.bancho.enc *.conf *.reg *.mxtsessions *.ini *.rtsx *.rts);;JSON (*.json);;"
           "Encrypted sessions (*.bancho.enc);;OpenSSH config (*.conf);;"
           "PuTTY registry (*.reg);;MobaXterm (*.mxtsessions);;"
           "SecureCRT (*.ini);;Royal TS (*.rtsx *.rts);;All Files (*)"));
    if (path.isEmpty())
        return;

    bool ok = false;
    QStringList importedMacroNames;
    QStringList importedMacroTexts;
    QList<Session> imported = SessionManager::importSessions(path, &ok, &importedMacroNames, &importedMacroTexts);
    if (!ok) {
        QMessageBox::critical(this, tr("Import Failed"), tr("Could not read sessions from the selected file."));
        return;
    }

    QSet<QString> knownIds;
    QSet<QString> knownNames;
    for (const Session& existing : m_sessions) {
        knownIds.insert(existing.id);
        knownNames.insert(existing.name.trimmed().toCaseFolded());
    }

    int conflicts = 0;
    for (const Session& session : imported) {
        if (knownIds.contains(session.id) || knownNames.contains(session.name.trimmed().toCaseFolded()))
            ++conflicts;
    }

    enum class ConflictPolicy { Rename, Skip, Cancel };
    ConflictPolicy policy = ConflictPolicy::Rename;
    if (conflicts > 0) {
        QMessageBox conflictBox(QMessageBox::Warning, tr("Import Conflicts"),
                                tr("%1 imported session(s) have an ID or name already in use.").arg(conflicts),
                                QMessageBox::NoButton, this);
        auto* renameButton = conflictBox.addButton(tr("Rename and import"), QMessageBox::AcceptRole);
        auto* skipButton = conflictBox.addButton(tr("Skip conflicts"), QMessageBox::DestructiveRole);
        auto* cancelButton = conflictBox.addButton(QMessageBox::Cancel);
        conflictBox.exec();
        if (conflictBox.clickedButton() == skipButton)
            policy = ConflictPolicy::Skip;
        else if (conflictBox.clickedButton() == cancelButton)
            policy = ConflictPolicy::Cancel;
        else if (conflictBox.clickedButton() != renameButton)
            policy = ConflictPolicy::Cancel;
    }
    if (policy == ConflictPolicy::Cancel)
        return;

    int added = 0;
    int skipped = 0;
    for (Session session : imported) {
        const bool idConflict = knownIds.contains(session.id);
        const bool nameConflict = knownNames.contains(session.name.trimmed().toCaseFolded());
        if (idConflict || nameConflict) {
            if (policy == ConflictPolicy::Skip) {
                ++skipped;
                continue;
            }

            if (idConflict)
                session.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
            const QString originalName = session.name.trimmed().isEmpty() ? tr("Imported Session") : session.name;
            QString candidate = originalName + tr(" (imported)");
            int suffix = 2;
            while (knownNames.contains(candidate.toCaseFolded()))
                candidate = originalName + tr(" (imported %1)").arg(suffix++);
            session.name = candidate;
        }

        // Also protect against duplicate IDs/names within the imported file.
        if (knownIds.contains(session.id))
            session.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
        if (knownNames.contains(session.name.trimmed().toCaseFolded())) {
            ++skipped;
            continue;
        }
        knownIds.insert(session.id);
        knownNames.insert(session.name.trimmed().toCaseFolded());
        m_sessions.append(session);
        ++added;
    }

    saveSessions();
    if (!importedMacroNames.isEmpty()) {
        QSettings settings;
        QStringList macroNames = settings.value("macros/names").toStringList();
        QStringList macroTexts = settings.value("macros/texts").toStringList();
        for (int i = 0; i < importedMacroNames.size() && i < importedMacroTexts.size(); ++i) {
            const int existing = macroNames.indexOf(importedMacroNames.at(i));
            if (existing >= 0)
                macroTexts[existing] = importedMacroTexts.at(i);
            else {
                macroNames.append(importedMacroNames.at(i));
                macroTexts.append(importedMacroTexts.at(i));
            }
        }
        settings.setValue("macros/names", macroNames);
        settings.setValue("macros/texts", macroTexts);
        emit macrosImported();
    }
    loadSessions();
    QMessageBox::information(this, tr("Import Complete"),
                             tr("Imported %1 session(s), skipped %2 and renamed %3 conflicting session(s).")
                                 .arg(added)
                                 .arg(skipped)
                                 .arg(conflicts > 0 && policy == ConflictPolicy::Rename ? conflicts : 0));
}

void SessionsSidebar::onExportSessions() {
    QString defaultName =
        QString("banchoxterm_sessions_%1.json").arg(QDateTime::currentDateTime().toString("yyyyMMdd_HHmmss"));
    QString path = QFileDialog::getSaveFileName(this, tr("Export Sessions"), QDir::homePath() + "/" + defaultName,
                                                tr("BanchoXterm Sessions (*.json);;Encrypted sessions (*.bancho.enc);;"
                                                   "OpenSSH config (*.conf)"));
    if (path.isEmpty())
        return;

    const bool openSsh = path.endsWith(QStringLiteral(".conf"), Qt::CaseInsensitive);
    const bool encrypted = path.endsWith(QStringLiteral(".bancho.enc"), Qt::CaseInsensitive);
    const bool exported = openSsh     ? SessionManager::exportOpenSshConfig(m_sessions, path)
                          : encrypted ? SessionManager::exportEncryptedSessions(m_sessions, path)
                                      : SessionManager::exportSessions(m_sessions, path);
    if (exported) {
        QMessageBox::information(this, tr("Export Complete"), tr("Exported %1 session(s).").arg(m_sessions.size()));
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
            recordRecent(session);
            emit connectSession(session);
            break;
        }
    }
}

void SessionsSidebar::showContextMenu(const QPoint& pos) {
    auto* item = m_treeWidget->itemAt(pos);
    m_treeWidget->setCurrentItem(item);
    QString id = sessionIdForItem(item);
    if (id.isEmpty()) {
        manageGroup(item, pos);
        return;
    }

    QMenu menu(this);
    auto* connectAction = menu.addAction(QIcon(":/icons/terminal.svg"), tr("Connect"));
    menu.addSeparator();
    auto* editAction = menu.addAction(QIcon(":/icons/edit.svg"), tr("Edit"));
    auto* duplicateAction = menu.addAction(QIcon(":/icons/terminal.svg"), tr("Duplicate"));
    auto* favoriteAction =
        menu.addAction(item->text(0).startsWith(QStringLiteral("★ ")) ? tr("Remove Favorite") : tr("Add Favorite"));
    auto* deleteAction = menu.addAction(QIcon(":/icons/delete.svg"), tr("Delete"));

    auto* selectedAction = menu.exec(m_treeWidget->mapToGlobal(pos));
    if (selectedAction == connectAction) {
        onItemDoubleClicked(item, 0);
    } else if (selectedAction == editAction) {
        onEditSession();
    } else if (selectedAction == duplicateAction) {
        onDuplicateSession();
    } else if (selectedAction == favoriteAction) {
        onToggleFavorite();
    } else if (selectedAction == deleteAction) {
        onDeleteSession();
    }
}

void SessionsSidebar::onToggleFavorite() {
    const QString id = sessionIdForItem(m_treeWidget->currentItem());
    if (id.isEmpty())
        return;
    for (Session& session : m_sessions) {
        if (session.id == id) {
            session.favorite = !session.favorite;
            saveSessions();
            loadSessions();
            return;
        }
    }
}

void SessionsSidebar::recordRecent(const Session& session) {
    QSettings settings;
    QStringList recent = settings.value("sessions/recent").toStringList();
    recent.removeAll(session.name);
    recent.prepend(session.name);
    while (recent.size() > 12)
        recent.removeLast();
    settings.setValue("sessions/recent", recent);
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
    if (m_loading || !m_filterEdit->text().isEmpty())
        return;
    QList<Session> reordered;
    QSet<QString> seen;
    QStringList groups;
    std::function<void(QTreeWidgetItem*, const QString&)> visit = [&](QTreeWidgetItem* item, const QString& parent) {
        if (item->data(0, kIsGroupRole).toBool()) {
            const QString path = parent.isEmpty() ? item->text(0) : parent + '/' + item->text(0);
            const QString oldPath = item->data(0, kGroupPathRole).toString();
            QSettings settings;
            QVariantMap metadata = settings.value("sessions/groupMetadata").toMap();
            if (oldPath != path && metadata.contains(oldPath)) {
                metadata.insert(path, metadata.take(oldPath));
                settings.setValue("sessions/groupMetadata", metadata);
            }
            item->setData(0, kGroupPathRole, path);
            groups.append(path);
            for (int i = 0; i < item->childCount(); ++i)
                visit(item->child(i), path);
        } else {
            const QString id = sessionIdForItem(item);
            for (Session session : m_sessions) {
                if (session.id == id && !seen.contains(id)) {
                    session.group = parent;
                    reordered.append(session);
                    seen.insert(id);
                    break;
                }
            }
            // Older trees may contain sessions dropped onto other sessions.
            for (int i = 0; i < item->childCount(); ++i)
                visit(item->child(i), parent);
        }
    };
    for (int i = 0; i < m_treeWidget->topLevelItemCount(); ++i)
        visit(m_treeWidget->topLevelItem(i), {});
    for (const Session& session : m_sessions)
        if (!seen.contains(session.id))
            reordered.append(session);
    m_sessions = reordered;
    QSettings().setValue("sessions/groups", groups);
    saveTreeOrder();
    saveGroupState();
    saveSessions();
}

void SessionsSidebar::saveTreeOrder() {
    QVariantMap order;
    std::function<void(QTreeWidgetItem*, const QString&)> visit = [&](QTreeWidgetItem* parent, const QString& path) {
        QStringList children;
        for (int i = 0; i < parent->childCount(); ++i) {
            auto* child = parent->child(i);
            children.append(itemOrderKey(child));
            if (child->data(0, kIsGroupRole).toBool())
                visit(child, child->data(0, kGroupPathRole).toString());
        }
        order.insert(path, children);
    };
    visit(m_treeWidget->invisibleRootItem(), {});
    QSettings().setValue("sessions/treeOrder", order);
}

void SessionsSidebar::saveGroupState() {
    if (m_loading || !m_filterEdit->text().isEmpty())
        return;
    QStringList collapsed;
    for (QTreeWidgetItemIterator it(m_treeWidget); *it; ++it)
        if ((*it)->data(0, kIsGroupRole).toBool() && !(*it)->isExpanded())
            collapsed.append((*it)->data(0, kGroupPathRole).toString());
    QSettings().setValue("sessions/collapsedGroups", collapsed);
}

void SessionsSidebar::applyFilter() {
    const QString query = m_filterEdit->text().trimmed();
    const QStringList terms = query.split(' ', Qt::SkipEmptyParts);
    m_treeWidget->setDragEnabled(query.isEmpty());
    m_treeWidget->setAcceptDrops(query.isEmpty());
    const QVariantMap metadata = QSettings().value("sessions/groupMetadata").toMap();
    auto matches = [&](QString searchable, const QJsonObject& tags) {
        for (auto it = tags.begin(); it != tags.end(); ++it) {
            searchable += ' ' + it.key();
            for (const auto& value : it.value().toArray())
                searchable += ' ' + it.key() + ':' + value.toString();
        }
        for (const QString& term : terms)
            if (!searchable.contains(term, Qt::CaseInsensitive))
                return false;
        return true;
    };
    std::function<bool(QTreeWidgetItem*)> visit = [&](QTreeWidgetItem* item) {
        bool visible = false;
        if (item->data(0, kIsGroupRole).toBool()) {
            const QString path = item->data(0, kGroupPathRole).toString();
            const auto data = metadata.value(path).toMap();
            visible = matches(path + ' ' + data.value("notes").toString(),
                              QJsonDocument::fromJson(data.value("tags").toByteArray()).object());
            for (int i = 0; i < item->childCount(); ++i)
                visible = visit(item->child(i)) || visible;
            if (!query.isEmpty() && visible)
                item->setExpanded(true);
        } else {
            for (const Session& session : m_sessions) {
                if (session.id != sessionIdForItem(item))
                    continue;
                visible = matches(session.name + ' ' + session.host + ' ' + session.user + ' ' + session.group + ' ' +
                                      session.notes,
                                  session.tags);
                break;
            }
        }
        item->setHidden(!visible);
        return visible;
    };
    m_loading = true;
    for (int i = 0; i < m_treeWidget->topLevelItemCount(); ++i)
        visit(m_treeWidget->topLevelItem(i));
    if (query.isEmpty()) {
        const auto collapsed = QSettings().value("sessions/collapsedGroups").toStringList();
        for (QTreeWidgetItemIterator it(m_treeWidget); *it; ++it)
            if ((*it)->data(0, kIsGroupRole).toBool())
                (*it)->setExpanded(!collapsed.contains((*it)->data(0, kGroupPathRole).toString()));
    }
    m_loading = false;
}

void SessionsSidebar::manageGroup(QTreeWidgetItem* item, const QPoint& pos) {
    QMenu menu(this);
    auto* create = menu.addAction(tr("Create group"));
    QAction* rename = nullptr;
    QAction* remove = nullptr;
    QAction* notes = nullptr;
    QAction* tags = nullptr;
    const QString path = item ? item->data(0, kGroupPathRole).toString() : QString();
    if (item) {
        rename = menu.addAction(tr("Rename group"));
        notes = menu.addAction(tr("Edit group notes"));
        tags = menu.addAction(tr("Edit group tags"));
        remove = menu.addAction(tr("Delete group and its sessions"));
    }
    const auto selected = menu.exec(m_treeWidget->mapToGlobal(pos));
    if (!selected)
        return;
    QSettings settings;
    QStringList groups = settings.value("sessions/groups").toStringList();
    for (QTreeWidgetItemIterator it(m_treeWidget); *it; ++it)
        if ((*it)->data(0, kIsGroupRole).toBool())
            groups.append((*it)->data(0, kGroupPathRole).toString());
    groups.removeDuplicates();
    QVariantMap metadata = settings.value("sessions/groupMetadata").toMap();
    auto data = metadata.value(path).toMap();
    bool ok = false;
    if (selected == create || selected == rename) {
        const QString name =
            QInputDialog::getText(this, selected->text(), tr("Group name (use / for subgroups):"), QLineEdit::Normal,
                                  selected == rename ? path
                                  : path.isEmpty()   ? QString()
                                                     : path + '/',
                                  &ok)
                .trimmed();
        if (!ok || name.isEmpty() || name.startsWith('/') || name.endsWith('/') || name.contains("//") ||
            groups.contains(name))
            return;
        if (selected == create)
            groups.append(name);
        else {
            const auto replacePath = [&](const QString& value) {
                return value == path || value.startsWith(path + '/') ? name + value.mid(path.size()) : value;
            };
            if (name.startsWith(path + '/'))
                return;
            for (const QString& group : groups) {
                const QString renamed = replacePath(group);
                if (renamed != group && groups.contains(renamed))
                    return;
            }
            for (Session& session : m_sessions)
                session.group = replacePath(session.group);
            for (QString& group : groups)
                group = replacePath(group);
            QVariantMap changed;
            for (auto it = metadata.begin(); it != metadata.end(); ++it)
                changed.insert(replacePath(it.key()), it.value());
            metadata = changed;
            QStringList collapsed = settings.value("sessions/collapsedGroups").toStringList();
            for (QString& group : collapsed)
                group = replacePath(group);
            settings.setValue("sessions/collapsedGroups", collapsed);
            const auto order = settings.value("sessions/treeOrder").toMap();
            QVariantMap renamedOrder;
            for (auto it = order.begin(); it != order.end(); ++it) {
                auto children = it.value().toStringList();
                for (QString& child : children)
                    if (child.startsWith("g:"))
                        child = QStringLiteral("g:") + replacePath(child.mid(2));
                renamedOrder.insert(replacePath(it.key()), children);
            }
            settings.setValue("sessions/treeOrder", renamedOrder);
        }
    } else if (selected == remove) {
        if (localizedQuestion(this, tr("Delete group"),
                              tr("Delete this group, its subgroups and all their saved sessions?"),
                              QMessageBox::Yes | QMessageBox::No) != QMessageBox::Yes)
            return;
        const auto belongs = [&](const QString& group) { return group == path || group.startsWith(path + '/'); };
        for (int i = m_sessions.size() - 1; i >= 0; --i)
            if (belongs(m_sessions[i].group))
                m_sessions.removeAt(i);
        for (int i = groups.size() - 1; i >= 0; --i)
            if (belongs(groups[i])) {
                metadata.remove(groups[i]);
                groups.removeAt(i);
            }
    } else if (selected == notes) {
        const QString text =
            QInputDialog::getMultiLineText(this, tr("Group notes"), tr("Notes:"), data.value("notes").toString(), &ok);
        if (!ok)
            return;
        data.insert("notes", text);
        metadata.insert(path, data);
    } else if (selected == tags) {
        const QString text = QInputDialog::getMultiLineText(
            this, tr("Group tags"), tr("One key:value per line (repeat keys for multiple values):"),
            formatSessionTags(QJsonDocument::fromJson(data.value("tags").toByteArray()).object()), &ok);
        if (!ok)
            return;
        bool valid = false;
        const auto tags = parseSessionTags(text, &valid);
        if (!valid) {
            QMessageBox::warning(this, tr("Invalid tags"), tr("Enter one key:value pair per line."));
            return;
        }
        data.insert("tags", QJsonDocument(tags).toJson(QJsonDocument::Compact));
        metadata.insert(path, data);
    }
    settings.setValue("sessions/groups", groups);
    settings.setValue("sessions/groupMetadata", metadata);
    saveSessions();
    loadSessions();
}
