#include "sshconnection.h"
#include "apppaths.h"
#include <QFile>
#include <QFileInfo>
#include <QDir>
#include <QThread>
#include <QProcess>
#include <QRegularExpression>
#include <QSocketNotifier>
#include <QTimer>
#include <cstring>
#include <cstdlib>
#include <QSettings>
#include <mutex>
#include <QDir>
#include <QCryptographicHash>
#include <QApplication>
#include <QMessageBox>
#include <QInputDialog>

#ifdef Q_OS_WIN
#include <winsock2.h>
#include <ws2tcpip.h>
using SockLenT = int;
#else
#include <sys/socket.h>
#include <sys/un.h>
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
ssize_t sockRead(int sock, void* buf, size_t len) {
    return ::recv(sock, static_cast<char*>(buf), static_cast<int>(len), 0);
}
ssize_t sockWrite(int sock, const void* buf, size_t len) {
    return ::send(sock, static_cast<const char*>(buf), static_cast<int>(len), 0);
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
ssize_t sockRead(int sock, void* buf, size_t len) {
    return ::read(sock, buf, len);
}
ssize_t sockWrite(int sock, const void* buf, size_t len) {
    return ::write(sock, buf, len);
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

QString hostKeyTypeName(int type) {
    switch (type) {
    case LIBSSH2_HOSTKEY_TYPE_RSA:
        return QStringLiteral("ssh-rsa");
    case LIBSSH2_HOSTKEY_TYPE_DSS:
        return QStringLiteral("ssh-dss");
    case LIBSSH2_HOSTKEY_TYPE_ECDSA_256:
        return QStringLiteral("ecdsa-sha2-nistp256");
    case LIBSSH2_HOSTKEY_TYPE_ECDSA_384:
        return QStringLiteral("ecdsa-sha2-nistp384");
    case LIBSSH2_HOSTKEY_TYPE_ECDSA_521:
        return QStringLiteral("ecdsa-sha2-nistp521");
    case LIBSSH2_HOSTKEY_TYPE_ED25519:
        return QStringLiteral("ssh-ed25519");
    default:
        return QStringLiteral("ssh-unknown");
    }
}

int knownhostKeyType(int type) {
    // Map the libssh2 session host key type to the LIBSSH2_KNOWNHOST_KEY_*
    // bit that libssh2_knownhost_addc() requires (the key type must be set in
    // the typemask, otherwise it fails with LIBSSH2_ERROR_INVAL).
    switch (type) {
    case LIBSSH2_HOSTKEY_TYPE_RSA:
        return LIBSSH2_KNOWNHOST_KEY_SSHRSA;
    case LIBSSH2_HOSTKEY_TYPE_DSS:
        return LIBSSH2_KNOWNHOST_KEY_SSHDSS;
    case LIBSSH2_HOSTKEY_TYPE_ECDSA_256:
        return LIBSSH2_KNOWNHOST_KEY_ECDSA_256;
    case LIBSSH2_HOSTKEY_TYPE_ECDSA_384:
        return LIBSSH2_KNOWNHOST_KEY_ECDSA_384;
    case LIBSSH2_HOSTKEY_TYPE_ECDSA_521:
        return LIBSSH2_KNOWNHOST_KEY_ECDSA_521;
    case LIBSSH2_HOSTKEY_TYPE_ED25519:
        return LIBSSH2_KNOWNHOST_KEY_ED25519;
    default:
        return LIBSSH2_KNOWNHOST_KEY_UNKNOWN;
    }
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
    if (dir == 0) {
        // libssh2 does not need to wait on the socket (it has buffered data to
        // process); yield briefly to avoid a busy loop, then let the caller
        // retry the call immediately.
#ifdef Q_OS_WIN
        Sleep(10);
#else
        usleep(10000);
#endif
        return true;
    }

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

QString SshConnection::knownHostsPath() const {
    return AppPaths::configDir() + QStringLiteral("/known_hosts");
}

QString SshConnection::hostKeyFingerprint(const QByteArray& key) const {
    QByteArray hash = QCryptographicHash::hash(key, QCryptographicHash::Sha256);
    QString b64 = QString::fromLatin1(hash.toBase64());
    b64.remove('=');
    return QStringLiteral("SHA256:") + b64;
}

bool SshConnection::promptHostKey(const QString& host, const QString& fingerprint, const QString& keyType,
                                  bool changed) {
    bool accept = false;

    auto showDialog = [&]() {
        QString text;
        if (changed) {
            text = tr("WARNING: The host key for '%1' has CHANGED!\n\n"
                      "This could mean someone is intercepting your connection (man-in-the-middle attack)\n"
                      "or that the server administrator changed the key.\n\n"
                      "New key fingerprint (%2):\n%3\n\n"
                      "Continue connecting anyway?")
                       .arg(host, keyType, fingerprint);
        } else {
            text = tr("The authenticity of host '%1' can't be established.\n\n"
                      "%2 key fingerprint:\n%3\n\n"
                      "Are you sure you want to continue connecting and remember this key?")
                       .arg(host, keyType, fingerprint);
        }

        QMessageBox box;
        box.setIcon(changed ? QMessageBox::Warning : QMessageBox::Question);
        box.setWindowTitle(tr("Host Key Verification"));
        box.setText(text);
        box.setStandardButtons(QMessageBox::Yes | QMessageBox::No);
        box.setDefaultButton(QMessageBox::No);
        accept = (box.exec() == QMessageBox::Yes);
    };

    if (QThread::currentThread() == qApp->thread()) {
        showDialog();
    } else {
        QMetaObject::invokeMethod(qApp, showDialog, Qt::BlockingQueuedConnection);
    }
    return accept;
}

bool SshConnection::verifyHostKey() {
    size_t keyLen = 0;
    int keyType = LIBSSH2_HOSTKEY_TYPE_UNKNOWN;
    const char* key = libssh2_session_hostkey(m_session, &keyLen, &keyType);
    if (!key || keyLen == 0)
        return true;

    const int typemask = LIBSSH2_KNOWNHOST_TYPE_PLAIN | LIBSSH2_KNOWNHOST_KEYENC_RAW | knownhostKeyType(keyType);

    LIBSSH2_KNOWNHOSTS* hosts = libssh2_knownhost_init(m_session);
    if (!hosts)
        return true;

    QString path = knownHostsPath();
    if (QFile::exists(path)) {
        libssh2_knownhost_readfile(hosts, path.toUtf8().constData(), LIBSSH2_KNOWNHOST_FILE_OPENSSH);
    }

    struct libssh2_knownhost* store = nullptr;
    int rc = libssh2_knownhost_checkp(hosts, m_host.toUtf8().constData(), m_port, key, keyLen, typemask, &store);

    bool ok = true;
    bool modified = false;

    if (rc == LIBSSH2_KNOWNHOST_CHECK_MATCH) {
        ok = true;
    } else if (rc == LIBSSH2_KNOWNHOST_CHECK_MISMATCH) {
        ok = promptHostKey(m_host, hostKeyFingerprint(QByteArray(key, static_cast<int>(keyLen))),
                           hostKeyTypeName(keyType), true);
        if (ok && store) {
            libssh2_knownhost_del(hosts, store);
            modified = true;
        }
    } else {
        // NOTFOUND or FAILURE: prompt for first-time trust.
        ok = promptHostKey(m_host, hostKeyFingerprint(QByteArray(key, static_cast<int>(keyLen))),
                           hostKeyTypeName(keyType), false);
        if (ok)
            modified = true;
    }

    if (ok && modified) {
        struct libssh2_knownhost* added = nullptr;
        if (libssh2_knownhost_addc(hosts, m_host.toUtf8().constData(), nullptr, key, keyLen, nullptr, 0, typemask,
                                   &added) == 0) {
            QDir().mkpath(QFileInfo(path).absolutePath());
            libssh2_knownhost_writefile(hosts, path.toUtf8().constData(), LIBSSH2_KNOWNHOST_FILE_OPENSSH);
        }
    }

    libssh2_knownhost_free(hosts);
    return ok;
}

void SshConnection::kbdIntResponseCallback(const char* name, int name_len, const char* instruction,
                                           int instruction_len, int num_prompts,
                                           const LIBSSH2_USERAUTH_KBDINT_PROMPT* prompts,
                                           LIBSSH2_USERAUTH_KBDINT_RESPONSE* responses, void** abstract) {
    SshConnection* self = static_cast<SshConnection*>(*abstract);
    if (!self) {
        for (int i = 0; i < num_prompts; ++i) {
            responses[i].text = nullptr;
            responses[i].length = 0;
        }
        return;
    }
    self->handleKbdInt(name, name_len, instruction, instruction_len, num_prompts, prompts, responses);
}

void SshConnection::handleKbdInt(const char* name, int name_len, const char* instruction, int instruction_len,
                                 int num_prompts, const LIBSSH2_USERAUTH_KBDINT_PROMPT* prompts,
                                 LIBSSH2_USERAUTH_KBDINT_RESPONSE* responses) {
    const QString nameStr = QString::fromUtf8(name, name_len);
    const QString instrStr = QString::fromUtf8(instruction, instruction_len);

    QList<QByteArray> promptTexts;
    QList<bool> echoFlags;
    for (int i = 0; i < num_prompts; ++i) {
        promptTexts.append(QByteArray(reinterpret_cast<const char*>(prompts[i].text),
                                      static_cast<int>(prompts[i].length)));
        echoFlags.append(prompts[i].echo != 0);
    }

    QStringList answers;
    const bool ok = promptKbdInteractive(nameStr, instrStr, promptTexts, echoFlags, answers);

    for (int i = 0; i < num_prompts; ++i) {
        if (ok && i < answers.size() && !answers[i].isEmpty()) {
            const QByteArray a = answers[i].toUtf8();
            responses[i].text = static_cast<char*>(malloc(static_cast<size_t>(a.size()) + 1));
            if (responses[i].text) {
                memcpy(responses[i].text, a.constData(), static_cast<size_t>(a.size()));
                responses[i].text[a.size()] = '\0';
                responses[i].length = static_cast<unsigned int>(a.size());
            } else {
                responses[i].length = 0;
            }
        } else {
            responses[i].text = nullptr;
            responses[i].length = 0;
        }
    }
}

bool SshConnection::promptKbdInteractive(const QString& name, const QString& instruction,
                                         const QList<QByteArray>& promptTexts, const QList<bool>& echoFlags,
                                         QStringList& answers) {
    bool ok = false;

    auto doPrompt = [&]() {
        QString header;
        if (!name.isEmpty())
            header += name + "\n";
        if (!instruction.isEmpty())
            header += instruction + "\n";

        for (int i = 0; i < promptTexts.size(); ++i) {
            const QString prompt = QString::fromUtf8(promptTexts[i]);
            bool okOne = false;
            QString answer = QInputDialog::getText(nullptr, tr("SSH Authentication"),
                                                   header + prompt,
                                                   echoFlags[i] ? QLineEdit::Password : QLineEdit::Normal, "", &okOne);
            if (!okOne) {
                answers.clear();
                ok = false;
                return;
            }
            answers.append(answer);
        }
        ok = true;
    };

    if (QThread::currentThread() == qApp->thread()) {
        doPrompt();
    } else {
        QMetaObject::invokeMethod(qApp, doPrompt, Qt::BlockingQueuedConnection);
    }
    return ok;
}

void SshConnection::connectToHost(const QString& host, int port, const QString& user, const QString& keyPath,
                                  const QString& password, const QList<TunnelConfig>& tunnels) {
    m_host = host;
    m_port = port;
    m_user = user;
    m_keyPath = keyPath;
    m_tunnelConfigs = tunnels;
    m_abstract = this;

    static std::once_flag init_flag;
    std::call_once(init_flag, []() { libssh2_init(0); });

    disconnectFromHost();

    if (!openSocket())
        return;

    m_session = libssh2_session_init_ex(nullptr, nullptr, nullptr, &m_abstract);
    if (!m_session) {
        emit connectionFailed("Failed to initialize SSH session");
        closeSocket();
        return;
    }

    libssh2_session_set_blocking(m_session, 0);

    // Per-session algorithm preferences (empty = libssh2 defaults).
    if (!m_cryptCipher.isEmpty()) {
        libssh2_session_method_pref(m_session, LIBSSH2_METHOD_CRYPT_CS, m_cryptCipher.toUtf8().constData());
        libssh2_session_method_pref(m_session, LIBSSH2_METHOD_CRYPT_SC, m_cryptCipher.toUtf8().constData());
    }
    if (!m_macAlgo.isEmpty()) {
        libssh2_session_method_pref(m_session, LIBSSH2_METHOD_MAC_CS, m_macAlgo.toUtf8().constData());
        libssh2_session_method_pref(m_session, LIBSSH2_METHOD_MAC_SC, m_macAlgo.toUtf8().constData());
    }
    if (!m_kexAlgo.isEmpty()) {
        libssh2_session_method_pref(m_session, LIBSSH2_METHOD_KEX, m_kexAlgo.toUtf8().constData());
    }

    if (m_x11Forwarding) {
        libssh2_session_callback_set2(m_session, LIBSSH2_CALLBACK_X11,
                                      reinterpret_cast<libssh2_cb_generic*>(&SshConnection::x11OpenCallback));
    }

    int rc = retry([this]() { return libssh2_session_handshake(m_session, m_sock); });
    if (rc != 0) {
        char* errmsg = nullptr;
        libssh2_session_last_error(m_session, &errmsg, nullptr, 0);
        emit connectionFailed(QString("SSH handshake failed: %1 (%2)").arg(rc).arg(errmsg ? errmsg : "?"));
        disconnectFromHost();
        return;
    }

    if (!verifyHostKey()) {
        emit connectionFailed("Host key verification failed");
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
            if (retry([&agent]() { return libssh2_agent_connect(agent); }) == 0) {
                if (retry([&agent]() { return libssh2_agent_list_identities(agent); }) == 0) {
                    struct libssh2_agent_publickey* identity = nullptr;
                    struct libssh2_agent_publickey* prev_identity = nullptr;
                    while (retry([&agent, &identity, &prev_identity]() {
                               return libssh2_agent_get_identity(agent, &identity, prev_identity);
                           }) == 0) {
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
        // Keyboard-interactive: used for OTP/2FA and password prompts.
        rc = retry([this]() {
            return libssh2_userauth_keyboard_interactive_ex(m_session, m_user.toUtf8().constData(),
                                                            static_cast<unsigned int>(m_user.toUtf8().size()),
                                                            &SshConnection::kbdIntResponseCallback);
        });
        if (rc == 0)
            authenticated = true;
    }

    if (!authenticated) {
        emit passwordRequired("Password required for " + user + "@" + host);
        disconnectFromHost();
        return;
    }

    m_sftp = retryPtr([this]() { return libssh2_sftp_init(m_session); });
    if (!m_sftp) {
        emit connectionFailed("Failed to initialize SFTP session");
        disconnectFromHost();
        return;
    }

    openShell();

    // Event-driven I/O: instead of a 15 ms polling timer, drive the libssh2
    // session from socket notifiers on the SSH socket. The read notifier stays
    // armed (shell/SFTP/X11 data arrives asynchronously); the write notifier is
    // armed only while libssh2 actually needs to send (armNotifiers()).
    m_readNotifier = new QSocketNotifier(m_sock, QSocketNotifier::Read, this);
    m_writeNotifier = new QSocketNotifier(m_sock, QSocketNotifier::Write, this);
    connect(m_readNotifier, &QSocketNotifier::activated, this, &SshConnection::onSocketActivity);
    connect(m_writeNotifier, &QSocketNotifier::activated, this, &SshConnection::onSocketActivity);
    m_readNotifier->setEnabled(true);
    m_writeNotifier->setEnabled(false);

    m_statsTimer = new QTimer(this);
    connect(m_statsTimer, &QTimer::timeout, this, &SshConnection::startStats);
    m_statsTimer->start(8000);
    QTimer::singleShot(500, this, &SshConnection::startStats);

    if (m_keepAliveSeconds > 0) {
        libssh2_keepalive_config(m_session, 1, m_keepAliveSeconds);
        m_keepAliveTimer = new QTimer(this);
        connect(m_keepAliveTimer, &QTimer::timeout, this, &SshConnection::onKeepAlive);
        m_keepAliveTimer->start(m_keepAliveSeconds * 1000);
    }

    m_connected = true;

    // Start active tunnels
    for (const TunnelConfig& config : m_tunnelConfigs) {
        SshTunnel* tunnel = new SshTunnel(m_session, config, this);
        if (tunnel->start()) {
            m_tunnels.append(tunnel);
        } else {
            delete tunnel;
        }
    }

    emit connectionSuccess();
}

void SshConnection::openShell() {
    m_channel = retryPtr([this]() { return libssh2_channel_open_session(m_session); });
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

    if (m_x11Forwarding) {
        // Empty auth protocol/cookie means "no access control" (the local X
        // server, e.g. VcXsrv/X410 on Windows, must accept local connections).
        const char* proto = m_x11Cookie.isEmpty() ? "" : "MIT-MAGIC-COOKIE-1";
        int xrc = retry([this, proto]() {
            return libssh2_channel_x11_req_ex(m_channel, 0, proto, m_x11Cookie.toLatin1().constData(), m_x11Screen);
        });
        if (xrc != 0) {
            m_x11Forwarding = false;
        }
    }

    rc = retry([this]() { return libssh2_channel_shell(m_channel); });
    if (rc != 0) {
        emit connectionFailed("Failed to start shell");
        return;
    }

    // Enable OSC 7 working directory reporting so the SFTP browser can follow
    // terminal navigation (bash and zsh), if enabled in settings. The trailing clear-screen
    // erases the command's own echo so the user does not see the injected snippet.
    QSettings settings;
    if (settings.value("terminal/shellIntegration", true).toBool()) {
        static const char* integration = "if [ -n \"$BASH_VERSION\" ]; then "
                                         "PROMPT_COMMAND='printf \"\\033]7;file://%s\\007\" \"$PWD\"'; "
                                         "elif [ -n \"$ZSH_VERSION\" ]; then "
                                         "precmd(){ printf \"\\033]7;file://%s\\007\" \"$PWD\"; }; fi"
                                         "; printf '\\033[2J\\033[H'\n";
        libssh2_channel_write(m_channel, integration, std::strlen(integration));
    }
}

void SshConnection::readShell() {
    if (!m_channel || !m_connected)
        return;

    // libssh2_channel_read() drives the transport read itself, pulling pending
    // packets off the OS socket into the session queue, so it is safe to call
    // from a socket notifier. (A libssh2_poll() gate would miss data still
    // sitting in the kernel socket buffer, since poll only inspects the
    // already-queued packets.)
    //
    // All chunks read in this pass are accumulated into one buffer and emitted
    // as a single queued signal. shellDataReceived crosses a thread boundary, so
    // emitting one signal per 8 KiB chunk would swamp the UI thread's event
    // queue with signal deliveries and buffer copies under heavy output.
    char buf[8192];
    ssize_t n;
    QByteArray data;
    while ((n = libssh2_channel_read(m_channel, buf, sizeof(buf))) > 0) {
        m_activityProgress = true;
        data.append(buf, static_cast<int>(n));
    }
    if (!data.isEmpty())
        emit shellDataReceived(data);
    if (n < 0 && n != LIBSSH2_ERROR_EAGAIN) {
        handleShellClosed();
        return;
    }

    if (libssh2_channel_eof(m_channel)) {
        // Drain any remaining buffered data before reporting closure.
        data.clear();
        while ((n = libssh2_channel_read(m_channel, buf, sizeof(buf))) > 0) {
            m_activityProgress = true;
            data.append(buf, static_cast<int>(n));
        }
        if (!data.isEmpty())
            emit shellDataReceived(data);
        handleShellClosed();
    }
}

void SshConnection::handleShellClosed() {
    m_connected = false;
    stopNotifiers();
    if (m_statsTimer)
        m_statsTimer->stop();
    if (m_keepAliveTimer)
        m_keepAliveTimer->stop();
    emit shellClosed();
}

void SshConnection::onSocketActivity() {
    if (!m_connected)
        return;
    m_activityProgress = false;
    readShell();
    if (!m_connected)
        return;
    pollX11Bridges();
    pollStats();

    for (SshTunnel* t : m_tunnels) {
        t->poll();
    }
    armNotifiers();

    // libssh2 can buffer data internally and return LIBSSH2_ERROR_EAGAIN even
    // though there is nothing pending on the OS socket (block_directions == 0).
    // In that case no socket notifier will ever fire, so the non-blocking state
    // machines (stats, tunnels) would stall forever. Kick them again on the next
    // event-loop turn so buffered data is processed.
    //
    // Guarded by m_activityProgress: only re-arm the kick while the state
    // machines actually consumed data in this pass. Once there is no buffered
    // progress left, stop kicking and let the always-armed read notifier wake us
    // when real network data arrives, instead of spinning the event loop.
    if (m_connected && m_session &&
        libssh2_session_block_directions(m_session) == 0 &&
        m_statsState != StatsState::Idle &&
        m_activityProgress && !m_socketKickPending) {
        m_socketKickPending = true;
        QTimer::singleShot(0, this, [this]() {
            m_socketKickPending = false;
            onSocketActivity();
        });
    }
}

void SshConnection::armNotifiers() {
    if (!m_readNotifier || !m_writeNotifier)
        return;
    if (!m_session || !m_connected) {
        m_readNotifier->setEnabled(false);
        m_writeNotifier->setEnabled(false);
        return;
    }
    // Inbound data can arrive at any time, so the read notifier stays armed.
    m_readNotifier->setEnabled(true);
    // Arm the write notifier only while libssh2 needs to send; while the socket
    // is writable a level-triggered write notifier would fire continuously.
    m_writeNotifier->setEnabled(libssh2_session_block_directions(m_session) & LIBSSH2_SESSION_BLOCK_OUTBOUND);
}

void SshConnection::stopNotifiers() {
    if (m_readNotifier) {
        m_readNotifier->setEnabled(false);
        delete m_readNotifier;
        m_readNotifier = nullptr;
    }
    if (m_writeNotifier) {
        m_writeNotifier->setEnabled(false);
        delete m_writeNotifier;
        m_writeNotifier = nullptr;
    }
}

void SshConnection::setKeepAliveSeconds(int seconds) {
    m_keepAliveSeconds = qMax(0, seconds);
}

void SshConnection::setCipherAlgorithms(const QString& ciphers) {
    m_cryptCipher = ciphers.trimmed();
}

void SshConnection::setKexAlgorithm(const QString& kex) {
    m_kexAlgo = kex.trimmed();
}

void SshConnection::setMacAlgorithm(const QString& mac) {
    m_macAlgo = mac.trimmed();
}

void SshConnection::onKeepAlive() {
    if (!m_connected || !m_session)
        return;
    // Non-blocking: sends a keepalive only when it is due. Re-arm the notifiers
    // in case libssh2 now wants to write (the write notifier is otherwise only
    // armed from onSocketActivity()).
    libssh2_keepalive_send(m_session, nullptr);
    armNotifiers();
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

    LIBSSH2_SFTP_HANDLE* handle =
        retryPtr([this, &path]() { return libssh2_sftp_opendir(m_sftp, path.toUtf8().constData()); });
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

    LIBSSH2_SFTP_HANDLE* handle = retryPtr([this, &remotePath]() {
        return libssh2_sftp_open(m_sftp, remotePath.toUtf8().constData(), LIBSSH2_FXF_READ, 0);
    });
    if (!handle) {
        emit operationFinished(false, "Failed to open remote file: " + remotePath);
        return;
    }

    qint64 totalBytes = -1;
    LIBSSH2_SFTP_ATTRIBUTES attrs;
    memset(&attrs, 0, sizeof(attrs));
    if (libssh2_sftp_fstat(handle, &attrs) == 0 && (attrs.flags & LIBSSH2_SFTP_ATTR_SIZE))
        totalBytes = static_cast<qint64>(attrs.filesize);

    QFile localFile(localPath);
    if (!localFile.open(QIODevice::WriteOnly)) {
        libssh2_sftp_close(handle);
        emit operationFinished(false, "Failed to open local file: " + localPath);
        return;
    }

    const QString displayName = QFileInfo(remotePath).fileName();
    char buffer[32768];
    qint64 doneBytes = 0;
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
        doneBytes += bytesRead;
        emit transferProgress(displayName, doneBytes, totalBytes);
    }

    localFile.close();
    libssh2_sftp_close(handle);
    emit transferProgress(displayName, totalBytes, totalBytes);
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

    LIBSSH2_SFTP_HANDLE* handle = retryPtr([this, &remotePath]() {
        return libssh2_sftp_open(
            m_sftp, remotePath.toUtf8().constData(), LIBSSH2_FXF_WRITE | LIBSSH2_FXF_CREAT | LIBSSH2_FXF_TRUNC,
            LIBSSH2_SFTP_S_IRUSR | LIBSSH2_SFTP_S_IWUSR | LIBSSH2_SFTP_S_IRGRP | LIBSSH2_SFTP_S_IROTH);
    });
    if (!handle) {
        localFile.close();
        emit operationFinished(false, "Failed to create remote file: " + remotePath);
        return;
    }

    char buffer[32768];
    qint64 doneBytes = 0;
    const qint64 totalBytes = localFile.size();
    const QString displayName = QFileInfo(localPath).fileName();
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
            doneBytes += bytesWritten;
        }
        emit transferProgress(displayName, doneBytes, totalBytes);
    }

    localFile.close();
    libssh2_sftp_close(handle);
    emit transferProgress(displayName, totalBytes, totalBytes);
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

void SshConnection::createDirectory(const QString& path) {
    if (!m_sftp) {
        emit operationFinished(false, "SFTP session not active");
        return;
    }
    int rc = retry([this, &path]() { return libssh2_sftp_mkdir(m_sftp, path.toUtf8().constData(), 0755); });
    if (rc == 0) {
        emit operationFinished(true, "Folder created successfully");
    } else {
        emit operationFinished(false, QString("Failed to create folder (Error code %1)").arg(rc));
    }
}

void SshConnection::renamePath(const QString& oldPath, const QString& newPath) {
    if (!m_sftp) {
        emit operationFinished(false, "SFTP session not active");
        return;
    }
    int rc = retry([this, &oldPath, &newPath]() {
        return libssh2_sftp_rename(m_sftp, oldPath.toUtf8().constData(), newPath.toUtf8().constData());
    });
    if (rc == 0) {
        emit operationFinished(true, "Renamed successfully");
    } else {
        emit operationFinished(false, QString("Rename failed (Error code %1)").arg(rc));
    }
}

void SshConnection::chmodPath(const QString& path, int mode) {
    if (!m_sftp) {
        emit operationFinished(false, "SFTP session not active");
        return;
    }
    LIBSSH2_SFTP_ATTRIBUTES attrs;
    memset(&attrs, 0, sizeof(attrs));
    attrs.flags = LIBSSH2_SFTP_ATTR_PERMISSIONS;
    attrs.permissions = static_cast<unsigned long>(mode);
    int rc = retry([this, &path, &attrs]() { return libssh2_sftp_setstat(m_sftp, path.toUtf8().constData(), &attrs); });
    if (rc == 0) {
        emit operationFinished(true, "Permissions updated successfully");
    } else {
        emit operationFinished(false, QString("chmod failed (Error code %1)").arg(rc));
    }
}

bool SshConnection::uploadOneFile(const QString& localPath, const QString& remotePath) {
    QFile f(localPath);
    if (!f.open(QIODevice::ReadOnly))
        return false;

    LIBSSH2_SFTP_HANDLE* handle = retryPtr([this, &remotePath]() {
        return libssh2_sftp_open(m_sftp, remotePath.toUtf8().constData(),
                                 LIBSSH2_FXF_WRITE | LIBSSH2_FXF_CREAT | LIBSSH2_FXF_TRUNC,
                                 LIBSSH2_SFTP_S_IRUSR | LIBSSH2_SFTP_S_IWUSR | LIBSSH2_SFTP_S_IRGRP | LIBSSH2_SFTP_S_IROTH);
    });
    if (!handle) {
        f.close();
        return false;
    }

    char buffer[32768];
    bool ok = true;
    qint64 doneBytes = 0;
    const qint64 totalBytes = f.size();
    const QString displayName = QFileInfo(localPath).fileName();
    while (true) {
        qint64 n = f.read(buffer, sizeof(buffer));
        if (n <= 0)
            break;
        char* p = buffer;
        qint64 remaining = n;
        while (remaining > 0) {
            int w = retry([this, &handle, &p, &remaining]() { return libssh2_sftp_write(handle, p, remaining); });
            if (w < 0) {
                ok = false;
                break;
            }
            p += w;
            remaining -= w;
            doneBytes += w;
        }
        if (!ok)
            break;
        emit transferProgress(displayName, doneBytes, totalBytes);
    }

    f.close();
    libssh2_sftp_close(handle);
    emit transferProgress(displayName, totalBytes, totalBytes);
    return ok;
}

bool SshConnection::uploadDirRecursive(const QString& localDir, const QString& remoteDir) {
    // mkdir; ignore "already exists" errors (best effort).
    retry([this, &remoteDir]() { return libssh2_sftp_mkdir(m_sftp, remoteDir.toUtf8().constData(), 0755); });

    QDir dir(localDir);
    const QFileInfoList entries = dir.entryInfoList(QDir::Dirs | QDir::Files | QDir::NoDotAndDotDot | QDir::Hidden);
    for (const QFileInfo& info : entries) {
        QString remotePath = remoteDir;
        if (!remotePath.endsWith('/'))
            remotePath += '/';
        remotePath += info.fileName();

        if (info.isDir()) {
            if (!uploadDirRecursive(info.absoluteFilePath(), remotePath))
                return false;
        } else {
            if (!uploadOneFile(info.absoluteFilePath(), remotePath))
                return false;
        }
    }
    return true;
}

void SshConnection::uploadDirectory(const QString& localPath, const QString& remoteBasePath) {
    if (!m_sftp) {
        emit operationFinished(false, "SFTP session not active");
        return;
    }

    QFileInfo li(localPath);
    if (!li.isDir()) {
        emit operationFinished(false, "Not a directory: " + localPath);
        return;
    }

    QString remoteRoot = remoteBasePath;
    if (!remoteRoot.endsWith('/'))
        remoteRoot += '/';
    remoteRoot += li.fileName();

    if (uploadDirRecursive(localPath, remoteRoot)) {
        emit operationFinished(true, "Folder uploaded successfully");
    } else {
        emit operationFinished(false, "Folder upload failed");
    }
}

void SshConnection::startStats() {
    if (!m_session || m_statsState != StatsState::Idle)
        return;
    m_statsState = StatsState::Opening;
    m_statsBuffer.clear();
    // No socket event necessarily fires to start the stats channel, so kick the
    // processing once; from then on the socket notifiers drive it.
    if (m_connected)
        onSocketActivity();
}

void SshConnection::pollStats() {
    switch (m_statsState) {
    case StatsState::Idle:
        return;
    case StatsState::Opening:
        m_statsChannel = libssh2_channel_open_session(m_session);
        if (m_statsChannel) {
            m_statsState = StatsState::Execing;
        } else if (libssh2_session_last_errno(m_session) != LIBSSH2_ERROR_EAGAIN) {
            m_statsState = StatsState::Idle;
        }
        break;
    case StatsState::Execing: {
        static const char* linuxCmd = "read cpu u n s id iw irq soft steal rest < /proc/stat; previdle=$((id + iw)); "
                                      "prevtotal=$((u + n + s + id + iw + irq + soft + steal)); sleep 0.2; "
                                      "read cpu u2 n2 s2 id2 iw2 irq2 soft2 steal2 rest < /proc/stat; idle=$((id2 + iw2)); "
                                      "total=$((u2 + n2 + s2 + id2 + iw2 + irq2 + soft2 + steal2)); "
                                      "diffidle=$((idle - previdle)); difftotal=$((total - prevtotal)); "
                                      "if [ $difftotal -eq 0 ]; then echo 0; else echo \"$((100 * (difftotal - diffidle) / "
                                      "difftotal))\"; fi; "
                                      "awk '/MemTotal/{t=$2} /MemAvailable/{a=$2} END{printf \"%.0f\\n\", 100*(t-a)/t}' "
                                      "/proc/meminfo; "
                                      "df / | tail -n 1 | awk '{print $5}' | sed 's/%//'; "
                                      "cat /proc/uptime | awk '{print $1}'";
        static const char* winCmd = R"(powershell -NoProfile -Command "$cpu=(Get-CimInstance Win32_Processor).LoadPercentage; $os=Get-CimInstance Win32_OperatingSystem; $mem=[math]::round(100*($os.TotalVisibleMemorySize-$os.FreePhysicalMemory)/$os.TotalVisibleMemorySize); $d=Get-CimInstance Win32_LogicalDisk -Filter 'DeviceID=''C:'''; $disk=[math]::round(100*($d.Size-$d.FreeSpace)/$d.Size); $up=((Get-Date)-$os.LastBootUpTime).TotalSeconds; Write-Output $cpu; Write-Output $mem; Write-Output $disk; Write-Output $up")";
        const char* cmd = m_remoteIsWindows ? winCmd : linuxCmd;
        int rc = libssh2_channel_exec(m_statsChannel, cmd);
        if (rc == 0) {
            m_statsState = StatsState::Reading;
        } else if (rc != LIBSSH2_ERROR_EAGAIN) {
            closeStats();
        }
        break;
    }
    case StatsState::Reading: {
        char buffer[256];
        while (true) {
            int bytesRead = libssh2_channel_read(m_statsChannel, buffer, sizeof(buffer));
            if (bytesRead > 0) {
                m_activityProgress = true;
                m_statsBuffer.append(buffer, bytesRead);
            } else if (bytesRead == 0) {
                finishStats();
                return;
            } else {
                break; // EAGAIN or error
            }
        }
        break;
    }
    }
}

void SshConnection::finishStats() {
    QString resStr = QString::fromUtf8(m_statsBuffer).trimmed();
    QStringList lines = resStr.split('\n');
    if (lines.size() >= 4) {
        double cpu = lines[0].toDouble();
        double mem = lines[1].toDouble();
        double disk = lines[2].toDouble();
        double uptimeSecs = lines[3].toDouble();
        emit remoteStatsUpdated(cpu, mem, disk, uptimeSecs);
    } else if (!m_remoteIsWindows) {
        // The Linux /proc command failed: the remote host is probably Windows.
        // Switch to the PowerShell probe for subsequent polls.
        m_remoteIsWindows = true;
    }
    closeStats();
}

void SshConnection::closeStats() {
    if (m_statsChannel) {
        libssh2_channel_close(m_statsChannel);
        libssh2_channel_free(m_statsChannel);
        m_statsChannel = nullptr;
    }
    m_statsState = StatsState::Idle;
}

void SshConnection::setX11Forwarding(bool enabled) {
    m_x11Forwarding = enabled;
    if (!enabled)
        return;

#ifdef Q_OS_WIN
    // No DISPLAY/xauth on Windows. Assume a local X server (VcXsrv, X410, Xming)
    // listening on TCP 6000 with no access control. Probe it before enabling.
    m_x11Display = QStringLiteral("127.0.0.1:0");
    m_x11Screen = 0;
    m_x11Cookie.clear();

    int probe = static_cast<int>(::socket(AF_INET, SOCK_STREAM, 0));
    if (probe < 0) {
        m_x11Forwarding = false;
        return;
    }
    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(6000);
    inet_pton(AF_INET, "127.0.0.1", &addr.sin_addr);
    int rc = ::connect(probe, reinterpret_cast<struct sockaddr*>(&addr), sizeof(addr));
    closeSocketFd(probe);
    if (rc != 0)
        m_x11Forwarding = false;
    return;
#else
    m_x11Display = QString::fromLocal8Bit(qgetenv("DISPLAY"));
    if (m_x11Display.isEmpty()) {
        m_x11Forwarding = false;
        return;
    }

    // Parse screen number (default 0) and strip it from the display string.
    int dotIdx = m_x11Display.lastIndexOf('.');
    int colonIdx = m_x11Display.lastIndexOf(':');
    if (dotIdx > colonIdx && colonIdx >= 0) {
        bool ok = false;
        int screen = m_x11Display.mid(dotIdx + 1).toInt(&ok);
        if (ok)
            m_x11Screen = screen;
    }

    QProcess proc;
    proc.start("xauth", {"list", m_x11Display});
    if (!proc.waitForFinished(2000)) {
        m_x11Forwarding = false;
        return;
    }
    QString out = QString::fromLocal8Bit(proc.readAllStandardOutput());
    const QStringList lines = out.split('\n');
    for (const QString& line : lines) {
        if (line.contains("MIT-MAGIC-COOKIE-1")) {
            const QStringList parts = line.split(QRegularExpression("\\s+"), Qt::SkipEmptyParts);
            if (parts.size() >= 3) {
                m_x11Cookie = parts.last().trimmed();
                return;
            }
        }
    }
    m_x11Forwarding = false;
#endif
}

int SshConnection::connectToXServer() {
    int colonIdx = m_x11Display.lastIndexOf(':');
    int dotIdx = m_x11Display.lastIndexOf('.');
    if (dotIdx < 0 || dotIdx < colonIdx)
        dotIdx = m_x11Display.size();

    QString hostPart = m_x11Display.left(colonIdx);
    int dispNum = m_x11Display.mid(colonIdx + 1, dotIdx - colonIdx - 1).toInt();

#ifndef Q_OS_WIN
    if (hostPart.isEmpty() || hostPart == "unix") {
        int sock = static_cast<int>(::socket(AF_UNIX, SOCK_STREAM, 0));
        if (sock >= 0) {
            struct sockaddr_un addr;
            memset(&addr, 0, sizeof(addr));
            addr.sun_family = AF_UNIX;
            QString path = QString("/tmp/.X11-unix/X%1").arg(dispNum);
            strncpy(addr.sun_path, path.toLatin1().constData(), sizeof(addr.sun_path) - 1);
            if (::connect(sock, reinterpret_cast<struct sockaddr*>(&addr), sizeof(addr)) == 0) {
                setNonBlocking(sock);
                return sock;
            }
            ::close(sock);
        }
    }
#endif

    // TCP: host:6000+display
    int sock = static_cast<int>(::socket(AF_INET, SOCK_STREAM, 0));
    if (sock < 0)
        return -1;
    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(6000 + dispNum);
    QString tcpHost = hostPart.isEmpty() ? QString("127.0.0.1") : hostPart;
    inet_pton(AF_INET, tcpHost.toLatin1().constData(), &addr.sin_addr);
    setNonBlocking(sock);
    if (::connect(sock, reinterpret_cast<struct sockaddr*>(&addr), sizeof(addr)) == 0) {
        return sock;
    }
    closeSocketFd(sock);
    return -1;
}

void SshConnection::x11OpenCallback(LIBSSH2_SESSION* session, LIBSSH2_CHANNEL* channel, const char* shost, int sport,
                                    void** abstract) {
    Q_UNUSED(session);
    SshConnection* self = static_cast<SshConnection*>(*abstract);
    if (self)
        self->handleX11Open(channel, shost, sport);
}

void SshConnection::handleX11Open(LIBSSH2_CHANNEL* channel, const char* shost, int sport) {
    Q_UNUSED(shost);
    Q_UNUSED(sport);

    int xSock = connectToXServer();
    if (xSock < 0) {
        libssh2_channel_close(channel);
        libssh2_channel_free(channel);
        return;
    }

    auto* bridge = new X11Bridge();
    bridge->channel = channel;
    bridge->xSock = xSock;
    // Forward X server -> SSH channel direction on its own notifier; the
    // channel -> X server direction is handled from the SSH socket notifier.
    bridge->xNotifier = new QSocketNotifier(xSock, QSocketNotifier::Read, this);
    connect(bridge->xNotifier, &QSocketNotifier::activated, this, [this, bridge]() {
        if (m_connected && m_x11Bridges.contains(bridge))
            pollX11Bridges();
    });
    m_x11Bridges.append(bridge);
}

void SshConnection::pollX11Bridges() {
    for (auto it = m_x11Bridges.begin(); it != m_x11Bridges.end();) {
        X11Bridge* b = *it;

        char buf[8192];
        ssize_t n;
        while ((n = libssh2_channel_read(b->channel, buf, sizeof(buf))) > 0) {
            m_activityProgress = true;
            ssize_t off = 0;
            while (off < n) {
                ssize_t w = sockWrite(b->xSock, buf + off, static_cast<size_t>(n - off));
                if (w <= 0) {
                    b->sockEof = true;
                    break;
                }
                off += w;
            }
        }
        if (n < 0 && n != LIBSSH2_ERROR_EAGAIN)
            b->channelEof = true;
        if (libssh2_channel_eof(b->channel))
            b->channelEof = true;

        while ((n = sockRead(b->xSock, buf, sizeof(buf))) > 0) {
            ssize_t off = 0;
            while (off < n) {
                ssize_t w = libssh2_channel_write(b->channel, buf + off, static_cast<size_t>(n - off));
                if (w == LIBSSH2_ERROR_EAGAIN) {
                    if (!waitSocket(1000))
                        break;
                    continue;
                }
                if (w <= 0)
                    break;
                off += w;
            }
        }
        if (n == 0)
            b->sockEof = true;

        if (b->channelEof && b->sockEof) {
            libssh2_channel_close(b->channel);
            libssh2_channel_free(b->channel);
            if (b->xNotifier) {
                b->xNotifier->setEnabled(false);
                delete b->xNotifier;
            }
            closeSocketFd(b->xSock);
            delete b;
            it = m_x11Bridges.erase(it);
        } else {
            ++it;
        }
    }
}

void SshConnection::disconnectFromHost() {
    m_connected = false;

    for (SshTunnel* t : m_tunnels) {
        t->stop();
        delete t;
    }
    m_tunnels.clear();
    m_tunnelConfigs.clear();

    closeStats();

    for (X11Bridge* b : m_x11Bridges) {
        if (b->xNotifier) {
            b->xNotifier->setEnabled(false);
            delete b->xNotifier;
        }
        if (b->channel) {
            libssh2_channel_close(b->channel);
            libssh2_channel_free(b->channel);
        }
        if (b->xSock >= 0)
            closeSocketFd(b->xSock);
        delete b;
    }
    m_x11Bridges.clear();

    stopNotifiers();
    if (m_statsTimer) {
        m_statsTimer->stop();
        delete m_statsTimer;
        m_statsTimer = nullptr;
    }
    if (m_keepAliveTimer) {
        m_keepAliveTimer->stop();
        delete m_keepAliveTimer;
        m_keepAliveTimer = nullptr;
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
