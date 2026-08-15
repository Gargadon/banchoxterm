#include "updater.h"
#include "apppaths.h"

#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QMessageBox>
#include <QProgressDialog>
#include <QFile>
#include <QFileInfo>
#include <QDir>
#include <QProcess>
#include <QPushButton>
#include <QUrl>
#include <QCoreApplication>
#include <QDateTime>
#include <QCryptographicHash>
#include <QSysInfo>

#ifndef BANCHO_VERSION
#define BANCHO_VERSION "1.0.0"
#endif

namespace {
const char* kRepoApi = "https://api.github.com/repos/Gargadon/banchoxterm/releases/latest";

QStringList parseVersion(const QString& tag) {
    QString v = tag;
    if (v.startsWith('v'))
        v = v.mid(1);
    QStringList parts = v.split('.');
    return parts;
}

bool isNewer(const QString& latestTag, const QString& current) {
    const QStringList a = parseVersion(latestTag);
    const QStringList b = parseVersion(current);
    const int n = qMax(a.size(), b.size());
    for (int i = 0; i < n; ++i) {
        const int ai = i < a.size() ? a[i].toInt() : 0;
        const int bi = i < b.size() ? b[i].toInt() : 0;
        if (ai != bi)
            return ai > bi;
    }
    return false;
}

QString findAssetUrl(const QJsonArray& assets, const QString& name) {
    for (const QJsonValue& val : assets) {
        const QJsonObject obj = val.toObject();
        if (obj.value("name").toString() == name)
            return obj.value("browser_download_url").toString();
    }
    return QString();
}

QString findAssetDigest(const QJsonArray& assets, const QString& name) {
    for (const QJsonValue& val : assets) {
        const QJsonObject obj = val.toObject();
        if (obj.value("name").toString() == name)
            return obj.value("digest").toString();
    }
    return {};
}

// Portable update: writes a small PowerShell script that waits for the app to
// exit, extracts the ZIP over the app directory and relaunches it.
void launchPortableUpdate(const QString& zipPath) {
    const QString appDir = AppPaths::applicationDir();
    const int pid = static_cast<int>(QCoreApplication::applicationPid());

    const QString script = QString("$ErrorActionPreference='Stop'\n"
                                   "Wait-Process -Id %1 -ErrorAction SilentlyContinue\n"
                                   "Start-Sleep -Seconds 1\n"
                                   "tar -xf '%2' -C '%3'\n"
                                   "Start-Process -FilePath '%3\\banchoxterm.exe' -WorkingDirectory '%3'\n")
                               .arg(pid)
                               .arg(QString(zipPath).replace(QChar('\''), QStringLiteral("''")))
                               .arg(QString(appDir).replace(QChar('\''), QStringLiteral("''")));

    const QString scriptPath =
        QDir::temp().filePath(QString("banchoxterm_update_%1.ps1").arg(QDateTime::currentMSecsSinceEpoch()));
    QFile f(scriptPath);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Text)) {
        QMessageBox::warning(nullptr, Updater::tr("Update"),
                             Updater::tr("Could not write the update script to %1.").arg(scriptPath));
        return;
    }
    f.write(script.toUtf8());
    f.close();

    QProcess::startDetached("powershell", {"-NoProfile", "-ExecutionPolicy", "Bypass", "-File", scriptPath});
    QCoreApplication::quit();
}
// Installed update: runs the installer after this app has exited, so it can
// replace the running executable without a file-lock error.
void launchInstallerUpdate(const QString& installerPath) {
    const int pid = static_cast<int>(QCoreApplication::applicationPid());

    const QString script = QString("$ErrorActionPreference='Stop'\n"
                                   "Wait-Process -Id %1 -ErrorAction SilentlyContinue\n"
                                   "Start-Sleep -Seconds 1\n"
                                   "Start-Process -FilePath '%2'\n")
                               .arg(pid)
                               .arg(QString(installerPath).replace(QChar('\''), QStringLiteral("''")));

    const QString scriptPath =
        QDir::temp().filePath(QString("banchoxterm_installer_%1.ps1").arg(QDateTime::currentMSecsSinceEpoch()));
    QFile f(scriptPath);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Text)) {
        QMessageBox::warning(nullptr, Updater::tr("Update"),
                             Updater::tr("Could not write the update script to %1.").arg(scriptPath));
        return;
    }
    f.write(script.toUtf8());
    f.close();

    QProcess::startDetached("powershell", {"-NoProfile", "-ExecutionPolicy", "Bypass", "-File", scriptPath});
    QCoreApplication::quit();
}
} // namespace

void Updater::checkForUpdates(QWidget* parent) {
#ifndef Q_OS_WIN
    QMessageBox::information(parent, tr("Check for Updates"), tr("Automatic updates are only available on Windows."));
    return;
#endif

    auto* nam = new QNetworkAccessManager;

    QNetworkRequest req{QUrl(QLatin1String(kRepoApi))};
    req.setHeader(QNetworkRequest::UserAgentHeader, "BanchoXterm/" BANCHO_VERSION);

    QNetworkReply* reply = nam->get(req);
    QObject::connect(reply, &QNetworkReply::finished, reply, [reply, nam, parent]() {
        reply->deleteLater();
        nam->deleteLater();

        if (reply->error() != QNetworkReply::NoError) {
            QMessageBox::warning(parent, Updater::tr("Update Check Failed"),
                                 Updater::tr("Could not check for updates: %1").arg(reply->errorString()));
            return;
        }

        const QJsonDocument doc = QJsonDocument::fromJson(reply->readAll());
        if (!doc.isObject()) {
            QMessageBox::warning(parent, Updater::tr("Update Check Failed"),
                                 Updater::tr("The update server returned an invalid response."));
            return;
        }

        const QJsonObject obj = doc.object();
        const QString tag = obj.value("tag_name").toString();
        const QString body = obj.value("body").toString();

        if (tag.isEmpty() || !isNewer(tag, QStringLiteral(BANCHO_VERSION))) {
            QMessageBox::information(
                parent, tr("Check for Updates"),
                tr("You are running the latest version (%1).").arg(QStringLiteral(BANCHO_VERSION)));
            return;
        }

        QMessageBox box(parent);
        box.setWindowTitle(tr("Update Available"));
        box.setIcon(QMessageBox::Information);
        box.setText(tr("Version %1 is available.\n\nCurrent version: %2").arg(tag).arg(QStringLiteral(BANCHO_VERSION)));
        if (!body.isEmpty())
            box.setInformativeText(tr("Release notes:\n%1").arg(body.left(800)));
        box.addButton(tr("Download & Update"), QMessageBox::AcceptRole);
        QPushButton* laterBtn = box.addButton(tr("Later"), QMessageBox::RejectRole);
        box.setDefaultButton(laterBtn);
        box.exec();
        if (box.clickedButton() == laterBtn)
            return;

        const bool portable = AppPaths::isPortable();
        const QString architecture = QSysInfo::currentCpuArchitecture().contains("arm", Qt::CaseInsensitive)
                                          ? QStringLiteral("arm64")
                                          : QStringLiteral("x64");
        const QString assetName = portable
                                      ? QStringLiteral("BanchoXterm-windows-%1.zip").arg(architecture)
                                      : QStringLiteral("BanchoXterm-Setup-%1.exe").arg(architecture);
        const QString url = findAssetUrl(obj.value("assets").toArray(), assetName);
        const QString expectedDigest = findAssetDigest(obj.value("assets").toArray(), assetName);
        if (url.isEmpty()) {
            QMessageBox::warning(parent, tr("Update"), tr("The release does not include the asset %1.").arg(assetName));
            return;
        }

        // Download the asset with a progress dialog.
        auto* downloadNam = new QNetworkAccessManager;
        QNetworkRequest dreq{QUrl(url)};
        dreq.setHeader(QNetworkRequest::UserAgentHeader, "BanchoXterm/" BANCHO_VERSION);
        QNetworkReply* dreply = downloadNam->get(dreq);

        auto* progress = new QProgressDialog(tr("Downloading %1...").arg(assetName), tr("Cancel"), 0, 0, parent);
        progress->setWindowTitle(tr("Update"));
        progress->setWindowModality(Qt::WindowModal);
        progress->setMinimumDuration(300);
        progress->setAutoReset(false);
        progress->setAutoClose(false);

        QObject::connect(dreply, &QNetworkReply::downloadProgress, progress, [progress](qint64 received, qint64 total) {
            progress->setMaximum(total > 0 ? static_cast<int>(total) : 0);
            progress->setValue(static_cast<int>(received));
        });

        QObject::connect(progress, &QProgressDialog::canceled, dreply, &QNetworkReply::abort);

        QObject::connect(dreply, &QNetworkReply::finished, progress, [=]() {
            progress->close();
            progress->deleteLater();

            if (dreply->error() != QNetworkReply::NoError) {
                QMessageBox::warning(parent, tr("Update Failed"), tr("Download failed: %1").arg(dreply->errorString()));
                dreply->deleteLater();
                downloadNam->deleteLater();
                return;
            }

            const QString destPath = QDir::temp().filePath(assetName);
            const QByteArray payload = dreply->readAll();
            const QString actualDigest = QStringLiteral("sha256:") +
                                         QString::fromLatin1(QCryptographicHash::hash(payload, QCryptographicHash::Sha256).toHex());
            if (expectedDigest.isEmpty() || expectedDigest.compare(actualDigest, Qt::CaseInsensitive) != 0) {
                QMessageBox::warning(parent, tr("Update Failed"),
                                     tr("The downloaded update failed its integrity check."));
                dreply->deleteLater();
                downloadNam->deleteLater();
                return;
            }
            QFile out(destPath);
            if (!out.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
                QMessageBox::warning(parent, tr("Update Failed"), tr("Could not write %1.").arg(destPath));
                dreply->deleteLater();
                downloadNam->deleteLater();
                return;
            }
            out.write(payload);
            out.close();
            dreply->deleteLater();
            downloadNam->deleteLater();

            if (portable) {
                launchPortableUpdate(destPath);
            } else {
                launchInstallerUpdate(destPath);
            }
        });
    });
}
