#include "sftpsidebar.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGridLayout>
#include <QLineEdit>
#include <QPushButton>
#include <QTreeWidget>
#include <QTreeWidgetItem>
#include <QSplitter>
#include <QLabel>
#include <QPainter>
#include <QStyle>
#include <QProgressBar>
#include <QFileDialog>
#include <QInputDialog>
#include <QMessageBox>
#include "localizedmessagebox.h"
#include <QMenu>
#include <QHeaderView>
#include <QIcon>
#include <QFont>
#include <QDir>
#include <QFileInfo>
#include <QDesktopServices>
#include <QUrl>
#include <QProcess>
#include <QSettings>
#include <QDrag>
#include <QMimeData>
#include <QJsonDocument>
#include <QJsonArray>
#include <QApplication>
#include <QDialog>
#include <QDialogButtonBox>
#include <QListWidget>
#include <QCheckBox>
#include <QGroupBox>
#include <QThread>
#include <QTimer>
#include <QScrollBar>
#include <QUuid>
#include <QMap>
#include <QEvent>
#include <QDropEvent>
#include <utility>
#include "keyring.h"
#include "remoteeditordialog.h"
#include "ftpclient.h"

// Icons are cached as statics: constructing a QIcon from an :/ resource
// re-decodes the SVG every time, which is noticeable when populating a
// directory listing with thousands of entries.
static const QIcon& folderIcon() {
    static const QIcon icon(QStringLiteral(":/icons/folder.svg"));
    return icon;
}
static const QIcon& fileIcon() {
    static const QIcon icon(QStringLiteral(":/icons/file.svg"));
    return icon;
}
static const QIcon& editIcon() {
    static const QIcon icon(QStringLiteral(":/icons/edit.svg"));
    return icon;
}
static const QIcon& downloadIcon() {
    static const QIcon icon(QStringLiteral(":/icons/download.svg"));
    return icon;
}
static const QIcon& uploadIcon() {
    static const QIcon icon(QStringLiteral(":/icons/upload.svg"));
    return icon;
}
static const QIcon& deleteIcon() {
    static const QIcon icon(QStringLiteral(":/icons/delete.svg"));
    return icon;
}
static const QIcon& refreshIcon() {
    static const QIcon icon(QStringLiteral(":/icons/refresh.svg"));
    return icon;
}

// Text must never contribute a filename-sized minimum width to the sidebar.
// Keep byte counts on their own line so eliding the name cannot hide them.
class SftpProgressLabel : public QLabel {
public:
    explicit SftpProgressLabel(QWidget* parent) : QLabel(parent) {
        setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
        setTextFormat(Qt::PlainText);
        setMinimumWidth(0);
    }
    void setText(const QString& value) {
        m_amount.clear();
        QLabel::setText(value);
        setToolTip(value);
        updateGeometry();
    }
    void setProgress(const QString& name, const QString& amount) {
        m_amount = amount;
        QLabel::setText(name + '\n' + amount);
        setToolTip(name + '\n' + amount);
        updateGeometry();
    }
    QSize sizeHint() const override {
        return QSize(0, fontMetrics().height() * (m_amount.isEmpty() ? 1 : 2) + 6);
    }
    QSize minimumSizeHint() const override {
        return sizeHint();
    }

protected:
    void paintEvent(QPaintEvent*) override {
        QPainter painter(this);
        const QRect area = contentsRect();
        QRect nameArea = area;
        if (!m_amount.isEmpty())
            nameArea.setHeight(area.height() / 2);
        const QString name = m_amount.isEmpty() ? text() : text().section('\n', 0, -2);
        style()->drawItemText(&painter, nameArea, Qt::AlignLeft | Qt::AlignVCenter, palette(), isEnabled(),
                              fontMetrics().elidedText(name, Qt::ElideMiddle, area.width()), foregroundRole());
        if (!m_amount.isEmpty()) {
            const QRect amountArea(area.left(), nameArea.bottom() + 1, area.width(), area.height() - nameArea.height());
            style()->drawItemText(&painter, amountArea, Qt::AlignLeft | Qt::AlignVCenter, palette(), isEnabled(),
                                  m_amount, foregroundRole());
        }
    }

private:
    QString m_amount;
};

static QString formatBytes(qint64 bytes) {
    const double b = static_cast<double>(bytes);
    if (b >= 1024.0 * 1024.0 * 1024.0)
        return QString("%1 GB").arg(b / (1024.0 * 1024.0 * 1024.0), 0, 'f', 1);
    if (b >= 1024.0 * 1024.0)
        return QString("%1 MB").arg(b / (1024.0 * 1024.0), 0, 'f', 1);
    if (b >= 1024.0)
        return QString("%1 KB").arg(b / 1024.0, 0, 'f', 1);
    return QString("%1 B").arg(bytes);
}

// Shared file list for both panes, with the appropriate transfer drag payload.
class SftpTreeWidget : public QTreeWidget {
public:
    explicit SftpTreeWidget(QWidget* parent = nullptr, bool local = false) : QTreeWidget(parent), m_local(local) {
    }

private:
    bool m_local;

protected:
    void startDrag(Qt::DropActions supportedActions) override {
        Q_UNUSED(supportedActions);
        QList<QTreeWidgetItem*> items = selectedItems();
        if (items.isEmpty())
            return;

        QJsonArray paths;
        for (QTreeWidgetItem* item : items) {
            const QString path = item->data(0, Qt::UserRole + 2).toString();
            if (!path.isEmpty())
                paths.append(path);
        }
        if (paths.isEmpty())
            return;

        auto* mime = new QMimeData;
        mime->setData(m_local ? "application/x-banchoxterm-sftp-local" : "application/x-banchoxterm-sftp-remote",
                      QJsonDocument(paths).toJson(QJsonDocument::Compact));
        if (m_local) {
            QList<QUrl> urls;
            for (const QJsonValue& value : paths)
                urls.append(QUrl::fromLocalFile(value.toString()));
            mime->setUrls(urls);
        }

        auto* drag = new QDrag(this);
        drag->setMimeData(mime);
        drag->exec(Qt::CopyAction);
    }
};

SftpSidebar::SftpSidebar(QWidget* parent) : QWidget(parent) {
    QSettings transferSettings;
    m_maxParallelTransfers =
        qBound(1, transferSettings.value(QStringLiteral("sftp/maxParallelTransfers"), 1).toInt(), 8);
    auto* mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(10, 10, 10, 10);
    mainLayout->setSpacing(8);

    auto* remotePanel = new QWidget(this);
    remotePanel->setObjectName("sftpRemotePanel");
    auto* remoteLayout = new QVBoxLayout(remotePanel);
    remoteLayout->setContentsMargins(10, 8, 10, 8);
    remoteLayout->setSpacing(6);

    auto* titleLabel = new QLabel(tr("Remote files"), remotePanel);
    titleLabel->setObjectName("sftpPaneTitle");
    QFont titleFont = titleLabel->font();
    titleFont.setCapitalization(QFont::AllUppercase);
    titleLabel->setFont(titleFont);
    remoteLayout->addWidget(titleLabel);

    auto* navLayout = new QHBoxLayout();
    navLayout->setSpacing(4);

    m_upBtn = new QPushButton(QIcon(":/icons/up.svg"), "", remotePanel);
    m_upBtn->setToolTip(tr("Go to parent directory"));
    m_upBtn->setFixedWidth(32);
    navLayout->addWidget(m_upBtn);

    m_pathEdit = new QLineEdit(remotePanel);
    m_pathEdit->setObjectName("remotePathEdit");
    m_pathEdit->setPlaceholderText("/");
    navLayout->addWidget(m_pathEdit);

    m_refreshBtn = new QPushButton(QIcon(":/icons/refresh.svg"), "", remotePanel);
    m_refreshBtn->setToolTip(tr("Refresh folder"));
    m_refreshBtn->setFixedWidth(32);
    navLayout->addWidget(m_refreshBtn);

    remoteLayout->addLayout(navLayout);

    auto* actionsLayout = new QHBoxLayout();
    actionsLayout->setSpacing(4);
    actionsLayout->setAlignment(Qt::AlignLeft);

    m_uploadBtn = new QPushButton(QIcon(":/icons/upload.svg"), QString(), remotePanel);
    m_uploadBtn->setObjectName("remoteActionButton");
    m_uploadBtn->setToolTip(tr("Upload files to the current remote folder"));
    m_uploadBtn->setAccessibleName(tr("Upload files"));
    actionsLayout->addWidget(m_uploadBtn);

    m_uploadDirBtn = new QPushButton(QIcon(":/icons/upload-folder.svg"), QString(), remotePanel);
    m_uploadDirBtn->setObjectName("remoteActionButton");
    m_uploadDirBtn->setToolTip(tr("Upload a folder to the current remote folder"));
    m_uploadDirBtn->setAccessibleName(tr("Upload Folder"));
    actionsLayout->addWidget(m_uploadDirBtn);

    m_compareBtn = new QPushButton(QIcon(":/icons/compare.svg"), QString(), remotePanel);
    m_compareBtn->setObjectName("remoteActionButton");
    m_compareBtn->setToolTip(tr("Compare the active local and remote folders"));
    m_compareBtn->setAccessibleName(tr("Compare folders"));
    actionsLayout->addWidget(m_compareBtn);

    m_tunnelsBtn = new QPushButton(QIcon(":/icons/tunnel.svg"), QString(), remotePanel);
    m_tunnelsBtn->setObjectName("remoteActionButton");
    m_tunnelsBtn->setToolTip(tr("Start or stop individual SSH tunnels"));
    m_tunnelsBtn->setAccessibleName(tr("Manage SSH tunnels"));
    actionsLayout->addWidget(m_tunnelsBtn);

    m_newFolderBtn = new QPushButton(QIcon(":/icons/new-folder.svg"), QString(), remotePanel);
    m_newFolderBtn->setObjectName("remoteActionButton");
    m_newFolderBtn->setToolTip(tr("Create a folder in the current remote folder"));
    m_newFolderBtn->setAccessibleName(tr("New Folder"));
    actionsLayout->addWidget(m_newFolderBtn);

    m_renameBtn = new QPushButton(QIcon(":/icons/rename.svg"), QString(), remotePanel);
    m_renameBtn->setObjectName("remoteActionButton");
    m_renameBtn->setToolTip(tr("Rename the selected remote item"));
    m_renameBtn->setAccessibleName(tr("Rename"));
    actionsLayout->addWidget(m_renameBtn);

    m_chmodBtn = new QPushButton(QIcon(":/icons/permissions.svg"), QString(), remotePanel);
    m_chmodBtn->setObjectName("remoteActionButton");
    m_chmodBtn->setToolTip(tr("Change permissions of the selected remote item"));
    m_chmodBtn->setAccessibleName(tr("Permissions"));
    actionsLayout->addWidget(m_chmodBtn);

    const QList<QPushButton*> remoteActionButtons = {m_uploadBtn,    m_uploadDirBtn, m_compareBtn, m_tunnelsBtn,
                                                     m_newFolderBtn, m_renameBtn,    m_chmodBtn};
    for (QPushButton* button : remoteActionButtons) {
        button->setIconSize(QSize(16, 16));
        button->setFixedSize(32, 32);
    }

    remoteLayout->addLayout(actionsLayout);

    m_treeWidget = new SftpTreeWidget(remotePanel);
    m_treeWidget->setHeaderLabels({tr("Name"), tr("Size"), tr("Modified")});
    m_treeWidget->setRootIsDecorated(false);
    m_treeWidget->setAlternatingRowColors(false);
    m_treeWidget->setObjectName("sftpTree");
    m_treeWidget->header()->setSectionResizeMode(0, QHeaderView::Stretch);
    m_treeWidget->header()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    m_treeWidget->header()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
    m_treeWidget->setContextMenuPolicy(Qt::CustomContextMenu);
    m_treeWidget->setSelectionMode(QAbstractItemView::ExtendedSelection);
    m_treeWidget->setDragEnabled(true);
    m_treeWidget->setAcceptDrops(true);
    m_treeWidget->setDragDropMode(QAbstractItemView::DragDrop);
    m_treeWidget->setDropIndicatorShown(true);
    m_treeWidget->viewport()->installEventFilter(this);
    remoteLayout->addWidget(m_treeWidget);

    auto* fileSplitter = new QSplitter(Qt::Vertical, this);
    m_fileSplitter = fileSplitter;
    fileSplitter->setObjectName("sftpFileSplitter");

    auto* localPanel = new QWidget(fileSplitter);
    localPanel->setObjectName("sftpLocalPanel");
    auto* localLayout = new QVBoxLayout(localPanel);
    localLayout->setContentsMargins(10, 8, 10, 8);
    localLayout->setSpacing(6);
    auto* localTitle = new QLabel(tr("Local files"), localPanel);
    localTitle->setObjectName("sftpPaneTitle");
    QFont localTitleFont = localTitle->font();
    localTitleFont.setCapitalization(QFont::AllUppercase);
    localTitle->setFont(localTitleFont);
    localLayout->addWidget(localTitle);

    auto* localPathLayout = new QHBoxLayout();
    localPathLayout->setSpacing(4);
    auto* localUpBtn = new QPushButton(QIcon(":/icons/up.svg"), QString(), localPanel);
    localUpBtn->setObjectName("localUpButton");
    localUpBtn->setToolTip(tr("Go to parent directory"));
    localUpBtn->setAccessibleName(tr("Go to parent directory"));
    localUpBtn->setFixedWidth(32);
    localPathLayout->addWidget(localUpBtn);
    m_localPathEdit = new QLineEdit(localPanel);
    m_localPathEdit->setObjectName("localPathEdit");
    m_localPathEdit->setPlaceholderText(tr("Local path"));
    m_localPathEdit->setText(QDir::homePath());
    localPathLayout->addWidget(m_localPathEdit);
    auto* localRefreshBtn = new QPushButton(refreshIcon(), QString(), localPanel);
    localRefreshBtn->setObjectName("localRefreshButton");
    localRefreshBtn->setToolTip(tr("Refresh folder"));
    localRefreshBtn->setAccessibleName(tr("Refresh folder"));
    localRefreshBtn->setFixedWidth(32);
    localPathLayout->addWidget(localRefreshBtn);
    localLayout->addLayout(localPathLayout);

    auto* localActions = new QHBoxLayout();
    localActions->setSpacing(4);
    localActions->setAlignment(Qt::AlignLeft);
    auto addLocalButton = [this, localPanel, localActions](const QString& icon, const QString& label,
                                                           const QString& name) {
        auto* button = new QPushButton(QIcon(icon), QString(), localPanel);
        button->setObjectName(name);
        button->setProperty("localAction", true);
        button->setToolTip(label);
        button->setAccessibleName(label);
        button->setIconSize(QSize(16, 16));
        button->setFixedSize(32, 32);
        localActions->addWidget(button);
        return button;
    };
    auto* browseBtn = addLocalButton(":/icons/folder.svg", tr("Select local folder"), "localBrowseButton");
    m_downloadToLocalBtn =
        addLocalButton(":/icons/download.svg", tr("Download selection to local folder"), "localDownloadButton");
    m_downloadToLocalBtn->setEnabled(false);
    auto* localNewBtn = addLocalButton(":/icons/new-folder.svg", tr("New Folder"), "localNewFolderButton");
    auto* localRenameBtn = addLocalButton(":/icons/rename.svg", tr("Rename"), "localRenameButton");
    localLayout->addLayout(localActions);

    m_localTree = new SftpTreeWidget(localPanel, true);
    m_localTree->setObjectName("localFileTree");
    m_localTree->setHeaderLabels({tr("Name"), tr("Size"), tr("Modified")});
    m_localTree->setRootIsDecorated(false);
    m_localTree->setAlternatingRowColors(false);
    m_localTree->setContextMenuPolicy(Qt::CustomContextMenu);
    m_localTree->setSelectionMode(QAbstractItemView::ExtendedSelection);
    m_localTree->setDragEnabled(true);
    m_localTree->setAcceptDrops(true);
    m_localTree->setDragDropMode(QAbstractItemView::DragDrop);
    m_localTree->setDropIndicatorShown(true);
    m_localTree->header()->setSectionResizeMode(0, QHeaderView::Stretch);
    m_localTree->header()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    m_localTree->header()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
    m_localWatcher = new QFileSystemWatcher(this);
    auto* localRefreshTimer = new QTimer(this);
    localRefreshTimer->setSingleShot(true);
    localRefreshTimer->setInterval(100);
    connect(m_localWatcher, &QFileSystemWatcher::directoryChanged, localRefreshTimer,
            [localRefreshTimer]() { localRefreshTimer->start(); });
    connect(localRefreshTimer, &QTimer::timeout, this, &SftpSidebar::refreshLocalFiles);
    navigateLocalTo(QDir::homePath());
    m_localTree->viewport()->installEventFilter(this);
    localLayout->addWidget(m_localTree);

    fileSplitter->addWidget(remotePanel);
    fileSplitter->addWidget(localPanel);
    fileSplitter->setStretchFactor(0, 2);
    fileSplitter->setStretchFactor(1, 1);
    QSettings layoutSettings;
    if (layoutSettings.contains("sftp/fileSplitter"))
        fileSplitter->restoreState(layoutSettings.value("sftp/fileSplitter").toByteArray());
    mainLayout->addWidget(fileSplitter);

    connect(m_localPathEdit, &QLineEdit::returnPressed, this,
            [this]() { navigateLocalTo(m_localPathEdit->text().trimmed()); });
    connect(localRefreshBtn, &QPushButton::clicked, this, &SftpSidebar::refreshLocalFiles);
    connect(localUpBtn, &QPushButton::clicked, this, [this]() {
        QDir dir(m_localPath);
        if (dir.cdUp())
            navigateLocalTo(dir.absolutePath());
    });
    connect(browseBtn, &QPushButton::clicked, this, [this]() {
        const QString path = QFileDialog::getExistingDirectory(this, tr("Select local folder"), m_localPath);
        if (!path.isEmpty())
            navigateLocalTo(path);
    });
    connect(m_downloadToLocalBtn, &QPushButton::clicked, this, &SftpSidebar::onDownloadToLocalClicked);
    connect(localNewBtn, &QPushButton::clicked, this, &SftpSidebar::onLocalNewFolderClicked);
    connect(localRenameBtn, &QPushButton::clicked, this, &SftpSidebar::onLocalRenameClicked);
    connect(m_localTree, &QTreeWidget::customContextMenuRequested, this, &SftpSidebar::showLocalContextMenu);
    connect(m_localTree, &QTreeWidget::itemDoubleClicked, this, [this](QTreeWidgetItem* item, int) {
        const QString special = item->data(0, Qt::UserRole + 1).toString();
        if (special == ".") {
            refreshLocalFiles();
        } else if (special == "..") {
            QDir dir(m_localPath);
            if (dir.cdUp())
                navigateLocalTo(dir.absolutePath());
        } else if (item->data(0, Qt::UserRole).toBool()) {
            navigateLocalTo(item->data(0, Qt::UserRole + 2).toString());
        }
    });
    auto updateDownloadButton = [this]() {
        m_downloadToLocalBtn->setEnabled(m_isConnected && !m_directoryDownloadActive &&
                                         !selectedRemotePaths().isEmpty());
    };
    connect(m_treeWidget, &QTreeWidget::itemSelectionChanged, this, updateDownloadButton);
    connect(m_treeWidget->model(), &QAbstractItemModel::modelReset, this, updateDownloadButton);

    m_progressLabel = new SftpProgressLabel(remotePanel);
    m_progressLabel->setObjectName("sftpTransferLabel");
    m_progressLabel->hide();
    remoteLayout->addWidget(m_progressLabel);

    m_progressBar = new QProgressBar(remotePanel);
    m_progressBar->setObjectName("sftpTransferProgress");
    m_progressBar->setTextVisible(false);
    m_progressBar->setFixedHeight(6);
    m_progressBar->setRange(0, 100);
    m_progressBar->setValue(0);
    m_progressBar->hide();
    remoteLayout->addWidget(m_progressBar);

    m_cancelTransferBtn = new QPushButton(tr("Cancel queued"), remotePanel);
    m_cancelTransferBtn->setObjectName("sftpTransferAction");
    m_cancelTransferBtn->setEnabled(false);
    m_cancelTransferBtn->hide();
    remoteLayout->addWidget(m_cancelTransferBtn);

    m_pauseTransferBtn = new QPushButton(tr("Pause transfer"), remotePanel);
    m_pauseTransferBtn->setObjectName("sftpTransferAction");
    m_pauseTransferBtn->setEnabled(false);
    m_pauseTransferBtn->hide();
    remoteLayout->addWidget(m_pauseTransferBtn);

    m_statusLabel = new SftpProgressLabel(remotePanel);
    m_statusLabel->setText(tr("Disconnected"));
    m_statusLabel->setObjectName("sftpStatus");
    remoteLayout->addWidget(m_statusLabel);

    m_upBtn->setEnabled(false);
    m_pathEdit->setEnabled(false);
    m_refreshBtn->setEnabled(false);
    m_uploadBtn->setEnabled(false);
    m_uploadDirBtn->setEnabled(false);
    m_newFolderBtn->setEnabled(false);
    m_renameBtn->setEnabled(false);
    m_chmodBtn->setEnabled(false);
    m_compareBtn->setEnabled(false);
    m_tunnelsBtn->setEnabled(false);
    m_treeWidget->setEnabled(false);

    m_fileWatcher = new QFileSystemWatcher(this);
    connect(m_fileWatcher, &QFileSystemWatcher::fileChanged, this, &SftpSidebar::onWatchedFileChanged);
    auto* monitor = new QDialog(window(), Qt::Tool | Qt::WindowTitleHint | Qt::WindowMinimizeButtonHint |
                                              Qt::WindowCloseButtonHint);
    monitor->setWindowTitle(tr("File transfers"));
    monitor->setObjectName("transferMonitor");
    monitor->setModal(false);
    auto* monitorLayout = new QVBoxLayout(monitor);
    auto* summary = new QLabel(monitor);
    summary->setWordWrap(true);
    summary->setTextFormat(Qt::PlainText);
    summary->setMaximumWidth(420);
    summary->setMinimumWidth(280);
    auto* progress = new QProgressBar(monitor);
    auto* actions = new QHBoxLayout();
    auto* pause = new QPushButton(tr("Pause transfer"), monitor);
    auto* cancel = new QPushButton(tr("Cancel transfers"), monitor);
    auto* minimize = new QPushButton(tr("Minimize"), monitor);
    actions->addWidget(pause);
    actions->addWidget(cancel);
    actions->addWidget(minimize);
    monitorLayout->addWidget(summary);
    monitorLayout->addWidget(progress);
    monitorLayout->addLayout(actions);
    connect(pause, &QPushButton::clicked, this, &SftpSidebar::onToggleTransferPause);
    connect(cancel, &QPushButton::clicked, this, &SftpSidebar::onCancelQueuedTransfers);
    minimize->setObjectName("transferMonitorToggle");
    connect(minimize, &QPushButton::clicked, monitor, [monitor, summary, progress, pause, cancel, minimize] {
        const bool collapse = !summary->isHidden();
        summary->setVisible(!collapse);
        progress->setVisible(!collapse);
        pause->setVisible(!collapse);
        cancel->setVisible(!collapse);
        minimize->setText(collapse ? tr("Restore") : tr("Minimize"));
        monitor->adjustSize();
    });
    // Closing the monitor minimizes it, so the running job remains discoverable.
    connect(monitor, &QDialog::rejected, monitor, [monitor, summary, minimize] {
        if (!summary->isHidden())
            minimize->click();
        monitor->show();
    });
    auto* monitorTimer = new QTimer(this);
    connect(monitorTimer, &QTimer::timeout, this, [this, monitor, summary, progress, pause, cancel] {
        const bool active = hasActiveTransfers();
        summary->setText(m_currentSession.host + "\n" + m_progressLabel->text() + "\n" + m_statusLabel->text());
        progress->setRange(m_progressBar->minimum(), m_progressBar->maximum());
        progress->setValue(m_progressBar->value());
        pause->setText(m_pauseTransferBtn->text());
        pause->setEnabled(active && m_pauseTransferBtn->isEnabled());
        cancel->setEnabled(active);
        if (active && !isVisible() && !monitor->isVisible()) {
            monitor->adjustSize();
            const QRect area = window()->frameGeometry();
            monitor->move(area.bottomRight() - QPoint(monitor->width() + 24, monitor->height() + 48));
            monitor->show();
        } else if (!active && monitor->isVisible()) {
            monitor->hide();
        }
    });
    monitorTimer->start(250);
    connect(this, &QObject::destroyed, monitor, &QObject::deleteLater);

    connect(m_upBtn, &QPushButton::clicked, this, &SftpSidebar::onParentDirClicked);
    connect(m_refreshBtn, &QPushButton::clicked, this, &SftpSidebar::onRefreshClicked);
    connect(m_uploadBtn, &QPushButton::clicked, this, &SftpSidebar::onUploadClicked);
    connect(m_uploadDirBtn, &QPushButton::clicked, this, &SftpSidebar::onUploadFolderClicked);
    connect(m_compareBtn, &QPushButton::clicked, this, &SftpSidebar::onCompareFoldersClicked);
    connect(m_tunnelsBtn, &QPushButton::clicked, this, &SftpSidebar::onManageTunnels);
    connect(m_cancelTransferBtn, &QPushButton::clicked, this, &SftpSidebar::onCancelQueuedTransfers);
    connect(m_pauseTransferBtn, &QPushButton::clicked, this, &SftpSidebar::onToggleTransferPause);
    connect(m_newFolderBtn, &QPushButton::clicked, this, &SftpSidebar::onNewFolderClicked);
    connect(m_renameBtn, &QPushButton::clicked, this, &SftpSidebar::onRenameClicked);
    connect(m_chmodBtn, &QPushButton::clicked, this, &SftpSidebar::onChmodClicked);
    connect(m_pathEdit, &QLineEdit::returnPressed, this, [this]() { emit requestNavigate(m_pathEdit->text()); });
    connect(m_treeWidget, &QTreeWidget::itemDoubleClicked, this, &SftpSidebar::onItemDoubleClicked);
    connect(m_treeWidget, &QTreeWidget::customContextMenuRequested, this, &SftpSidebar::showContextMenu);
}

SftpSidebar::~SftpSidebar() {
    stopParallelTransfers();
    saveLayout();
    detachConnection();
}

void SftpSidebar::saveLayout() {
    if (!m_fileSplitter)
        return;
    QSettings settings;
    settings.setValue("sftp/fileSplitter", m_fileSplitter->saveState());
}

void SftpSidebar::setConnection(SshConnection* connection) {
    if (m_connection == connection)
        return;

    stopParallelTransfers();
    detachConnection();
    detachFtp();
    m_connection = connection;
    if (!m_connection)
        return;

    connect(this, &SftpSidebar::requestConnect, m_connection, &SshConnection::connectToHost);
    connect(this, &SftpSidebar::requestList, m_connection, &SshConnection::listDirectory);
    connect(this, &SftpSidebar::requestNavigate, m_connection, &SshConnection::navigateDirectory);
    connect(this, &SftpSidebar::requestDownload, m_connection, &SshConnection::downloadFile);
    connect(this, &SftpSidebar::requestUpload, m_connection, &SshConnection::uploadFile);
    connect(this, &SftpSidebar::requestDelete, m_connection, &SshConnection::deleteFile);
    connect(this, &SftpSidebar::requestCreateDir, m_connection, &SshConnection::createDirectory);
    connect(this, &SftpSidebar::requestRename, m_connection, &SshConnection::renamePath);
    connect(this, &SftpSidebar::requestChmod, m_connection, &SshConnection::chmodPath);
    connect(this, &SftpSidebar::requestUploadDir, m_connection, &SshConnection::uploadDirectory);
    connect(this, &SftpSidebar::requestDisconnect, m_connection, &SshConnection::disconnectFromHost);

    // Queued signals from the previous tab can arrive after disconnect().
    connect(m_connection, &SshConnection::connectionSuccess, this, [this, connection]() {
        if (m_connection == connection)
            onConnectionSuccess();
    });
    connect(m_connection, &SshConnection::connectionFailed, this, [this, connection](const QString& error) {
        if (m_connection == connection)
            onConnectionFailed(error);
    });
    connect(m_connection, &SshConnection::directoryListed, this,
            [this, connection](const QString& path, const QList<SftpFile>& files) {
                if (m_connection == connection)
                    onDirectoryListed(path, files);
            });
    connect(m_connection, &SshConnection::operationFinished, this, &SftpSidebar::onOperationFinished);
    connect(m_connection, &SshConnection::transferProgress, this, &SftpSidebar::onTransferProgress);
    connect(m_connection, &SshConnection::passwordRequired, this, &SftpSidebar::onPasswordRequired);
    connect(m_connection, &SshConnection::remoteStatsUpdated, this, &SftpSidebar::remoteStatsUpdated);
    connect(m_connection, &SshConnection::tunnelStateChanged, this, &SftpSidebar::onTunnelStateChanged);
}

void SftpSidebar::detachConnection() {
    if (!m_connection)
        return;

    disconnect(this, nullptr, m_connection, nullptr);
    disconnect(m_connection, nullptr, this, nullptr);
    m_connection = nullptr;
}

void SftpSidebar::setFtpClient(FtpClient* client) {
    if (m_ftp == client)
        return;
    detachFtp();
    m_ftp = client;
    if (!m_ftp)
        return;

    connect(this, &SftpSidebar::requestFtpConnect, m_ftp, &FtpClient::connectToHost);
    connect(this, &SftpSidebar::requestList, m_ftp, &FtpClient::listDirectory);
    connect(this, &SftpSidebar::requestNavigate, m_ftp, &FtpClient::listDirectory);
    connect(this, &SftpSidebar::requestDownload, m_ftp, &FtpClient::downloadFile);
    connect(this, &SftpSidebar::requestUpload, m_ftp, &FtpClient::uploadFile);
    connect(this, &SftpSidebar::requestUploadDir, m_ftp, &FtpClient::uploadDirectory);
    connect(this, &SftpSidebar::requestDelete, m_ftp, &FtpClient::deleteFile);
    connect(this, &SftpSidebar::requestCreateDir, m_ftp, &FtpClient::createDirectory);
    connect(this, &SftpSidebar::requestRename, m_ftp, &FtpClient::renamePath);

    connect(m_ftp, &FtpClient::connectionSuccess, this, &SftpSidebar::onConnectionSuccess);
    connect(m_ftp, &FtpClient::connectionFailed, this, &SftpSidebar::onConnectionFailed);
    connect(m_ftp, &FtpClient::directoryListed, this, &SftpSidebar::onDirectoryListed);
    connect(m_ftp, &FtpClient::operationFinished, this, &SftpSidebar::onOperationFinished);
    connect(m_ftp, &FtpClient::transferProgress, this, &SftpSidebar::onTransferProgress);
}

void SftpSidebar::detachFtp() {
    if (!m_ftp)
        return;
    disconnect(this, nullptr, m_ftp, nullptr);
    disconnect(m_ftp, nullptr, this, nullptr);
    m_ftp->deleteLater();
    m_ftp = nullptr;
}

void SftpSidebar::startSession(const Session& session) {
    if (hasActiveTransfers() && m_currentSession.id == session.id)
        return;
    if (session.type == SessionType::FTP) {
        detachConnection();
        m_currentSession = session;
        m_statusLabel->setText(tr("Connecting to %1...").arg(session.host));
        if (!m_ftp)
            setFtpClient(new FtpClient(this));
        const QString password = Keyring::lookupPassword(session.id);
        emit requestFtpConnect(session.host, session.port, session.user, password, session.ftpTls,
                               session.ftpTlsMinimumVersion, session.ftpTlsCaFile,
                               session.ftpTlsAllowInvalidCertificates);
        return;
    }
    if (session.type != SessionType::SSH) {
        stopSession();
        return;
    }
    m_currentSession = session;

    if (m_connection && m_connection->isConnected()) {
        // Already connected: just refresh the current directory listing.
        onConnectionSuccess();
        if (!session.remoteDirectory.isEmpty())
            navigateTo(session.remoteDirectory);
        return;
    }

    m_statusLabel->setText(
        session.jumpHost.trimmed().isEmpty()
            ? tr("Connecting to %1...").arg(session.host)
            : tr("Connecting to %1 via bastion %2...").arg(session.host, session.jumpHost.trimmed()));

    QString password = Keyring::lookupPassword(session.id);
    emit requestConnect(session.host, session.port, session.user, session.keyPath, password, session.tunnels,
                        session.jumpHost, session.jumpPort, session.jumpUser, session.jumpKeyPath, session.id);
}

void SftpSidebar::stopSession() {
    stopParallelTransfers();
    m_isConnected = false;
    detachConnection();
    detachFtp();
    m_treeWidget->clear();
    m_currentRemoteFiles.clear();
    m_tunnelStates.clear();
    m_pathEdit->clear();
    m_statusLabel->setText(tr("Disconnected"));

    m_transferQueue.clear();
    m_transferActive = false;
    m_transferPaused = false;
    m_progressBar->hide();
    m_progressLabel->hide();
    m_cancelTransferBtn->setEnabled(false);
    m_cancelTransferBtn->hide();
    m_pauseTransferBtn->setEnabled(false);
    m_pauseTransferBtn->hide();

    m_upBtn->setEnabled(false);
    m_pathEdit->setEnabled(false);
    m_refreshBtn->setEnabled(false);
    m_uploadBtn->setEnabled(false);
    m_uploadDirBtn->setEnabled(false);
    m_compareBtn->setEnabled(false);
    m_tunnelsBtn->setEnabled(false);
    m_newFolderBtn->setEnabled(false);
    m_renameBtn->setEnabled(false);
    m_chmodBtn->setEnabled(false);
    m_treeWidget->setEnabled(false);
    m_downloadToLocalBtn->setEnabled(false);

    for (const QString& path : m_watchedFiles.keys()) {
        m_fileWatcher->removePath(path);
        QFile::remove(path);
    }
    m_watchedFiles.clear();
}

void SftpSidebar::onConnectionSuccess() {
    m_isConnected = true;
    m_statusLabel->setText(m_currentSession.type == SessionType::SSH && !m_currentSession.jumpHost.trimmed().isEmpty()
                               ? tr("Connected via bastion %1").arg(m_currentSession.jumpHost.trimmed())
                               : tr("Connected"));

    m_upBtn->setEnabled(true);
    m_pathEdit->setEnabled(true);
    m_refreshBtn->setEnabled(true);
    m_uploadBtn->setEnabled(true);
    m_uploadDirBtn->setEnabled(true);
    m_compareBtn->setEnabled(true);
    m_tunnelStates = QList<bool>(m_currentSession.tunnels.size(), false);
    m_tunnelsBtn->setEnabled(m_currentSession.type == SessionType::SSH && !m_currentSession.tunnels.isEmpty());
    m_newFolderBtn->setEnabled(true);
    m_renameBtn->setEnabled(true);
    m_chmodBtn->setEnabled(true);
    m_treeWidget->setEnabled(true);

    m_currentPath = m_currentSession.remoteDirectory.isEmpty() ? "." : m_currentSession.remoteDirectory;
    emit requestList(m_currentPath);
}

void SftpSidebar::onConnectionFailed(const QString& error) {
    m_isConnected = false;
    const QString protocol = m_ftp ? QStringLiteral("FTP") : QStringLiteral("SFTP");
    emit diagnosticMessage(QStringLiteral("[%1 connection error] ").arg(protocol) + error);
    m_statusLabel->setText(tr("Failed: %1").arg(error));
    stopSession();
}

void SftpSidebar::onTunnelStateChanged(int index, bool active) {
    if (index < 0)
        return;
    if (index >= m_tunnelStates.size())
        m_tunnelStates.resize(index + 1);
    m_tunnelStates[index] = active;
}

void SftpSidebar::onManageTunnels() {
    if (!m_connection || m_currentSession.tunnels.isEmpty())
        return;

    QDialog dialog(this);
    dialog.setObjectName("sftpUtilityDialog");
    dialog.setWindowTitle(tr("SSH Tunnels"));
    dialog.resize(520, 300);
    auto* layout = new QVBoxLayout(&dialog);
    auto* list = new QListWidget(&dialog);
    layout->addWidget(list);
    const auto refresh = [this, list]() {
        list->clear();
        for (int i = 0; i < m_currentSession.tunnels.size(); ++i) {
            const TunnelConfig& tunnel = m_currentSession.tunnels.at(i);
            const QString type = tunnel.type == TunnelConfig::Type::Local
                                     ? tr("Local")
                                     : (tunnel.type == TunnelConfig::Type::Remote ? tr("Remote") : tr("SOCKS5"));
            const int port = tunnel.type == TunnelConfig::Type::Remote ? tunnel.remotePort : tunnel.localPort;
            const bool active = i < m_tunnelStates.size() && m_tunnelStates.at(i);
            list->addItem(tr("%1 — port %2 — %3").arg(type).arg(port).arg(active ? tr("Running") : tr("Stopped")));
        }
    };
    refresh();
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Close, &dialog);
    auto* start = buttons->addButton(tr("Start"), QDialogButtonBox::ActionRole);
    auto* stop = buttons->addButton(tr("Stop"), QDialogButtonBox::ActionRole);
    connect(start, &QPushButton::clicked, &dialog, [this, list]() {
        if (list->currentRow() >= 0)
            QMetaObject::invokeMethod(m_connection, "startTunnel", Qt::QueuedConnection,
                                      Q_ARG(int, list->currentRow()));
    });
    connect(stop, &QPushButton::clicked, &dialog, [this, list]() {
        if (list->currentRow() >= 0)
            QMetaObject::invokeMethod(m_connection, "stopTunnel", Qt::QueuedConnection, Q_ARG(int, list->currentRow()));
    });
    connect(m_connection, &SshConnection::tunnelStateChanged, &dialog, [this, refresh](int index, bool active) {
        onTunnelStateChanged(index, active);
        refresh();
    });
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    layout->addWidget(buttons);
    dialog.exec();
}

void SftpSidebar::onDirectoryListed(const QString& path, const QList<SftpFile>& files) {
    if (m_directoryDownloadActive && path == m_activeTransfer.remotePath) {
        if (!m_directoryDownloadCancelled) {
            for (const SftpFile& file : files) {
                // Never allow a server-provided name to escape the destination folder.
                if (file.name == "." || file.name == "..")
                    continue;
                if (file.name.isEmpty() || file.name.contains('/') || file.name.contains('\\')) {
                    onOperationFinished(false, tr("Invalid remote file name."));
                    return;
                }
                TransferItem child;
                child.remotePath = (path.endsWith('/') ? path : path + '/') + file.name;
                child.localPath = QDir(m_activeTransfer.localPath).filePath(file.name);
                child.isDirDownload = file.isDirectory;
                m_transferQueue.append(child);
            }
        }
        m_directoryDownloadActive = false;
        m_directoryDownloadCancelled = false;
        if (m_currentSession.type == SessionType::SSH)
            startParallelTransfers();
        else
            startNextTransfer();
        return;
    }
    m_currentRemoteFiles = files;
    updatePath(path);
    populateFileTree(m_treeWidget, m_currentPath, files, m_currentPath != "/" && !m_currentPath.isEmpty());
}

void SftpSidebar::populateFileTree(QTreeWidget* tree, const QString& path, const QList<SftpFile>& files,
                                   bool hasParent) {
    tree->setUpdatesEnabled(false);
    tree->clear();

    // "." entry (refresh current directory)
    auto* dotItem = new QTreeWidgetItem();
    dotItem->setText(0, ".");
    dotItem->setIcon(0, folderIcon());
    dotItem->setData(0, Qt::UserRole, true);
    dotItem->setData(0, Qt::UserRole + 1, QStringLiteral("."));
    tree->addTopLevelItem(dotItem);

    // ".." entry (go to parent) when not at root
    if (hasParent) {
        auto* dotdotItem = new QTreeWidgetItem();
        dotdotItem->setText(0, "..");
        dotdotItem->setIcon(0, folderIcon());
        dotdotItem->setData(0, Qt::UserRole, true);
        dotdotItem->setData(0, Qt::UserRole + 1, QStringLiteral(".."));
        tree->addTopLevelItem(dotdotItem);
    }

    QList<QTreeWidgetItem*> folders;
    QList<QTreeWidgetItem*> normalFiles;

    const QString base = path.endsWith('/') ? path : path + "/";

    for (const SftpFile& file : files) {
        auto* item = new QTreeWidgetItem();
        item->setText(0, file.name);
        item->setData(0, Qt::UserRole, file.isDirectory);
        item->setData(0, Qt::UserRole + 1, QString());
        item->setData(0, Qt::UserRole + 2, base + file.name);

        if (file.isDirectory) {
            item->setIcon(0, folderIcon());
            item->setText(1, tr("<DIR>"));
            item->setText(2, file.mtime.isValid() ? file.mtime.toString("yyyy-MM-dd hh:mm") : "");
            folders.append(item);
        } else {
            item->setIcon(0, fileIcon());

            double sizeKB = file.size / 1024.0;
            if (sizeKB > 1024.0) {
                item->setText(1, QString("%1 MB").arg(sizeKB / 1024.0, 0, 'f', 1));
            } else {
                item->setText(1, QString("%1 KB").arg(sizeKB, 0, 'f', 1));
            }

            item->setText(2, file.mtime.isValid() ? file.mtime.toString("yyyy-MM-dd hh:mm") : "");
            normalFiles.append(item);
        }
    }

    tree->addTopLevelItems(folders);
    tree->addTopLevelItems(normalFiles);
    tree->setUpdatesEnabled(true);
}

void SftpSidebar::onCompareFoldersClicked() {
    const QString localPath = QDir::fromNativeSeparators(m_localPathEdit->text().trimmed());
    QDir localDir(localPath);
    if (!localDir.exists()) {
        QMessageBox::warning(this, tr("Compare folders"), tr("The selected local folder does not exist."));
        return;
    }
    if (m_currentRemoteFiles.isEmpty()) {
        QMessageBox::information(this, tr("Compare folders"), tr("Load a remote folder before comparing it."));
        return;
    }

    QMap<QString, QFileInfo> localEntries;
    const QFileInfoList entries = localDir.entryInfoList(QDir::AllEntries | QDir::NoDotAndDotDot, QDir::Name);
    for (const QFileInfo& entry : entries)
        localEntries.insert(entry.fileName(), entry);

    QMap<QString, SftpFile> remoteEntries;
    for (const SftpFile& entry : m_currentRemoteFiles)
        remoteEntries.insert(entry.name, entry);

    QStringList differences;
    QStringList uploadPaths;
    QStringList downloadPaths;
    for (auto it = remoteEntries.cbegin(); it != remoteEntries.cend(); ++it) {
        if (!localEntries.contains(it.key())) {
            differences << tr("Remote only: %1").arg(it.key());
            if (!it.value().isDirectory)
                downloadPaths << (m_currentPath.endsWith('/') ? m_currentPath : m_currentPath + '/') + it.key();
            continue;
        }
        const QFileInfo local = localEntries.value(it.key());
        if (local.isDir() != it.value().isDirectory) {
            differences << tr("Type differs: %1").arg(it.key());
        } else if (!it.value().isDirectory && local.size() != it.value().size) {
            differences
                << tr("Size differs: %1 (local %2, remote %3)").arg(it.key()).arg(local.size()).arg(it.value().size);
            uploadPaths << local.absoluteFilePath();
            downloadPaths << (m_currentPath.endsWith('/') ? m_currentPath : m_currentPath + '/') + it.key();
        }
        localEntries.remove(it.key());
    }
    for (auto it = localEntries.cbegin(); it != localEntries.cend(); ++it) {
        differences << tr("Local only: %1").arg(it.key());
        if (!it.value().isDir())
            uploadPaths << it.value().absoluteFilePath();
    }

    QDialog dialog(this);
    dialog.setObjectName("sftpUtilityDialog");
    dialog.setWindowTitle(tr("Compare folders"));
    dialog.resize(620, 420);
    auto* layout = new QVBoxLayout(&dialog);
    auto* summary =
        new QLabel(differences.isEmpty() ? tr("The active folders have the same entries and file sizes.")
                                         : tr("Differences in local %1 and remote %2:").arg(localPath, m_currentPath),
                   &dialog);
    summary->setWordWrap(true);
    summary->setTextFormat(Qt::PlainText);
    summary->setMaximumWidth(420);
    layout->addWidget(summary);
    auto* list = new QListWidget(&dialog);
    if (differences.isEmpty())
        list->addItem(tr("No differences found."));
    else
        list->addItems(differences);
    layout->addWidget(list);
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Close, &dialog);
    QPushButton* syncUpload = nullptr;
    QPushButton* syncDownload = nullptr;
    if (!uploadPaths.isEmpty())
        syncUpload = buttons->addButton(tr("Sync local → remote"), QDialogButtonBox::ActionRole);
    if (!downloadPaths.isEmpty())
        syncDownload = buttons->addButton(tr("Sync remote → local"), QDialogButtonBox::ActionRole);
    int syncMode = 0;
    if (syncUpload)
        connect(syncUpload, &QPushButton::clicked, &dialog, [&syncMode, &dialog]() {
            syncMode = 1;
            dialog.accept();
        });
    if (syncDownload)
        connect(syncDownload, &QPushButton::clicked, &dialog, [&syncMode, &dialog]() {
            syncMode = 2;
            dialog.accept();
        });
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    layout->addWidget(buttons);
    dialog.exec();
    if (syncMode == 1)
        enqueueUpload(uploadPaths);
    else if (syncMode == 2)
        enqueueDownloadTo(downloadPaths, localPath);
}

void SftpSidebar::onOperationFinished(bool success, const QString& error) {
    refreshLocalFiles();
    if (m_directoryDownloadActive) {
        m_directoryDownloadActive = false;
        m_directoryDownloadCancelled = false;
        if (!success) {
            m_transferQueue.clear();
            m_transferActive = false;
            setTransferUi(false);
            m_progressBar->hide();
            m_progressLabel->hide();
            m_statusLabel->setText(error);
            QMessageBox::warning(this, tr("SFTP Transfer Error"), error);
        }
        return;
    }
    if (m_transferActive) {
        finishTransferQueue(success, error);
        return;
    }

    m_statusLabel->setText(error);
    if (success) {

        if (!m_pendingEditRemotePath.isEmpty()) {
            QString localPath = m_pendingEditLocalPath;
            QString remotePath = m_pendingEditRemotePath;

            m_pendingEditRemotePath.clear();
            m_pendingEditLocalPath.clear();

            QSettings settings;
            bool useCustom = settings.value("editor/useCustom", false).toBool();
            QString customPath = settings.value("editor/customPath", "").toString();

            if (useCustom && !customPath.isEmpty()) {
                m_watchedFiles[localPath] = remotePath;
                m_fileWatcher->addPath(localPath);

                bool started = QProcess::startDetached(customPath, {localPath});
                if (started) {
                    m_statusLabel->setText(tr("Opened in custom editor. Watching..."));
                } else {
                    m_fileWatcher->removePath(localPath);
                    m_watchedFiles.remove(localPath);
                    QMessageBox::warning(
                        this, tr("Editor Error"),
                        tr("Failed to launch custom editor: %1. Falling back to built-in editor.").arg(customPath));
                    RemoteEditorDialog dialog(QFileInfo(remotePath).fileName(), localPath, this);
                    if (dialog.exec() == QDialog::Accepted && dialog.isSaved()) {
                        m_statusLabel->setText(tr("Uploading %1...").arg(QFileInfo(remotePath).fileName()));
                        emit requestUpload(localPath, remotePath);
                    }
                }
            } else {
                m_watchedFiles[localPath] = remotePath;
                m_fileWatcher->addPath(localPath);

                bool opened = QDesktopServices::openUrl(QUrl::fromLocalFile(localPath));
                if (opened) {
                    m_statusLabel->setText(tr("Opened in default editor. Watching..."));
                } else {
                    m_fileWatcher->removePath(localPath);
                    m_watchedFiles.remove(localPath);
                    RemoteEditorDialog dialog(QFileInfo(remotePath).fileName(), localPath, this);
                    if (dialog.exec() == QDialog::Accepted && dialog.isSaved()) {
                        m_statusLabel->setText(tr("Uploading %1...").arg(QFileInfo(remotePath).fileName()));
                        emit requestUpload(localPath, remotePath);
                    }
                }
            }
            return;
        }

        emit requestList(m_currentPath);
    } else {
        m_pendingEditRemotePath.clear();
        m_pendingEditLocalPath.clear();
        const QString protocol = m_ftp ? QStringLiteral("FTP") : QStringLiteral("SFTP");
        emit diagnosticMessage(QStringLiteral("[%1 operation error] ").arg(protocol) + error);
        QMessageBox::critical(this, tr("%1 Error").arg(protocol), error);
    }
}

void SftpSidebar::onPasswordRequired(const QString& prompt) {
    bool ok;
    QString password = QInputDialog::getText(this, tr("SSH Password Required"), prompt, QLineEdit::Password, "", &ok);
    if (ok) {
        m_statusLabel->setText(tr("Connecting with password..."));
        emit requestConnect(m_currentSession.host, m_currentSession.port, m_currentSession.user,
                            m_currentSession.keyPath, password, m_currentSession.tunnels, m_currentSession.jumpHost,
                            m_currentSession.jumpPort, m_currentSession.jumpUser, m_currentSession.jumpKeyPath,
                            m_currentSession.id);
    } else {
        stopSession();
    }
}

void SftpSidebar::onParentDirClicked() {
    if (m_currentPath == "/" || m_currentPath == "." || m_currentPath.isEmpty())
        return;

    int lastSlash = m_currentPath.lastIndexOf('/');
    if (lastSlash <= 0) {
        m_currentPath = "/";
    } else {
        m_currentPath = m_currentPath.left(lastSlash);
    }
    emit requestNavigate(m_currentPath);
}

void SftpSidebar::navigateTo(const QString& path) {
    if (path.isEmpty())
        return;
    m_currentSession.remoteDirectory = path;
    if (!m_isConnected || m_directoryDownloadActive)
        return;

    const QString cleanPath = path;
    if (cleanPath.isEmpty() || cleanPath == m_currentPath)
        return;

    m_currentPath = cleanPath;
    m_pathEdit->setText(cleanPath);
    emit requestList(cleanPath);
}

void SftpSidebar::onRefreshClicked() {
    emit requestList(m_currentPath);
}

void SftpSidebar::onUploadClicked() {
    if (!m_isConnected)
        return;
    QStringList paths =
        QFileDialog::getOpenFileNames(this, tr("Select Files to Upload"), QDir::homePath(), tr("All Files (*)"));
    if (!paths.isEmpty())
        enqueueUpload(paths);
}

void SftpSidebar::onUploadFolderClicked() {
    if (!m_isConnected || m_transferActive)
        return;
    QString path = QFileDialog::getExistingDirectory(this, tr("Select Folder to Upload"), QDir::homePath());
    if (!path.isEmpty()) {
        if (!m_ftp) {
            enqueueUpload({path});
            return;
        }
        if (m_ftp) {
            beginTransferBatch();
            m_activeTransfer = {QString(), path, false, true, 0};
            m_transferActive = true;
            setTransferUi(true);
            m_transferCurrentName = QFileInfo(path).fileName();
            m_progressLabel->setText(tr("Uploading folder %1...").arg(m_transferCurrentName));
            m_progressLabel->show();
            m_progressBar->setRange(0, 0);
            m_progressBar->show();
        }
        m_statusLabel->setText(tr("Uploading folder %1...").arg(QFileInfo(path).fileName()));
        emit requestUploadDir(path, m_currentPath);
    }
}

void SftpSidebar::onNewFolderClicked() {
    if (!m_isConnected || m_transferActive)
        return;
    bool ok = false;
    QString name = QInputDialog::getText(this, tr("New Folder"), tr("Folder name:"), QLineEdit::Normal, "", &ok);
    if (!ok || name.trimmed().isEmpty())
        return;

    QString remotePath = m_currentPath;
    if (!remotePath.endsWith("/"))
        remotePath += "/";
    remotePath += name.trimmed();

    m_statusLabel->setText(tr("Creating folder %1...").arg(name.trimmed()));
    emit requestCreateDir(remotePath);
}

void SftpSidebar::onRenameClicked() {
    if (!m_isConnected || m_transferActive)
        return;
    auto* item = m_treeWidget->currentItem();
    if (!item)
        return;

    QString special = item->data(0, Qt::UserRole + 1).toString();
    if (special == "." || special == "..")
        return;

    QString oldName = item->text(0);
    bool ok = false;
    QString newName = QInputDialog::getText(this, tr("Rename"), tr("New name:"), QLineEdit::Normal, oldName, &ok);
    if (!ok || newName.trimmed().isEmpty() || newName.trimmed() == oldName)
        return;

    QString base = m_currentPath;
    if (!base.endsWith("/"))
        base += "/";

    m_statusLabel->setText(tr("Renaming %1...").arg(oldName));
    emit requestRename(base + oldName, base + newName.trimmed());
}

void SftpSidebar::onChmodClicked() {
    if (!m_isConnected || m_transferActive)
        return;
    auto* item = m_treeWidget->currentItem();
    if (!item)
        return;

    QString special = item->data(0, Qt::UserRole + 1).toString();
    if (special == "." || special == "..")
        return;

    const QString name = item->text(0);
    QDialog dialog(this);
    dialog.setObjectName("sftpUtilityDialog");
    dialog.setWindowTitle(tr("Permissions — %1").arg(name));
    dialog.setMinimumWidth(440);

    auto* layout = new QVBoxLayout(&dialog);
    layout->setContentsMargins(20, 18, 20, 16);
    layout->setSpacing(12);

    auto* description = new QLabel(tr("Choose what each group can do:"), &dialog);
    layout->addWidget(description);

    auto* accessGroup = new QGroupBox(tr("File access"), &dialog);
    auto* accessGrid = new QGridLayout(accessGroup);
    accessGrid->setContentsMargins(12, 16, 12, 12);
    accessGrid->setHorizontalSpacing(20);
    accessGrid->setVerticalSpacing(8);
    accessGrid->addWidget(new QLabel(QString(), accessGroup), 0, 0);
    const QStringList roles = {tr("Owner"), tr("Group"), tr("Others")};
    for (int column = 0; column < roles.size(); ++column) {
        auto* roleLabel = new QLabel(roles.at(column), accessGroup);
        roleLabel->setAlignment(Qt::AlignCenter);
        accessGrid->addWidget(roleLabel, 0, column + 1);
    }

    const QStringList accessNames = {tr("Read"), tr("Write"), tr("Execute")};
    const int accessBits[3][3] = {{0400, 0040, 0004}, {0200, 0020, 0002}, {0100, 0010, 0001}};
    QList<QCheckBox*> modeChecks;
    QList<int> modeBits;
    constexpr int initialMode = 0755;
    for (int row = 0; row < accessNames.size(); ++row) {
        accessGrid->addWidget(new QLabel(accessNames.at(row), accessGroup), row + 1, 0);
        for (int column = 0; column < roles.size(); ++column) {
            auto* check = new QCheckBox(accessGroup);
            check->setAccessibleName(QStringLiteral("%1: %2").arg(roles.at(column), accessNames.at(row)));
            check->setToolTip(
                tr("Allow %1 to %2 this item.").arg(roles.at(column).toLower(), accessNames.at(row).toLower()));
            check->setChecked((initialMode & accessBits[row][column]) != 0);
            accessGrid->addWidget(check, row + 1, column + 1, Qt::AlignCenter);
            modeChecks.append(check);
            modeBits.append(accessBits[row][column]);
        }
    }
    layout->addWidget(accessGroup);

    auto* specialGroup = new QGroupBox(tr("Special permissions"), &dialog);
    auto* specialLayout = new QHBoxLayout(specialGroup);
    specialLayout->setContentsMargins(12, 16, 12, 12);
    const QList<QPair<QString, int>> specialModes = {
        {tr("Set user ID"), 04000}, {tr("Set group ID"), 02000}, {tr("Sticky bit"), 01000}};
    for (const auto& specialMode : specialModes) {
        auto* check = new QCheckBox(specialMode.first, specialGroup);
        check->setToolTip(tr("Enable the %1 permission bit.").arg(specialMode.first));
        specialLayout->addWidget(check);
        modeChecks.append(check);
        modeBits.append(specialMode.second);
    }
    layout->addWidget(specialGroup);

    auto* previewRow = new QHBoxLayout();
    auto* modePreview = new QLabel(&dialog);
    modePreview->setStyleSheet(QStringLiteral("font-weight: 600;"));
    auto* symbolicPreview = new QLabel(&dialog);
    symbolicPreview->setObjectName("permissionSymbolicPreview");
    previewRow->addWidget(modePreview);
    previewRow->addStretch();
    previewRow->addWidget(symbolicPreview);
    layout->addLayout(previewRow);

    auto updatePreview = [modeChecks, modeBits, modePreview, symbolicPreview]() {
        int mode = 0;
        for (int i = 0; i < modeChecks.size(); ++i) {
            if (modeChecks.at(i)->isChecked())
                mode |= modeBits.at(i);
        }
        QString symbolic;
        const int bits[3][3] = {{0400, 0200, 0100}, {0040, 0020, 0010}, {0004, 0002, 0001}};
        const QChar symbols[3] = {QChar('r'), QChar('w'), QChar('x')};
        for (int group = 0; group < 3; ++group) {
            for (int permission = 0; permission < 3; ++permission) {
                if (permission != 2) {
                    symbolic.append((mode & bits[group][permission]) ? symbols[permission] : QChar('-'));
                    continue;
                }

                const bool executable = (mode & bits[group][permission]) != 0;
                const int specialBit = group == 0 ? 04000 : (group == 1 ? 02000 : 01000);
                if (mode & specialBit)
                    symbolic.append(group == 2 ? (executable ? QChar('t') : QChar('T'))
                                               : (executable ? QChar('s') : QChar('S')));
                else
                    symbolic.append(executable ? QChar('x') : QChar('-'));
            }
        }
        modePreview->setText(QObject::tr("Mode: %1").arg(mode, 4, 8, QChar('0')));
        symbolicPreview->setText(symbolic);
    };
    for (QCheckBox* check : modeChecks)
        connect(check, &QCheckBox::toggled, &dialog, [updatePreview](bool) { updatePreview(); });
    updatePreview();

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    buttons->button(QDialogButtonBox::Ok)->setText(tr("Apply"));
    buttons->button(QDialogButtonBox::Ok)->setDefault(true);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    layout->addWidget(buttons);

    if (dialog.exec() != QDialog::Accepted)
        return;

    int mode = 0;
    for (int i = 0; i < modeChecks.size(); ++i) {
        if (modeChecks.at(i)->isChecked())
            mode |= modeBits.at(i);
    }

    QString base = m_currentPath;
    if (!base.endsWith("/"))
        base += "/";

    m_statusLabel->setText(tr("Changing permissions of %1...").arg(name));
    emit requestChmod(base + name, mode);
}

void SftpSidebar::navigateLocalTo(const QString& path) {
    const QFileInfo info(QDir::fromNativeSeparators(path));
    if (info.isDir()) {
        if (!m_localWatcher->directories().isEmpty())
            m_localWatcher->removePaths(m_localWatcher->directories());
        m_localPath = QDir::cleanPath(QDir::fromNativeSeparators(info.absoluteFilePath()));
        m_localWatcher->addPath(m_localPath);
        m_localTree->clear();
        refreshLocalFiles();
    }
    m_localPathEdit->setText(QDir::toNativeSeparators(m_localPath));
}

void SftpSidebar::refreshLocalFiles() {
    QStringList selectedPaths;
    for (QTreeWidgetItem* item : m_localTree->selectedItems())
        selectedPaths.append(item->data(0, Qt::UserRole + 2).toString());
    const int scroll = m_localTree->verticalScrollBar()->value();
    QList<SftpFile> files;
    const QDir dir(m_localPath);
    const QFileInfoList entries = dir.entryInfoList(QDir::AllEntries | QDir::NoDotAndDotDot | QDir::System,
                                                    QDir::DirsFirst | QDir::Name | QDir::IgnoreCase);
    for (const QFileInfo& entry : entries)
        files.append({entry.fileName(), entry.isDir(), entry.size(), entry.lastModified()});
    populateFileTree(m_localTree, m_localPath, files, !dir.isRoot());
    for (int i = 0; i < m_localTree->topLevelItemCount(); ++i) {
        auto* item = m_localTree->topLevelItem(i);
        const QString path = item->data(0, Qt::UserRole + 2).toString();
        if (!path.isEmpty() && selectedPaths.contains(path))
            item->setSelected(true);
    }
    m_localTree->verticalScrollBar()->setValue(scroll);
}

static bool validLocalName(const QString& name) {
    return !name.isEmpty() && name != "." && name != ".." && !name.contains('/') && !name.contains('\\');
}

void SftpSidebar::onLocalNewFolderClicked() {
    bool ok = false;
    const QString name =
        QInputDialog::getText(this, tr("New Folder"), tr("Folder name:"), QLineEdit::Normal, QString(), &ok).trimmed();
    if (!ok)
        return;
    if (!validLocalName(name) || !QDir(m_localPath).mkdir(name))
        QMessageBox::warning(this, tr("New Folder"), tr("Could not create the local folder."));
    else
        refreshLocalFiles();
}

void SftpSidebar::onLocalRenameClicked() {
    const auto items = m_localTree->selectedItems();
    if (items.size() != 1 || !items.first()->data(0, Qt::UserRole + 1).toString().isEmpty())
        return;
    const QFileInfo info(items.first()->data(0, Qt::UserRole + 2).toString());
    bool ok = false;
    const QString name =
        QInputDialog::getText(this, tr("Rename"), tr("New name:"), QLineEdit::Normal, info.fileName(), &ok).trimmed();
    if (!ok || name == info.fileName())
        return;
    if (!validLocalName(name) || !QDir(info.absolutePath()).rename(info.fileName(), name))
        QMessageBox::warning(this, tr("Rename"), tr("Could not rename the local item."));
    else
        refreshLocalFiles();
}

void SftpSidebar::showLocalContextMenu(const QPoint& pos) {
    auto* item = m_localTree->itemAt(pos);
    if (item && !item->isSelected())
        m_localTree->setCurrentItem(item);
    QMenu menu(this);
    auto* download = menu.addAction(downloadIcon(), tr("Download selection to local folder"), this,
                                    &SftpSidebar::onDownloadToLocalClicked);
    download->setEnabled(m_isConnected && !m_directoryDownloadActive && !selectedRemotePaths().isEmpty());
    menu.addSeparator();
    menu.addAction(QIcon(":/icons/new-folder.svg"), tr("New Folder"), this, &SftpSidebar::onLocalNewFolderClicked);
    auto* rename = menu.addAction(editIcon(), tr("Rename"), this, &SftpSidebar::onLocalRenameClicked);
    rename->setEnabled(item && item->data(0, Qt::UserRole + 1).toString().isEmpty() &&
                       m_localTree->selectedItems().size() == 1);
    menu.exec(m_localTree->viewport()->mapToGlobal(pos));
}

QStringList SftpSidebar::selectedRemotePaths() const {
    QStringList paths;
    for (QTreeWidgetItem* item : m_treeWidget->selectedItems()) {
        const QString special = item->data(0, Qt::UserRole + 1).toString();
        if (special == "." || special == "..")
            continue;
        QString path = item->data(0, Qt::UserRole + 2).toString();
        if (path.isEmpty())
            path = (m_currentPath.endsWith('/') ? m_currentPath : m_currentPath + '/') + item->text(0);
        paths.append(path);
    }
    return paths;
}

void SftpSidebar::onDownloadClicked() {
    const QStringList paths = selectedRemotePaths();
    if (!paths.isEmpty())
        enqueueDownload(paths);
}

void SftpSidebar::onDownloadToLocalClicked() {
    enqueueDownloadTo(selectedRemotePaths(), m_localPath);
}

void SftpSidebar::onDeleteClicked() {
    if (m_transferActive)
        return;
    auto* item = m_treeWidget->currentItem();
    if (!item)
        return;

    QString name = item->text(0);
    bool isDir = item->data(0, Qt::UserRole).toBool();
    QString remotePath = m_currentPath;
    if (!remotePath.endsWith("/"))
        remotePath += "/";
    remotePath += name;

    auto result = localizedQuestion(this, tr("Delete File"), tr("Are you sure you want to delete '%1'?").arg(name),
                                    QMessageBox::Yes | QMessageBox::No);
    if (result == QMessageBox::Yes) {
        m_statusLabel->setText(tr("Deleting %1...").arg(name));
        emit requestDelete(remotePath, isDir);
    }
}

void SftpSidebar::onItemDoubleClicked(QTreeWidgetItem* item, int column) {
    Q_UNUSED(column);
    if (!item)
        return;

    QString name = item->text(0);
    QString special = item->data(0, Qt::UserRole + 1).toString();
    bool isDir = item->data(0, Qt::UserRole).toBool();

    if (special == ".") {
        onRefreshClicked();
        return;
    }
    if (special == "..") {
        onParentDirClicked();
        return;
    }

    if (isDir) {
        if (m_currentPath == "/") {
            m_currentPath = "/" + name;
        } else if (m_currentPath == ".") {
            m_currentPath = name;
        } else {
            if (!m_currentPath.endsWith("/"))
                m_currentPath += "/";
            m_currentPath += name;
        }
        emit requestNavigate(m_currentPath);
    } else {
        QString lowerName = name.toLower();
        if (lowerName.endsWith(".txt") || lowerName.endsWith(".py") || lowerName.endsWith(".sh") ||
            lowerName.endsWith(".json") || lowerName.endsWith(".cpp") || lowerName.endsWith(".h") ||
            lowerName.endsWith(".html") || lowerName.endsWith(".css") || lowerName.endsWith(".conf") ||
            lowerName.endsWith(".xml") || lowerName.endsWith(".ini") || lowerName.endsWith(".yml") ||
            lowerName.endsWith(".yaml") || lowerName.endsWith(".md")) {
            onEditClicked();
        } else {
            onDownloadClicked();
        }
    }
}

void SftpSidebar::showContextMenu(const QPoint& pos) {
    auto* item = m_treeWidget->itemAt(pos);
    if (!item)
        return;

    QString special = item->data(0, Qt::UserRole + 1).toString();

    QMenu menu(this);
    bool isDir = item->data(0, Qt::UserRole).toBool();

    QAction* editAct = nullptr;
    if (!isDir)
        editAct = menu.addAction(QIcon(":/icons/edit.svg"), tr("Edit"));
    auto* downloadAct = menu.addAction(downloadIcon(), tr("Download"));
    auto* renameAct = menu.addAction(QIcon(":/icons/edit.svg"), tr("Rename"));
    auto* chmodAct = menu.addAction(QIcon(":/icons/edit.svg"), tr("Permissions"));
    auto* deleteAct = menu.addAction(QIcon(":/icons/delete.svg"), tr("Delete"));
    menu.addSeparator();
    auto* newFolderAct = menu.addAction(QIcon(":/icons/folder.svg"), tr("New Folder"));
    auto* refreshAct = menu.addAction(QIcon(":/icons/refresh.svg"), tr("Refresh"));

    if (special == "." || special == "..") {
        if (editAct)
            editAct->setEnabled(false);
        if (downloadAct)
            downloadAct->setEnabled(false);
        deleteAct->setEnabled(false);
        renameAct->setEnabled(false);
        chmodAct->setEnabled(false);
    }

    auto* selected = menu.exec(m_treeWidget->mapToGlobal(pos));
    if (selected == editAct) {
        onEditClicked();
    } else if (selected == downloadAct) {
        onDownloadClicked();
    } else if (selected == deleteAct) {
        if (!m_transferActive)
            onDeleteClicked();
    } else if (selected == renameAct) {
        if (!m_transferActive)
            onRenameClicked();
    } else if (selected == chmodAct) {
        if (!m_transferActive)
            onChmodClicked();
    } else if (selected == newFolderAct) {
        if (!m_transferActive)
            onNewFolderClicked();
    } else if (selected == refreshAct) {
        onRefreshClicked();
    }
}

void SftpSidebar::updatePath(const QString& path) {
    m_currentPath = path;
    m_pathEdit->setText(path);
}

void SftpSidebar::onEditClicked() {
    if (m_transferActive)
        return;
    auto* item = m_treeWidget->currentItem();
    if (!item)
        return;

    bool isDir = item->data(0, Qt::UserRole).toBool();
    if (isDir)
        return;

    QString name = item->text(0);
    QString remotePath = m_currentPath;
    if (!remotePath.endsWith("/"))
        remotePath += "/";
    remotePath += name;

    m_pendingEditRemotePath = remotePath;
    m_pendingEditLocalPath = QDir::tempPath() + "/banchoxterm_" + name;

    m_statusLabel->setText(tr("Downloading %1 for editing...").arg(name));
    emit requestDownload(m_pendingEditRemotePath, m_pendingEditLocalPath);
}

void SftpSidebar::setTransferUi(bool active) {
    m_downloadToLocalBtn->setEnabled(m_isConnected && !m_directoryDownloadActive && !selectedRemotePaths().isEmpty());
    m_compareBtn->setEnabled(m_isConnected && !m_directoryDownloadActive);
    m_treeWidget->setEnabled(m_isConnected && !m_directoryDownloadActive);
    m_pathEdit->setEnabled(m_isConnected && !m_directoryDownloadActive);
    m_upBtn->setEnabled(m_isConnected && !m_directoryDownloadActive);
    m_refreshBtn->setEnabled(m_isConnected && !m_directoryDownloadActive);
    bool enabled = !active && m_isConnected;
    m_uploadBtn->setEnabled(enabled);
    m_uploadDirBtn->setEnabled(enabled);
    m_newFolderBtn->setEnabled(enabled);
    m_renameBtn->setEnabled(enabled);
    m_chmodBtn->setEnabled(enabled);
    const bool canCancelActive = active && (!m_ftp.isNull() || !m_connection.isNull());
    m_cancelTransferBtn->setText(m_transferQueue.isEmpty() ? tr("Cancel transfer") : tr("Cancel transfers"));
    m_cancelTransferBtn->setEnabled(canCancelActive || (active && !m_transferQueue.isEmpty()));
    m_cancelTransferBtn->setVisible(canCancelActive || (active && !m_transferQueue.isEmpty()));
    const bool canPause = active && (!m_ftp.isNull() || !m_connection.isNull());
    m_pauseTransferBtn->setText(m_transferPaused ? tr("Resume transfer") : tr("Pause transfer"));
    m_pauseTransferBtn->setEnabled(canPause);
    m_pauseTransferBtn->setVisible(canPause);
}

void SftpSidebar::onToggleTransferPause() {
    if (!m_transferActive || (m_ftp.isNull() && m_connection.isNull()))
        return;

    if (m_transferPaused) {
        if (m_ftp)
            QMetaObject::invokeMethod(m_ftp, "resumeTransfer", Qt::DirectConnection);
        else if (!m_parallelTransfers.isEmpty()) {
            for (const ParallelTransfer& transfer : std::as_const(m_parallelTransfers))
                QMetaObject::invokeMethod(transfer.worker, "resume", Qt::DirectConnection);
        } else
            QMetaObject::invokeMethod(m_connection, "resumeTransfer", Qt::DirectConnection);
        m_transferPaused = false;
        m_statusLabel->setText(tr("Resuming transfer..."));
    } else {
        if (m_ftp)
            QMetaObject::invokeMethod(m_ftp, "pauseTransfer", Qt::DirectConnection);
        else if (!m_parallelTransfers.isEmpty()) {
            for (const ParallelTransfer& transfer : std::as_const(m_parallelTransfers))
                QMetaObject::invokeMethod(transfer.worker, "pause", Qt::DirectConnection);
        } else
            QMetaObject::invokeMethod(m_connection, "pauseTransfer", Qt::DirectConnection);
        m_transferPaused = true;
        m_statusLabel->setText(tr("Transfer paused."));
    }
    m_pauseTransferBtn->setText(m_transferPaused ? tr("Resume transfer") : tr("Pause transfer"));
}

void SftpSidebar::onCancelQueuedTransfers() {
    m_conflict->cancelled = true;
    if (m_directoryDownloadActive) {
        m_directoryDownloadCancelled = true;
        m_transferQueue.clear();
        m_statusLabel->setText(tr("Cancelling transfer..."));
        return;
    }
    const bool cancelActive = m_transferActive && (!m_ftp.isNull() || !m_connection.isNull());
    if (m_transferQueue.isEmpty() && !cancelActive)
        return;

    const int cancelled = m_transferQueue.size();
    m_transferQueue.clear();
    if (cancelActive) {
        if (m_ftp)
            QMetaObject::invokeMethod(m_ftp, "cancelTransfer", Qt::DirectConnection);
        else if (!m_parallelTransfers.isEmpty()) {
            for (const ParallelTransfer& transfer : std::as_const(m_parallelTransfers))
                QMetaObject::invokeMethod(transfer.worker, "cancel", Qt::DirectConnection);
        } else
            QMetaObject::invokeMethod(m_connection, "cancelTransfer", Qt::DirectConnection);
    }
    m_cancelTransferBtn->setEnabled(false);
    m_cancelTransferBtn->hide();
    m_statusLabel->setText(cancelActive
                               ? tr("Cancelling active transfer; %1 queued transfer(s) removed.").arg(cancelled)
                               : tr("Cancelled %1 queued transfer(s).").arg(cancelled));
}

void SftpSidebar::onWatchedFileChanged(const QString& path) {
    if (m_watchedFiles.contains(path)) {
        QString remotePath = m_watchedFiles[path];
        m_statusLabel->setText(tr("Auto-uploading changes to %1...").arg(QFileInfo(remotePath).fileName()));
        emit requestUpload(path, remotePath);
    }
}

void SftpSidebar::startParallelTransfers() {
    if (m_directoryDownloadActive)
        return;
    if (m_currentSession.type != SessionType::SSH)
        return startNextTransfer();

    while (!m_transferQueue.isEmpty() && m_parallelTransfers.size() < m_maxParallelTransfers) {
        // Never open the same destination concurrently: the next file must see
        // the completed destination before deciding whether to overwrite it.
        const auto& next = m_transferQueue.first();
        bool destinationBusy = false;
        for (const auto& active : std::as_const(m_parallelTransfers)) {
            if (next.isUpload == active.item.isUpload &&
                (next.isUpload ? next.remotePath == active.item.remotePath : next.localPath == active.item.localPath)) {
                destinationBusy = true;
                break;
            }
        }
        if (destinationBusy)
            break;

        if (m_transferQueue.first().isDirDownload) {
            // Directory listings use the browser connection, after existing workers finish.
            if (!m_parallelTransfers.isEmpty())
                break;
            m_activeTransfer = m_transferQueue.takeFirst();
            startDirectoryDownload();
            return;
        }
        const TransferItem item = m_transferQueue.takeFirst();
        SftpTransferRequest request;
        request.conflict = m_conflict;
        request.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
        request.remotePath = item.remotePath.isEmpty() ? m_currentPath : item.remotePath;
        request.localPath = item.localPath;
        request.isUpload = item.isUpload;
        request.isDirUpload = item.isDirUpload;

        auto* thread = new QThread(this);
        auto* worker = new SftpTransferWorker(m_currentSession, request);
        worker->moveToThread(thread);

        ParallelTransfer active;
        active.id = request.id;
        active.item = item;
        active.thread = thread;
        active.worker = worker;
        m_parallelTransfers.append(active);

        connect(thread, &QThread::started, worker, &SftpTransferWorker::start);
        connect(worker, &SftpTransferWorker::progress, this,
                [this](const QString&, const QString& fileName, qint64 done, qint64 total) {
                    onTransferProgress(fileName, done, total);
                });
        connect(worker, &SftpTransferWorker::finished, this,
                [this](const QString& id, bool success, const QString& error) {
                    finishParallelTransfer(id, success, error);
                });
        connect(thread, &QThread::finished, worker, &QObject::deleteLater);
        connect(thread, &QThread::finished, thread, &QObject::deleteLater);
        thread->start();
    }

    if (m_parallelTransfers.isEmpty() && m_transferQueue.isEmpty()) {
        m_transferActive = false;
        setTransferUi(false);
        m_progressBar->hide();
        m_progressLabel->hide();
        m_statusLabel->setText(m_conflict->cancelled ? tr("Transfer cancelled.") : tr("All transfers finished."));
        emit requestList(m_currentPath);
        return;
    }

    m_transferActive = true;
    setTransferUi(true);
    m_progressBar->setRange(0, 0);
    m_progressBar->show();
    m_progressLabel->setText(tr("Transferring %1 file(s) in parallel...").arg(m_parallelTransfers.size()));
    m_progressLabel->show();
    m_statusLabel->setText(
        tr("%1 transfer(s) active, %2 queued.").arg(m_parallelTransfers.size()).arg(m_transferQueue.size()));
}

void SftpSidebar::finishParallelTransfer(const QString& id, bool success, const QString& error) {
    if (error.startsWith(QStringLiteral("Transfer cancelled")) || m_conflict->cancelled) {
        m_transferQueue.clear();
        for (const auto& transfer : std::as_const(m_parallelTransfers))
            if (transfer.id != id)
                QMetaObject::invokeMethod(transfer.worker, "cancel", Qt::DirectConnection);
    }

    refreshLocalFiles();
    int index = -1;
    for (int i = 0; i < m_parallelTransfers.size(); ++i) {
        if (m_parallelTransfers.at(i).id == id) {
            index = i;
            break;
        }
    }
    if (index < 0)
        return;

    const TransferItem item = m_parallelTransfers.at(index).item;
    if (m_parallelTransfers.at(index).thread)
        m_parallelTransfers.at(index).thread->quit();
    m_parallelTransfers.removeAt(index);

    if (!success && !m_conflict->cancelled && !error.startsWith(QStringLiteral("Transfer cancelled")) &&
        item.attempts < 2 && m_isConnected) {
        TransferItem retryItem = item;
        ++retryItem.attempts;
        m_transferQueue.prepend(retryItem);
        m_statusLabel->setText(tr("Transfer failed; retrying (%1/2)...").arg(retryItem.attempts));
    } else if (!success && !error.startsWith(QStringLiteral("Transfer cancelled"))) {
        emit diagnosticMessage(tr("Parallel transfer failed: %1").arg(error));
        m_statusLabel->setText(error);
    }

    startParallelTransfers();
}

void SftpSidebar::stopParallelTransfers() {
    m_conflict->cancelled = true;
    m_transferQueue.clear();
    m_directoryDownloadActive = false;
    m_directoryDownloadCancelled = false;
    for (const ParallelTransfer& transfer : std::as_const(m_parallelTransfers)) {
        if (transfer.worker)
            QMetaObject::invokeMethod(transfer.worker, "cancel", Qt::DirectConnection);
        if (transfer.thread)
            transfer.thread->quit();
    }
    m_parallelTransfers.clear();
    m_transferActive = false;
}

void SftpSidebar::beginTransferBatch() {
    if (!hasActiveTransfers())
        m_conflict = std::make_shared<TransferConflict>();
    if (m_connection)
        m_connection->transferConflict = m_conflict;
    if (m_ftp)
        m_ftp->transferConflict = m_conflict;
}

void SftpSidebar::enqueueUpload(const QStringList& localPaths) {
    if (!m_isConnected || m_directoryDownloadActive)
        return;

    const bool parallel = m_currentSession.type == SessionType::SSH;

    beginTransferBatch();
    for (const QString& localPath : localPaths) {
        QFileInfo info(localPath);
        if (info.isDir()) {
            if (m_ftp)
                continue; // FTP has no recursive folder upload
            if (!m_transferActive && !parallel) {
                m_activeTransfer = {QString(), localPath, false, true, 0};
                m_transferActive = true;
                setTransferUi(true);
                m_transferCurrentName = info.fileName();
                m_progressLabel->setText(tr("Uploading folder %1...").arg(info.fileName()));
                m_progressLabel->show();
                m_progressBar->setRange(0, 0);
                m_progressBar->show();
                m_statusLabel->setText(tr("Uploading folder %1...").arg(info.fileName()));
                emit requestUploadDir(localPath, m_currentPath);
            } else {
                m_transferQueue.append({m_currentPath, localPath, false, true});
                m_cancelTransferBtn->setEnabled(true);
                m_cancelTransferBtn->show();
            }
            continue;
        }
        QString remotePath = m_currentPath;
        if (!remotePath.endsWith("/"))
            remotePath += "/";
        remotePath += info.fileName();

        if (!m_transferActive && !parallel) {
            m_activeTransfer = {remotePath, localPath, true, false, 0};
            m_transferActive = true;
            setTransferUi(true);
            m_transferCurrentName = info.fileName();
            m_progressLabel->setText(tr("Uploading %1...").arg(info.fileName()));
            m_progressLabel->show();
            m_progressBar->setRange(0, m_ftp ? 0 : 100);
            m_progressBar->setValue(0);
            m_progressBar->show();
            m_statusLabel->setText(tr("Uploading %1...").arg(info.fileName()));
            emit requestUpload(localPath, remotePath);
        } else {
            m_transferQueue.append({remotePath, localPath, true, false});
            m_cancelTransferBtn->setEnabled(true);
            m_cancelTransferBtn->show();
        }
    }

    if (parallel)
        startParallelTransfers();
}

void SftpSidebar::enqueueDownload(const QStringList& remotePaths) {
    enqueueDownloadTo(remotePaths, m_localPath);
}

void SftpSidebar::enqueueDownloadTo(const QStringList& remotePaths, const QString& destinationDir) {
    if (!m_isConnected || m_directoryDownloadActive || destinationDir.isEmpty() || !QFileInfo(destinationDir).isDir())
        return;

    const bool parallel = m_currentSession.type == SessionType::SSH;

    beginTransferBatch();
    int added = 0;
    for (const QString& remotePath : remotePaths) {
        QString fileName = QFileInfo(remotePath).fileName();
        if (fileName.isEmpty())
            fileName = remotePath.section('/', -1);
        if (fileName.isEmpty() || fileName == "." || fileName == ".." || fileName.contains('\\'))
            continue;
        QString localPath = QDir(destinationDir).filePath(fileName);
        bool isDirectory = false;
        for (const SftpFile& file : std::as_const(m_currentRemoteFiles)) {
            if (file.name == fileName) {
                isDirectory = file.isDirectory;
                break;
            }
        }
        TransferItem item{remotePath, localPath, false};
        item.isDirDownload = isDirectory;
        m_transferQueue.append(item);
        ++added;
    }

    if (added > 1 && m_progressLabel->isVisible())
        m_progressLabel->setText(tr("Downloading %1... (%2 queued)").arg(m_transferCurrentName).arg(added - 1));

    if (added > 0) {
        if (parallel)
            startParallelTransfers();
        else if (!m_transferActive) {
            m_transferActive = true;
            setTransferUi(true);
            m_progressLabel->show();
            m_progressBar->show();
            startNextTransfer();
        }
    }
}

void SftpSidebar::startDirectoryDownload() {
    m_transferActive = true;
    m_directoryDownloadActive = true;
    m_directoryDownloadCancelled = false;
    setTransferUi(true);
    m_pauseTransferBtn->hide();
    m_statusLabel->setText(tr("Downloading %1...").arg(QFileInfo(m_activeTransfer.remotePath).fileName()));
    m_progressLabel->setText(m_statusLabel->text());
    m_progressLabel->show();
    m_progressBar->setRange(0, 0);
    m_progressBar->show();
    m_cancelTransferBtn->setEnabled(true);
    m_cancelTransferBtn->show();
    if (!QDir().mkpath(m_activeTransfer.localPath)) {
        onOperationFinished(false, tr("Could not create the local folder."));
        return;
    }
    emit requestList(m_activeTransfer.remotePath);
}

void SftpSidebar::startNextTransfer() {
    if (m_transferQueue.isEmpty()) {
        m_transferActive = false;
        setTransferUi(false);
        m_progressBar->hide();
        m_progressLabel->hide();
        m_cancelTransferBtn->setEnabled(false);
        m_cancelTransferBtn->hide();
        m_statusLabel->setText(m_conflict->cancelled ? tr("Transfer cancelled.") : tr("All transfers finished."));
        emit requestList(m_currentPath);
        return;
    }

    TransferItem item = m_transferQueue.takeFirst();
    m_activeTransfer = item;
    if (item.isDirDownload) {
        startDirectoryDownload();
        return;
    }
    setTransferUi(true);
    m_transferCurrentName = QFileInfo(item.isUpload ? item.localPath : item.remotePath).fileName();
    m_progressBar->setRange(0, (item.isDirUpload || m_ftp) ? 0 : 100);
    m_progressBar->setValue(0);
    const QString actionText = item.isDirUpload ? tr("Uploading folder %1...").arg(m_transferCurrentName)
                                                : (item.isUpload ? tr("Uploading %1...").arg(m_transferCurrentName)
                                                                 : tr("Downloading %1...").arg(m_transferCurrentName));
    m_progressLabel->setText(actionText);
    m_statusLabel->setText(actionText);

    if (item.isDirUpload)
        emit requestUploadDir(item.localPath, m_currentPath);
    else if (item.isUpload)
        emit requestUpload(item.localPath, item.remotePath);
    else
        emit requestDownload(item.remotePath, item.localPath);
}

void SftpSidebar::finishTransferQueue(bool success, const QString& error) {
    if (!success) {
        if (error.startsWith(QStringLiteral("Transfer cancelled"))) {
            m_transferQueue.clear();
            m_transferActive = false;
            setTransferUi(false);
            m_progressBar->hide();
            m_progressLabel->hide();
            m_cancelTransferBtn->setEnabled(false);
            m_cancelTransferBtn->hide();
            m_pauseTransferBtn->setEnabled(false);
            m_pauseTransferBtn->hide();
            m_transferPaused = false;
            m_statusLabel->setText(tr("Transfer cancelled."));
            return;
        }
        constexpr int maxRetries = 2;
        if (m_activeTransfer.attempts < maxRetries && m_isConnected) {
            ++m_activeTransfer.attempts;
            m_transferQueue.prepend(m_activeTransfer);
            m_statusLabel->setText(
                tr("Transfer failed; retrying (%1/%2)...").arg(m_activeTransfer.attempts).arg(maxRetries));
            startNextTransfer();
            return;
        }
        m_transferQueue.clear();
        m_transferActive = false;
        setTransferUi(false);
        m_progressBar->hide();
        m_progressLabel->hide();
        m_cancelTransferBtn->setEnabled(false);
        m_cancelTransferBtn->hide();
        m_pauseTransferBtn->setEnabled(false);
        m_pauseTransferBtn->hide();
        m_transferPaused = false;
        m_statusLabel->setText(error);
        QMessageBox::critical(this, tr("SFTP Transfer Error"), error);
        return;
    }

    startNextTransfer();
}

void SftpSidebar::onTransferProgress(const QString& fileName, qint64 bytesDone, qint64 totalBytes) {
    m_transferCurrentName = fileName;
    if (totalBytes > 0) {
        m_progressBar->setRange(0, 100);
        m_progressBar->setValue(static_cast<int>((bytesDone * 100) / totalBytes));
    } else {
        m_progressBar->setRange(0, 0);
    }
    if (m_progressBar->isVisible()) {
        const QString totalText = totalBytes > 0 ? formatBytes(totalBytes) : tr("? bytes");
        m_progressLabel->setProgress(fileName, tr("%1 / %2").arg(formatBytes(bytesDone), totalText));
    }
}

bool SftpSidebar::eventFilter(QObject* watched, QEvent* event) {
    if (watched == m_treeWidget->viewport()) {
        if (event->type() == QEvent::DragEnter || event->type() == QEvent::DragMove) {
            auto* dragEvent = static_cast<QDropEvent*>(event);
            const QMimeData* mime = dragEvent->mimeData();
            if (mime->hasFormat("application/x-banchoxterm-sftp-remote")) {
                dragEvent->acceptProposedAction();
                return true;
            }
            if (mime->hasFormat("application/x-banchoxterm-sftp-local")) {
                dragEvent->acceptProposedAction();
                return true;
            }
            if (mime->hasUrls()) {
                bool localOnly = true;
                const QList<QUrl> urls = mime->urls();
                for (const QUrl& url : urls) {
                    if (!url.isLocalFile()) {
                        localOnly = false;
                        break;
                    }
                }
                if (localOnly && !urls.isEmpty()) {
                    dragEvent->acceptProposedAction();
                    return true;
                }
            }
        } else if (event->type() == QEvent::Drop) {
            auto* dropEvent = static_cast<QDropEvent*>(event);
            const QMimeData* mime = dropEvent->mimeData();
            if (mime->hasFormat("application/x-banchoxterm-sftp-local")) {
                const QByteArray payload = mime->data("application/x-banchoxterm-sftp-local");
                const QJsonArray arr = QJsonDocument::fromJson(payload).array();
                QStringList localPaths;
                for (const QJsonValue& value : arr) {
                    if (!value.toString().isEmpty())
                        localPaths.append(value.toString());
                }
                if (!localPaths.isEmpty())
                    enqueueUpload(localPaths);
                dropEvent->acceptProposedAction();
                return true;
            }
            if (mime->hasFormat("application/x-banchoxterm-sftp-remote")) {
                const QByteArray payload = mime->data("application/x-banchoxterm-sftp-remote");
                const QJsonArray arr = QJsonDocument::fromJson(payload).array();
                QStringList remotePaths;
                for (const QJsonValue& v : arr)
                    remotePaths.append(v.toString());
                if (!remotePaths.isEmpty())
                    enqueueDownload(remotePaths);
                dropEvent->acceptProposedAction();
                return true;
            }
            if (mime->hasUrls()) {
                QStringList localPaths;
                const QList<QUrl> urls = mime->urls();
                for (const QUrl& url : urls) {
                    if (url.isLocalFile())
                        localPaths.append(url.toLocalFile());
                }
                if (!localPaths.isEmpty())
                    enqueueUpload(localPaths);
                dropEvent->acceptProposedAction();
                return true;
            }
        }
    } else if (watched == m_localTree->viewport()) {
        if (event->type() == QEvent::DragEnter || event->type() == QEvent::DragMove) {
            auto* dragEvent = static_cast<QDropEvent*>(event);
            if (dragEvent->mimeData()->hasFormat("application/x-banchoxterm-sftp-remote")) {
                dragEvent->acceptProposedAction();
                return true;
            }
        } else if (event->type() == QEvent::Drop) {
            auto* dropEvent = static_cast<QDropEvent*>(event);
            const QMimeData* mime = dropEvent->mimeData();
            if (mime->hasFormat("application/x-banchoxterm-sftp-remote")) {
                const QByteArray payload = mime->data("application/x-banchoxterm-sftp-remote");
                const QJsonArray arr = QJsonDocument::fromJson(payload).array();
                QStringList remotePaths;
                for (const QJsonValue& value : arr) {
                    if (!value.toString().isEmpty())
                        remotePaths.append(value.toString());
                }

                QString destinationDir = m_localPath;
                auto* target = m_localTree->itemAt(dropEvent->position().toPoint());
                if (target && target->data(0, Qt::UserRole).toBool() &&
                    !target->data(0, Qt::UserRole + 2).toString().isEmpty())
                    destinationDir = target->data(0, Qt::UserRole + 2).toString();
                if (!remotePaths.isEmpty() && !destinationDir.isEmpty())
                    enqueueDownloadTo(remotePaths, destinationDir);
                dropEvent->acceptProposedAction();
                return true;
            }
        }
    }
    return QWidget::eventFilter(watched, event);
}
