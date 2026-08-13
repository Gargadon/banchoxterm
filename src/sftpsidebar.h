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
                        const QString& password, const QList<TunnelConfig>& tunnels);
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

    QPointer<SshConnection> m_connection;

    Session m_currentSession;
    QString m_currentPath;
    bool m_isConnected = false;

    QString m_pendingEditRemotePath;
    QString m_pendingEditLocalPath;

    QFileSystemWatcher* m_fileWatcher = nullptr;
    QMap<QString, QString> m_watchedFiles;
};
