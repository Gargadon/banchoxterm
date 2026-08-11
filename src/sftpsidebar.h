#pragma once
#include <QWidget>
#include <QThread>
#include <QTcpSocket>
#include <QDateTime>
#include <QTimer>
#include <QFileSystemWatcher>
#include <QMap>
#include <libssh2.h>
#include <libssh2_sftp.h>
#include "session.h"

class QTreeWidget;
class QTreeWidgetItem;
class QLineEdit;
class QPushButton;
class QLabel;

struct SftpFile {
    QString name;
    bool isDirectory;
    qint64 size;
    QDateTime mtime;
};

// SftpWorker handles all libssh2 calls in a separate thread
class SftpWorker : public QObject {
    Q_OBJECT
public:
    SftpWorker();
    ~SftpWorker() override;

signals:
    void connectionSuccess();
    void connectionFailed(const QString& error);
    void directoryListed(const QString& path, const QList<SftpFile>& files);
    void operationFinished(bool success, const QString& error);
    void passwordRequired(const QString& prompt);
    void remoteStatsUpdated(double cpu, double mem, double disk, double uptimeSecs);

public slots:
    void connectToHost(const QString& host, int port, const QString& user, const QString& keyPath,
                       const QString& password = "");
    void listDirectory(const QString& path);
    void downloadFile(const QString& remotePath, const QString& localPath);
    void uploadFile(const QString& localPath, const QString& remotePath);
    void deleteFile(const QString& remotePath, bool isDir);
    void disconnectFromHost();
    void queryStats();

private:
    QTcpSocket* m_socket = nullptr;
    LIBSSH2_SESSION* m_sshSession = nullptr;
    LIBSSH2_SFTP* m_sftpSession = nullptr;
    QString m_host;
    int m_port = 22;
    QString m_user;
    QString m_keyPath;
    QTimer* m_statsTimer = nullptr;
};

// SftpSidebar is the UI container
class SftpSidebar : public QWidget {
    Q_OBJECT
public:
    explicit SftpSidebar(QWidget* parent = nullptr);
    ~SftpSidebar() override;

    void startSession(const Session& session);
    void stopSession();

private slots:
    void onConnectionSuccess();
    void onConnectionFailed(const QString& error);
    void onDirectoryListed(const QString& path, const QList<SftpFile>& files);
    void onOperationFinished(bool success, const QString& error);
    void onPasswordRequired(const QString& prompt);

    void onParentDirClicked();
    void onRefreshClicked();
    void onUploadClicked();
    void onDownloadClicked();
    void onDeleteClicked();
    void onEditClicked();
    void onItemDoubleClicked(QTreeWidgetItem* item, int column);
    void showContextMenu(const QPoint& pos);
    void onWatchedFileChanged(const QString& path);

signals:
    void requestConnect(const QString& host, int port, const QString& user, const QString& keyPath,
                        const QString& password);
    void requestList(const QString& path);
    void requestDownload(const QString& remotePath, const QString& localPath);
    void requestUpload(const QString& localPath, const QString& remotePath);
    void requestDelete(const QString& remotePath, bool isDir);
    void requestDisconnect();
    void remoteStatsUpdated(double cpu, double mem, double disk, double uptimeSecs);

private:
    void updatePath(const QString& path);

    QLineEdit* m_pathEdit;
    QPushButton* m_upBtn;
    QPushButton* m_refreshBtn;
    QPushButton* m_uploadBtn;
    QTreeWidget* m_treeWidget;
    QLabel* m_statusLabel;

    QThread m_workerThread;
    SftpWorker* m_worker;

    Session m_currentSession;
    QString m_currentPath;
    bool m_isConnected = false;

    QString m_pendingEditRemotePath;
    QString m_pendingEditLocalPath;

    QFileSystemWatcher* m_fileWatcher = nullptr;
    QMap<QString, QString> m_watchedFiles;
};
