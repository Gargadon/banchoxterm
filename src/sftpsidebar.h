#pragma once
#include <QWidget>
#include <QFileSystemWatcher>
#include <QMap>
#include <QPointer>
#include "session.h"
#include "sshconnection.h"

class QTreeWidget;
class QTreeWidgetItem;
class QLineEdit;
class QPushButton;
class QLabel;
class QProgressBar;
class FtpClient;

class SftpSidebar : public QWidget {
    Q_OBJECT
public:
    explicit SftpSidebar(QWidget* parent = nullptr);
    ~SftpSidebar() override;

    void setConnection(SshConnection* connection);
    void detachConnection();
    SshConnection* connection() const {
        return m_connection;
    }

    void startSession(const Session& session);
    void stopSession();

public slots:
    void navigateTo(const QString& path);

private slots:
    void onConnectionSuccess();
    void onConnectionFailed(const QString& error);
    void onDirectoryListed(const QString& path, const QList<SftpFile>& files);
    void onOperationFinished(bool success, const QString& error);
    void onTransferProgress(const QString& fileName, qint64 bytesDone, qint64 totalBytes);
    void onPasswordRequired(const QString& prompt);

    void onParentDirClicked();
    void onRefreshClicked();
    void onUploadClicked();
    void onDownloadClicked();
    void onDeleteClicked();
    void onEditClicked();
    void onNewFolderClicked();
    void onRenameClicked();
    void onChmodClicked();
    void onUploadFolderClicked();
    void onItemDoubleClicked(QTreeWidgetItem* item, int column);
    void showContextMenu(const QPoint& pos);
    void onWatchedFileChanged(const QString& path);

signals:
    void requestConnect(const QString& host, int port, const QString& user, const QString& keyPath,
                        const QString& password, const QList<TunnelConfig>& tunnels);
    void requestFtpConnect(const QString& host, int port, const QString& user, const QString& password);
    void requestList(const QString& path);
    void requestDownload(const QString& remotePath, const QString& localPath);
    void requestUpload(const QString& localPath, const QString& remotePath);
    void requestDelete(const QString& remotePath, bool isDir);
    void requestCreateDir(const QString& path);
    void requestRename(const QString& oldPath, const QString& newPath);
    void requestChmod(const QString& path, int mode);
    void requestUploadDir(const QString& localPath, const QString& remoteBasePath);
    void requestDisconnect();
    void remoteStatsUpdated(double cpu, double mem, double disk, double uptimeSecs);

private:
    void updatePath(const QString& path);
    void setFtpClient(FtpClient* client);
    void detachFtp();
    bool eventFilter(QObject* watched, QEvent* event) override;

    // Transfer queue (serialized, one transfer at a time)
    struct TransferItem {
        QString remotePath;
        QString localPath;
        bool isUpload = false;
        bool isDirUpload = false;
    };
    void enqueueUpload(const QStringList& localPaths);
    void enqueueDownload(const QStringList& remotePaths);
    void startNextTransfer();
    void finishTransferQueue(bool success, const QString& error);
    void setTransferUi(bool active);

    QLineEdit* m_pathEdit;
    QPushButton* m_upBtn;
    QPushButton* m_refreshBtn;
    QPushButton* m_uploadBtn;
    QPushButton* m_uploadDirBtn;
    QPushButton* m_newFolderBtn;
    QPushButton* m_renameBtn;
    QPushButton* m_chmodBtn;
    QTreeWidget* m_treeWidget;
    QLabel* m_statusLabel;
    QProgressBar* m_progressBar;
    QLabel* m_progressLabel;

    QPointer<SshConnection> m_connection;
    QPointer<FtpClient> m_ftp;

    Session m_currentSession;
    QString m_currentPath;
    bool m_isConnected = false;

    QList<TransferItem> m_transferQueue;
    bool m_transferActive = false;
    QString m_transferCurrentName;

    QString m_pendingEditRemotePath;
    QString m_pendingEditLocalPath;

    QFileSystemWatcher* m_fileWatcher = nullptr;
    QMap<QString, QString> m_watchedFiles;
};
