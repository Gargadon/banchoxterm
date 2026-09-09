#pragma once

#include <QObject>
#include <QString>

class QProcess;

// Owns an optional Windows X server used by SSH X11 forwarding. The server
// remains a separate process so x64 and ARM64 builds can ship different
// providers without coupling them to the main executable.
class XServerManager final : public QObject {
    Q_OBJECT
public:
    static XServerManager* instance();

    bool ensureVcXsrvRunning(QString* error = nullptr);
    bool isRunning() const;
    QString executablePath() const;

public slots:
    void stop();

private:
    explicit XServerManager(QObject* parent = nullptr);
    QString findVcXsrv() const;
    bool displayAvailable() const;

    QProcess* m_process = nullptr;
    QString m_executablePath;
};
