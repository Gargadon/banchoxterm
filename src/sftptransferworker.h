#pragma once

#include <QObject>
#include <QString>

#include "session.h"

class SshConnection;

struct SftpTransferRequest {
    QString id;
    QString remotePath;
    QString localPath;
    bool isUpload = false;
    bool isDirUpload = false;
};

class SftpTransferWorker : public QObject {
    Q_OBJECT
public:
    SftpTransferWorker(const Session& session, const SftpTransferRequest& request, QObject* parent = nullptr);
    ~SftpTransferWorker() override;

public slots:
    void start();
    void cancel();
    void pause();
    void resume();

signals:
    void finished(const QString& id, bool success, const QString& message);
    void progress(const QString& id, const QString& fileName, qint64 bytesDone, qint64 totalBytes);

private slots:
    void onConnected();
    void onConnectionFailed(const QString& error);
    void onOperationFinished(bool success, const QString& message);
    void onTransferProgress(const QString& fileName, qint64 bytesDone, qint64 totalBytes);

private:
    void finish(bool success, const QString& message);

    Session m_session;
    SftpTransferRequest m_request;
    SshConnection* m_connection = nullptr;
    bool m_started = false;
    bool m_finished = false;
};
