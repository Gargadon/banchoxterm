#include "ftpclient.h"
#include <QTcpSocket>
#include <QSslSocket>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>

FtpClient::FtpClient(QObject* parent) : QObject(parent) {
}

FtpClient::~FtpClient() {
    disconnectFromHost();
}

void FtpClient::connectToHost(const QString& host, int port, const QString& user, const QString& password, bool tls) {
    disconnectFromHost();

    m_host = host;
    m_port = port;
    m_tls = tls;

    if (m_tls) {
        auto* ssl = new QSslSocket(this);
        ssl->setPeerVerifyMode(QSslSocket::VerifyPeer);
        ssl->setPeerVerifyName(host);
        m_control = ssl;
    } else {
        m_control = new QTcpSocket(this);
    }
    connect(m_control, &QIODevice::readyRead, this, [this]() {
        m_replyBuffer += m_control->readAll();
        while (true) {
            int nl = m_replyBuffer.indexOf('\n');
            if (nl < 0)
                break;
            QByteArray line = m_replyBuffer.left(nl);
            if (line.endsWith('\r'))
                line.chop(1);
            m_replyBuffer.remove(0, nl + 1);

            if (line.size() < 3)
                continue;
            bool ok = false;
            const int code = line.left(3).toInt(&ok);
            if (!ok)
                continue;

            m_replyLines.append(QString::fromUtf8(line.mid(4)));
            const bool final = (line.size() < 4) || line[3] == ' ';
            if (final) {
                Reply r;
                r.code = code;
                r.lines = m_replyLines;
                m_replyLines.clear();
                if (m_pendingHandler) {
                    auto h = std::move(m_pendingHandler);
                    m_pendingHandler = nullptr;
                    h(r);
                }
            }
        }
    });
    connect(m_control, &QAbstractSocket::errorOccurred, this, [this](QAbstractSocket::SocketError) {
        if (!m_connected) {
            emit connectionFailed(tr("FTP connection failed: %1").arg(m_control->errorString()));
        }
    });

    // Greeting (220) -> AUTH TLS (optional) -> USER -> PASS -> TYPE I.
    auto finishLogin = [this](const Reply& r) {
        if (r.code == 230 || r.code == 202) {
            auto finishType = [this](const Reply&) {
                auto markConnected = [this](const Reply&) {
                    m_connected = true;
                    emit connectionSuccess();
                };
                if (!m_tls) {
                    markConnected(Reply{});
                    return;
                }
                sendCommand("PBSZ 0", [this, markConnected](const Reply& pbsz) {
                    if (pbsz.code < 200 || pbsz.code >= 300) {
                        emit connectionFailed(tr("FTPS PBSZ negotiation failed"));
                        return;
                    }
                    sendCommand("PROT P", [this, markConnected](const Reply& prot) {
                        if (prot.code < 200 || prot.code >= 300) {
                            emit connectionFailed(tr("FTPS data protection negotiation failed"));
                            return;
                        }
                        markConnected(Reply{});
                    });
                });
            };
            sendCommand("TYPE I", finishType);
        } else {
            emit connectionFailed(tr("FTP login failed"));
        }
    };

    auto startLogin = [this, user, password, finishLogin]() {
        sendCommand("USER " + user, [this, user, password, finishLogin](const Reply& r) {
            if (r.code == 331) {
                sendCommand("PASS " + password, finishLogin);
            } else if (r.code == 230) {
                finishLogin(r);
            } else {
                emit connectionFailed(tr("FTP login failed (code %1)").arg(r.code));
            }
        });
    };

    sendCommand("", [this, startLogin](const Reply&) {
        if (!m_tls) {
            startLogin();
            return;
        }
        sendCommand("AUTH TLS", [this, startLogin](const Reply& r) {
            if (r.code != 234 && r.code != 334) {
                emit connectionFailed(tr("Server does not support explicit FTPS"));
                return;
            }
            auto* ssl = qobject_cast<QSslSocket*>(m_control);
            if (!ssl) {
                emit connectionFailed(tr("FTPS socket initialization failed"));
                return;
            }
            connect(ssl, &QSslSocket::encrypted, this, startLogin, Qt::SingleShotConnection);
            ssl->startClientEncryption();
        });
    });

    m_control->connectToHost(host, static_cast<quint16>(port));
}

void FtpClient::disconnectFromHost() {
    m_connected = false;
    if (m_control) {
        m_control->disconnect(this);
        m_control->deleteLater();
        m_control = nullptr;
    }
    m_replyBuffer.clear();
    m_replyLines.clear();
    m_pendingHandler = nullptr;
}

void FtpClient::sendCommand(const QString& cmd, std::function<void(const Reply&)> handler) {
    if (!m_control)
        return;
    m_pendingHandler = std::move(handler);
    if (!cmd.isEmpty())
        m_control->write(cmd.toUtf8() + "\r\n");
}

void FtpClient::expectReply(std::function<void(const Reply&)> handler) {
    m_pendingHandler = std::move(handler);
}

void FtpClient::openDataConnection(std::function<void(QTcpSocket*)> onConnected) {
    sendCommand("PASV", [this, onConnected](const Reply& r) {
        if (r.code != 227) {
            emit operationFinished(false, tr("PASV failed: %1").arg(r.lines.join(' ')));
            return;
        }
        const QString text = r.lines.join(' ');
        const QRegularExpression re(QStringLiteral(R"(\((\d+),(\d+),(\d+),(\d+),(\d+),(\d+)\))"));
        const auto m = re.match(text);
        if (!m.hasMatch()) {
            emit operationFinished(false, tr("Could not parse PASV reply"));
            return;
        }
        const QString host = QString("%1.%2.%3.%4").arg(m.captured(1), m.captured(2), m.captured(3), m.captured(4));
        const int port = m.captured(5).toInt() * 256 + m.captured(6).toInt();

        QTcpSocket* data = nullptr;
        if (m_tls) {
            auto* ssl = new QSslSocket(this);
            ssl->setPeerVerifyMode(QSslSocket::VerifyPeer);
            ssl->setPeerVerifyName(m_host);
            data = ssl;
            connect(ssl, &QTcpSocket::connected, ssl, &QSslSocket::startClientEncryption);
            connect(
                ssl, &QSslSocket::encrypted, this, [onConnected, data]() { onConnected(data); },
                Qt::SingleShotConnection);
        } else {
            data = new QTcpSocket(this);
            connect(data, &QTcpSocket::connected, this, [onConnected, data]() { onConnected(data); });
        }
        connect(data, &QTcpSocket::errorOccurred, this, [this, data](QAbstractSocket::SocketError) {
            emit operationFinished(false, tr("FTP data connection failed"));
            data->deleteLater();
        });
        data->connectToHost(host, static_cast<quint16>(port));
    });
}

void FtpClient::listDirectory(const QString& path) {
    if (!m_connected)
        return;
    const QString target = path.isEmpty() ? QStringLiteral(".") : path;
    openDataConnection([this, target](QTcpSocket* data) {
        auto* buffer = new QByteArray();
        connect(data, &QTcpSocket::readyRead, this, [data, buffer]() { buffer->append(data->readAll()); });
        connect(data, &QTcpSocket::disconnected, this, [this, data, buffer, target]() {
            expectReply([this, data, buffer, target](const Reply&) {
                const QByteArray listing = *buffer;
                delete buffer;
                data->deleteLater();
                emit directoryListed(target, parseListing(listing));
            });
        });
        sendCommand("LIST " + target, [](const Reply&) { /* 150 */ });
    });
}

void FtpClient::downloadFile(const QString& remotePath, const QString& localPath) {
    if (!m_connected)
        return;
    openDataConnection([this, remotePath, localPath](QTcpSocket* data) {
        QFile* file = new QFile(localPath, this);
        if (!file->open(QIODevice::WriteOnly)) {
            emit operationFinished(false, tr("Failed to open local file: %1").arg(localPath));
            delete file;
            data->deleteLater();
            return;
        }
        connect(data, &QTcpSocket::readyRead, this, [data, file]() { file->write(data->readAll()); });
        connect(data, &QTcpSocket::disconnected, this, [this, data, file, remotePath]() {
            expectReply([this, data, file, remotePath](const Reply&) {
                file->close();
                delete file;
                data->deleteLater();
                emit operationFinished(true, tr("Download finished: %1").arg(QFileInfo(remotePath).fileName()));
            });
        });
        sendCommand("RETR " + remotePath, [](const Reply&) { /* 150 */ });
    });
}

void FtpClient::uploadFile(const QString& localPath, const QString& remotePath) {
    if (!m_connected)
        return;
    QFile* file = new QFile(localPath, this);
    if (!file->open(QIODevice::ReadOnly)) {
        emit operationFinished(false, tr("Failed to open local file: %1").arg(localPath));
        delete file;
        return;
    }
    openDataConnection([this, file, localPath, remotePath](QTcpSocket* data) {
        sendCommand("STOR " + remotePath, [this, data, file, localPath, remotePath](const Reply& r) {
            if (r.code != 150 && r.code != 125) {
                file->close();
                delete file;
                data->deleteLater();
                emit operationFinished(false, tr("STOR failed"));
                return;
            }
            const QByteArray payload = file->readAll();
            file->close();
            delete file;

            connect(data, &QTcpSocket::disconnected, this, [this, data, localPath, remotePath]() {
                expectReply([this, data, localPath, remotePath](const Reply&) {
                    data->deleteLater();
                    emit operationFinished(true, tr("Upload finished: %1").arg(QFileInfo(localPath).fileName()));
                });
            });

            if (!payload.isEmpty())
                data->write(payload);
            data->disconnectFromHost(); // flush pending writes, then close
        });
    });
}

void FtpClient::deleteFile(const QString& remotePath, bool isDir) {
    if (!m_connected)
        return;
    const QString cmd = isDir ? "RMD " : "DELE ";
    sendCommand(cmd + remotePath, [this](const Reply& r) {
        emit operationFinished(r.code == 250, r.code == 250 ? tr("Deleted successfully") : tr("Delete failed"));
    });
}

void FtpClient::createDirectory(const QString& path) {
    if (!m_connected)
        return;
    sendCommand("MKD " + path, [this](const Reply& r) {
        emit operationFinished(r.code == 257, r.code == 257 ? tr("Folder created") : tr("MKD failed"));
    });
}

void FtpClient::renamePath(const QString& oldPath, const QString& newPath) {
    if (!m_connected)
        return;
    sendCommand("RNFR " + oldPath, [this, newPath](const Reply& r) {
        if (r.code != 350) {
            emit operationFinished(false, tr("Rename failed"));
            return;
        }
        sendCommand("RNTO " + newPath, [this](const Reply& r2) {
            emit operationFinished(r2.code == 250, r2.code == 250 ? tr("Renamed successfully") : tr("Rename failed"));
        });
    });
}

QList<SftpFile> FtpClient::parseListing(const QByteArray& data) {
    QList<SftpFile> files;
    const QRegularExpression re(
        QStringLiteral(R"(^([\-d])[\-rwxstST]{9}\s+\d+\s+\S+\s+\S+\s+(\d+)\s+\S+\s+\S+\s+\S+\s+(.+)$)"));

    const QList<QByteArray> lines = data.split('\n');
    for (const QByteArray& line : lines) {
        QString s = QString::fromUtf8(line).trimmed();
        if (s.isEmpty() || s.startsWith("total "))
            continue;
        const auto m = re.match(s);
        if (m.hasMatch()) {
            SftpFile f;
            f.isDirectory = (m.captured(1) == "d");
            f.size = m.captured(2).toLongLong();
            f.name = m.captured(3);
            files.append(f);
        } else {
            // Fallback: treat the last token as the name (best effort).
            const QStringList parts = s.split(QRegularExpression("\\s+"), Qt::SkipEmptyParts);
            if (parts.isEmpty())
                continue;
            SftpFile f;
            f.name = parts.last();
            files.append(f);
        }
    }
    return files;
}
