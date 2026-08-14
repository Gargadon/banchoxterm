#include "sftpsidebar.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLineEdit>
#include <QPushButton>
#include <QTreeWidget>
#include <QTreeWidgetItem>
#include <QLabel>
#include <QProgressBar>
#include <QFileDialog>
#include <QInputDialog>
#include <QMessageBox>
#include <QMenu>
#include <QHeaderView>
#include <QIcon>
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
#include <QEvent>
#include <QDropEvent>
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

// Tree widget that starts a custom drag carrying the selected remote paths,
// so remote files can be dropped anywhere in the sidebar to download them.
class SftpTreeWidget : public QTreeWidget {
public:
    using QTreeWidget::QTreeWidget;

protected:
    void startDrag(Qt::DropActions supportedActions) override {
        Q_UNUSED(supportedActions);
        QList<QTreeWidgetItem*> items = selectedItems();
        if (items.isEmpty())
            return;

        QJsonArray paths;
        for (QTreeWidgetItem* item : items) {
            const QString remotePath = item->data(0, Qt::UserRole + 2).toString();
            if (!remotePath.isEmpty())
                paths.append(remotePath);
        }
        if (paths.isEmpty())
            return;

        auto* mime = new QMimeData;
        mime->setData("application/x-banchoxterm-sftp-remote", QJsonDocument(paths).toJson(QJsonDocument::Compact));

        auto* drag = new QDrag(this);
        drag->setMimeData(mime);
        drag->exec(Qt::CopyAction);
    }
};

SftpSidebar::SftpSidebar(QWidget* parent) : QWidget(parent) {
    auto* mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(10, 10, 10, 10);
    mainLayout->setSpacing(8);

    auto* titleLabel = new QLabel(tr("SFTP FILES"), this);
    titleLabel->setStyleSheet(
        "font-weight: bold; color: #787c99; font-size: 11px; letter-spacing: 1px; margin-bottom: 4px;");
    mainLayout->addWidget(titleLabel);

    auto* navLayout = new QHBoxLayout();
    navLayout->setSpacing(4);

    m_upBtn = new QPushButton(QIcon(":/icons/up.svg"), "", this);
    m_upBtn->setToolTip(tr("Go to parent directory"));
    m_upBtn->setFixedWidth(32);
    navLayout->addWidget(m_upBtn);

    m_pathEdit = new QLineEdit(this);
    m_pathEdit->setPlaceholderText("/");
    navLayout->addWidget(m_pathEdit);

    m_refreshBtn = new QPushButton(QIcon(":/icons/refresh.svg"), "", this);
    m_refreshBtn->setToolTip(tr("Refresh folder"));
    m_refreshBtn->setFixedWidth(32);
    navLayout->addWidget(m_refreshBtn);

    mainLayout->addLayout(navLayout);

    auto* toolsLayout = new QHBoxLayout();
    toolsLayout->setSpacing(6);

    m_uploadBtn = new QPushButton(QIcon(":/icons/upload.svg"), tr("Upload"), this);
    toolsLayout->addWidget(m_uploadBtn);

    m_uploadDirBtn = new QPushButton(QIcon(":/icons/folder.svg"), tr("Upload Folder"), this);
    toolsLayout->addWidget(m_uploadDirBtn);

    mainLayout->addLayout(toolsLayout);

    auto* fileOpsLayout = new QHBoxLayout();
    fileOpsLayout->setSpacing(6);

    m_newFolderBtn = new QPushButton(QIcon(":/icons/folder.svg"), tr("New Folder"), this);
    fileOpsLayout->addWidget(m_newFolderBtn);

    m_renameBtn = new QPushButton(QIcon(":/icons/edit.svg"), tr("Rename"), this);
    fileOpsLayout->addWidget(m_renameBtn);

    m_chmodBtn = new QPushButton(QIcon(":/icons/edit.svg"), tr("Permissions"), this);
    fileOpsLayout->addWidget(m_chmodBtn);

    mainLayout->addLayout(fileOpsLayout);

    m_treeWidget = new SftpTreeWidget(this);
    m_treeWidget->setHeaderLabels({tr("Name"), tr("Size"), tr("Modified")});
    m_treeWidget->setRootIsDecorated(false);
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
    mainLayout->addWidget(m_treeWidget);

    m_progressLabel = new QLabel(this);
    m_progressLabel->setStyleSheet("color: #7aa2f7; font-weight: bold;");
    m_progressLabel->hide();
    mainLayout->addWidget(m_progressLabel);

    m_progressBar = new QProgressBar(this);
    m_progressBar->setTextVisible(false);
    m_progressBar->setFixedHeight(6);
    m_progressBar->setRange(0, 100);
    m_progressBar->setValue(0);
    m_progressBar->hide();
    mainLayout->addWidget(m_progressBar);

    m_statusLabel = new QLabel(tr("Disconnected"), this);
    m_statusLabel->setStyleSheet("color: #787c99; font-style: italic;");
    mainLayout->addWidget(m_statusLabel);

    m_upBtn->setEnabled(false);
    m_pathEdit->setEnabled(false);
    m_refreshBtn->setEnabled(false);
    m_uploadBtn->setEnabled(false);
    m_uploadDirBtn->setEnabled(false);
    m_newFolderBtn->setEnabled(false);
    m_renameBtn->setEnabled(false);
    m_chmodBtn->setEnabled(false);
    m_treeWidget->setEnabled(false);

    m_fileWatcher = new QFileSystemWatcher(this);
    connect(m_fileWatcher, &QFileSystemWatcher::fileChanged, this, &SftpSidebar::onWatchedFileChanged);

    connect(m_upBtn, &QPushButton::clicked, this, &SftpSidebar::onParentDirClicked);
    connect(m_refreshBtn, &QPushButton::clicked, this, &SftpSidebar::onRefreshClicked);
    connect(m_uploadBtn, &QPushButton::clicked, this, &SftpSidebar::onUploadClicked);
    connect(m_uploadDirBtn, &QPushButton::clicked, this, &SftpSidebar::onUploadFolderClicked);
    connect(m_newFolderBtn, &QPushButton::clicked, this, &SftpSidebar::onNewFolderClicked);
    connect(m_renameBtn, &QPushButton::clicked, this, &SftpSidebar::onRenameClicked);
    connect(m_chmodBtn, &QPushButton::clicked, this, &SftpSidebar::onChmodClicked);
    connect(m_pathEdit, &QLineEdit::returnPressed, this, [this]() { emit requestList(m_pathEdit->text().trimmed()); });
    connect(m_treeWidget, &QTreeWidget::itemDoubleClicked, this, &SftpSidebar::onItemDoubleClicked);
    connect(m_treeWidget, &QTreeWidget::customContextMenuRequested, this, &SftpSidebar::showContextMenu);
}

SftpSidebar::~SftpSidebar() {
    detachConnection();
}

void SftpSidebar::setConnection(SshConnection* connection) {
    if (m_connection == connection)
        return;

    detachConnection();
    detachFtp();
    m_connection = connection;
    if (!m_connection)
        return;

    connect(this, &SftpSidebar::requestConnect, m_connection, &SshConnection::connectToHost);
    connect(this, &SftpSidebar::requestList, m_connection, &SshConnection::listDirectory);
    connect(this, &SftpSidebar::requestDownload, m_connection, &SshConnection::downloadFile);
    connect(this, &SftpSidebar::requestUpload, m_connection, &SshConnection::uploadFile);
    connect(this, &SftpSidebar::requestDelete, m_connection, &SshConnection::deleteFile);
    connect(this, &SftpSidebar::requestCreateDir, m_connection, &SshConnection::createDirectory);
    connect(this, &SftpSidebar::requestRename, m_connection, &SshConnection::renamePath);
    connect(this, &SftpSidebar::requestChmod, m_connection, &SshConnection::chmodPath);
    connect(this, &SftpSidebar::requestUploadDir, m_connection, &SshConnection::uploadDirectory);
    connect(this, &SftpSidebar::requestDisconnect, m_connection, &SshConnection::disconnectFromHost);

    connect(m_connection, &SshConnection::connectionSuccess, this, &SftpSidebar::onConnectionSuccess);
    connect(m_connection, &SshConnection::connectionFailed, this, &SftpSidebar::onConnectionFailed);
    connect(m_connection, &SshConnection::directoryListed, this, &SftpSidebar::onDirectoryListed);
    connect(m_connection, &SshConnection::operationFinished, this, &SftpSidebar::onOperationFinished);
    connect(m_connection, &SshConnection::transferProgress, this, &SftpSidebar::onTransferProgress);
    connect(m_connection, &SshConnection::passwordRequired, this, &SftpSidebar::onPasswordRequired);
    connect(m_connection, &SshConnection::remoteStatsUpdated, this, &SftpSidebar::remoteStatsUpdated);
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
    connect(this, &SftpSidebar::requestDownload, m_ftp, &FtpClient::downloadFile);
    connect(this, &SftpSidebar::requestUpload, m_ftp, &FtpClient::uploadFile);
    connect(this, &SftpSidebar::requestDelete, m_ftp, &FtpClient::deleteFile);
    connect(this, &SftpSidebar::requestCreateDir, m_ftp, &FtpClient::createDirectory);
    connect(this, &SftpSidebar::requestRename, m_ftp, &FtpClient::renamePath);

    connect(m_ftp, &FtpClient::connectionSuccess, this, &SftpSidebar::onConnectionSuccess);
    connect(m_ftp, &FtpClient::connectionFailed, this, &SftpSidebar::onConnectionFailed);
    connect(m_ftp, &FtpClient::directoryListed, this, &SftpSidebar::onDirectoryListed);
    connect(m_ftp, &FtpClient::operationFinished, this, &SftpSidebar::onOperationFinished);
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
    if (session.type == SessionType::FTP) {
        detachConnection();
        m_currentSession = session;
        m_statusLabel->setText(tr("Connecting to %1...").arg(session.host));
        m_statusLabel->setStyleSheet("color: #7aa2f7; font-weight: bold;");
        if (!m_ftp)
            setFtpClient(new FtpClient(this));
        const QString password = Keyring::lookupPassword(session.id);
        emit requestFtpConnect(session.host, session.port, session.user, password);
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
        return;
    }

    m_statusLabel->setText(tr("Connecting to %1...").arg(session.host));
    m_statusLabel->setStyleSheet("color: #7aa2f7; font-weight: bold;");

    QString password = Keyring::lookupPassword(session.id);
    emit requestConnect(session.host, session.port, session.user, session.keyPath, password, session.tunnels);
}

void SftpSidebar::stopSession() {
    m_isConnected = false;
    detachFtp();
    m_treeWidget->clear();
    m_pathEdit->clear();
    m_statusLabel->setText(tr("Disconnected"));
    m_statusLabel->setStyleSheet("color: #787c99; font-style: italic;");

    m_transferQueue.clear();
    m_transferActive = false;
    m_progressBar->hide();
    m_progressLabel->hide();

    m_upBtn->setEnabled(false);
    m_pathEdit->setEnabled(false);
    m_refreshBtn->setEnabled(false);
    m_uploadBtn->setEnabled(false);
    m_uploadDirBtn->setEnabled(false);
    m_newFolderBtn->setEnabled(false);
    m_renameBtn->setEnabled(false);
    m_chmodBtn->setEnabled(false);
    m_treeWidget->setEnabled(false);

    for (const QString& path : m_watchedFiles.keys()) {
        m_fileWatcher->removePath(path);
        QFile::remove(path);
    }
    m_watchedFiles.clear();
}

void SftpSidebar::onConnectionSuccess() {
    m_isConnected = true;
    m_statusLabel->setText(tr("Connected"));
    m_statusLabel->setStyleSheet("color: #50fa7b; font-weight: bold;");

    m_upBtn->setEnabled(true);
    m_pathEdit->setEnabled(true);
    m_refreshBtn->setEnabled(true);
    m_uploadBtn->setEnabled(true);
    m_uploadDirBtn->setEnabled(true);
    m_newFolderBtn->setEnabled(true);
    m_renameBtn->setEnabled(true);
    m_chmodBtn->setEnabled(true);
    m_treeWidget->setEnabled(true);

    m_currentPath = ".";
    emit requestList(m_currentPath);
}

void SftpSidebar::onConnectionFailed(const QString& error) {
    m_isConnected = false;
    m_statusLabel->setText(tr("Failed: %1").arg(error));
    m_statusLabel->setStyleSheet("color: #f7768e; font-weight: bold;");
    stopSession();
}

void SftpSidebar::onDirectoryListed(const QString& path, const QList<SftpFile>& files) {
    m_treeWidget->setUpdatesEnabled(false);
    m_treeWidget->clear();
    updatePath(path);

    // "." entry (refresh current directory)
    auto* dotItem = new QTreeWidgetItem();
    dotItem->setText(0, ".");
    dotItem->setIcon(0, folderIcon());
    dotItem->setData(0, Qt::UserRole, true);
    dotItem->setData(0, Qt::UserRole + 1, QStringLiteral("."));
    m_treeWidget->addTopLevelItem(dotItem);

    // ".." entry (go to parent) when not at root
    if (m_currentPath != "/" && !m_currentPath.isEmpty()) {
        auto* dotdotItem = new QTreeWidgetItem();
        dotdotItem->setText(0, "..");
        dotdotItem->setIcon(0, folderIcon());
        dotdotItem->setData(0, Qt::UserRole, true);
        dotdotItem->setData(0, Qt::UserRole + 1, QStringLiteral(".."));
        m_treeWidget->addTopLevelItem(dotdotItem);
    }

    QList<QTreeWidgetItem*> folders;
    QList<QTreeWidgetItem*> normalFiles;

    const QString base = m_currentPath == "/" ? "/" : m_currentPath + "/";

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

    m_treeWidget->addTopLevelItems(folders);
    m_treeWidget->addTopLevelItems(normalFiles);
    m_treeWidget->setUpdatesEnabled(true);
}

void SftpSidebar::onOperationFinished(bool success, const QString& error) {
    if (m_transferActive) {
        finishTransferQueue(success, error);
        return;
    }

    m_statusLabel->setText(error);
    if (success) {
        m_statusLabel->setStyleSheet("color: #50fa7b; font-style: italic;");

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
        m_statusLabel->setStyleSheet("color: #f7768e; font-weight: bold;");
        m_pendingEditRemotePath.clear();
        m_pendingEditLocalPath.clear();
        QMessageBox::critical(this, tr("SFTP Error"), error);
    }
}

void SftpSidebar::onPasswordRequired(const QString& prompt) {
    bool ok;
    QString password = QInputDialog::getText(this, tr("SSH Password Required"), prompt, QLineEdit::Password, "", &ok);
    if (ok) {
        m_statusLabel->setText(tr("Connecting with password..."));
        emit requestConnect(m_currentSession.host, m_currentSession.port, m_currentSession.user,
                            m_currentSession.keyPath, password, m_currentSession.tunnels);
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
    emit requestList(m_currentPath);
}

void SftpSidebar::navigateTo(const QString& path) {
    if (!m_isConnected)
        return;

    QString cleanPath = path.trimmed();
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

    QString name = item->text(0);
    bool ok = false;
    QString modeStr = QInputDialog::getText(this, tr("Permissions"), tr("Mode (octal, e.g. 755):"),
                                            QLineEdit::Normal, "755", &ok);
    if (!ok)
        return;

    bool parseOk = false;
    int mode = modeStr.trimmed().toInt(&parseOk, 8);
    if (!parseOk || mode < 0 || mode > 07777) {
        QMessageBox::warning(this, tr("Invalid Permissions"), tr("Please enter a valid octal mode (e.g. 755)."));
        return;
    }

    QString base = m_currentPath;
    if (!base.endsWith("/"))
        base += "/";

    m_statusLabel->setText(tr("Changing permissions of %1...").arg(name));
    emit requestChmod(base + name, mode);
}

void SftpSidebar::onDownloadClicked() {
    QList<QTreeWidgetItem*> items = m_treeWidget->selectedItems();
    if (items.isEmpty())
        return;

    QStringList remotePaths;
    for (QTreeWidgetItem* item : items) {
        QString special = item->data(0, Qt::UserRole + 1).toString();
        if (special == "." || special == "..")
            continue;
        if (item->data(0, Qt::UserRole).toBool())
            continue;
        QString remotePath = item->data(0, Qt::UserRole + 2).toString();
        if (remotePath.isEmpty()) {
            QString name = item->text(0);
            QString base = m_currentPath;
            if (!base.endsWith("/"))
                base += "/";
            remotePath = base + name;
        }
        remotePaths.append(remotePath);
    }

    if (remotePaths.isEmpty())
        return;

    enqueueDownload(remotePaths);
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

    auto result = QMessageBox::question(this, tr("Delete File"), tr("Are you sure you want to delete '%1'?").arg(name),
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
        emit requestList(m_currentPath);
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
    QAction* downloadAct = nullptr;
    if (!isDir) {
        editAct = menu.addAction(QIcon(":/icons/edit.svg"), tr("Edit"));
        downloadAct = menu.addAction(QIcon(":/icons/download.svg"), tr("Download"));
    }
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
    bool enabled = !active && m_isConnected;
    m_uploadBtn->setEnabled(enabled);
    m_uploadDirBtn->setEnabled(enabled);
    m_newFolderBtn->setEnabled(enabled);
    m_renameBtn->setEnabled(enabled);
    m_chmodBtn->setEnabled(enabled);
}

void SftpSidebar::onWatchedFileChanged(const QString& path) {
    if (m_watchedFiles.contains(path)) {
        QString remotePath = m_watchedFiles[path];
        m_statusLabel->setText(tr("Auto-uploading changes to %1...").arg(QFileInfo(remotePath).fileName()));
        m_statusLabel->setStyleSheet("color: #7aa2f7; font-style: italic;");
        emit requestUpload(path, remotePath);
    }
}

void SftpSidebar::enqueueUpload(const QStringList& localPaths) {
    if (!m_isConnected)
        return;

    for (const QString& localPath : localPaths) {
        QFileInfo info(localPath);
        if (info.isDir()) {
            if (m_ftp)
                continue; // FTP has no recursive folder upload
            if (!m_transferActive) {
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
                m_transferQueue.append({QString(), localPath, false, true});
            }
            continue;
        }
        QString remotePath = m_currentPath;
        if (!remotePath.endsWith("/"))
            remotePath += "/";
        remotePath += info.fileName();

        if (!m_transferActive) {
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
        }
    }
}

void SftpSidebar::enqueueDownload(const QStringList& remotePaths) {
    if (!m_isConnected)
        return;

    QString destDir = QFileDialog::getExistingDirectory(this, tr("Select Download Folder"), QDir::homePath());
    if (destDir.isEmpty())
        return;

    int added = 0;
    for (const QString& remotePath : remotePaths) {
        QString fileName = QFileInfo(remotePath).fileName();
        QString localPath = destDir + "/" + fileName;
        int n = 1;
        while (QFile::exists(localPath)) {
            QFileInfo fi(remotePath);
            localPath = destDir + "/" + fi.completeBaseName() + QString(" (%1).").arg(n) + fi.suffix();
            ++n;
        }

        if (!m_transferActive) {
            m_transferActive = true;
            setTransferUi(true);
            m_transferCurrentName = fileName;
            m_progressLabel->setText(tr("Downloading %1...").arg(fileName));
            m_progressLabel->show();
            m_progressBar->setRange(0, m_ftp ? 0 : 100);
            m_progressBar->setValue(0);
            m_progressBar->show();
            m_statusLabel->setText(tr("Downloading %1...").arg(fileName));
            emit requestDownload(remotePath, localPath);
        } else {
            m_transferQueue.append({remotePath, localPath, false});
        }
        ++added;
    }

    if (added > 1 && m_progressLabel->isVisible())
        m_progressLabel->setText(tr("Downloading %1... (%2 queued)").arg(m_transferCurrentName).arg(added - 1));
}

void SftpSidebar::startNextTransfer() {
    if (m_transferQueue.isEmpty()) {
        m_transferActive = false;
        setTransferUi(false);
        m_progressBar->hide();
        m_progressLabel->hide();
        m_statusLabel->setText(tr("All transfers finished."));
        m_statusLabel->setStyleSheet("color: #50fa7b; font-style: italic;");
        emit requestList(m_currentPath);
        return;
    }

    TransferItem item = m_transferQueue.takeFirst();
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
        m_transferQueue.clear();
        m_transferActive = false;
        setTransferUi(false);
        m_progressBar->hide();
        m_progressLabel->hide();
        m_statusLabel->setText(error);
        m_statusLabel->setStyleSheet("color: #f7768e; font-weight: bold;");
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
        m_progressLabel->setText(tr("%1: %2 / %3").arg(fileName).arg(formatBytes(bytesDone)).arg(totalText));
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
    }
    return QWidget::eventFilter(watched, event);
}
