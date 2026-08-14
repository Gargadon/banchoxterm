#pragma once
#include <QObject>
#include <QList>
#include <QString>
#include <functional>
#include "session.h"

class QTcpSocket;

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

public slots:
    void connectToHost(const QString& host, int port, const QString& user, const QString& password, bool tls = true);
    void disconnectFromHost();
    void listDirectory(const QString& path);
    void downloadFile(const QString& remotePath, const QString& localPath);
    void uploadFile(const QString& localPath, const QString& remotePath);
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
    static QList<SftpFile> parseListing(const QByteArray& data);

    QTcpSocket* m_control = nullptr;
    QByteArray m_replyBuffer;
    QStringList m_replyLines;
    std::function<void(const Reply&)> m_pendingHandler;
    bool m_connected = false;
    QString m_host;
    int m_port = 21;
    bool m_tls = true;
};
