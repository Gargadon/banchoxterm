#include "sftptransferworker.h"

#include "keyring.h"
#include "sshconnection.h"

SftpTransferWorker::SftpTransferWorker(const Session& session, const SftpTransferRequest& request, QObject* parent)
    : QObject(parent), m_session(session), m_request(request) {
}

SftpTransferWorker::~SftpTransferWorker() {
    delete m_connection;
}

void SftpTransferWorker::start() {
    if (m_connection || m_finished)
        return;

    m_connection = new SshConnection();
    connect(m_connection, &SshConnection::connectionSuccess, this, &SftpTransferWorker::onConnected);
    connect(m_connection, &SshConnection::connectionFailed, this, &SftpTransferWorker::onConnectionFailed);
    connect(m_connection, &SshConnection::operationFinished, this, &SftpTransferWorker::onOperationFinished);
    connect(m_connection, &SshConnection::transferProgress, this, &SftpTransferWorker::onTransferProgress);

    const QString password = Keyring::lookupPassword(m_session.id);
    m_connection->connectToHost(m_session.host, m_session.port, m_session.user, m_session.keyPath, password,
                                m_session.tunnels, m_session.jumpHost, m_session.jumpPort, m_session.jumpUser,
                                m_session.jumpKeyPath, m_session.id);
}

void SftpTransferWorker::onConnected() {
    if (m_started || m_finished)
        return;
    m_started = true;

    if (m_request.isDirUpload) {
        m_connection->uploadDirectory(m_request.localPath, m_request.remotePath);
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
    if (m_connection)
        m_connection->cancelTransfer();
}

void SftpTransferWorker::pause() {
    if (m_connection)
        m_connection->pauseTransfer();
}

void SftpTransferWorker::resume() {
    if (m_connection)
        m_connection->resumeTransfer();
}

void SftpTransferWorker::finish(bool success, const QString& message) {
    if (m_finished)
        return;
    m_finished = true;
    if (m_connection)
        m_connection->disconnectFromHost();
    emit finished(m_request.id, success, message);
}
