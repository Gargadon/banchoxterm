#pragma once
#include <QObject>
#include <QByteArray>
#include <QList>
#include <QDateTime>
#include <QTimer>
#include <functional>
#include <libssh2.h>
#include <libssh2_sftp.h>
#include "session.h"
#include "sshtunnel.h"

class QSocketNotifier;

// Bridges an incoming X11 channel to the local X server socket.
struct X11Bridge {
    LIBSSH2_CHANNEL* channel = nullptr;
    int xSock = -1;
    bool channelEof = false;
    bool sockEof = false;
    QSocketNotifier* xNotifier = nullptr;
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
                       const QString& password, const QList<TunnelConfig>& tunnels, const QString& jumpHost = QString(),
                       int jumpPort = 22, const QString& jumpUser = QString(), const QString& jumpKeyPath = QString());
    void setX11Forwarding(bool enabled);
    void setKeepAliveSeconds(int seconds);
    void setCipherAlgorithms(const QString& ciphers);
    void setKexAlgorithm(const QString& kex);
    void setMacAlgorithm(const QString& mac);
    void disconnectFromHost();
    void sendToShell(const QByteArray& data);
    void resizePty(int rows, int cols);
    void listDirectory(const QString& path);
    void downloadFile(const QString& remotePath, const QString& localPath);
    void uploadFile(const QString& localPath, const QString& remotePath);
    void deleteFile(const QString& remotePath, bool isDir);
    void createDirectory(const QString& path);
    void renamePath(const QString& oldPath, const QString& newPath);
    void chmodPath(const QString& path, int mode);
    void uploadDirectory(const QString& localPath, const QString& remoteBasePath);

signals:
    void connectionSuccess();
    void connectionFailed(const QString& error);
    void passwordRequired(const QString& prompt);
    void shellDataReceived(const QByteArray& data);
    void shellClosed();
    void directoryListed(const QString& path, const QList<SftpFile>& files);
    void operationFinished(bool success, const QString& error);
    void transferProgress(const QString& fileName, qint64 bytesDone, qint64 totalBytes);
    void remoteStatsUpdated(double cpu, double mem, double disk, double uptimeSecs);

private slots:
    void onSocketActivity();
    void onKeepAlive();
    void onStatsTimer();

private:
    bool openSocket();
    void closeSocket();
    bool waitSocket(int timeoutMs);
    int retry(const std::function<int()>& fn);
    bool authenticateSession(const QString& user, const QString& keyPath, const QString& password);
    void configureSession(LIBSSH2_SESSION* session, bool enableX11);
    static ssize_t proxySend(libssh2_socket_t socket, const void* buffer, size_t length, int flags, void** abstract);
    static ssize_t proxyRecv(libssh2_socket_t socket, void* buffer, size_t length, int flags, void** abstract);

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
    void handleShellClosed();
    void armNotifiers();
    void stopNotifiers();
    void setupX11Cookie();
    int connectToXServer();
    void handleX11Open(LIBSSH2_CHANNEL* channel, const char* shost, int sport);
    void pollX11Bridges();
    static void x11OpenCallback(LIBSSH2_SESSION* session, LIBSSH2_CHANNEL* channel, const char* shost, int sport,
                                void** abstract);

    bool uploadOneFile(const QString& localPath, const QString& remotePath);
    bool uploadDirRecursive(const QString& localDir, const QString& remoteDir);

    void startStats();
    void pollStats();
    void finishStats();
    void closeStats();

    bool verifyHostKey();
    bool promptHostKey(const QString& host, const QString& fingerprint, const QString& keyType, bool changed);
    QString hostKeyFingerprint(const QByteArray& key) const;
    QString knownHostsPath() const;

    static void kbdIntResponseCallback(const char* name, int name_len, const char* instruction, int instruction_len,
                                       int num_prompts, const LIBSSH2_USERAUTH_KBDINT_PROMPT* prompts,
                                       LIBSSH2_USERAUTH_KBDINT_RESPONSE* responses, void** abstract);
    void handleKbdInt(const char* name, int name_len, const char* instruction, int instruction_len, int num_prompts,
                      const LIBSSH2_USERAUTH_KBDINT_PROMPT* prompts, LIBSSH2_USERAUTH_KBDINT_RESPONSE* responses);
    bool promptKbdInteractive(const QString& name, const QString& instruction, const QList<QByteArray>& promptTexts,
                              const QList<bool>& echoFlags, QStringList& answers);

    int m_sock = -1;
    LIBSSH2_SESSION* m_session = nullptr;
    LIBSSH2_CHANNEL* m_channel = nullptr;
    LIBSSH2_SFTP* m_sftp = nullptr;
    int m_jumpSock = -1;
    LIBSSH2_SESSION* m_jumpSession = nullptr;
    LIBSSH2_CHANNEL* m_jumpChannel = nullptr;
    bool m_proxyTransport = false;

    QString m_host;
    int m_port = 22;
    QString m_user;
    QString m_keyPath;

    int m_ptyRows = 24;
    int m_ptyCols = 80;

    QTimer* m_statsTimer = nullptr;
    QTimer* m_keepAliveTimer = nullptr;
    QSocketNotifier* m_readNotifier = nullptr;
    QSocketNotifier* m_writeNotifier = nullptr;
    bool m_connected = false;

    // Per-session SSH options (set before connectToHost)
    int m_keepAliveSeconds = 0;
    QString m_cryptCipher;
    QString m_kexAlgo;
    QString m_macAlgo;

    // non-blocking stats query state machine
    enum class StatsState { Idle, Opening, Execing, Reading };
    StatsState m_statsState = StatsState::Idle;
    LIBSSH2_CHANNEL* m_statsChannel = nullptr;
    QByteArray m_statsBuffer;
    bool m_remoteIsWindows = false;

    // Event-loop kick guard: when libssh2 is buffering data internally without
    // socket activity, onSocketActivity() re-arms itself via a queued kick. These
    // two members prevent the kick from spinning the event loop when no progress
    // is being made (m_activityProgress) or while a kick is already queued.
    bool m_activityProgress = false;
    bool m_socketKickPending = false;

    // X11 forwarding
    bool m_x11Forwarding = false;
    QString m_x11Display;
    QString m_x11Cookie;
    int m_x11Screen = 0;
    QList<X11Bridge*> m_x11Bridges;
    void* m_abstract = nullptr;

    // Tunnels
    QList<TunnelConfig> m_tunnelConfigs;
    QList<SshTunnel*> m_tunnels;
};
