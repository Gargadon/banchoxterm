#include "ftpclient.h"
#include "uploadfolderentries.h"
#include <QTcpSocket>
#include <QSslSocket>
#include <QSslCertificate>
#include <QSslConfiguration>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>
#include <QTimer>
#include <algorithm>
#include <utility>

FtpClient::FtpClient(QObject* parent) : QObject(parent) {
}

FtpClient::~FtpClient() {
    disconnectFromHost();
}

void FtpClient::connectToHost(const QString& host, int port, const QString& user, const QString& password, bool tls,
                              int minimumTlsVersion, const QString& caFile, bool allowInvalidCertificates) {
    if (!m_autoReconnectInProgress) {
        m_reconnectAttempts = 0;
    }
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
            for (const QSslError& error : errors) {
                details.append(error.errorString());
            }
            const QString message = tr("FTPS certificate validation failed: %1").arg(details.join("; "));
            if (m_tlsAllowInvalidCertificates) {
                ssl->ignoreSslErrors(errors);
                return;
            }
            if (m_control) {
                m_control->abort();
            }
            emit connectionFailed(message);
        });
    } else {
        m_control = new QTcpSocket(this);
    }
    connect(m_control, &QIODevice::readyRead, this, [this] {
        m_replyBuffer += m_control->readAll();
        while (true) {
            qsizetype nl = m_replyBuffer.indexOf('\n');
            if (nl < 0) {
                break;
            }
            QByteArray line = m_replyBuffer.left(nl);
            if (line.endsWith('\r')) {
                line.chop(1);
            }
            m_replyBuffer.remove(0, nl + 1);

            if (line.size() < 3) {
                continue;
            }
            bool ok = false;
            const int code = line.left(3).toInt(&ok);
            if (!ok) {
                continue;
            }

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
                } else if (m_transferInProgress && r.code >= 200) {
                    // Control and data sockets are independent: the final
                    // reply can arrive before the data disconnected signal.
                    m_earlyTransferReply = r;
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
    connect(m_control, &QAbstractSocket::disconnected, this, [this] {
        if (!m_connected) {
            return;
        }
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

    auto startLogin = [this, user, password, finishLogin] {
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
    if (m_reconnectTimer != nullptr) {
        m_reconnectTimer->stop();
        m_reconnectTimer->deleteLater();
        m_reconnectTimer = nullptr;
    }
    if (m_control != nullptr) {
        m_control->disconnect(this);
        m_control->deleteLater();
        m_control = nullptr;
    }
    m_replyBuffer.clear();
    m_replyLines.clear();
    m_pendingHandler = nullptr;
    m_earlyTransferReply.reset();
    m_transferInProgress = false;
}

void FtpClient::scheduleReconnect() {
    if ((m_reconnectTimer != nullptr) || m_host.isEmpty() || m_reconnectAttempts >= 3) {
        return;
    }

    ++m_reconnectAttempts;
    m_reconnectTimer = new QTimer(this);
    m_reconnectTimer->setSingleShot(true);
    m_reconnectTimer->setInterval(3000);
    connect(m_reconnectTimer, &QTimer::timeout, this, [this] {
        m_reconnectTimer->deleteLater();
        m_reconnectTimer = nullptr;
        m_autoReconnectInProgress = true;
        connectToHost(m_host, m_port, m_user, m_password, m_tls);
    });
    m_reconnectTimer->start();
}

void FtpClient::sendCommand(const QString& cmd, std::function<void(const Reply&)> handler) {
    if (m_control == nullptr) {
        return;
    }
    if (cmd.startsWith("LIST ") || cmd.startsWith("RETR ") || cmd.startsWith("STOR ")) {
        m_earlyTransferReply.reset();
        m_transferInProgress = true;
    }
    m_pendingHandler = std::move(handler);
    if (!cmd.isEmpty()) {
        m_control->write(cmd.toUtf8() + "\r\n");
    }
}

void FtpClient::expectReply(std::function<void(const Reply&)> handler) {
    m_pendingHandler = std::move(handler);
}

void FtpClient::openDataConnection(std::function<void(QTcpSocket*)> onConnected,
                                   std::function<void(const QString&)> onError) {
    const auto fail = [this, onError](const QString& error) {
        if (onError) {
            onError(error);
        } else {
            emit operationFinished(false, error);
        }
    };
    sendCommand("PASV", [this, onConnected, fail](const Reply& r) {
        if (r.code != 227) {
            fail(tr("PASV failed: %1").arg(r.lines.join(' ')));
            return;
        }
        const QString text = r.lines.join(' ');
        const QRegularExpression re(QStringLiteral(R"(\((\d+),(\d+),(\d+),(\d+),(\d+),(\d+)\))"));
        const auto m = re.match(text);
        if (!m.hasMatch()) {
            fail(tr("Could not parse PASV reply"));
            return;
        }
        const QString host = QString("%1.%2.%3.%4").arg(m.captured(1), m.captured(2), m.captured(3), m.captured(4));
        const int port = (m.captured(5).toInt() * 256) + m.captured(6).toInt();

        QTcpSocket* data = nullptr;
        if (m_tls) {
            auto* ssl = new QSslSocket(this);
            QString tlsError;
            if (!configureTlsSocket(ssl, &tlsError)) {
                fail(tlsError);
                ssl->deleteLater();
                return;
            }
            data = ssl;
            connect(ssl, &QSslSocket::sslErrors, this, [this, data, fail](const QList<QSslError>& errors) {
                QStringList details;
                for (const QSslError& error : errors) {
                    details.append(error.errorString());
                }
                if (m_tlsAllowInvalidCertificates) {
                    if (auto* sslSocket = qobject_cast<QSslSocket*>(data)) {
                        sslSocket->ignoreSslErrors(errors);
                    }
                    return;
                }
                fail(tr("FTPS data certificate validation failed: %1").arg(details.join("; ")));
                data->abort();
            });
            connect(ssl, &QTcpSocket::connected, ssl, &QSslSocket::startClientEncryption);
            connect(
                ssl, &QSslSocket::encrypted, this, [onConnected, data] { onConnected(data); },
                Qt::SingleShotConnection);
        } else {
            data = new QTcpSocket(this);
            connect(data, &QTcpSocket::connected, this, [onConnected, data] { onConnected(data); });
        }
        connect(data, &QTcpSocket::errorOccurred, this, [data, fail](QAbstractSocket::SocketError error) {
            // A peer closing a completed FTP data stream is reported by Qt as
            // RemoteHostClosedError before disconnected().  The latter owns
            // the normal 226 reply and operation completion.
            if (error == QAbstractSocket::RemoteHostClosedError) {
                return;
            }
            fail(tr("FTP data connection failed"));
            // The socket emits disconnected() after an error.  Cleanup is
            // owned by the operation-specific disconnected() handler so a
            // close cannot enqueue two deferred destructions.
            if (data->state() != QAbstractSocket::UnconnectedState) {
                data->abort();
            }
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
            if (error != nullptr) {
                *error = tr("Could not load FTPS CA bundle: %1").arg(m_tlsCaFile);
            }
            return false;
        }
        configuration.setCaCertificates(certificates);
    }
    socket->setSslConfiguration(configuration);
    socket->setPeerVerifyMode(m_tlsAllowInvalidCertificates ? QSslSocket::VerifyNone : QSslSocket::VerifyPeer);
    socket->setPeerVerifyName(m_host);
    return true;
}

// An empty data stream may close before the preliminary 150 reply arrives.
// Keep waiting for the final reply before starting the next queued operation.
void FtpClient::expectTransferReply(std::function<void(const Reply&)> handler) {
    if (m_earlyTransferReply) {
        const Reply reply = *m_earlyTransferReply;
        m_earlyTransferReply.reset();
        m_transferInProgress = false;
        handler(reply);
        return;
    }
    expectReply([this, handler](const Reply& reply) {
        if (reply.code >= 100 && reply.code < 200) {
            {
                expectTransferReply(handler);
            }
        } else {
            m_transferInProgress = false;
            handler(reply);
        }
    });
}

void FtpClient::listDirectory(const QString& path) {
    if (!m_connected) {
        return;
    }
    const QString target = path.isEmpty() ? QStringLiteral(".") : path;
    readDirectory(target, false, [this, target](const QList<SftpFile>& files, const QString& error) {
        if (error.isEmpty()) {
            emit directoryListed(target, files);
        } else {
            emit operationFinished(false, error);
        }
    });
}

void FtpClient::readDirectory(const QString& path, bool strict,
                              std::function<void(const QList<SftpFile>&, const QString&)> done) {
    struct Listing {
        QByteArray data;
        bool finished = false;
    };
    const auto listing = std::make_shared<Listing>();
    const auto complete = [listing, done](const QList<SftpFile>& files, const QString& error) {
        if (!std::exchange(listing->finished, true)) {
            done(files, error);
        }
    };
    openDataConnection(
        [this, path, strict, listing, complete](QTcpSocket* data) {
            connect(data, &QTcpSocket::readyRead, this, [data, listing] { listing->data.append(data->readAll()); });
            connect(data, &QTcpSocket::disconnected, this, [this, data, strict, listing, complete] {
                listing->data.append(data->readAll());
                data->deleteLater();
                if (listing->finished) {
                    return;
                }
                expectTransferReply([strict, listing, complete, this](const Reply& reply) {
                    if (reply.code != 226 && reply.code != 250) {
                        complete({}, tr("Directory listing failed: %1").arg(reply.lines.join(" ")));
                        return;
                    }
                    bool recognized = true;
                    const auto files = parseListing(listing->data, strict ? &recognized : nullptr);
                    if (!recognized) {
                        complete({}, tr("Could not read every entry in the remote folder."));
                    } else {
                        complete(files, {});
                    }
                });
            });
            sendCommand("LIST " + path, [this, data, complete](const Reply& reply) {
                if (reply.code >= 100 && reply.code < 200) {
                    return;
                }
                m_transferInProgress = false;
                complete({}, tr("Directory listing failed: %1").arg(reply.lines.join(" ")));
                data->abort();
            });
        },
        [complete](const QString& error) { complete({}, error); });
}

void FtpClient::downloadFile(const QString& remotePath, const QString& localPath) {
    if (transferConflict && (QFileInfo::exists(localPath) || QFileInfo(localPath).isSymLink())) {
        const int choice = transferConflict->resolve(localPath);
        if (choice != TransferConflict::Overwrite) {
            emit operationFinished(choice == TransferConflict::Skip,
                                   choice == TransferConflict::Skip ? QStringLiteral("File skipped")
                                                                    : QStringLiteral("Transfer cancelled by user"));
            return;
        }
    }

    if (!m_connected) {
        return;
    }
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
        m_downloadWriteFailed = false;
        connect(data, &QTcpSocket::readyRead, this, &FtpClient::pumpDownload);
        emit transferProgress(QFileInfo(remotePath).fileName(), 0, -1);
        connect(data, &QTcpSocket::disconnected, this, [this, data, file, remotePath] {
            expectTransferReply([this, data, file, remotePath](const Reply& reply) {
                const QString localPath = file->fileName();
                const bool written = file->flush() && !m_downloadWriteFailed;
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
                } else if (!written) {
                    emit operationFinished(false, tr("Failed to write local file: %1").arg(localPath));
                } else if (reply.code != 226 && reply.code != 250) {
                    emit operationFinished(false, tr("Download failed: %1").arg(remotePath));
                } else {
                    emit fileTransferred(localPath);
                    emit operationFinished(true, tr("Download finished: %1").arg(QFileInfo(remotePath).fileName()));
                }
            });
        });
        sendCommand("RETR " + remotePath, [](const Reply&) { /* 150 */ });
    });
}

void FtpClient::uploadFile(const QString& localPath, const QString& remotePath) {
    if (!m_connected) {
        return;
    }
    if (!transferConflict) {
        uploadFileApproved(localPath, remotePath);
        return;
    }
    sendCommand("SIZE " + remotePath, [this, localPath, remotePath](const Reply& reply) {
        if (reply.code == 213 && transferConflict) {
            const int choice = transferConflict->resolve(remotePath);
            if (choice != TransferConflict::Overwrite) {
                reportUploadResult(choice == TransferConflict::Skip,
                                   choice == TransferConflict::Skip ? QStringLiteral("File skipped")
                                                                    : QStringLiteral("Transfer cancelled by user"));
                return;
            }
        } else if (reply.code != 213 && reply.code != 550) {
            reportUploadResult(false, tr("Could not check the destination file: %1").arg(remotePath));
            return;
        }
        uploadFileApproved(localPath, remotePath);
    });
}

void FtpClient::uploadFileApproved(const QString& localPath, const QString& remotePath) {
    if (!m_connected) {
        return;
    }
    if (!m_directoryUploadActive) {
        m_cancelTransferRequested = false;
    }
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
            connect(data, &QTcpSocket::disconnected, this, [this, data, file, localPath, remotePath] {
                expectTransferReply([this, data, localPath, remotePath, file](const Reply& reply) {
                    file->close();
                    file->deleteLater();
                    data->deleteLater();
                    m_activeData = nullptr;
                    m_activeUploadFile = nullptr;
                    m_activeUploadName.clear();
                    if (!m_directoryUploadActive) {
                        m_pauseTransferRequested = false;
                    }
                    if (m_cancelTransferRequested) {
                        m_cancelTransferRequested = false;
                        reportUploadResult(false, tr("Transfer cancelled by user"));
                    } else if (reply.code != 226 && reply.code != 250) {
                        reportUploadResult(false, tr("Upload failed: %1").arg(remotePath));
                    } else {
                        emit fileTransferred(localPath);
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
    uploadPreparedDirectory(localPath, remoteBasePath, {});
}

void FtpClient::uploadPreparedDirectory(const QString& localPath, const QString& remoteBasePath,
                                        const std::shared_ptr<const UploadFolderPlan>& plan) {
    if (!m_connected || m_directoryUploadActive) {
        return;
    }
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

    if (plan) {
        for (const auto& entry : *plan) {
            const QString remotePath = remoteRoot + '/' + entry.relativePath;
            if (entry.directory) {
                m_directoryUploadDirs.append(remotePath);
            } else {
                m_directoryUploadFiles.append({root.filePath(entry.relativePath), remotePath});
            }
        }
    } else {
        auto iterator = uploadFolderEntries(localPath, false);
        while (iterator.hasNext()) {
            const QString path = iterator.next();
            const QFileInfo info = iterator.fileInfo();
            const QString remotePath = remoteRoot + '/' + root.relativeFilePath(path);
            if (info.isDir()) {
                m_directoryUploadDirs.append(remotePath);
            } else if (info.isFile()) {
                m_directoryUploadFiles.append({path, remotePath});
            }
        }
    }

    std::sort(m_directoryUploadDirs.begin(), m_directoryUploadDirs.end(),
              [](const QString& left, const QString& right) { return left.count('/') < right.count('/'); });
    m_directoryUploadActive = true;
    processDirectoryUpload();
}

void FtpClient::processDirectoryUpload() {
    if (!m_directoryUploadActive) {
        return;
    }
    if (m_cancelTransferRequested) {
        reportUploadResult(false, tr("Transfer cancelled by user"));
        return;
    }
    if (m_pauseTransferRequested) {
        return;
    }
    if (!m_directoryUploadDirs.isEmpty()) {
        const QString remoteDir = m_directoryUploadDirs.takeFirst();
        sendCommand("MKD " + remoteDir, [this](const Reply& reply) {
            // 550 commonly means that the directory already exists.
            if ((reply.code >= 200 && reply.code < 300) || reply.code == 550) {
                processDirectoryUpload();
            } else {
                reportUploadResult(false, tr("Could not create remote directory"));
            }
        });
        return;
    }

    if (!m_directoryUploadFiles.isEmpty()) {
        const auto file = m_directoryUploadFiles.takeFirst();
        uploadFile(file.first, file.second);
        return;
    }

    m_directoryUploadActive = false;
    emit operationFinished(true, tr("Folder uploaded successfully"));
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
    if (!m_directoryUploadActive && (m_activeData == nullptr)) {
        return;
    }
    m_cancelTransferRequested = true;
    if (m_activeData != nullptr) {
        m_activeData->abort();
    } else if (m_directoryUploadActive) {
        processDirectoryUpload();
    }
}

void FtpClient::pauseTransfer() {
    if (m_directoryUploadActive || (m_activeData != nullptr)) {
        m_pauseTransferRequested = true;
    }
}

void FtpClient::resumeTransfer() {
    if (!m_pauseTransferRequested) {
        return;
    }
    m_pauseTransferRequested = false;
    if (m_activeDownloadFile != nullptr) {
        pumpDownload();
    }
    if (m_activeUploadFile != nullptr) {
        pumpUpload();
    }
    if (m_directoryUploadActive && (m_activeData == nullptr)) {
        processDirectoryUpload();
    }
}

void FtpClient::pumpDownload() {
    if (m_pauseTransferRequested || (m_activeData == nullptr) || (m_activeDownloadFile == nullptr)) {
        return;
    }
    const QByteArray chunk = m_activeData->readAll();
    if (chunk.isEmpty()) {
        return;
    }
    if (m_activeDownloadFile->write(chunk) != chunk.size()) {
        m_downloadWriteFailed = true;
    }
    m_activeDownloadReceived += chunk.size();
    emit transferProgress(m_activeDownloadName, m_activeDownloadReceived, -1);
}

void FtpClient::pumpUpload() {
    if (m_pauseTransferRequested || (m_activeData == nullptr) || (m_activeUploadFile == nullptr)) {
        return;
    }
    if (!m_activeUploadFile->atEnd() && m_activeData->bytesToWrite() < 64 * 1024) {
        m_activeData->write(m_activeUploadFile->read(64 * 1024));
    }
    emit transferProgress(m_activeUploadName, m_activeUploadFile->pos(), m_activeUploadFile->size());
    if (m_activeUploadFile->atEnd() && m_activeData->bytesToWrite() == 0) {
        m_activeData->disconnectFromHost();
    }
}

void FtpClient::deleteFile(const QString& remotePath, bool isDir) {
    if (!m_connected) {
        return;
    }
    const QString cleanPath = QDir::cleanPath(remotePath);
    if (remotePath.isEmpty() || cleanPath == "/" || cleanPath == "." || cleanPath == ".." ||
        remotePath.contains('\r') || remotePath.contains('\n') || remotePath.contains(QChar::Null)) {
        emit operationFinished(false, tr("Invalid remote path for deletion."));
        return;
    }

    m_deleteEntries.append({remotePath, isDir});
    processDeletion();
}

void FtpClient::processDeletion() {
    if (m_deleteEntries.isEmpty()) {
        finishDeletion(true, tr("Deleted successfully"));
        return;
    }

    const DeleteEntry entry = m_deleteEntries.takeLast();
    if (!entry.directory || entry.visited) {
        sendCommand((entry.directory ? "RMD " : "DELE ") + entry.path, [this, entry](const Reply& reply) {
            if (reply.code == 250) {
                processDeletion();
            } else {
                finishDeletion(false, tr("Could not delete %1: %2").arg(entry.path, reply.lines.join(' ')));
            }
        });
        return;
    }

    readDirectory(entry.path, true, [this, entry](const QList<SftpFile>& files, const QString& error) {
        if (!error.isEmpty()) {
            finishDeletion(false, tr("Could not delete %1: %2").arg(entry.path, error));
            return;
        }
        m_deleteEntries.append({entry.path, true, true});
        for (const auto& file : files) {
            if (file.name == "." || file.name == "..") {
                continue;
            }
            if (file.name.isEmpty() || file.name.contains('/') || file.name.contains('\\') ||
                file.name.contains('\r') || file.name.contains('\n') || file.name.contains(QChar::Null)) {
                finishDeletion(false, tr("Invalid remote file name."));
                return;
            }
            m_deleteEntries.append({entry.path + (entry.path.endsWith('/') ? "" : "/") + file.name, file.isDirectory});
        }
        processDeletion();
    });
}

void FtpClient::finishDeletion(bool success, const QString& message) {
    m_deleteEntries.clear();
    emit operationFinished(success, message);
}

void FtpClient::createDirectory(const QString& path) {
    if (!m_connected) {
        return;
    }
    sendCommand("MKD " + path, [this](const Reply& r) {
        emit operationFinished(r.code == 257, r.code == 257 ? tr("Folder created") : tr("MKD failed"));
    });
}

void FtpClient::renamePath(const QString& oldPath, const QString& newPath) {
    if (!m_connected) {
        return;
    }
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

QList<SftpFile> FtpClient::parseListing(const QByteArray& data, bool* recognized) {
    QList<SftpFile> files;
    const QRegularExpression re(
        QStringLiteral(R"(^([\-dl])[\-rwxstST]{9}\s+\d+\s+\S+\s+\S+\s+(\d+)\s+\S+\s+\S+\s+\S+(\s+)(.+)$)"));
    const QRegularExpression dosRe(
        QStringLiteral(R"(^\s*\d{1,2}[\-/]\d{1,2}[\-/]\d{2,4}\s+\d{1,2}:\d{2}(?:\s*[AP]M)?\s+(<DIR>|\d+)\s+(.+?)\s*$)"),
        QRegularExpression::CaseInsensitiveOption);

    const QList<QByteArray> lines = data.split('\n');
    for (const QByteArray& line : lines) {
        QByteArray raw = line;
        if (raw.endsWith('\r')) {
            raw.chop(1);
        }
        const QString decoded = QString::fromUtf8(raw);
        const QString s = decoded.trimmed();
        if (s.isEmpty() || s.startsWith("total ")) {
            continue;
        }
        if ((recognized != nullptr) && (decoded.toUtf8() != raw || (!decoded.isEmpty() && decoded.back().isSpace()))) {
            *recognized = false;
            return {};
        }
        const auto m = re.match(s);
        if (m.hasMatch()) {
            SftpFile f;
            f.isDirectory = (m.captured(1) == "d");
            f.size = m.captured(2).toLongLong();
            if ((recognized != nullptr) && m.capturedLength(3) != 1) {
                *recognized = false;
                return {};
            }
            f.name = m.captured(4);
            if (m.captured(1) == "l") {
                const auto separator = f.name.indexOf(" -> ");
                if (separator < 0 || f.name.indexOf(" -> ", separator + 4) >= 0) {
                    if (recognized != nullptr) {
                        *recognized = false;
                        return {};
                    }
                    continue;
                }
                f.name.truncate(separator);
            }
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
            if (recognized != nullptr) {
                *recognized = false;
                return {};
            }
            // Fallback: treat the last token as the name (best effort).
            const QStringList parts = s.split(QRegularExpression("\\s+"), Qt::SkipEmptyParts);
            if (parts.isEmpty()) {
                continue;
            }
            SftpFile f;
            f.name = parts.last();
            files.append(f);
        }
    }
    return files;
}
