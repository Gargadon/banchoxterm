#include "ftpclient.h"
#include <QTcpSocket>
#include <QSslSocket>
#include <QSslCertificate>
#include <QSslConfiguration>
#include <QFile>
#include <QFileInfo>
#include <QDirIterator>
#include <QRegularExpression>
#include <QTimer>
#include <algorithm>

FtpClient::FtpClient(QObject* parent) : QObject(parent) {
}

FtpClient::~FtpClient() {
    disconnectFromHost();
}

void FtpClient::connectToHost(const QString& host, int port, const QString& user, const QString& password, bool tls,
                              int minimumTlsVersion, const QString& caFile, bool allowInvalidCertificates) {
    if (!m_autoReconnectInProgress)
        m_reconnectAttempts = 0;
    m_autoReconnectInProgress = false;
    disconnectFromHost();

    m_host = host;
    m_user = user;
    m_password = password;
    m_port = port;
    m_tls = tls;
    m_tlsMinimumVersion = minimumTlsVersion == 13 ? 13 : 12;
    m_tlsCaFile = caFile;
    m_tlsAllowInvalidCertificates = allowInvalidCertificates;

    if (m_tls) {
        auto* ssl = new QSslSocket(this);
        QString tlsError;
        if (!configureTlsSocket(ssl, &tlsError)) {
            emit connectionFailed(tlsError);
            ssl->deleteLater();
            return;
        }
        m_control = ssl;
        connect(ssl, &QSslSocket::sslErrors, this, [this, ssl](const QList<QSslError>& errors) {
            QStringList details;
            for (const QSslError& error : errors)
                details.append(error.errorString());
            const QString message = tr("FTPS certificate validation failed: %1").arg(details.join("; "));
            if (m_tlsAllowInvalidCertificates) {
                ssl->ignoreSslErrors(errors);
                return;
            }
            if (m_control)
                m_control->abort();
            emit connectionFailed(message);
        });
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
        } else {
            m_connected = false;
            emit connectionFailed(tr("FTP connection lost: %1").arg(m_control->errorString()));
            scheduleReconnect();
        }
    });
    connect(m_control, &QAbstractSocket::disconnected, this, [this]() {
        if (!m_connected)
            return;
        m_connected = false;
        emit connectionFailed(tr("FTP control connection lost"));
        scheduleReconnect();
    });

    // Greeting (220) -> AUTH TLS (optional) -> USER -> PASS -> TYPE I.
    auto finishLogin = [this](const Reply& r) {
        if (r.code == 230 || r.code == 202) {
            auto finishType = [this](const Reply&) {
                auto markConnected = [this](const Reply&) {
                    m_connected = true;
                    m_reconnectAttempts = 0;
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
    if (m_reconnectTimer) {
        m_reconnectTimer->stop();
        m_reconnectTimer->deleteLater();
        m_reconnectTimer = nullptr;
    }
    if (m_control) {
        m_control->disconnect(this);
        m_control->deleteLater();
        m_control = nullptr;
    }
    m_replyBuffer.clear();
    m_replyLines.clear();
    m_pendingHandler = nullptr;
}

void FtpClient::scheduleReconnect() {
    if (m_reconnectTimer || m_host.isEmpty() || m_reconnectAttempts >= 3)
        return;

    ++m_reconnectAttempts;
    m_reconnectTimer = new QTimer(this);
    m_reconnectTimer->setSingleShot(true);
    m_reconnectTimer->setInterval(3000);
    connect(m_reconnectTimer, &QTimer::timeout, this, [this]() {
        m_reconnectTimer->deleteLater();
        m_reconnectTimer = nullptr;
        m_autoReconnectInProgress = true;
        connectToHost(m_host, m_port, m_user, m_password, m_tls);
    });
    m_reconnectTimer->start();
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
            QString tlsError;
            if (!configureTlsSocket(ssl, &tlsError)) {
                emit operationFinished(false, tlsError);
                ssl->deleteLater();
                return;
            }
            data = ssl;
            connect(ssl, &QSslSocket::sslErrors, this, [this, data](const QList<QSslError>& errors) {
                QStringList details;
                for (const QSslError& error : errors)
                    details.append(error.errorString());
                if (m_tlsAllowInvalidCertificates) {
                    if (auto* sslSocket = qobject_cast<QSslSocket*>(data))
                        sslSocket->ignoreSslErrors(errors);
                    return;
                }
                emit operationFinished(false,
                                       tr("FTPS data certificate validation failed: %1").arg(details.join("; ")));
                data->abort();
            });
            connect(ssl, &QTcpSocket::connected, ssl, &QSslSocket::startClientEncryption);
            connect(
                ssl, &QSslSocket::encrypted, this, [onConnected, data]() { onConnected(data); },
                Qt::SingleShotConnection);
        } else {
            data = new QTcpSocket(this);
            connect(data, &QTcpSocket::connected, this, [onConnected, data]() { onConnected(data); });
        }
        connect(data, &QTcpSocket::errorOccurred, this, [this, data](QAbstractSocket::SocketError error) {
            // A peer closing a completed FTP data stream is reported by Qt as
            // RemoteHostClosedError before disconnected().  The latter owns
            // the normal 226 reply and operation completion.
            if (error == QAbstractSocket::RemoteHostClosedError)
                return;
            emit operationFinished(false, tr("FTP data connection failed"));
            // The socket emits disconnected() after an error.  Cleanup is
            // owned by the operation-specific disconnected() handler so a
            // close cannot enqueue two deferred destructions.
            if (data->state() != QAbstractSocket::UnconnectedState)
                data->abort();
        });
        data->connectToHost(host, static_cast<quint16>(port));
    });
}

bool FtpClient::configureTlsSocket(QSslSocket* socket, QString* error) const {
    QSslConfiguration configuration = QSslConfiguration::defaultConfiguration();
    configuration.setProtocol(m_tlsMinimumVersion == 13 ? QSsl::TlsV1_3OrLater : QSsl::TlsV1_2OrLater);
    if (!m_tlsCaFile.isEmpty()) {
        const QList<QSslCertificate> certificates = QSslCertificate::fromPath(m_tlsCaFile);
        if (certificates.isEmpty()) {
            if (error)
                *error = tr("Could not load FTPS CA bundle: %1").arg(m_tlsCaFile);
            return false;
        }
        configuration.setCaCertificates(certificates);
    }
    socket->setSslConfiguration(configuration);
    socket->setPeerVerifyMode(m_tlsAllowInvalidCertificates ? QSslSocket::VerifyNone : QSslSocket::VerifyPeer);
    socket->setPeerVerifyName(m_host);
    return true;
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
    m_cancelTransferRequested = false;
    m_pauseTransferRequested = false;
    openDataConnection([this, remotePath, localPath](QTcpSocket* data) {
        m_activeData = data;
        QFile* file = new QFile(localPath, this);
        if (!file->open(QIODevice::WriteOnly)) {
            emit operationFinished(false, tr("Failed to open local file: %1").arg(localPath));
            delete file;
            m_activeData = nullptr;
            data->deleteLater();
            return;
        }
        m_activeDownloadFile = file;
        m_activeDownloadName = QFileInfo(remotePath).fileName();
        m_activeDownloadReceived = 0;
        connect(data, &QTcpSocket::readyRead, this, &FtpClient::pumpDownload);
        emit transferProgress(QFileInfo(remotePath).fileName(), 0, -1);
        connect(data, &QTcpSocket::disconnected, this, [this, data, file, remotePath]() {
            expectReply([this, data, file, remotePath](const Reply&) {
                file->close();
                delete file;
                data->deleteLater();
                m_activeData = nullptr;
                m_activeDownloadFile = nullptr;
                m_activeDownloadName.clear();
                m_activeDownloadReceived = 0;
                m_pauseTransferRequested = false;
                if (m_cancelTransferRequested) {
                    m_cancelTransferRequested = false;
                    emit operationFinished(false, tr("Transfer cancelled by user"));
                } else {
                    emit operationFinished(true, tr("Download finished: %1").arg(QFileInfo(remotePath).fileName()));
                }
            });
        });
        sendCommand("RETR " + remotePath, [](const Reply&) { /* 150 */ });
    });
}

void FtpClient::uploadFile(const QString& localPath, const QString& remotePath) {
    if (!m_connected)
        return;
    if (!m_directoryUploadActive)
        m_cancelTransferRequested = false;
    m_pauseTransferRequested = false;
    QFile* file = new QFile(localPath, this);
    if (!file->open(QIODevice::ReadOnly)) {
        reportUploadResult(false, tr("Failed to open local file: %1").arg(localPath));
        delete file;
        return;
    }
    openDataConnection([this, file, localPath, remotePath](QTcpSocket* data) {
        m_activeData = data;
        m_activeUploadFile = file;
        m_activeUploadName = QFileInfo(localPath).fileName();
        sendCommand("STOR " + remotePath, [this, data, file, localPath, remotePath](const Reply& r) {
            if (r.code != 150 && r.code != 125) {
                file->close();
                delete file;
                m_activeData = nullptr;
                m_activeUploadFile = nullptr;
                m_activeUploadName.clear();
                data->deleteLater();
                reportUploadResult(false, tr("STOR failed"));
                return;
            }
            connect(data, &QTcpSocket::disconnected, this, [this, data, file, localPath, remotePath]() {
                expectReply([this, data, localPath, remotePath, file](const Reply&) {
                    file->close();
                    file->deleteLater();
                    data->deleteLater();
                    m_activeData = nullptr;
                    m_activeUploadFile = nullptr;
                    m_activeUploadName.clear();
                    if (!m_directoryUploadActive)
                        m_pauseTransferRequested = false;
                    if (m_cancelTransferRequested) {
                        m_cancelTransferRequested = false;
                        reportUploadResult(false, tr("Transfer cancelled by user"));
                    } else {
                        reportUploadResult(true, tr("Upload finished: %1").arg(QFileInfo(localPath).fileName()));
                    }
                });
            });
            emit transferProgress(QFileInfo(localPath).fileName(), 0, file->size());
            connect(data, &QTcpSocket::bytesWritten, this, [this](qint64) { pumpUpload(); });
            pumpUpload();
        });
    });
}

void FtpClient::uploadDirectory(const QString& localPath, const QString& remoteBasePath) {
    if (!m_connected || m_directoryUploadActive)
        return;
    m_cancelTransferRequested = false;
    m_pauseTransferRequested = false;

    const QFileInfo rootInfo(localPath);
    if (!rootInfo.isDir()) {
        emit operationFinished(false, tr("Not a directory: %1").arg(localPath));
        return;
    }

    m_directoryUploadDirs.clear();
    m_directoryUploadFiles.clear();
    const QDir root(localPath);
    const QString rootName = rootInfo.fileName();
    const QString remoteRoot = remoteBasePath == QStringLiteral("/") ? QStringLiteral("/") + rootName
                                                                     : remoteBasePath + QStringLiteral("/") + rootName;
    m_directoryUploadDirs.append(remoteRoot);

    QDirIterator iterator(localPath, QDir::AllEntries | QDir::NoDotAndDotDot, QDirIterator::Subdirectories);
    while (iterator.hasNext()) {
        const QString path = iterator.next();
        const QFileInfo info(path);
        const QString relative = root.relativeFilePath(path).replace(QDir::separator(), '/');
        const QString remotePath = remoteRoot + '/' + relative;
        if (info.isDir())
            m_directoryUploadDirs.append(remotePath);
        else if (info.isFile())
            m_directoryUploadFiles.append({path, remotePath});
    }

    std::sort(m_directoryUploadDirs.begin(), m_directoryUploadDirs.end(),
              [](const QString& left, const QString& right) { return left.count('/') < right.count('/'); });
    m_directoryUploadActive = true;
    processDirectoryUpload();
}

void FtpClient::processDirectoryUpload() {
    if (!m_directoryUploadActive)
        return;
    if (m_pauseTransferRequested)
        return;
    if (m_cancelTransferRequested) {
        reportUploadResult(false, tr("Transfer cancelled by user"));
        return;
    }
    if (!m_directoryUploadDirs.isEmpty()) {
        const QString remoteDir = m_directoryUploadDirs.takeFirst();
        sendCommand("MKD " + remoteDir, [this](const Reply& reply) {
            // 550 commonly means that the directory already exists.
            if ((reply.code >= 200 && reply.code < 300) || reply.code == 550)
                processDirectoryUpload();
            else
                reportUploadResult(false, tr("Could not create remote directory"));
        });
        return;
    }

    if (!m_directoryUploadFiles.isEmpty()) {
        const auto file = m_directoryUploadFiles.takeFirst();
        uploadFile(file.first, file.second);
        return;
    }

    reportUploadResult(true, tr("Folder uploaded successfully"));
}

void FtpClient::reportUploadResult(bool success, const QString& message) {
    if (m_directoryUploadActive) {
        if (!success) {
            m_directoryUploadActive = false;
            m_directoryUploadDirs.clear();
            m_directoryUploadFiles.clear();
            emit operationFinished(false, message);
        } else {
            processDirectoryUpload();
        }
        return;
    }
    emit operationFinished(success, message);
}

void FtpClient::cancelTransfer() {
    if (!m_directoryUploadActive && !m_activeData)
        return;
    m_cancelTransferRequested = true;
    if (m_activeData)
        m_activeData->abort();
    else if (m_directoryUploadActive)
        processDirectoryUpload();
}

void FtpClient::pauseTransfer() {
    if (m_directoryUploadActive || m_activeData)
        m_pauseTransferRequested = true;
}

void FtpClient::resumeTransfer() {
    if (!m_pauseTransferRequested)
        return;
    m_pauseTransferRequested = false;
    if (m_activeDownloadFile)
        pumpDownload();
    if (m_activeUploadFile)
        pumpUpload();
    if (m_directoryUploadActive && !m_activeData)
        processDirectoryUpload();
}

void FtpClient::pumpDownload() {
    if (m_pauseTransferRequested || !m_activeData || !m_activeDownloadFile)
        return;
    const QByteArray chunk = m_activeData->readAll();
    if (chunk.isEmpty())
        return;
    m_activeDownloadFile->write(chunk);
    m_activeDownloadReceived += chunk.size();
    emit transferProgress(m_activeDownloadName, m_activeDownloadReceived, -1);
}

void FtpClient::pumpUpload() {
    if (m_pauseTransferRequested || !m_activeData || !m_activeUploadFile)
        return;
    if (!m_activeUploadFile->atEnd() && m_activeData->bytesToWrite() < 64 * 1024)
        m_activeData->write(m_activeUploadFile->read(64 * 1024));
    emit transferProgress(m_activeUploadName, m_activeUploadFile->pos(), m_activeUploadFile->size());
    if (m_activeUploadFile->atEnd() && m_activeData->bytesToWrite() == 0)
        m_activeData->disconnectFromHost();
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
    const QRegularExpression dosRe(
        QStringLiteral(R"(^\s*\d{1,2}[\-/]\d{1,2}[\-/]\d{2,4}\s+\d{1,2}:\d{2}(?:\s*[AP]M)?\s+(<DIR>|\d+)\s+(.+?)\s*$)"),
        QRegularExpression::CaseInsensitiveOption);

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
            const auto dosMatch = dosRe.match(s);
            if (dosMatch.hasMatch()) {
                SftpFile f;
                f.isDirectory = dosMatch.captured(1).compare(QStringLiteral("<DIR>"), Qt::CaseInsensitive) == 0;
                f.size = f.isDirectory ? 0 : dosMatch.captured(1).toLongLong();
                f.name = dosMatch.captured(2);
                files.append(f);
                continue;
            }
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
