#pragma once
#include <QObject>
#include <QList>
#include <QString>
#include <functional>
#include "session.h"

class QTcpSocket;
class QSslSocket;
class QTimer;
class QFile;

// Minimal passive-mode FTP client for browsing, uploading and downloading.
// Runs asynchronously on the GUI thread using QTcpSocket.
class FtpClient : public QObject {
    Q_OBJECT
public:
    explicit FtpClient(QObject* parent = nullptr);
    ~FtpClient() override;

    bool isConnected() const {
        return m_connected;
    }

signals:
    void connectionSuccess();
    void connectionFailed(const QString& error);
    void directoryListed(const QString& path, const QList<SftpFile>& files);
    void operationFinished(bool success, const QString& message);
    void transferProgress(const QString& fileName, qint64 bytesDone, qint64 totalBytes);

public slots:
    void connectToHost(const QString& host, int port, const QString& user, const QString& password, bool tls = true,
                       int minimumTlsVersion = 12, const QString& caFile = QString(),
                       bool allowInvalidCertificates = false);
    void disconnectFromHost();
    void listDirectory(const QString& path);
    void downloadFile(const QString& remotePath, const QString& localPath);
    void uploadFile(const QString& localPath, const QString& remotePath);
    void uploadDirectory(const QString& localPath, const QString& remoteBasePath);
    void cancelTransfer();
    void pauseTransfer();
    void resumeTransfer();
    void deleteFile(const QString& remotePath, bool isDir);
    void createDirectory(const QString& path);
    void renamePath(const QString& oldPath, const QString& newPath);

private:
    struct Reply {
        int code = 0;
        QStringList lines;
    };

    void sendCommand(const QString& cmd, std::function<void(const Reply&)> handler);
    void expectReply(std::function<void(const Reply&)> handler);
    void openDataConnection(std::function<void(QTcpSocket*)> onConnected);
    void processDirectoryUpload();
    void reportUploadResult(bool success, const QString& message);
    void pumpDownload();
    void pumpUpload();
    void scheduleReconnect();
    bool configureTlsSocket(QSslSocket* socket, QString* error = nullptr) const;
    static QList<SftpFile> parseListing(const QByteArray& data);

    QTcpSocket* m_control = nullptr;
    QByteArray m_replyBuffer;
    QStringList m_replyLines;
    std::function<void(const Reply&)> m_pendingHandler;
    bool m_connected = false;
    QString m_host;
    QString m_user;
    QString m_password;
    int m_port = 21;
    bool m_tls = true;
    int m_tlsMinimumVersion = 12;
    QString m_tlsCaFile;
    bool m_tlsAllowInvalidCertificates = false;
    int m_reconnectAttempts = 0;
    QTimer* m_reconnectTimer = nullptr;
    bool m_autoReconnectInProgress = false;
    bool m_directoryUploadActive = false;
    bool m_cancelTransferRequested = false;
    bool m_pauseTransferRequested = false;
    QTcpSocket* m_activeData = nullptr;
    QFile* m_activeDownloadFile = nullptr;
    QFile* m_activeUploadFile = nullptr;
    QString m_activeDownloadName;
    QString m_activeUploadName;
    qint64 m_activeDownloadReceived = 0;
    QStringList m_directoryUploadDirs;
    QList<QPair<QString, QString>> m_directoryUploadFiles;
};
