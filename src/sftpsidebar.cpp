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
#include <mutex>
#include "keyring.h"
#include "remoteeditordialog.h"

SftpWorker::SftpWorker() {
}

SftpWorker::~SftpWorker() {
    disconnectFromHost();
}

void SftpWorker::connectToHost(const QString& host, int port, const QString& user, const QString& keyPath,
                               const QString& password) {
    m_host = host;
    m_port = port;
    m_user = user;
    m_keyPath = keyPath;

    static std::once_flag init_flag;
    std::call_once(init_flag, []() { libssh2_init(0); });

    disconnectFromHost();

    m_socket = new QTcpSocket();
    m_socket->connectToHost(host, port);
    if (!m_socket->waitForConnected(5000)) {
        emit connectionFailed("TCP socket connection timeout: " + m_socket->errorString());
        delete m_socket;
        m_socket = nullptr;
        return;
    }

    m_sshSession = libssh2_session_init();
    if (!m_sshSession) {
        emit connectionFailed("Failed to initialize SSH session");
        m_socket->disconnectFromHost();
        delete m_socket;
        m_socket = nullptr;
        return;
    }

    // Set blocking mode
    libssh2_session_set_blocking(m_sshSession, 1);

    int rc = libssh2_session_handshake(m_sshSession, m_socket->socketDescriptor());
    if (rc) {
        emit connectionFailed(QString("SSH handshake failed: %1").arg(rc));
        disconnectFromHost();
        return;
    }

    bool authenticated = false;

    // 1. Try public key if provided
    if (!keyPath.isEmpty()) {
        rc = libssh2_userauth_publickey_fromfile(m_sshSession, user.toUtf8().constData(), nullptr,
                                                 keyPath.toUtf8().constData(), nullptr);
        if (rc == 0) {
            authenticated = true;
        }
    }

    // 2. Try password if provided
    if (!authenticated && !password.isEmpty()) {
        rc = libssh2_userauth_password(m_sshSession, user.toUtf8().constData(), password.toUtf8().constData());
        if (rc == 0) {
            authenticated = true;
        }
    }

    // 3. Try agent if not authenticated and no keyPath or password provided
    if (!authenticated && keyPath.isEmpty() && password.isEmpty()) {
        LIBSSH2_AGENT* agent = libssh2_agent_init(m_sshSession);
        if (agent) {
            if (libssh2_agent_connect(agent) == 0) {
                if (libssh2_agent_list_identities(agent) == 0) {
                    struct libssh2_agent_publickey* identity = nullptr;
                    struct libssh2_agent_publickey* prev_identity = nullptr;
                    while (libssh2_agent_get_identity(agent, &identity, prev_identity) == 0) {
                        rc = libssh2_agent_userauth(agent, user.toUtf8().constData(), identity);
                        if (rc == 0) {
                            authenticated = true;
                            break;
                        }
                        prev_identity = identity;
                    }
                }
                libssh2_agent_disconnect(agent);
            }
            libssh2_agent_free(agent);
        }
    }

    // 4. Prompt password if still not authenticated
    if (!authenticated) {
        emit passwordRequired("Password required for " + user + "@" + host);
        disconnectFromHost();
        return;
    }

    // Init SFTP
    m_sftpSession = libssh2_sftp_init(m_sshSession);
    if (!m_sftpSession) {
        emit connectionFailed("Failed to initialize SFTP session");
        disconnectFromHost();
        return;
    }

    // Start stats timer in the worker thread (every 8 seconds)
    m_statsTimer = new QTimer(this);
    connect(m_statsTimer, &QTimer::timeout, this, &SftpWorker::queryStats);
    m_statsTimer->start(8000);

    // Run once after 500ms
    QTimer::singleShot(500, this, &SftpWorker::queryStats);

    emit connectionSuccess();
}

void SftpWorker::listDirectory(const QString& path) {
    if (!m_sftpSession) {
        emit operationFinished(false, "SFTP session not active");
        return;
    }

    LIBSSH2_SFTP_HANDLE* handle = libssh2_sftp_opendir(m_sftpSession, path.toUtf8().constData());
    if (!handle) {
        emit operationFinished(false, "Failed to open remote directory: " + path);
        return;
    }

    QList<SftpFile> files;
    char filename[512];
    LIBSSH2_SFTP_ATTRIBUTES attrs;

    while (true) {
        int rc = libssh2_sftp_readdir(handle, filename, sizeof(filename), &attrs);
        if (rc <= 0)
            break;

        QString name = QString::fromUtf8(filename);
        if (name == "." || name == "..")
            continue;

        SftpFile file;
        file.name = name;
        file.isDirectory =
            (attrs.flags & LIBSSH2_SFTP_ATTR_PERMISSIONS) ? LIBSSH2_SFTP_S_ISDIR(attrs.permissions) : false;
        file.size = (attrs.flags & LIBSSH2_SFTP_ATTR_SIZE) ? attrs.filesize : 0;
        file.mtime =
            (attrs.flags & LIBSSH2_SFTP_ATTR_ACMODTIME) ? QDateTime::fromSecsSinceEpoch(attrs.mtime) : QDateTime();

        files.append(file);
    }

    libssh2_sftp_closedir(handle);
    emit directoryListed(path, files);
}

void SftpWorker::downloadFile(const QString& remotePath, const QString& localPath) {
    if (!m_sftpSession) {
        emit operationFinished(false, "SFTP session not active");
        return;
    }

    LIBSSH2_SFTP_HANDLE* handle =
        libssh2_sftp_open(m_sftpSession, remotePath.toUtf8().constData(), LIBSSH2_FXF_READ, 0);
    if (!handle) {
        emit operationFinished(false, "Failed to open remote file: " + remotePath);
        return;
    }

    QFile localFile(localPath);
    if (!localFile.open(QIODevice::WriteOnly)) {
        libssh2_sftp_close(handle);
        emit operationFinished(false, "Failed to open local file: " + localPath);
        return;
    }

    char buffer[32768];
    while (true) {
        int bytesRead = libssh2_sftp_read(handle, buffer, sizeof(buffer));
        if (bytesRead < 0) {
            localFile.close();
            libssh2_sftp_close(handle);
            emit operationFinished(false, "Error reading remote file: " + remotePath);
            return;
        }
        if (bytesRead == 0)
            break;
        localFile.write(buffer, bytesRead);
    }

    localFile.close();
    libssh2_sftp_close(handle);
    emit operationFinished(true, "Download finished successfully: " + QFileInfo(remotePath).fileName());
}

void SftpWorker::uploadFile(const QString& localPath, const QString& remotePath) {
    if (!m_sftpSession) {
        emit operationFinished(false, "SFTP session not active");
        return;
    }

    QFile localFile(localPath);
    if (!localFile.open(QIODevice::ReadOnly)) {
        emit operationFinished(false, "Failed to open local file: " + localPath);
        return;
    }

    LIBSSH2_SFTP_HANDLE* handle = libssh2_sftp_open(
        m_sftpSession, remotePath.toUtf8().constData(), LIBSSH2_FXF_WRITE | LIBSSH2_FXF_CREAT | LIBSSH2_FXF_TRUNC,
        LIBSSH2_SFTP_S_IRUSR | LIBSSH2_SFTP_S_IWUSR | LIBSSH2_SFTP_S_IRGRP | LIBSSH2_SFTP_S_IROTH);
    if (!handle) {
        localFile.close();
        emit operationFinished(false, "Failed to create remote file: " + remotePath);
        return;
    }

    char buffer[32768];
    while (true) {
        qint64 bytesRead = localFile.read(buffer, sizeof(buffer));
        if (bytesRead <= 0)
            break;

        char* ptr = buffer;
        qint64 bytesToWrite = bytesRead;
        while (bytesToWrite > 0) {
            int bytesWritten = libssh2_sftp_write(handle, ptr, bytesToWrite);
            if (bytesWritten < 0) {
                localFile.close();
                libssh2_sftp_close(handle);
                emit operationFinished(false, "Error writing remote file: " + remotePath);
                return;
            }
            bytesToWrite -= bytesWritten;
            ptr += bytesWritten;
        }
    }

    localFile.close();
    libssh2_sftp_close(handle);
    emit operationFinished(true, "Upload finished successfully: " + QFileInfo(localPath).fileName());
}

void SftpWorker::deleteFile(const QString& remotePath, bool isDir) {
    if (!m_sftpSession) {
        emit operationFinished(false, "SFTP session not active");
        return;
    }

    int rc;
    if (isDir) {
        rc = libssh2_sftp_rmdir(m_sftpSession, remotePath.toUtf8().constData());
    } else {
        rc = libssh2_sftp_unlink(m_sftpSession, remotePath.toUtf8().constData());
    }

    if (rc == 0) {
        emit operationFinished(true, "Deleted item successfully");
    } else {
        emit operationFinished(false, QString("Delete failed (Error code %1)").arg(rc));
    }
}

void SftpWorker::disconnectFromHost() {
    if (m_statsTimer) {
        m_statsTimer->stop();
        delete m_statsTimer;
        m_statsTimer = nullptr;
    }
    if (m_sftpSession) {
        libssh2_sftp_shutdown(m_sftpSession);
        m_sftpSession = nullptr;
    }
    if (m_sshSession) {
        libssh2_session_disconnect(m_sshSession, "Disconnecting");
        libssh2_session_free(m_sshSession);
        m_sshSession = nullptr;
    }
    if (m_socket) {
        m_socket->disconnectFromHost();
        delete m_socket;
        m_socket = nullptr;
    }
}

void SftpWorker::queryStats() {
    if (!m_sshSession)
        return;

    LIBSSH2_CHANNEL* channel = libssh2_channel_open_session(m_sshSession);
    if (!channel)
        return;

    QString cmd = "read cpu u n s id iw irq soft steal rest < /proc/stat; previdle=$((id + iw)); prevtotal=$((u + n + "
                  "s + id + iw + irq + soft + steal)); sleep 0.2; "
                  "read cpu u2 n2 s2 id2 iw2 irq2 soft2 steal2 rest < /proc/stat; idle=$((id2 + iw2)); total=$((u2 + "
                  "n2 + s2 + id2 + iw2 + irq2 + soft2 + steal2)); diffidle=$((idle - previdle)); "
                  "difftotal=$((total - prevtotal)); if [ $difftotal -eq 0 ]; then echo 0; else echo \"$((100 * "
                  "(difftotal - diffidle) / difftotal))\"; fi; "
                  "awk '/MemTotal/{t=$2} /MemAvailable/{a=$2} END{printf \"%.0f\\n\", 100*(t-a)/t}' /proc/meminfo; "
                  "df / | tail -n 1 | awk '{print $5}' | sed 's/%//'; "
                  "cat /proc/uptime | awk '{print $1}'";

    int rc = libssh2_channel_exec(channel, cmd.toUtf8().constData());
    if (rc == 0) {
        QByteArray response;
        char buffer[256];
        while (true) {
            int bytesRead = libssh2_channel_read(channel, buffer, sizeof(buffer));
            if (bytesRead <= 0)
                break;
            response.append(buffer, bytesRead);
        }

        QString resStr = QString::fromUtf8(response).trimmed();
        QStringList lines = resStr.split('\n');
        if (lines.size() >= 4) {
            double cpu = lines[0].toDouble();
            double mem = lines[1].toDouble();
            double disk = lines[2].toDouble();
            double uptimeSecs = lines[3].toDouble();
            emit remoteStatsUpdated(cpu, mem, disk, uptimeSecs);
        }
    }

    libssh2_channel_close(channel);
    libssh2_channel_free(channel);
}

/* SftpSidebar UI implementation */

SftpSidebar::SftpSidebar(QWidget* parent) : QWidget(parent) {
    auto* mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(10, 10, 10, 10);
    mainLayout->setSpacing(8);

    auto* titleLabel = new QLabel(tr("SFTP FILES"), this);
    titleLabel->setStyleSheet(
        "font-weight: bold; color: #787c99; font-size: 11px; letter-spacing: 1px; margin-bottom: 4px;");
    mainLayout->addWidget(titleLabel);

    // Path navigation bar
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

    // Toolbar buttons
    auto* toolsLayout = new QHBoxLayout();
    toolsLayout->setSpacing(6);

    m_uploadBtn = new QPushButton(QIcon(":/icons/upload.svg"), tr("Upload"), this);
    toolsLayout->addWidget(m_uploadBtn);

    mainLayout->addLayout(toolsLayout);

    // Tree view for files
    m_treeWidget = new QTreeWidget(this);
    m_treeWidget->setHeaderLabels({tr("Name"), tr("Size"), tr("Modified")});
    m_treeWidget->header()->setSectionResizeMode(0, QHeaderView::Stretch);
    m_treeWidget->header()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    m_treeWidget->header()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
    m_treeWidget->setContextMenuPolicy(Qt::CustomContextMenu);
    mainLayout->addWidget(m_treeWidget);

    // Status / Alert bar
    m_statusLabel = new QLabel(tr("Disconnected"), this);
    m_statusLabel->setStyleSheet("color: #787c99; font-style: italic;");
    mainLayout->addWidget(m_statusLabel);

    // Disable components until connected
    m_upBtn->setEnabled(false);
    m_pathEdit->setEnabled(false);
    m_refreshBtn->setEnabled(false);
    m_uploadBtn->setEnabled(false);
    m_treeWidget->setEnabled(false);

    // Initialize file watcher for text editor integration
    m_fileWatcher = new QFileSystemWatcher(this);
    connect(m_fileWatcher, &QFileSystemWatcher::fileChanged, this, &SftpSidebar::onWatchedFileChanged);

    // Set up background thread
    m_worker = new SftpWorker();
    m_worker->moveToThread(&m_workerThread);

    // Worker signals connection
    connect(this, &SftpSidebar::requestConnect, m_worker, &SftpWorker::connectToHost);
    connect(this, &SftpSidebar::requestList, m_worker, &SftpWorker::listDirectory);
    connect(this, &SftpSidebar::requestDownload, m_worker, &SftpWorker::downloadFile);
    connect(this, &SftpSidebar::requestUpload, m_worker, &SftpWorker::uploadFile);
    connect(this, &SftpSidebar::requestDelete, m_worker, &SftpWorker::deleteFile);
    connect(this, &SftpSidebar::requestDisconnect, m_worker, &SftpWorker::disconnectFromHost);

    connect(m_worker, &SftpWorker::connectionSuccess, this, &SftpSidebar::onConnectionSuccess);
    connect(m_worker, &SftpWorker::connectionFailed, this, &SftpSidebar::onConnectionFailed);
    connect(m_worker, &SftpWorker::directoryListed, this, &SftpSidebar::onDirectoryListed);
    connect(m_worker, &SftpWorker::operationFinished, this, &SftpSidebar::onOperationFinished);
    connect(m_worker, &SftpWorker::passwordRequired, this, &SftpSidebar::onPasswordRequired);
    connect(m_worker, &SftpWorker::remoteStatsUpdated, this, &SftpSidebar::remoteStatsUpdated);

    // UI actions connection
    connect(m_upBtn, &QPushButton::clicked, this, &SftpSidebar::onParentDirClicked);
    connect(m_refreshBtn, &QPushButton::clicked, this, &SftpSidebar::onRefreshClicked);
    connect(m_uploadBtn, &QPushButton::clicked, this, &SftpSidebar::onUploadClicked);
    connect(m_pathEdit, &QLineEdit::returnPressed, this, [this]() { emit requestList(m_pathEdit->text().trimmed()); });
    connect(m_treeWidget, &QTreeWidget::itemDoubleClicked, this, &SftpSidebar::onItemDoubleClicked);
    connect(m_treeWidget, &QTreeWidget::customContextMenuRequested, this, &SftpSidebar::showContextMenu);

    m_workerThread.start();
}

SftpSidebar::~SftpSidebar() {
    emit requestDisconnect();
    m_workerThread.quit();
    m_workerThread.wait();
    delete m_worker;
}

void SftpSidebar::startSession(const Session& session) {
    if (session.type != SessionType::SSH) {
        stopSession();
        return;
    }
    m_currentSession = session;
    m_statusLabel->setText(tr("Connecting to %1...").arg(session.host));
    m_statusLabel->setStyleSheet("color: #7aa2f7; font-weight: bold;");

    QString password = Keyring::lookupPassword(session.id);
    emit requestConnect(session.host, session.port, session.user, session.keyPath, password);
}

void SftpSidebar::stopSession() {
    m_isConnected = false;
    emit requestDisconnect();
    m_treeWidget->clear();
    m_pathEdit->clear();
    m_statusLabel->setText(tr("Disconnected"));
    m_statusLabel->setStyleSheet("color: #787c99; font-style: italic;");

    m_upBtn->setEnabled(false);
    m_pathEdit->setEnabled(false);
    m_refreshBtn->setEnabled(false);
    m_uploadBtn->setEnabled(false);
    m_treeWidget->setEnabled(false);

    // Clear and remove watched temporary files
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
    m_treeWidget->setEnabled(true);

    // List remote home directory
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

    QList<QTreeWidgetItem*> folders;
    QList<QTreeWidgetItem*> normalFiles;

    for (const SftpFile& file : files) {
        auto* item = new QTreeWidgetItem();
        item->setText(0, file.name);
        item->setData(0, Qt::UserRole, file.isDirectory); // Directory flag

        if (file.isDirectory) {
            item->setIcon(0, QIcon(":/icons/folder.svg"));
            item->setText(1, tr("<DIR>"));
            item->setText(2, file.mtime.isValid() ? file.mtime.toString("yyyy-MM-dd hh:mm") : "");
            folders.append(item);
        } else {
            item->setIcon(0, QIcon(":/icons/file.svg"));

            // Format size
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

    // Add folders first, then files
    m_treeWidget->addTopLevelItems(folders);
    m_treeWidget->addTopLevelItems(normalFiles);
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
                            m_currentSession.keyPath, password);
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

void SftpSidebar::onDownloadClicked() {
    auto* item = m_treeWidget->currentItem();
    if (!item)
        return;

    bool isDir = item->data(0, Qt::UserRole).toBool();
    if (isDir)
        return; // Directory download not supported in this simple model

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
    bool isDir = item->data(0, Qt::UserRole).toBool();

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
        // Edit if text file, else download
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

    QMenu menu(this);
    bool isDir = item->data(0, Qt::UserRole).toBool();

    QAction* editAct = nullptr;
    QAction* downloadAct = nullptr;
    if (!isDir) {
        editAct = menu.addAction(QIcon(":/icons/edit.svg"), tr("Edit"));
        downloadAct = menu.addAction(QIcon(":/icons/download.svg"), tr("Download"));
    }
    auto* deleteAct = menu.addAction(QIcon(":/icons/delete.svg"), tr("Delete"));
    auto* refreshAct = menu.addAction(QIcon(":/icons/refresh.svg"), tr("Refresh"));

    auto* selected = menu.exec(m_treeWidget->mapToGlobal(pos));
    if (selected == editAct) {
        onEditClicked();
    } else if (selected == downloadAct) {
        onDownloadClicked();
    } else if (selected == deleteAct) {
        onDeleteClicked();
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
