#include "sftpsidebar.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLineEdit>
#include <QPushButton>
#include <QTreeWidget>
#include <QTreeWidgetItem>
#include <QLabel>
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
#include "keyring.h"
#include "remoteeditordialog.h"
#include "ftpclient.h"

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

    m_treeWidget = new QTreeWidget(this);
    m_treeWidget->setHeaderLabels({tr("Name"), tr("Size"), tr("Modified")});
    m_treeWidget->setRootIsDecorated(false);
    m_treeWidget->header()->setSectionResizeMode(0, QHeaderView::Stretch);
    m_treeWidget->header()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    m_treeWidget->header()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
    m_treeWidget->setContextMenuPolicy(Qt::CustomContextMenu);
    mainLayout->addWidget(m_treeWidget);

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
    m_treeWidget->clear();
    updatePath(path);

    // "." entry (refresh current directory)
    auto* dotItem = new QTreeWidgetItem();
    dotItem->setText(0, ".");
    dotItem->setIcon(0, QIcon(":/icons/folder.svg"));
    dotItem->setData(0, Qt::UserRole, true);
    dotItem->setData(0, Qt::UserRole + 1, QStringLiteral("."));
    m_treeWidget->addTopLevelItem(dotItem);

    // ".." entry (go to parent) when not at root
    if (m_currentPath != "/" && !m_currentPath.isEmpty()) {
        auto* dotdotItem = new QTreeWidgetItem();
        dotdotItem->setText(0, "..");
        dotdotItem->setIcon(0, QIcon(":/icons/folder.svg"));
        dotdotItem->setData(0, Qt::UserRole, true);
        dotdotItem->setData(0, Qt::UserRole + 1, QStringLiteral(".."));
        m_treeWidget->addTopLevelItem(dotdotItem);
    }

    QList<QTreeWidgetItem*> folders;
    QList<QTreeWidgetItem*> normalFiles;

    for (const SftpFile& file : files) {
        auto* item = new QTreeWidgetItem();
        item->setText(0, file.name);
        item->setData(0, Qt::UserRole, file.isDirectory);
        item->setData(0, Qt::UserRole + 1, QString());

        if (file.isDirectory) {
            item->setIcon(0, QIcon(":/icons/folder.svg"));
            item->setText(1, tr("<DIR>"));
            item->setText(2, file.mtime.isValid() ? file.mtime.toString("yyyy-MM-dd hh:mm") : "");
            folders.append(item);
        } else {
            item->setIcon(0, QIcon(":/icons/file.svg"));

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

    for (QTreeWidgetItem* f : folders)
        m_treeWidget->addTopLevelItem(f);
    for (QTreeWidgetItem* f : normalFiles)
        m_treeWidget->addTopLevelItem(f);
}

void SftpSidebar::onOperationFinished(bool success, const QString& error) {
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
    QString path =
        QFileDialog::getOpenFileName(this, tr("Select File to Upload"), QDir::homePath(), tr("All Files (*)"));
    if (!path.isEmpty()) {
        QFileInfo info(path);
        QString remotePath = m_currentPath;
        if (!remotePath.endsWith("/"))
            remotePath += "/";
        remotePath += info.fileName();

        m_statusLabel->setText(tr("Uploading %1...").arg(info.fileName()));
        emit requestUpload(path, remotePath);
    }
}

void SftpSidebar::onUploadFolderClicked() {
    if (!m_isConnected)
        return;
    QString path = QFileDialog::getExistingDirectory(this, tr("Select Folder to Upload"), QDir::homePath());
    if (!path.isEmpty()) {
        m_statusLabel->setText(tr("Uploading folder %1...").arg(QFileInfo(path).fileName()));
        emit requestUploadDir(path, m_currentPath);
    }
}

void SftpSidebar::onNewFolderClicked() {
    if (!m_isConnected)
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
    if (!m_isConnected)
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
    if (!m_isConnected)
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

    QString localPath =
        QFileDialog::getSaveFileName(this, tr("Save File As"), QDir::homePath() + "/" + name, tr("All Files (*)"));
    if (!localPath.isEmpty()) {
        m_statusLabel->setText(tr("Downloading %1...").arg(name));
        emit requestDownload(remotePath, localPath);
    }
}

void SftpSidebar::onDeleteClicked() {
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
        onDeleteClicked();
    } else if (selected == renameAct) {
        onRenameClicked();
    } else if (selected == chmodAct) {
        onChmodClicked();
    } else if (selected == newFolderAct) {
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

void SftpSidebar::onWatchedFileChanged(const QString& path) {
    if (m_watchedFiles.contains(path)) {
        QString remotePath = m_watchedFiles[path];
        m_statusLabel->setText(tr("Auto-uploading changes to %1...").arg(QFileInfo(remotePath).fileName()));
        m_statusLabel->setStyleSheet("color: #7aa2f7; font-style: italic;");
        emit requestUpload(path, remotePath);
    }
}
