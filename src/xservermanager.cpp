#include "xservermanager.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QProcess>
#include <QSettings>
#include <QSysInfo>
#include <QTcpSocket>

XServerManager* XServerManager::instance() {
    static XServerManager* manager = new XServerManager(QCoreApplication::instance());
    return manager;
}

XServerManager::XServerManager(QObject* parent) : QObject(parent) {
    connect(QCoreApplication::instance(), &QCoreApplication::aboutToQuit, this, &XServerManager::stop);
}

QString XServerManager::findVcXsrv() const {
    QSettings settings;
    QString configured = settings.value(QStringLiteral("x11/vcxsrvPath")).toString().trimmed();
    if (configured.isEmpty())
        configured = qEnvironmentVariable("BANCHOTERM_VCXSRV").trimmed();
    if (!configured.isEmpty() && QFileInfo::exists(configured))
        return QFileInfo(configured).absoluteFilePath();

    const QString appDir = QCoreApplication::applicationDirPath();
    QStringList candidates;
    if (QSysInfo::currentCpuArchitecture() == QStringLiteral("arm64")) {
        candidates << QDir(appDir).filePath(QStringLiteral("xservers/vcxsrv-arm64/vcxsrv.exe"));
        candidates << QDir(appDir).filePath(QStringLiteral("xservers/vcxsrv-x64/vcxsrv.exe"));
    } else {
        candidates << QDir(appDir).filePath(QStringLiteral("xservers/vcxsrv-x64/vcxsrv.exe"));
        candidates << QDir(appDir).filePath(QStringLiteral("xservers/vcxsrv/vcxsrv.exe"));
    }
    candidates << QDir(appDir).filePath(QStringLiteral("vcxsrv.exe"));
    candidates << QDir::fromNativeSeparators(qEnvironmentVariable("ProgramFiles") +
                                              QStringLiteral("/VcXsrv/vcxsrv.exe"));
    candidates << QDir::fromNativeSeparators(qEnvironmentVariable("ProgramFiles(x86)") +
                                              QStringLiteral("/VcXsrv/vcxsrv.exe"));

    for (const QString& candidate : candidates) {
        if (QFileInfo::exists(candidate))
            return QFileInfo(candidate).absoluteFilePath();
    }
    return {};
}

bool XServerManager::displayAvailable() const {
    QTcpSocket socket;
    socket.connectToHost(QStringLiteral("127.0.0.1"), 6000);
    const bool connected = socket.waitForConnected(250);
    socket.abort();
    return connected;
}

bool XServerManager::ensureVcXsrvRunning(QString* error) {
#ifndef Q_OS_WIN
    Q_UNUSED(error);
    return true;
#else
    if (displayAvailable())
        return true;

    if (m_process && m_process->state() != QProcess::NotRunning)
        return m_process->state() == QProcess::Running;

    m_executablePath = findVcXsrv();
    if (m_executablePath.isEmpty()) {
        if (error)
            *error = tr("No se encontró VcXsrv. Instala VcXsrv o configura su ruta en x11/vcxsrvPath.");
        return false;
    }

    if (!m_process)
        m_process = new QProcess(this);

    // -ac is required by the current Windows X11 bridge, which does not yet
    // create an MIT-MAGIC-COOKIE file. Keep the server on the local machine;
    // cookie authentication should replace this compatibility mode later.
    const QStringList arguments = {QStringLiteral(":0"), QStringLiteral("-multiwindow"),
                                   QStringLiteral("-clipboard"), QStringLiteral("-ac")};
    m_process->start(m_executablePath, arguments);
    if (!m_process->waitForStarted(3000)) {
        if (error)
            *error = tr("No se pudo iniciar VcXsrv: %1").arg(m_process->errorString());
        return false;
    }

    for (int attempt = 0; attempt < 20 && !displayAvailable(); ++attempt)
        m_process->waitForReadyRead(100);

    if (!displayAvailable()) {
        if (error)
            *error = tr("VcXsrv inició, pero no abrió el display 127.0.0.1:6000.");
        return false;
    }
    return true;
#endif
}

bool XServerManager::isRunning() const {
    return m_process && m_process->state() == QProcess::Running;
}

QString XServerManager::executablePath() const {
    return m_executablePath;
}

void XServerManager::stop() {
    if (!m_process || m_process->state() == QProcess::NotRunning)
        return;
    m_process->terminate();
    if (!m_process->waitForFinished(1000))
        m_process->kill();
}
