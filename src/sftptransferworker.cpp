#include "sftptransferworker.h"

#include "keyring.h"
#include "sshconnection.h"

SftpTransferWorker::SftpTransferWorker(const Session& session, const SftpTransferRequest& request, QObject* parent,
                                       bool reusable)
    : QObject(parent), m_session(session), m_request(request), m_reusable(reusable) {
    // Construct before moving the worker to its thread. Control slots only
    // touch SshConnection's atomic transfer flags and can be called directly.
    m_connection = new SshConnection();
    m_connection->setParent(this);
    m_connection->transferConflict = request.conflict;
}

SftpTransferWorker::~SftpTransferWorker() {
    delete m_connection;
}

void SftpTransferWorker::start() {
    if (m_started || m_finished) {
        return;
    }
    m_started = true;

    m_connection->setKeepAliveSeconds(m_session.keepAliveSeconds);
    m_connection->setCipherAlgorithms(m_session.cryptCipher);
    m_connection->setKexAlgorithm(m_session.kexAlgo);
    m_connection->setMacAlgorithm(m_session.macAlgo);
    connect(m_connection, &SshConnection::connectionSuccess, this, &SftpTransferWorker::onConnected);
    connect(m_connection, &SshConnection::connectionFailed, this, &SftpTransferWorker::onConnectionFailed);
    connect(m_connection, &SshConnection::operationFinished, this, &SftpTransferWorker::onOperationFinished);
    connect(m_connection, &SshConnection::transferProgress, this, &SftpTransferWorker::onTransferProgress);
    connect(m_connection, &SshConnection::fileTransferred, this,
            [this](const QString& path) { emit fileTransferred(m_request.id, path); });

    const QString password = Keyring::lookupPassword(m_session.id);
    m_connection->connectToHost(m_session.host, m_session.port, m_session.user, m_session.keyPath, password, {},
                                m_session.jumpHost, m_session.jumpPort, m_session.jumpUser, m_session.jumpKeyPath,
                                m_session.id);
}

void SftpTransferWorker::submit(const SftpTransferRequest& request) {
    m_request = request;
    m_finished = false;
    onConnected();
}

void SftpTransferWorker::onConnected() {
    if (m_cancelRequested.load()) {
        finish(false, QStringLiteral("Transfer cancelled by user"));
        return;
    }
    if (m_finished) {
        return;
    }
    if (!m_connection->isConnected()) {
        finish(false, QStringLiteral("SFTP session not active"));
        return;
    }

    if (m_request.isDirUpload) {
        m_connection->uploadPreparedDirectory(m_request.localPath, m_request.remotePath, m_request.uploadPlan);
    } else if (m_request.isUpload) {
        m_connection->uploadFile(m_request.localPath, m_request.remotePath);
    } else {
        m_connection->downloadFile(m_request.remotePath, m_request.localPath);
    }
}

void SftpTransferWorker::onConnectionFailed(const QString& error) {
    finish(false, error);
}

void SftpTransferWorker::onOperationFinished(bool success, const QString& message) {
    finish(success, message);
}

void SftpTransferWorker::onTransferProgress(const QString& fileName, qint64 bytesDone, qint64 totalBytes) {
    emit progress(m_request.id, fileName, bytesDone, totalBytes);
}

void SftpTransferWorker::cancel() {
    m_cancelRequested.store(true);
    if (m_connection != nullptr) {
        m_connection->cancelTransfer();
    }
}

void SftpTransferWorker::pause() {
    if (m_connection != nullptr) {
        m_connection->pauseTransfer();
    }
}

void SftpTransferWorker::resume() {
    if (m_connection != nullptr) {
        m_connection->resumeTransfer();
    }
}

void SftpTransferWorker::finish(bool success, const QString& message) {
    if (m_finished) {
        return;
    }
    m_finished = true;
    if ((m_connection != nullptr) && (!m_reusable || !success)) {
        m_connection->disconnectFromHost();
    }
    emit finished(m_request.id, success, message);
}
