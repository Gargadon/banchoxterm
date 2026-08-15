#include "sshtunnel.h"
#include <QHostAddress>
#include <QtEndian>

SshTunnel::SshTunnel(LIBSSH2_SESSION* sshSession, const TunnelConfig& config, QObject* parent)
    : QObject(parent), m_sshSession(sshSession), m_config(config) {
}

SshTunnel::~SshTunnel() {
    stop();
}

bool SshTunnel::start() {
    if (m_config.type == TunnelConfig::Type::Local || m_config.type == TunnelConfig::Type::Dynamic) {
        m_tcpServer = new QTcpServer(this);
        connect(m_tcpServer, &QTcpServer::newConnection, this, &SshTunnel::onNewConnection);

        // Escuchar localmente en el puerto configurado
        if (!m_tcpServer->listen(QHostAddress::LocalHost, m_config.localPort)) {
            m_tcpServer->deleteLater();
            m_tcpServer = nullptr;
            return false;
        }
        return true;
    } else if (m_config.type == TunnelConfig::Type::Remote) {
        // Para reenvío remoto, le pedimos al servidor SSH que escuche en su puerto
        libssh2_session_set_blocking(m_sshSession, 1);
        m_listener = libssh2_channel_forward_listen_ex(m_sshSession, "0.0.0.0", m_config.remotePort, nullptr, 16);
        libssh2_session_set_blocking(m_sshSession, 0);

        return m_listener != nullptr;
    }
    return false;
}

void SshTunnel::stop() {
    if (m_tcpServer) {
        m_tcpServer->close();
        m_tcpServer->deleteLater();
        m_tcpServer = nullptr;
    }

    if (m_listener) {
        libssh2_session_set_blocking(m_sshSession, 1);
        libssh2_channel_forward_cancel(m_listener);
        libssh2_session_set_blocking(m_sshSession, 0);
        m_listener = nullptr;
    }

    for (ChannelBridge* bridge : m_bridges) {
        closeBridge(bridge);
    }
    m_bridges.clear();
}

void SshTunnel::onNewConnection() {
    while (m_tcpServer && m_tcpServer->hasPendingConnections()) {
        QTcpSocket* socket = m_tcpServer->nextPendingConnection();
        if (!socket)
            continue;

        ChannelBridge* bridge = new ChannelBridge();
        bridge->socket = socket;

        connect(socket, &QTcpSocket::readyRead, this, &SshTunnel::onSocketReadyRead);
        connect(socket, &QTcpSocket::disconnected, this, &SshTunnel::onSocketDisconnected);

        if (m_config.type == TunnelConfig::Type::Local) {
            // Abrir canal SSH direct-tcpip en modo bloqueante temporal
            libssh2_session_set_blocking(m_sshSession, 1);
            LIBSSH2_CHANNEL* channel = libssh2_channel_direct_tcpip(
                m_sshSession, m_config.remoteHost.toLatin1().constData(), m_config.remotePort);
            libssh2_session_set_blocking(m_sshSession, 0);

            if (channel) {
                bridge->channel = channel;
                bridge->socksHandshakeDone = true;
                m_bridges.append(bridge);
            } else {
                socket->close();
                socket->deleteLater();
                delete bridge;
            }
        } else if (m_config.type == TunnelConfig::Type::Dynamic) {
            // Esperar el protocolo de saludo SOCKS5
            bridge->socksHandshakeDone = false;
            bridge->socksStep = 0;
            m_bridges.append(bridge);
        }
    }
}

void SshTunnel::onSocketReadyRead() {
    QTcpSocket* socket = qobject_cast<QTcpSocket*>(sender());
    if (!socket)
        return;

    ChannelBridge* bridge = nullptr;
    for (ChannelBridge* b : m_bridges) {
        if (b->socket == socket) {
            bridge = b;
            break;
        }
    }

    if (!bridge)
        return;

    if (!bridge->socksHandshakeDone) {
        handleSocksHandshake(bridge);
        return;
    }

    if (!bridge->channel)
        return;

    QByteArray data = socket->readAll();
    if (!data.isEmpty()) {
        libssh2_session_set_blocking(m_sshSession, 1);
        int written = 0;
        while (written < data.size()) {
            int w = libssh2_channel_write(bridge->channel, data.constData() + written, data.size() - written);
            if (w <= 0)
                break;
            written += w;
        }
        libssh2_session_set_blocking(m_sshSession, 0);
    }
}

void SshTunnel::handleSocksHandshake(ChannelBridge* bridge) {
    QTcpSocket* socket = bridge->socket;

    if (bridge->socksStep == 0) {
        // 1. Saludo SOCKS5: [VER, NMETHODS, METHODS...]
        if (socket->bytesAvailable() < 2)
            return;

        char verAndMethods[2];
        socket->peek(verAndMethods, 2);
        if (verAndMethods[0] != 0x05) {
            closeBridge(bridge);
            m_bridges.removeOne(bridge);
            return;
        }

        int numMethods = static_cast<unsigned char>(verAndMethods[1]);
        if (socket->bytesAvailable() < 2 + numMethods)
            return;

        socket->read(2);          // Descartar los dos primeros bytes ya leídos
        socket->read(numMethods); // Descartar métodos

        // Responder con NO AUTHENTICATIONREQUIRED (0x05, 0x00)
        char response[2] = {0x05, 0x00};
        socket->write(response, 2);

        bridge->socksStep = 1;
    }

    if (bridge->socksStep == 1) {
        // 2. Solicitud SOCKS5: [VER, CMD, RSV, ATYP, DST.ADDR, DST.PORT]
        if (socket->bytesAvailable() < 4)
            return;

        char header[4];
        socket->peek(header, 4);
        char atyp = header[3];

        int expectedBytes = 4;
        if (atyp == 0x01) { // IPv4
            expectedBytes += 4 + 2;
        } else if (atyp == 0x03) { // Nombre de dominio
            if (socket->bytesAvailable() < 5)
                return;
            char domainLenChar;
            // Peek de longitud de dominio (byte 4)
            socket->peek(&domainLenChar, 1);
            int domainLen =
                static_cast<unsigned char>(domainLenChar); // Necesitamos leer la longitud del dominio del buffer real
            // Para peek seguro con offset de 4:
            QByteArray peekBuf = socket->peek(5);
            domainLen = static_cast<unsigned char>(peekBuf[4]);
            expectedBytes += 1 + domainLen + 2;
        } else {
            // Protocolo no soportado (ej. IPv6 0x04)
            closeBridge(bridge);
            m_bridges.removeOne(bridge);
            return;
        }

        if (socket->bytesAvailable() < expectedBytes)
            return;

        socket->read(4); // Descartar cabecera

        QString host;
        if (atyp == 0x01) {
            QByteArray ipBytes = socket->read(4);
            host = QString("%1.%2.%3.%4")
                       .arg(static_cast<unsigned char>(ipBytes[0]))
                       .arg(static_cast<unsigned char>(ipBytes[1]))
                       .arg(static_cast<unsigned char>(ipBytes[2]))
                       .arg(static_cast<unsigned char>(ipBytes[3]));
        } else if (atyp == 0x03) {
            int domainLen = static_cast<unsigned char>(socket->read(1)[0]);
            host = QString::fromUtf8(socket->read(domainLen));
        }

        QByteArray portBytes = socket->read(2);
        quint16 port = qFromBigEndian<quint16>(reinterpret_cast<const uchar*>(portBytes.constData()));

        // Abrir canal direct-tcpip mediante el túnel SSH
        libssh2_session_set_blocking(m_sshSession, 1);
        LIBSSH2_CHANNEL* channel = libssh2_channel_direct_tcpip(m_sshSession, host.toLatin1().constData(), port);
        libssh2_session_set_blocking(m_sshSession, 0);

        if (channel) {
            bridge->channel = channel;
            bridge->socksHandshakeDone = true;
            // Responder éxito de conexión SOCKS5
            char resp[10] = {0x05, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
            socket->write(resp, 10);
        } else {
            // Responder fallo
            char resp[10] = {0x05, 0x01, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
            socket->write(resp, 10);
            socket->flush();
            closeBridge(bridge);
            m_bridges.removeOne(bridge);
        }
    }
}

void SshTunnel::onSocketDisconnected() {
    QTcpSocket* socket = qobject_cast<QTcpSocket*>(sender());
    if (!socket)
        return;

    for (int i = 0; i < m_bridges.size(); ++i) {
        if (m_bridges[i]->socket == socket) {
            closeBridge(m_bridges[i]);
            m_bridges.removeAt(i);
            break;
        }
    }
}

void SshTunnel::closeBridge(ChannelBridge* bridge) {
    if (bridge->socket) {
        bridge->socket->close();
        bridge->socket->deleteLater();
    }
    if (bridge->channel) {
        libssh2_session_set_blocking(m_sshSession, 1);
        libssh2_channel_close(bridge->channel);
        libssh2_channel_free(bridge->channel);
        libssh2_session_set_blocking(m_sshSession, 0);
    }
    delete bridge;
}

void SshTunnel::poll() {
    // 1. Aceptar conexiones en Remote forwarding (si aplica)
    if (m_listener) {
        LIBSSH2_CHANNEL* channel = libssh2_channel_forward_accept(m_listener);
        if (channel) {
            QTcpSocket* socket = new QTcpSocket(this);
            socket->connectToHost(m_config.remoteHost, m_config.remotePort);

            // Esperar conexión brevemente de forma bloqueante
            if (socket->waitForConnected(100)) {
                ChannelBridge* bridge = new ChannelBridge();
                bridge->socket = socket;
                bridge->channel = channel;
                bridge->socksHandshakeDone = true;

                connect(socket, &QTcpSocket::readyRead, this, &SshTunnel::onSocketReadyRead);
                connect(socket, &QTcpSocket::disconnected, this, &SshTunnel::onSocketDisconnected);

                m_bridges.append(bridge);
            } else {
                socket->deleteLater();
                libssh2_session_set_blocking(m_sshSession, 1);
                libssh2_channel_close(channel);
                libssh2_channel_free(channel);
                libssh2_session_set_blocking(m_sshSession, 0);
            }
        }
    }

    // 2. Transferir datos de los canales SSH a los Sockets locales de los puentes activos
    for (auto it = m_bridges.begin(); it != m_bridges.end();) {
        ChannelBridge* bridge = *it;
        if (!bridge->channel || !bridge->socksHandshakeDone) {
            ++it;
            continue;
        }

        char buf[8192];
        ssize_t n;
        while ((n = libssh2_channel_read(bridge->channel, buf, sizeof(buf))) > 0) {
            bridge->socket->write(buf, n);
        }

        if (n < 0 && n != LIBSSH2_ERROR_EAGAIN) {
            bridge->socketEof = true;
        }

        if (libssh2_channel_eof(bridge->channel)) {
            bridge->socketEof = true;
        }

        // Si el canal SSH está cerrado y el socket local no tiene datos por enviar, destruimos el puente
        if (bridge->socketEof && bridge->socket->bytesToWrite() == 0) {
            closeBridge(bridge);
            it = m_bridges.erase(it);
        } else {
            ++it;
        }
    }
}
