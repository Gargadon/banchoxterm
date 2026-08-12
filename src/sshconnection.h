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

// Bridges an incoming X11 channel to the local X server socket.
struct X11Bridge {
    LIBSSH2_CHANNEL* channel = nullptr;
    int xSock = -1;
    bool channelEof = false;
    bool sockEof = false;
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
    void setX11Forwarding(bool enabled);
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

    template <typename Fn> auto retryPtr(Fn&& fn) -> decltype(fn()) {
        while (true) {
            auto result = fn();
            if (result)
                return result;
            if (libssh2_session_last_errno(m_session) != LIBSSH2_ERROR_EAGAIN)
                return nullptr;
            if (!waitSocket(15000))
                return nullptr;
        }
    }

    void openShell();
    void readShell();
    void setupX11Cookie();
    int connectToXServer();
    void handleX11Open(LIBSSH2_CHANNEL* channel, const char* shost, int sport);
    void pollX11Bridges();
    static void x11OpenCallback(LIBSSH2_SESSION* session, LIBSSH2_CHANNEL* channel, const char* shost, int sport,
                                void** abstract);

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

    // X11 forwarding
    bool m_x11Forwarding = false;
    QString m_x11Display;
    QString m_x11Cookie;
    int m_x11Screen = 0;
    QList<X11Bridge*> m_x11Bridges;
    void* m_abstract = nullptr;
};
