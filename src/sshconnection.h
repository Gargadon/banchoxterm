#pragma once
#include <QObject>
#include <QByteArray>
#include <QList>
#include <QDateTime>
#include <QTimer>
#include <functional>
#include <libssh2.h>
#include <libssh2_sftp.h>

struct SftpFile {
    QString name;
    bool isDirectory = false;
    qint64 size = 0;
    QDateTime mtime;
};

// Owns a single libssh2 session shared by the terminal shell channel and the
// SFTP file browser. Runs in its own thread; all libssh2 calls happen there.
class SshConnection : public QObject {
    Q_OBJECT
public:
    explicit SshConnection();
    ~SshConnection() override;

    bool isConnected() const {
        return m_connected;
    }

public slots:
    void connectToHost(const QString& host, int port, const QString& user, const QString& keyPath,
                       const QString& password = QString());
    void disconnectFromHost();
    void sendToShell(const QByteArray& data);
    void resizePty(int rows, int cols);
    void listDirectory(const QString& path);
    void downloadFile(const QString& remotePath, const QString& localPath);
    void uploadFile(const QString& localPath, const QString& remotePath);
    void deleteFile(const QString& remotePath, bool isDir);
    void queryStats();

signals:
    void connectionSuccess();
    void connectionFailed(const QString& error);
    void passwordRequired(const QString& prompt);
    void shellDataReceived(const QByteArray& data);
    void shellClosed();
    void directoryListed(const QString& path, const QList<SftpFile>& files);
    void operationFinished(bool success, const QString& error);
    void remoteStatsUpdated(double cpu, double mem, double disk, double uptimeSecs);

private slots:
    void onPollTimer();

private:
    bool openSocket();
    void closeSocket();
    bool waitSocket(int timeoutMs);
    int retry(const std::function<int()>& fn);
    void openShell();
    void readShell();

    int m_sock = -1;
    LIBSSH2_SESSION* m_session = nullptr;
    LIBSSH2_CHANNEL* m_channel = nullptr;
    LIBSSH2_SFTP* m_sftp = nullptr;

    QString m_host;
    int m_port = 22;
    QString m_user;
    QString m_keyPath;

    int m_ptyRows = 24;
    int m_ptyCols = 80;

    QTimer* m_pollTimer = nullptr;
    QTimer* m_statsTimer = nullptr;
    bool m_connected = false;
};
