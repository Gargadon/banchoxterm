#pragma once
#include <QObject>
#include <QTcpServer>
#include <QTcpSocket>
#include <QList>
#include "session.h"
#include <libssh2.h>

class SshTunnel : public QObject {
    Q_OBJECT
public:
    SshTunnel(LIBSSH2_SESSION* sshSession, const TunnelConfig& config, QObject* parent = nullptr);
    ~SshTunnel() override;

    bool start();
    void stop();
    void poll(); // Invocado periódicamente para leer de los canales SSH y escribir en los sockets

private slots:
    void onNewConnection();
    void onSocketReadyRead();
    void onSocketDisconnected();

private:
    struct ChannelBridge {
        QTcpSocket* socket = nullptr;
        LIBSSH2_CHANNEL* channel = nullptr;
        bool socksHandshakeDone = false;
        int socksStep = 0; // 0 = wait greeting, 1 = wait request
        bool channelCloseSent = false;
        bool socketEof = false;
    };

    LIBSSH2_SESSION* m_sshSession;
    TunnelConfig m_config;
    QTcpServer* m_tcpServer = nullptr;
    LIBSSH2_LISTENER* m_listener = nullptr; // Para Remote forwarding
    QList<ChannelBridge*> m_bridges;

    void handleSocksHandshake(ChannelBridge* bridge);
    void closeBridge(ChannelBridge* bridge);
};
