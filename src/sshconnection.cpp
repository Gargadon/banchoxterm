#include "sshconnection.h"
#include <QFile>
#include <QFileInfo>
#include <QThread>
#include <cstring>
#include <mutex>

#ifdef Q_OS_WIN
#include <winsock2.h>
#include <ws2tcpip.h>
using SockLenT = int;
#else
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
using SockLenT = socklen_t;
#endif

namespace {

#ifdef Q_OS_WIN
int lastError() {
    return WSAGetLastError();
}
bool wouldBlock(int err) {
    return err == WSAEWOULDBLOCK || err == WSAEINPROGRESS;
}
void closeSocketFd(int sock) {
    ::closesocket(sock);
}
#else
int lastError() {
    return errno;
}
bool wouldBlock(int err) {
    return err == EWOULDBLOCK || err == EINPROGRESS;
}
void closeSocketFd(int sock) {
    ::close(sock);
}
#endif

void setNonBlocking(int sock) {
#ifdef Q_OS_WIN
    u_long mode = 1;
    ioctlsocket(sock, FIONBIO, &mode);
#else
    int flags = fcntl(sock, F_GETFL, 0);
    fcntl(sock, F_SETFL, flags | O_NONBLOCK);
#endif
}

} // namespace

SshConnection::SshConnection() {
}

SshConnection::~SshConnection() {
    disconnectFromHost();
}

bool SshConnection::openSocket() {
    m_sock = static_cast<int>(::socket(AF_INET, SOCK_STREAM, 0));
    if (m_sock < 0) {
        emit connectionFailed(QString("socket() failed: %1").arg(lastError()));
        return false;
    }

    struct addrinfo hints;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;

    struct addrinfo* res = nullptr;
    QByteArray portBytes = QString::number(m_port).toUtf8();
    int rc = getaddrinfo(m_host.toUtf8().constData(), portBytes.constData(), &hints, &res);
    if (rc != 0 || !res) {
        emit connectionFailed(QString("Could not resolve host %1").arg(m_host));
        closeSocketFd(m_sock);
        m_sock = -1;
        return false;
    }

    setNonBlocking(m_sock);
    int cres = ::connect(m_sock, res->ai_addr, static_cast<SockLenT>(res->ai_addrlen));
    freeaddrinfo(res);

    if (cres != 0 && !wouldBlock(lastError())) {
        emit connectionFailed(QString("connect() failed: %1").arg(lastError()));
        closeSocketFd(m_sock);
        m_sock = -1;
        return false;
    }

    // Wait for the connection to complete (writable) with a 10s timeout.
    fd_set wfds;
    FD_ZERO(&wfds);
    FD_SET(m_sock, &wfds);
    struct timeval tv;
    tv.tv_sec = 10;
    tv.tv_usec = 0;
    int sres = select(m_sock + 1, nullptr, &wfds, nullptr, &tv);
    if (sres <= 0) {
        emit connectionFailed("TCP connection timeout");
        closeSocketFd(m_sock);
        m_sock = -1;
        return false;
    }

    int soerr = 0;
    SockLenT len = sizeof(soerr);
    getsockopt(m_sock, SOL_SOCKET, SO_ERROR, reinterpret_cast<char*>(&soerr), &len);
    if (soerr != 0) {
        emit connectionFailed(QString("TCP connect error: %1").arg(soerr));
        closeSocketFd(m_sock);
        m_sock = -1;
        return false;
    }

    return true;
}

void SshConnection::closeSocket() {
    if (m_sock >= 0) {
        closeSocketFd(m_sock);
        m_sock = -1;
    }
}

bool SshConnection::waitSocket(int timeoutMs) {
    if (!m_session)
        return false;

    int dir = libssh2_session_block_directions(m_session);
    fd_set rfds;
    fd_set wfds;
    FD_ZERO(&rfds);
    FD_ZERO(&wfds);
    if (dir & LIBSSH2_SESSION_BLOCK_INBOUND)
        FD_SET(m_sock, &rfds);
    if (dir & LIBSSH2_SESSION_BLOCK_OUTBOUND)
        FD_SET(m_sock, &wfds);

    struct timeval tv;
    tv.tv_sec = timeoutMs / 1000;
    tv.tv_usec = (timeoutMs % 1000) * 1000;

    int rc = select(m_sock + 1, &rfds, &wfds, nullptr, &tv);
    return rc > 0;
}

int SshConnection::retry(const std::function<int()>& fn) {
    int rc;
    while ((rc = fn()) == LIBSSH2_ERROR_EAGAIN) {
        if (!waitSocket(15000))
            return -1;
    }
    return rc;
}

void SshConnection::connectToHost(const QString& host, int port, const QString& user, const QString& keyPath,
                                  const QString& password) {
    m_host = host;
    m_port = port;
    m_user = user;
    m_keyPath = keyPath;

    static std::once_flag init_flag;
    std::call_once(init_flag, []() { libssh2_init(0); });

    disconnectFromHost();

    if (!openSocket())
        return;

    m_session = libssh2_session_init();
    if (!m_session) {
        emit connectionFailed("Failed to initialize SSH session");
        closeSocket();
        return;
    }

    libssh2_session_set_blocking(m_session, 0);

    int rc = retry([this]() { return libssh2_session_handshake(m_session, m_sock); });
    if (rc != 0) {
        emit connectionFailed(QString("SSH handshake failed: %1").arg(rc));
        disconnectFromHost();
        return;
    }

    bool authenticated = false;

    if (!keyPath.isEmpty()) {
        rc = retry([this, &keyPath]() {
            return libssh2_userauth_publickey_fromfile(m_session, m_user.toUtf8().constData(), nullptr,
                                                       keyPath.toUtf8().constData(), nullptr);
        });
        if (rc == 0)
            authenticated = true;
    }

    if (!authenticated && !password.isEmpty()) {
        rc = retry([this, &password]() {
            return libssh2_userauth_password(m_session, m_user.toUtf8().constData(), password.toUtf8().constData());
        });
        if (rc == 0)
            authenticated = true;
    }

    if (!authenticated && keyPath.isEmpty() && password.isEmpty()) {
        LIBSSH2_AGENT* agent = libssh2_agent_init(m_session);
        if (agent) {
            if (libssh2_agent_connect(agent) == 0) {
                if (libssh2_agent_list_identities(agent) == 0) {
                    struct libssh2_agent_publickey* identity = nullptr;
                    struct libssh2_agent_publickey* prev_identity = nullptr;
                    while (libssh2_agent_get_identity(agent, &identity, prev_identity) == 0) {
                        rc = retry([this, &agent, &identity]() {
                            return libssh2_agent_userauth(agent, m_user.toUtf8().constData(), identity);
                        });
                        if (rc == 0) {
                            authenticated = true;
                            break;
                        }
                        prev_identity = identity;
                    }
                }
                libssh2_agent_disconnect(agent);
            }
            libssh2_agent_free(agent);
        }
    }

    if (!authenticated) {
        emit passwordRequired("Password required for " + user + "@" + host);
        disconnectFromHost();
        return;
    }

    m_sftp = libssh2_sftp_init(m_session);
    if (!m_sftp) {
        emit connectionFailed("Failed to initialize SFTP session");
        disconnectFromHost();
        return;
    }

    openShell();

    m_pollTimer = new QTimer(this);
    connect(m_pollTimer, &QTimer::timeout, this, &SshConnection::onPollTimer);
    m_pollTimer->start(15);

    m_statsTimer = new QTimer(this);
    connect(m_statsTimer, &QTimer::timeout, this, &SshConnection::queryStats);
    m_statsTimer->start(8000);
    QTimer::singleShot(500, this, &SshConnection::queryStats);

    m_connected = true;
    emit connectionSuccess();
}

void SshConnection::openShell() {
    m_channel = libssh2_channel_open_session(m_session);
    if (!m_channel) {
        emit connectionFailed("Failed to open shell channel");
        return;
    }

    int rc = retry([this]() { return libssh2_channel_request_pty(m_channel, "xterm-256color"); });
    if (rc != 0) {
        emit connectionFailed("Failed to request PTY");
        return;
    }

    libssh2_channel_request_pty_size(m_channel, m_ptyCols, m_ptyRows);

    rc = retry([this]() { return libssh2_channel_shell(m_channel); });
    if (rc != 0) {
        emit connectionFailed("Failed to start shell");
        return;
    }

    // Enable OSC 7 working directory reporting so the SFTP browser can follow
    // terminal navigation (bash and zsh).
    static const char* integration = "if [ -n \"$BASH_VERSION\" ]; then "
                                     "PROMPT_COMMAND='printf \"\\033]7;file://%s\\007\" \"$PWD\"'; "
                                     "elif [ -n \"$ZSH_VERSION\" ]; then "
                                     "precmd(){ printf \"\\033]7;file://%s\\007\" \"$PWD\"; }; fi\n";
    libssh2_channel_write(m_channel, integration, std::strlen(integration));
}

void SshConnection::readShell() {
    if (!m_channel)
        return;

    LIBSSH2_POLLFD pfds[1];
    pfds[0].type = LIBSSH2_POLLFD_CHANNEL;
    pfds[0].fd.channel = m_channel;
    pfds[0].events = LIBSSH2_POLLFD_POLLIN;
    pfds[0].revents = 0;

    int prc = libssh2_poll(pfds, 1, 0);
    if (prc < 0)
        return;

    if (pfds[0].revents & LIBSSH2_POLLFD_POLLIN) {
        char buf[8192];
        ssize_t n;
        while ((n = libssh2_channel_read(m_channel, buf, sizeof(buf))) > 0) {
            emit shellDataReceived(QByteArray(buf, static_cast<int>(n)));
        }
        if (n < 0 && n != LIBSSH2_ERROR_EAGAIN) {
            emit shellClosed();
            return;
        }
    }

    if (libssh2_channel_eof(m_channel)) {
        // Drain any remaining buffered data before reporting closure.
        char buf[8192];
        ssize_t n;
        while ((n = libssh2_channel_read(m_channel, buf, sizeof(buf))) > 0) {
            emit shellDataReceived(QByteArray(buf, static_cast<int>(n)));
        }
        emit shellClosed();
    }
}

void SshConnection::onPollTimer() {
    if (!m_connected)
        return;
    readShell();
}

void SshConnection::sendToShell(const QByteArray& data) {
    if (!m_channel || data.isEmpty())
        return;

    const char* ptr = data.constData();
    ssize_t remaining = data.size();
    while (remaining > 0) {
        ssize_t written = libssh2_channel_write(m_channel, ptr, remaining);
        if (written == LIBSSH2_ERROR_EAGAIN) {
            if (!waitSocket(5000))
                break;
            continue;
        }
        if (written <= 0)
            break;
        ptr += written;
        remaining -= written;
    }
}

void SshConnection::resizePty(int rows, int cols) {
    if (rows <= 0 || cols <= 0)
        return;
    m_ptyRows = rows;
    m_ptyCols = cols;
    if (m_channel) {
        libssh2_channel_request_pty_size(m_channel, cols, rows);
    }
}

void SshConnection::listDirectory(const QString& path) {
    if (!m_sftp) {
        emit operationFinished(false, "SFTP session not active");
        return;
    }

    LIBSSH2_SFTP_HANDLE* handle = libssh2_sftp_opendir(m_sftp, path.toUtf8().constData());
    if (!handle) {
        emit operationFinished(false, "Failed to open remote directory: " + path);
        return;
    }

    QList<SftpFile> files;
    char filename[512];
    LIBSSH2_SFTP_ATTRIBUTES attrs;

    while (true) {
        int rc = retry([this, &handle, &filename, &attrs]() {
            return libssh2_sftp_readdir(handle, filename, sizeof(filename), &attrs);
        });
        if (rc <= 0)
            break;

        QString name = QString::fromUtf8(filename);
        if (name == "." || name == "..")
            continue;

        SftpFile file;
        file.name = name;
        file.isDirectory =
            (attrs.flags & LIBSSH2_SFTP_ATTR_PERMISSIONS) ? LIBSSH2_SFTP_S_ISDIR(attrs.permissions) : false;
        file.size = (attrs.flags & LIBSSH2_SFTP_ATTR_SIZE) ? attrs.filesize : 0;
        file.mtime =
            (attrs.flags & LIBSSH2_SFTP_ATTR_ACMODTIME) ? QDateTime::fromSecsSinceEpoch(attrs.mtime) : QDateTime();

        files.append(file);
    }

    libssh2_sftp_closedir(handle);
    emit directoryListed(path, files);
}

void SshConnection::downloadFile(const QString& remotePath, const QString& localPath) {
    if (!m_sftp) {
        emit operationFinished(false, "SFTP session not active");
        return;
    }

    LIBSSH2_SFTP_HANDLE* handle = libssh2_sftp_open(m_sftp, remotePath.toUtf8().constData(), LIBSSH2_FXF_READ, 0);
    if (!handle) {
        emit operationFinished(false, "Failed to open remote file: " + remotePath);
        return;
    }

    QFile localFile(localPath);
    if (!localFile.open(QIODevice::WriteOnly)) {
        libssh2_sftp_close(handle);
        emit operationFinished(false, "Failed to open local file: " + localPath);
        return;
    }

    char buffer[32768];
    while (true) {
        int bytesRead = retry([this, &handle, &buffer]() { return libssh2_sftp_read(handle, buffer, sizeof(buffer)); });
        if (bytesRead < 0) {
            localFile.close();
            libssh2_sftp_close(handle);
            emit operationFinished(false, "Error reading remote file: " + remotePath);
            return;
        }
        if (bytesRead == 0)
            break;
        localFile.write(buffer, bytesRead);
    }

    localFile.close();
    libssh2_sftp_close(handle);
    emit operationFinished(true, "Download finished successfully: " + QFileInfo(remotePath).fileName());
}

void SshConnection::uploadFile(const QString& localPath, const QString& remotePath) {
    if (!m_sftp) {
        emit operationFinished(false, "SFTP session not active");
        return;
    }

    QFile localFile(localPath);
    if (!localFile.open(QIODevice::ReadOnly)) {
        emit operationFinished(false, "Failed to open local file: " + localPath);
        return;
    }

    LIBSSH2_SFTP_HANDLE* handle = libssh2_sftp_open(
        m_sftp, remotePath.toUtf8().constData(), LIBSSH2_FXF_WRITE | LIBSSH2_FXF_CREAT | LIBSSH2_FXF_TRUNC,
        LIBSSH2_SFTP_S_IRUSR | LIBSSH2_SFTP_S_IWUSR | LIBSSH2_SFTP_S_IRGRP | LIBSSH2_SFTP_S_IROTH);
    if (!handle) {
        localFile.close();
        emit operationFinished(false, "Failed to create remote file: " + remotePath);
        return;
    }

    char buffer[32768];
    while (true) {
        qint64 bytesRead = localFile.read(buffer, sizeof(buffer));
        if (bytesRead <= 0)
            break;

        char* ptr = buffer;
        qint64 bytesToWrite = bytesRead;
        while (bytesToWrite > 0) {
            int bytesWritten =
                retry([this, &handle, &ptr, &bytesToWrite]() { return libssh2_sftp_write(handle, ptr, bytesToWrite); });
            if (bytesWritten < 0) {
                localFile.close();
                libssh2_sftp_close(handle);
                emit operationFinished(false, "Error writing remote file: " + remotePath);
                return;
            }
            bytesToWrite -= bytesWritten;
            ptr += bytesWritten;
        }
    }

    localFile.close();
    libssh2_sftp_close(handle);
    emit operationFinished(true, "Upload finished successfully: " + QFileInfo(localPath).fileName());
}

void SshConnection::deleteFile(const QString& remotePath, bool isDir) {
    if (!m_sftp) {
        emit operationFinished(false, "SFTP session not active");
        return;
    }

    int rc;
    if (isDir) {
        rc = retry([this, &remotePath]() { return libssh2_sftp_rmdir(m_sftp, remotePath.toUtf8().constData()); });
    } else {
        rc = retry([this, &remotePath]() { return libssh2_sftp_unlink(m_sftp, remotePath.toUtf8().constData()); });
    }

    if (rc == 0) {
        emit operationFinished(true, "Deleted item successfully");
    } else {
        emit operationFinished(false, QString("Delete failed (Error code %1)").arg(rc));
    }
}

void SshConnection::queryStats() {
    if (!m_session)
        return;

    LIBSSH2_CHANNEL* channel = libssh2_channel_open_session(m_session);
    if (!channel)
        return;

    QString cmd = "read cpu u n s id iw irq soft steal rest < /proc/stat; previdle=$((id + iw)); prevtotal=$((u + n + "
                  "s + id + iw + irq + soft + steal)); sleep 0.2; "
                  "read cpu u2 n2 s2 id2 iw2 irq2 soft2 steal2 rest < /proc/stat; idle=$((id2 + iw2)); total=$((u2 + "
                  "n2 + s2 + id2 + iw2 + irq2 + soft2 + steal2)); diffidle=$((idle - previdle)); "
                  "difftotal=$((total - prevtotal)); if [ $difftotal -eq 0 ]; then echo 0; else echo \"$((100 * "
                  "(difftotal - diffidle) / difftotal))\"; fi; "
                  "awk '/MemTotal/{t=$2} /MemAvailable/{a=$2} END{printf \"%.0f\\n\", 100*(t-a)/t}' /proc/meminfo; "
                  "df / | tail -n 1 | awk '{print $5}' | sed 's/%//'; "
                  "cat /proc/uptime | awk '{print $1}'";

    int rc = retry([this, &channel, &cmd]() { return libssh2_channel_exec(channel, cmd.toUtf8().constData()); });
    if (rc == 0) {
        QByteArray response;
        char buffer[256];
        while (true) {
            int bytesRead =
                retry([this, &channel, &buffer]() { return libssh2_channel_read(channel, buffer, sizeof(buffer)); });
            if (bytesRead <= 0)
                break;
            response.append(buffer, bytesRead);
        }

        QString resStr = QString::fromUtf8(response).trimmed();
        QStringList lines = resStr.split('\n');
        if (lines.size() >= 4) {
            double cpu = lines[0].toDouble();
            double mem = lines[1].toDouble();
            double disk = lines[2].toDouble();
            double uptimeSecs = lines[3].toDouble();
            emit remoteStatsUpdated(cpu, mem, disk, uptimeSecs);
        }
    }

    libssh2_channel_close(channel);
    libssh2_channel_free(channel);
}

void SshConnection::disconnectFromHost() {
    m_connected = false;

    if (m_pollTimer) {
        m_pollTimer->stop();
        delete m_pollTimer;
        m_pollTimer = nullptr;
    }
    if (m_statsTimer) {
        m_statsTimer->stop();
        delete m_statsTimer;
        m_statsTimer = nullptr;
    }
    if (m_channel) {
        libssh2_channel_close(m_channel);
        libssh2_channel_free(m_channel);
        m_channel = nullptr;
    }
    if (m_sftp) {
        libssh2_sftp_shutdown(m_sftp);
        m_sftp = nullptr;
    }
    if (m_session) {
        libssh2_session_disconnect(m_session, "Disconnecting");
        libssh2_session_free(m_session);
        m_session = nullptr;
    }
    closeSocket();
}
