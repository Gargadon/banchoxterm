#include <QBuffer>
#include <QLayout>
#include <QFileDialog>
#include "sessiontags.h"
#include <QPlainTextEdit>
#include <QTest>
#include <QCoreApplication>
#include <QTemporaryDir>
#include <QTemporaryFile>
#include <QProcess>
#include <QRegularExpression>
#include <QFile>
#include <QFileInfo>
#include <QStandardPaths>
#include <QUuid>
#include <QDir>
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>
#include <QSettings>
#include <QSignalSpy>
#include <QTimer>
#include <QDialog>
#include <QLineEdit>
#include <QElapsedTimer>
#include <QThread>
#include <QScopeGuard>
#include <QTabWidget>
#include <QToolButton>
#include <QMouseEvent>
#include <QFrame>
#include <QPushButton>
#include <QPointer>
#include <QStandardPaths>
#include <memory>
#include <cmath>
#include <vector>
#include <QTcpServer>
#include <QTcpSocket>
#include <QSslConfiguration>
#include <QSslKey>
#include <QSslSocket>
#include "session.h"
#include "apppaths.h"
#include "keyring.h"
#include "masterpasswordmanager.h"
#include "vtterminalwidget.h"
#include "terminaltab.h"
#include "mainwindow.h"
#include "terminalpane.h"
#include "../third_party/qtermwidget/lib/Session.h"
#include "../third_party/qtermwidget/lib/Emulation.h"
#include <QDragEnterEvent>
#include <QDropEvent>
#include "ftpclient.h"
#include "sshconnection.h"
#include "sshbreak.h"
#include "sessiondialog.h"
#include <QComboBox>
#include <QAbstractItemView>
#include "sftptransferworker.h"
#include "sftpsidebar.h"
#include "sessionssidebar.h"
#include <QTreeWidget>
#include <QInputDialog>
#include <QListWidget>
#include <QLabel>
#include <QDialogButtonBox>
#include "sensitivedatafilter.h"
#include <QAction>
#include <QShortcut>
#include <QMenu>
#include <QClipboard>
#include "terminalappearance.h"
#include "settingsdialog.h"
#include "keyboardshortcuts.h"
#include <QKeySequenceEdit>
#include <QMessageBox>
#include <QSpinBox>
#include <QCheckBox>
#include <QScreen>
#include <QPainter>
#include <QStyle>
#ifdef Q_OS_WIN
#include "conpty.h"
#include <qt_windows.h>
#include <dwmapi.h>
#endif
#include <qtermwidget.h>
#include <TerminalDisplay.h>
#include <QScrollArea>
#include <QScrollBar>
#include <QProgressBar>
#include <QSplitter>
#include <QGroupBox>

class ExplicitTlsTestServer : public QTcpServer {
protected:
    void incomingConnection(qintptr socketDescriptor) override {
        auto* socket = new QSslSocket(this);
        if (!socket->setSocketDescriptor(socketDescriptor)) {
            socket->deleteLater();
            return;
        }
        addPendingConnection(socket);
    }
};

class TestSession : public QObject {
    Q_OBJECT

signals:
    void parallelTransferFinished(bool success, const QString& message);

private:
    static QString settingsPath() {
        return QDir(QDir::tempPath()).filePath("banchoxterm-test-settings");
    }

private slots:
    void testTransferContinuesWhenChangingTabs() {
        MainWindow window;
        auto* sidebar = window.findChild<SftpSidebar*>();
        QVERIFY(sidebar);
        QTemporaryDir dir;
        auto* localPath = sidebar->findChild<QLineEdit*>("localPathEdit");
        localPath->setText(dir.path());
        QVERIFY(QMetaObject::invokeMethod(localPath, "returnPressed"));
        QVERIFY(QMetaObject::invokeMethod(sidebar, "onConnectionSuccess"));
        QVERIFY(QMetaObject::invokeMethod(sidebar, "onDirectoryListed", Qt::DirectConnection,
            Q_ARG(QString, "/remote"), Q_ARG(QList<SftpFile>, QList<SftpFile>({{"folder", true}}))));
        auto* tree = sidebar->findChild<QTreeWidget*>("sftpTree");
        tree->topLevelItem(2)->setSelected(true);
        sidebar->findChild<QPushButton*>("localDownloadButton")->click();
        QVERIFY(sidebar->hasActiveTransfers());
        auto* pane = window.findChild<QTabWidget*>();
        QVERIFY(pane);
        QVERIFY(QMetaObject::invokeMethod(&window, "onCurrentTabChanged", Qt::DirectConnection,
            Q_ARG(QTabWidget*, pane), Q_ARG(int, -1)));
        QVERIFY(sidebar->hasActiveTransfers());
        auto* monitor = window.findChild<QDialog*>("transferMonitor");
        QVERIFY(monitor);
        QTRY_VERIFY(monitor->isVisible());
        monitor->grab().save("build/transfer-monitor-preview.png");
        auto* toggle = monitor->findChild<QPushButton*>("transferMonitorToggle");
        QVERIFY(toggle);
        const int expanded = monitor->height();
        toggle->click();
        QVERIFY(monitor->height() < expanded);
        QVERIFY(sidebar->hasActiveTransfers());
        toggle->click();
        QVERIFY(monitor->height() >= expanded);
        QVERIFY(QMetaObject::invokeMethod(sidebar, "onDirectoryListed", Qt::DirectConnection,
            Q_ARG(QString, "/remote/folder"), Q_ARG(QList<SftpFile>, QList<SftpFile>())));
        QVERIFY(!sidebar->hasActiveTransfers());
    }

    void testTransferConflictChoices() {
        QSettings settings;
        const QVariant original = settings.value("sftp/existingFilePolicy");
        auto restore = qScopeGuard([&] {
            if (original.isValid()) settings.setValue("sftp/existingFilePolicy", original);
            else settings.remove("sftp/existingFilePolicy");
        });
        settings.setValue("sftp/existingFilePolicy", 0);
        TransferConflict batch;
        QTimer::singleShot(0, [] {
            auto* box = qobject_cast<QMessageBox*>(QApplication::activeModalWidget());
            QVERIFY(box);
            box->checkBox()->setChecked(true);
            box->grab().save("build/transfer-conflict-preview.png");
            for (auto* button : box->buttons())
                if (box->buttonRole(button) == QMessageBox::ActionRole) button->click();
        });
        QCOMPARE(batch.resolve("/remote/existing.txt"), int(TransferConflict::Skip));
        QCOMPARE(batch.resolve("/remote/second.txt"), int(TransferConflict::Skip));
        QCOMPARE(settings.value("sftp/existingFilePolicy").toInt(), 0);
        TransferConflict permanent;
        QTimer::singleShot(0, [] {
            auto* box = qobject_cast<QMessageBox*>(QApplication::activeModalWidget());
            QVERIFY(box);
            for (auto* check : box->findChildren<QCheckBox*>()) check->setChecked(true);
            for (auto* button : box->buttons())
                if (box->buttonRole(button) == QMessageBox::AcceptRole) button->click();
        });
        QCOMPARE(permanent.resolve("existing.txt"), int(TransferConflict::Overwrite));
        TransferConflict later;
        QCOMPARE(later.resolve("other.txt"), int(TransferConflict::Overwrite));
        {
            SettingsDialog dialog;
            auto* policy = dialog.findChild<QComboBox*>("existingFilePolicy");
            QVERIFY(policy);
            QCOMPARE(policy->currentIndex(), 1);
            policy->setCurrentIndex(2);
            QVERIFY(QMetaObject::invokeMethod(&dialog, "onAccept", Qt::DirectConnection));
            QCOMPARE(settings.value("sftp/existingFilePolicy").toInt(), 2);
            SettingsDialog reopened;
            QCOMPARE(reopened.findChild<QComboBox*>("existingFilePolicy")->currentIndex(), 2);
        }
        settings.setValue("sftp/existingFilePolicy", 0);
        TransferConflict cancelled;
        QTimer::singleShot(0, [] {
            auto* box = qobject_cast<QMessageBox*>(QApplication::activeModalWidget());
            QVERIFY(box);
            box->reject();
        });
        QCOMPARE(cancelled.resolve("existing.txt"), int(TransferConflict::Cancel));
        QCOMPARE(cancelled.resolve("second.txt"), int(TransferConflict::Cancel));
    }

    void testTransferSkipPreservesLocalFile() {
        QTemporaryDir dir;
        const QString path = dir.filePath("existing.txt");
        QFile file(path);
        QVERIFY(file.open(QIODevice::WriteOnly)); file.write("original"); file.close();
        auto conflict = std::make_shared<TransferConflict>();
        conflict->decision = TransferConflict::Skip;
        SshConnection ssh;
        ssh.transferConflict = conflict;
        QSignalSpy sshDone(&ssh, &SshConnection::operationFinished);
        ssh.downloadFile("remote.txt", path);
        QCOMPARE(sshDone.count(), 1);
        QVERIFY(sshDone.first().first().toBool());
        FtpClient ftp;
        ftp.transferConflict = conflict;
        QSignalSpy ftpDone(&ftp, &FtpClient::operationFinished);
        ftp.downloadFile("remote.txt", path);
        QCOMPARE(ftpDone.count(), 1);
        QVERIFY(ftpDone.first().first().toBool());
        QVERIFY(file.open(QIODevice::ReadOnly)); QCOMPARE(file.readAll(), QByteArray("original"));
    }
    void testSftpLongTransferNamesKeepSidebarWidth() {
        QSplitter splitter;
        auto* sidebar = new SftpSidebar(&splitter);
        splitter.addWidget(sidebar);
        splitter.addWidget(new QWidget(&splitter));
        splitter.resize(1100, 900);
        splitter.setSizes({380, 720});
        splitter.show();
        auto* progress = sidebar->findChild<QLabel*>("sftpTransferLabel");
        auto* bar = sidebar->findChild<QProgressBar*>("sftpTransferProgress");
        auto* status = sidebar->findChild<QLabel*>("sftpStatus");
        QVERIFY(progress && bar && status);
        progress->show();
        bar->show();
        QCoreApplication::processEvents();
        const int width = sidebar->width();
        const QString name = QString(400, 'a') + ".zip";
        status->setText("Downloading " + name);
        QVERIFY(QMetaObject::invokeMethod(sidebar, "onTransferProgress", Qt::DirectConnection, Q_ARG(QString, name),
                                          Q_ARG(qint64, 524288), Q_ARG(qint64, 1048576)));
        QCoreApplication::processEvents();
        QCOMPARE(sidebar->width(), width);
        QVERIFY(progress->text().contains("512.0 KB / 1.0 MB"));
        QVERIFY(progress->toolTip().contains(name));
        QVERIFY(progress->height() >= progress->fontMetrics().height() * 2);
        QVERIFY(progress->fontMetrics().horizontalAdvance("512.0 KB / 1.0 MB") <= progress->contentsRect().width());
        QVERIFY(progress->minimumSizeHint().width() < width);
        splitter.resize(900, 900);
        QCoreApplication::processEvents();
        QVERIFY(progress->fontMetrics().horizontalAdvance("512.0 KB / 1.0 MB") <= progress->contentsRect().width());
    }

    void testSftpUserNavigationIsSeparateFromTerminalUpdates() {
        SftpSidebar sidebar;
        QVERIFY(QMetaObject::invokeMethod(&sidebar, "onConnectionSuccess", Qt::DirectConnection));
        QSignalSpy navigation(&sidebar, &SftpSidebar::requestNavigate);
        QSignalSpy listing(&sidebar, &SftpSidebar::requestList);
        sidebar.navigateTo("/terminal");
        QCOMPARE(navigation.count(), 0);
        QCOMPARE(listing.count(), 1);
        auto* path = sidebar.findChild<QLineEdit*>("remotePathEdit");
        path->setText("/chosen folder");
        QVERIFY(QMetaObject::invokeMethod(path, "returnPressed", Qt::DirectConnection));
        QCOMPARE(navigation.count(), 1);
        QCOMPARE(navigation.last().first().toString(), QString("/chosen folder"));
        auto* tree = sidebar.findChild<QTreeWidget*>("sftpTree");
        auto* directory = new QTreeWidgetItem(tree, {"nested"});
        directory->setData(0, Qt::UserRole, true);
        QVERIFY(QMetaObject::invokeMethod(&sidebar, "onItemDoubleClicked", Qt::DirectConnection,
                                          Q_ARG(QTreeWidgetItem*, directory), Q_ARG(int, 0)));
        QCOMPARE(navigation.last().first().toString(), QString("/terminal/nested"));
        QVERIFY(QMetaObject::invokeMethod(&sidebar, "onParentDirClicked", Qt::DirectConnection));
        QCOMPARE(navigation.last().first().toString(), QString("/terminal"));
    }

    void testRemoteDirectoryAnnouncements() {
        QSettings settings;
        const auto previous = settings.value("terminal/shellIntegration");
        const auto restore = qScopeGuard([&]() {
            if (previous.isValid())
                settings.setValue("terminal/shellIntegration", previous);
            else
                settings.remove("terminal/shellIntegration");
        });
        settings.setValue("terminal/shellIntegration", true);
        Session session;
        session.type = SessionType::SSH;
        TerminalTab tab(session);
        QSignalSpy changed(&tab, &TerminalTab::remoteDirChanged);
        auto feed = [&](const QByteArray& bytes) { tab.connection()->shellDataReceived(bytes); };
        feed("\x1b]7;file://remote.example/srv/my%20");
        feed("project\x1b");
        feed("\\");
        QCOMPARE(changed.count(), 1);
        QCOMPARE(changed.last().first().toString(), QString("/srv/my project"));
        QCOMPARE(tab.session().remoteDirectory, QString("/srv/my project"));
        feed("\x1b]7;file://another-host/var/log\x07");
        QCOMPARE(changed.count(), 2);
        QCOMPARE(tab.session().remoteDirectory, QString("/var/log"));
        feed("\x1b]7;https://remote.example/wrong\x07");
        QCOMPARE(changed.count(), 2);
        settings.setValue("terminal/shellIntegration", false);
        feed("\x1b]7;file://remote.example/ignored\x07");
        QCOMPARE(changed.count(), 2);
    }

    void testRemotePanelsFollowActiveTab() {
        MainWindow window;
        Session ssh;
        ssh.type = SessionType::SSH;
        ssh.name = "remote-one";
        ssh.host = "127.0.0.1";
        ssh.port = 1;
        QVERIFY(QMetaObject::invokeMethod(&window, "onConnectSession", Qt::DirectConnection, Q_ARG(Session, ssh)));
        auto* first = window.findChild<TerminalTab*>();
        QVERIFY(first);
        auto* pane = qobject_cast<QTabWidget*>(first->parentWidget()->parentWidget());
        QVERIFY(pane);
        auto* sidebar = window.findChild<SftpSidebar*>();
        auto* monitor = window.findChild<QFrame*>("remoteMonitorWidget");
        QVERIFY(sidebar && monitor);
        first->connection()->remoteStatsUpdated(10, 20, 30, 60);
        QVERIFY(!monitor->isHidden());

        Session local;
        local.type = SessionType::Local;
        // Do not show the window: this test needs no local shell process.
        auto* localTab = new TerminalTab(local, pane);
        pane->addTab(localTab, "local");
        pane->setCurrentWidget(localTab);
        QVERIFY(monitor->isHidden());
        QVERIFY(!sidebar->connection());
        first->connection()->remoteStatsUpdated(11, 21, 31, 61);
        QVERIFY(monitor->isHidden());

        ssh.name = "remote-two";
        QVERIFY(QMetaObject::invokeMethod(&window, "onConnectSession", Qt::DirectConnection, Q_ARG(Session, ssh)));
        auto* second = qobject_cast<TerminalTab*>(pane->currentWidget());
        QVERIFY(second && second != first);
        first->connection()->remoteStatsUpdated(99, 99, 99, 99);
        QVERIFY(monitor->isHidden());
        second->connection()->remoteStatsUpdated(12, 22, 32, 62);
        QVERIFY(!monitor->isHidden());

        QVERIFY(QMetaObject::invokeMethod(sidebar, "onConnectionSuccess", Qt::DirectConnection));
        QSignalSpy lists(sidebar, &SftpSidebar::requestList);
        first->remoteDirChanged("/background");
        QCOMPARE(lists.count(), 0);
        QCOMPARE(first->session().remoteDirectory, QString("/background"));
        second->remoteDirChanged("/active");
        QCOMPARE(lists.count(), 1);
        QCOMPARE(lists.last().first().toString(), QString("/active"));
        pane->setCurrentWidget(first);
        QCOMPARE(sidebar->connection(), first->connection());
        QVERIFY(monitor->isHidden());
        QVERIFY(QMetaObject::invokeMethod(sidebar, "onConnectionSuccess", Qt::DirectConnection));
        QCOMPARE(lists.last().first().toString(), QString("/background"));
    }

    void testGlobalTerminalFontValidation() {
        QSettings settings;
        const QVariant previous = settings.value("terminal/font");
        const bool existed = settings.contains("terminal/font");
        const auto restore = qScopeGuard([&]() {
            if (existed)
                settings.setValue("terminal/font", previous);
            else
                settings.remove("terminal/font");
        });
        settings.remove("terminal/font");
        const QFont fallback = TerminalAppearance::defaultFont();
        QCOMPARE(TerminalAppearance::globalFont(), fallback);
        for (const QString& value : {QString(), QStringLiteral("invalid,font,data")}) {
            settings.setValue("terminal/font", value);
            QCOMPARE(TerminalAppearance::globalFont(), fallback);
            SettingsDialog dialog;
            QCOMPARE(dialog.selectedFont(), fallback);
        }
        QFont chosen(QStringLiteral("Consolas"), 17);
        settings.setValue("terminal/font", chosen.toString());
        QCOMPARE(TerminalAppearance::globalFont().family(), chosen.family());
        QCOMPARE(TerminalAppearance::globalFont().pointSize(), chosen.pointSize());
        SettingsDialog dialog;
        QCOMPARE(dialog.selectedFont().family(), chosen.family());
        QCOMPARE(dialog.selectedFont().pointSize(), chosen.pointSize());
        QVERIFY(QMetaObject::invokeMethod(&dialog, "onAccept", Qt::DirectConnection));
        const QString saved = settings.value("terminal/font").toString();
        QCOMPARE(saved.split(',').size(), 16);
        QFont restored;
        QVERIFY(restored.fromString(saved));
        QCOMPARE(restored.family(), chosen.family());
        QCOMPARE(restored.pointSize(), chosen.pointSize());
    }

    void testTerminalFontSurvivesStyleRefresh() {
        const QString previousStyle = qApp->styleSheet();
        const auto restore = qScopeGuard([&]() { qApp->setStyleSheet(previousStyle); });
        QTermWidget terminal(0);
        QFont chosen(QStringLiteral("Consolas"), 17);
        chosen.setStyleHint(QFont::Monospace);
        chosen.setFixedPitch(true);
        terminal.setTerminalFont(chosen);
        terminal.show();
        QCoreApplication::processEvents();
        const QFont before = terminal.getTerminalFont();
        qApp->setStyleSheet(QStringLiteral("QWidget { color: #eeeeee; background-color: #222222; }"));
        QCoreApplication::processEvents();
        QCOMPARE(terminal.getTerminalFont().family(), before.family());
        QCOMPARE(terminal.getTerminalFont().pointSize(), before.pointSize());
        qApp->setStyleSheet(QStringLiteral("QWidget { color: #222222; background-color: #eeeeee; }"));
        for (QWidget* widget : QApplication::allWidgets()) {
            qApp->style()->unpolish(widget);
            qApp->style()->polish(widget);
        }
        QCoreApplication::processEvents();
        QCOMPARE(terminal.getTerminalFont().family(), before.family());
        QCOMPARE(terminal.getTerminalFont().pointSize(), before.pointSize());
    }

    void testSftpLocalActions() {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        SftpSidebar sidebar;
        auto* path = sidebar.findChild<QLineEdit*>("localPathEdit");
        auto* tree = sidebar.findChild<QTreeWidget*>("localFileTree");
        path->setText(dir.path());
        QVERIFY(QMetaObject::invokeMethod(path, "returnPressed"));
        QCOMPARE(path->text(), QDir::toNativeSeparators(dir.path()));
        QFile localFile(dir.filePath("sample.txt"));
        QVERIFY(localFile.open(QIODevice::WriteOnly));
        QCOMPARE(localFile.write(QByteArray(2048, 'x')), qint64(2048));
        localFile.close();
        QTRY_COMPARE(tree->findItems("sample.txt", Qt::MatchExactly).size(), 1);
        const QList<SftpFile> remoteFiles = {{"sample.txt", false, 2048, QFileInfo(localFile).lastModified()}};
        QVERIFY(QMetaObject::invokeMethod(&sidebar, "onDirectoryListed", Qt::DirectConnection,
                                          Q_ARG(QString, QString("/remote")), Q_ARG(QList<SftpFile>, remoteFiles)));
        auto* remoteTree = sidebar.findChild<QTreeWidget*>("sftpTree");
        auto* localItem = tree->findItems("sample.txt", Qt::MatchExactly).first();
        auto* remoteItem = remoteTree->findItems("sample.txt", Qt::MatchExactly).first();
        QCOMPARE(tree->columnCount(), remoteTree->columnCount());
        for (int column = 0; column < tree->columnCount(); ++column) {
            QCOMPARE(tree->headerItem()->text(column), remoteTree->headerItem()->text(column));
            QCOMPARE(localItem->text(column), remoteItem->text(column));
        }
        QCOMPARE(localItem->icon(0).cacheKey(), remoteItem->icon(0).cacheKey());
        path->setText(dir.filePath("missing"));
        QVERIFY(QMetaObject::invokeMethod(path, "returnPressed"));
        QCOMPARE(path->text(), QDir::toNativeSeparators(dir.path()));

        QTimer::singleShot(0, []() {
            auto* dialog = qobject_cast<QInputDialog*>(QApplication::activeModalWidget());
            QVERIFY(dialog);
            dialog->setTextValue("created");
            dialog->accept();
        });
        sidebar.findChild<QPushButton*>("localNewFolderButton")->click();
        QVERIFY(QFileInfo(dir.filePath("created")).isDir());
        QCOMPARE(tree->findItems("created", Qt::MatchExactly).size(), 1);
        tree->setCurrentItem(tree->findItems("created", Qt::MatchExactly).first());
        QTimer::singleShot(0, []() {
            auto* dialog = qobject_cast<QInputDialog*>(QApplication::activeModalWidget());
            QVERIFY(dialog);
            dialog->setTextValue("renamed");
            dialog->accept();
        });
        sidebar.findChild<QPushButton*>("localRenameButton")->click();
        QVERIFY(QFileInfo(dir.filePath("renamed")).isDir());
        QVERIFY(!QFileInfo::exists(dir.filePath("created")));
        QCOMPARE(tree->findItems("renamed", Qt::MatchExactly).size(), 1);
        auto* folderItem = tree->findItems("renamed", Qt::MatchExactly).first();
        QVERIFY(!tree->rootIsDecorated());
        QCOMPARE(tree->columnCount(), 3);
        QCOMPARE(folderItem->text(1), QString("<DIR>"));
        QVERIFY(
            QMetaObject::invokeMethod(tree, "itemDoubleClicked", Q_ARG(QTreeWidgetItem*, folderItem), Q_ARG(int, 0)));
        QCOMPARE(path->text(), QDir::toNativeSeparators(dir.filePath("renamed")));
        QCOMPARE(tree->topLevelItem(0)->text(0), QString("."));
        QCOMPARE(tree->topLevelItem(1)->text(0), QString(".."));
        sidebar.findChild<QPushButton*>("localUpButton")->click();
        QCOMPARE(path->text(), QDir::toNativeSeparators(dir.path()));
        sidebar.findChild<QPushButton*>("localUpButton")->click();
        QCOMPARE(path->text(), QDir::toNativeSeparators(QFileInfo(dir.path()).absolutePath()));
    }

    void testSftpDownloadDirectories_data() {
        QTest::addColumn<bool>("cancel");
        QTest::addColumn<bool>("remoteAction");
        QTest::newRow("local-download") << false << false;
        QTest::newRow("remote-download") << false << true;
        QTest::newRow("cancel") << true << false;
    }

    void testSftpDownloadDirectories() {
        QFETCH(bool, cancel);
        QFETCH(bool, remoteAction);
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        QVERIFY(QDir(dir.path()).mkdir("folder"));
        SftpSidebar sidebar;
        auto* path = sidebar.findChild<QLineEdit*>("localPathEdit");
        path->setText(dir.path());
        QVERIFY(QMetaObject::invokeMethod(path, "returnPressed"));
        QVERIFY(QMetaObject::invokeMethod(&sidebar, "onConnectionSuccess"));
        auto list = [&sidebar](const QString& remotePath, const QList<SftpFile>& files) {
            return QMetaObject::invokeMethod(&sidebar, "onDirectoryListed", Qt::DirectConnection,
                                             Q_ARG(QString, remotePath), Q_ARG(QList<SftpFile>, files));
        };
        QVERIFY(list("/remote", {{"folder", true}}));
        auto* remoteTree = sidebar.findChild<QTreeWidget*>("sftpTree");
        remoteTree->topLevelItem(2)->setSelected(true);
        auto* download = sidebar.findChild<QPushButton*>("localDownloadButton");
        QVERIFY(download->isEnabled());
        QSignalSpy listings(&sidebar, &SftpSidebar::requestList);
        if (remoteAction)
            QVERIFY(QMetaObject::invokeMethod(&sidebar, "onDownloadClicked"));
        else
            download->click();
        QCOMPARE(listings.last().first().toString(), QString("/remote/folder"));
        QVERIFY(QFileInfo(dir.filePath("folder")).isDir());
        QVERIFY(!remoteTree->isEnabled());
        if (cancel)
            QVERIFY(QMetaObject::invokeMethod(&sidebar, "onCancelQueuedTransfers"));
        QVERIFY(list("/remote/folder", {{"nested", true}, {".", true}, {"..", true}}));
        if (cancel) {
            QVERIFY(!QFileInfo::exists(dir.filePath("folder/nested")));
        } else {
            QCOMPARE(listings.last().first().toString(), QString("/remote/folder/nested"));
            QVERIFY(QFileInfo(dir.filePath("folder/nested")).isDir());
            QVERIFY(list("/remote/folder/nested", {}));
        }
        QVERIFY(remoteTree->isEnabled());
        QCOMPARE(sidebar.findChild<QLineEdit*>("remotePathEdit")->text(), QString("/remote"));
        QCOMPARE(listings.last().first().toString(), QString("/remote"));
    }

    void testSessionDialogEscape() {
        for (int type = 0; type < 7; ++type) {
            for (int action = 0; action < 3; ++action) {
                Session session;
                session.type = static_cast<SessionType>(type);
                SessionDialog dialog(session);
                QTimer::singleShot(0, &dialog, [&dialog, action]() {
                    if (action == 0)
                        QTest::keyClick(&dialog, Qt::Key_Escape);
                    else if (action == 1)
                        dialog.findChild<QPushButton*>("cancelButton")->click();
                    else
                        dialog.close();
                });
                QCOMPARE(dialog.exec(), int(QDialog::Rejected));
            }
        }
    }

    void initTestCase() {
        qRegisterMetaType<Session>("Session");
        qRegisterMetaType<SftpFile>("SftpFile");
        qRegisterMetaType<QList<SftpFile>>("QList<SftpFile>");
        // Match the app's QSettings namespace so MasterPasswordManager tests
        // use a valid settings path.
        QCoreApplication::setOrganizationName(QStringLiteral("BanchoXterm"));
        QCoreApplication::setApplicationName(QStringLiteral("BanchoXterm"));
        // Keep tests independent of the developer's registry/home settings.
        QSettings::setDefaultFormat(QSettings::IniFormat);
        QStandardPaths::setTestModeEnabled(true);
        const QString configOverride = QDir(QDir::tempPath()).filePath("banchoxterm-session-test-config");
        QDir(configOverride).removeRecursively();
        qputenv("BANCHO_CONFIG_DIR", configOverride.toUtf8());
        QSettings::setPath(QSettings::IniFormat, QSettings::UserScope,
                           QDir(QDir::tempPath()).filePath("banchoxterm-test-settings"));
        QDir(settingsPath()).removeRecursively();
    }

    void testSessionOrganizationRoundtrip() {
        Session original;
        original.id = "organization";
        original.group = "Production/Databases";
        original.notes = "Database maintenance instructions";
        original.tags = parseSessionTags("App:MiApp\nApp:OtherApp\nDatabase:PostgreSQL");
        const Session restored = Session::fromJson(original.toJson());
        QCOMPARE(restored.group, original.group);
        QCOMPARE(restored.notes, original.notes);
        QCOMPARE(restored.tags, original.tags);
        QVERIFY(Session::fromJson(QJsonObject()).tags.isEmpty());
        bool valid = false;
        QCOMPARE(parseSessionTags(formatSessionTags(original.tags), &valid), original.tags);
        QVERIFY(valid);
        parseSessionTags("missing separator", &valid);
        QVERIFY(!valid);
    }

    void testSessionSaveChoice() {
        const auto previous = SessionManager::loadSessions();
        const auto restore = qScopeGuard([&]() { SessionManager::saveSessions(previous); });
        SessionManager::saveSessions({});
        SessionDialog temporary;
        temporary.findChild<QComboBox*>()->setCurrentIndex(1);
        QVERIFY(!temporary.findChild<QCheckBox*>("saveSessionCheck")->isChecked());
        temporary.accept();
        QVERIFY(SessionManager::loadSessions().isEmpty());
        SessionDialog saved;
        saved.findChild<QComboBox*>()->setCurrentIndex(1);
        saved.setSaveSession(true);
        saved.findChild<QPlainTextEdit*>("sessionTagsEdit")->setPlainText("App:MiApp");
        saved.findChild<QPlainTextEdit*>("sessionNotesEdit")->setPlainText("Example notes");
        saved.accept();
        auto sessions = SessionManager::loadSessions();
        QCOMPARE(sessions.size(), 1);
        QCOMPARE(sessions.first().notes, QString("Example notes"));
        sessions.first().favorite = true;
        SessionDialog edit(sessions.first());
        edit.accept();
        QCOMPARE(SessionManager::loadSessions().size(), 1);
        QVERIFY(SessionManager::loadSessions().first().favorite);
    }

    void testNewSessionConnectsWithOptionalSave() {
        const auto previous = SessionManager::loadSessions();
        const auto restore = qScopeGuard([&]() { SessionManager::saveSessions(previous); });
        SessionManager::saveSessions({});
        SessionsSidebar sidebar;
        QSignalSpy connections(&sidebar, &SessionsSidebar::connectSession);
        for (bool save : {false, true}) {
            bool savingInitiallyDisabled = false;
            QTimer::singleShot(0, &sidebar, [&]() {
                auto* dialog = qobject_cast<SessionDialog*>(QApplication::activeModalWidget());
                if (!dialog)
                    return;
                savingInitiallyDisabled = !dialog->savesSession();
                dialog->findChild<QComboBox*>()->setCurrentIndex(1);
                dialog->setSaveSession(save);
                dialog->accept();
            });
            sidebar.onNewSession();
            QVERIFY(savingInitiallyDisabled);
            QCOMPARE(connections.count(), save ? 2 : 1);
            const auto session = qvariant_cast<Session>(connections.last().first());
            const auto stored = SessionManager::loadSessions();
            QCOMPARE(stored.size(), save ? 1 : 0);
            if (save)
                QCOMPARE(stored.first().id, session.id);
        }
        auto* tree = sidebar.findChild<QTreeWidget*>("sessionTree");
        QCOMPARE(tree->topLevelItemCount(), 1);
        QTimer::singleShot(0, &sidebar, []() {
            if (auto* dialog = qobject_cast<SessionDialog*>(QApplication::activeModalWidget()))
                dialog->reject();
        });
        sidebar.onNewSession();
        QCOMPARE(connections.count(), 2);
        QCOMPARE(SessionManager::loadSessions().size(), 1);
    }

    void testSessionGroupsAndFiltering() {
        const auto previous = SessionManager::loadSessions();
        QSettings settings;
        const auto oldGroups = settings.value("sessions/groups");
        const auto oldCollapsed = settings.value("sessions/collapsedGroups");
        const auto oldMetadata = settings.value("sessions/groupMetadata");
        const auto oldOrder = settings.value("sessions/treeOrder");
        const auto restore = qScopeGuard([&]() {
            SessionManager::saveSessions(previous);
            settings.setValue("sessions/groups", oldGroups);
            settings.setValue("sessions/collapsedGroups", oldCollapsed);
            settings.setValue("sessions/groupMetadata", oldMetadata);
            settings.setValue("sessions/treeOrder", oldOrder);
        });
        settings.remove("sessions/treeOrder");
        settings.setValue("sessions/groups", QStringList{"Production", "Development", "Empty"});
        settings.remove("sessions/collapsedGroups");
        QVariantMap groupData;
        groupData.insert(
            "tags", QJsonDocument(parseSessionTags("Environment:Live\nApp:OtherApp")).toJson(QJsonDocument::Compact));
        QVariantMap metadata;
        metadata.insert("Production", groupData);
        settings.setValue("sessions/groupMetadata", metadata);
        Session first;
        first.id = "first";
        first.name = "Primary";
        first.group = "Production/Databases";
        first.tags = parseSessionTags("App:MiApp\nDatabase:PostgreSQL\nEnvironment:Live");
        Session second;
        second.id = "second";
        second.name = "Secondary";
        second.group = "Development";
        SessionManager::saveSessions({first, second});
        SessionsSidebar sidebar;
        auto* tree = sidebar.findChild<QTreeWidget*>("sessionTree");
        auto* filter = sidebar.findChild<QLineEdit*>("sessionFilterEdit");
        QCOMPARE(tree->topLevelItemCount(), 3);
        auto* production = tree->topLevelItem(0);
        production->setExpanded(false);
        sidebar.refreshSessions();
        production = tree->topLevelItem(0);
        QVERIFY(!production->isExpanded());
        QCOMPARE(production->child(0)->text(0), QString("Databases"));
        filter->setText("App:MiApp Environment:Live");
        QVERIFY(!production->isHidden());
        QVERIFY(production->isExpanded());
        QVERIFY(tree->topLevelItem(1)->isHidden());
        filter->setText("App:OtherApp");
        QVERIFY(!production->isHidden());          // The group itself defines this value.
        QVERIFY(production->child(0)->isHidden()); // Its sessions do not inherit it.
        filter->clear();
        QVERIFY(!production->isExpanded());
        filter->setText("no-match");
        QVERIFY(production->isHidden());
        filter->clear();
        auto* development = tree->takeTopLevelItem(1);
        production->addChild(development);
        sidebar.saveCurrentOrder();
        QCOMPARE(SessionManager::loadSessions().last().group, QString("Production/Development"));
        sidebar.refreshSessions();
        QCOMPARE(tree->topLevelItem(0)->childCount(), 2);
        QVERIFY(settings.value("sessions/groups").toStringList().contains("Empty"));
    }

    void testExplicitSessionTagsAndGroupMatches() {
        QSettings settings;
        const auto previous = SessionManager::loadSessions();
        const auto previousSettings = settings.value("sessions/groupMetadata");
        const auto previousGroups = settings.value("sessions/groups");
        const auto previousCollapsed = settings.value("sessions/collapsedGroups");
        const auto previousOrder = settings.value("sessions/treeOrder");
        const auto restore = qScopeGuard([&]() {
            SessionManager::saveSessions(previous);
            settings.setValue("sessions/groupMetadata", previousSettings);
            settings.setValue("sessions/groups", previousGroups);
            settings.setValue("sessions/collapsedGroups", previousCollapsed);
            settings.setValue("sessions/treeOrder", previousOrder);
        });
        settings.remove("sessions/treeOrder");
        settings.setValue("sessions/groups", QStringList{"Production", "Development", "Empty"});
        settings.setValue("sessions/collapsedGroups", QStringList{"Production"});
        QVariantMap metadata;
        QVariantMap definition;
        definition.insert("tags",
                          QJsonDocument(parseSessionTags("App:MiApp\nApp:OtherApp")).toJson(QJsonDocument::Compact));
        metadata.insert("Production", definition);
        definition.insert("tags", QJsonDocument(parseSessionTags("App:EmptyApp")).toJson(QJsonDocument::Compact));
        definition.insert("notes", "Empty group runbook");
        metadata.insert("Empty", definition);
        settings.setValue("sessions/groupMetadata", metadata);
        Session assigned;
        assigned.id = "assigned";
        assigned.name = "Assigned";
        assigned.group = "Production";
        assigned.tags = parseSessionTags("App:MiApp");
        Session unassigned = assigned;
        unassigned.id = "unassigned";
        unassigned.name = "Unassigned";
        unassigned.tags = {};
        Session development = assigned;
        development.id = "development";
        development.group = "Development";
        SessionManager::saveSessions({assigned, unassigned, development});
        SessionsSidebar sidebar;
        auto* tree = sidebar.findChild<QTreeWidget*>("sessionTree");
        auto* filter = sidebar.findChild<QLineEdit*>("sessionFilterEdit");
        auto* production = tree->topLevelItem(0);
        auto* dev = tree->topLevelItem(1);
        auto* empty = tree->topLevelItem(2);
        filter->setText("app:miapp");
        QVERIFY(!production->isHidden());
        QVERIFY(!production->child(0)->isHidden());
        QVERIFY(production->child(1)->isHidden());
        QVERIFY(!dev->isHidden()); // The matching session keeps its parent visible.
        QVERIFY(!dev->child(0)->isHidden());
        filter->setText("App:OtherApp");
        QVERIFY(!production->isHidden());
        QVERIFY(production->child(0)->isHidden());
        QVERIFY(production->child(1)->isHidden());
        QVERIFY(dev->isHidden());
        filter->setText("App:EmptyApp");
        QVERIFY(!empty->isHidden());
        QVERIFY(production->isHidden());
        filter->setText("Empty runbook");
        QVERIFY(!empty->isHidden());
        filter->setText("App");
        QVERIFY(!empty->isHidden());
        QVERIFY(production->child(1)->isHidden());
        filter->setText("Production");
        QVERIFY(!production->child(1)->isHidden()); // Its actual group name is searchable.
        filter->clear();
        QVERIFY(!production->isExpanded());
        QVERIFY(!production->child(1)->isHidden());
        QVERIFY(SessionManager::loadSessions()[1].tags.isEmpty());
    }

    void testMixedSessionTreeOrder() {
        QSettings settings;
        const auto previous = SessionManager::loadSessions();
        const auto previousGroups = settings.value("sessions/groups");
        const auto previousCollapsed = settings.value("sessions/collapsedGroups");
        const auto previousOrder = settings.value("sessions/treeOrder");
        const auto restore = qScopeGuard([&]() {
            SessionManager::saveSessions(previous);
            settings.setValue("sessions/groups", previousGroups);
            settings.setValue("sessions/collapsedGroups", previousCollapsed);
            settings.setValue("sessions/treeOrder", previousOrder);
        });
        settings.remove("sessions/treeOrder");
        settings.setValue("sessions/groups", QStringList{"G", "G/Nested", "Empty"});
        settings.setValue("sessions/collapsedGroups", QStringList{"G"});
        QList<Session> sessions;
        for (const QString& name : {"Loose1", "Grouped1", "Loose2", "Grouped2"}) {
            Session session;
            session.id = session.name = name;
            if (name.startsWith("Grouped"))
                session.group = "G";
            sessions.append(session);
        }
        SessionManager::saveSessions(sessions);
        SessionsSidebar sidebar;
        auto* tree = sidebar.findChild<QTreeWidget*>("sessionTree");
        auto* root = tree->invisibleRootItem();
        auto children = root->takeChildren(); // Legacy load: G, Empty, Loose1, Loose2.
        QCOMPARE(children.size(), 4);
        auto* group = children[0];
        root->addChildren({children[3], children[1], children[0], children[2]});
        auto nested = group->takeChildren(); // Nested, Grouped1, Grouped2.
        QCOMPARE(nested.size(), 3);
        group->addChildren({nested[2], nested[0], nested[1]});
        group->setExpanded(false);
        sidebar.saveCurrentOrder();
        auto verifyOrder = [&](QTreeWidget* current) {
            QStringList top;
            for (int i = 0; i < current->topLevelItemCount(); ++i)
                top.append(current->topLevelItem(i)->text(0));
            QCOMPARE(top, (QStringList{"Loose2", "Empty", "G", "Loose1"}));
            auto* g = current->topLevelItem(2);
            QStringList inner;
            for (int i = 0; i < g->childCount(); ++i)
                inner.append(g->child(i)->text(0));
            QCOMPARE(inner, (QStringList{"Grouped2", "Nested", "Grouped1"}));
            QVERIFY(!g->isExpanded());
        };
        sidebar.refreshSessions();
        verifyOrder(tree);
        {
            SessionsSidebar reopened;
            verifyOrder(reopened.findChild<QTreeWidget*>("sessionTree"));
        }
        Session added;
        added.id = added.name = "New session";
        sessions = SessionManager::loadSessions();
        sessions.append(added);
        SessionManager::saveSessions(sessions);
        sidebar.refreshSessions();
        QCOMPARE(tree->topLevelItemCount(), 5);
        QCOMPARE(tree->topLevelItem(0)->text(0), QString("Loose2"));
        QCOMPARE(tree->topLevelItem(4)->text(0), added.name);
        // A stale/deleted entry must neither resurrect a profile nor lose its siblings' order.
        sessions.removeLast();
        SessionManager::saveSessions(sessions);
        sidebar.refreshSessions();
        verifyOrder(tree);
        QVERIFY(!settings.value("sessions/treeOrder").toMap().value("").toStringList().contains("s:New session"));
        QCoreApplication::processEvents(); // Flush deferred saves from the manual moves.
        verifyOrder(tree);
    }

    void testGroupManagement() {
        QSettings settings;
        const auto previous = SessionManager::loadSessions();
        const auto oldGroups = settings.value("sessions/groups");
        const auto oldCollapsed = settings.value("sessions/collapsedGroups");
        const auto oldMetadata = settings.value("sessions/groupMetadata");
        const auto oldOrder = settings.value("sessions/treeOrder");
        const auto restore = qScopeGuard([&]() {
            SessionManager::saveSessions(previous);
            settings.setValue("sessions/groups", oldGroups);
            settings.setValue("sessions/collapsedGroups", oldCollapsed);
            settings.setValue("sessions/groupMetadata", oldMetadata);
            settings.setValue("sessions/treeOrder", oldOrder);
        });
        settings.remove("sessions/treeOrder");
        settings.setValue("sessions/groups", QStringList{"Production", "Production/Database"});
        settings.setValue("sessions/collapsedGroups", QStringList{"Production"});
        settings.remove("sessions/groupMetadata");
        Session session;
        session.id = "group-management";
        session.group = "Production/Database";
        SessionManager::saveSessions({session});
        SessionsSidebar sidebar;
        sidebar.resize(500, 600);
        sidebar.show();
        auto* tree = sidebar.findChild<QTreeWidget*>("sessionTree");
        auto runAction = [&](const QString& name, const QString& text, bool deleting = false) {
            QTimer::singleShot(0, &sidebar, [&, name, text, deleting]() {
                auto* menu = qobject_cast<QMenu*>(QApplication::activePopupWidget());
                if (!menu)
                    return;
                QAction* selected = nullptr;
                for (auto* action : menu->actions())
                    if (action->text() == name)
                        selected = action;
                if (!selected) {
                    menu->close();
                    return;
                }
                QTimer::singleShot(0, &sidebar, [text, deleting]() {
                    if (deleting) {
                        auto* box = qobject_cast<QMessageBox*>(QApplication::activeModalWidget());
                        if (box)
                            box->button(QMessageBox::Yes)->click();
                    } else {
                        auto* input = qobject_cast<QInputDialog*>(QApplication::activeModalWidget());
                        if (input) {
                            input->setTextValue(text);
                            input->accept();
                        }
                    }
                });
                menu->setActiveAction(selected);
                QTest::keyClick(menu, Qt::Key_Return);
            });
            return QMetaObject::invokeMethod(&sidebar, "showContextMenu", Qt::DirectConnection,
                                             Q_ARG(QPoint, tree->visualItemRect(tree->topLevelItem(0)).center()));
        };
        QVERIFY(runAction("Edit group notes", "Production runbook"));
        QVERIFY(runAction("Edit group tags", "App:MiApp\nApp:OtherApp"));
        {
            SessionDialog dialog(session);
            auto* choices = dialog.findChild<QComboBox*>("groupTagChoices");
            QVERIFY(choices);
            QCOMPARE(choices->count(), 3);
            QVERIFY(dialog.getSession().tags.isEmpty());
            const int index = choices->findText("App:MiApp");
            QVERIFY(index > 0);
            QVERIFY(QMetaObject::invokeMethod(choices, "activated", Q_ARG(int, index)));
            QCOMPARE(dialog.getSession().tags, parseSessionTags("App:MiApp"));
        }
        QVERIFY(runAction("Rename group", "Live"));
        QCOMPARE(SessionManager::loadSessions().first().group, QString("Live/Database"));
        QCOMPARE(tree->topLevelItem(0)->text(0), QString("Live"));
        QVERIFY(!tree->topLevelItem(0)->isExpanded());
        QCOMPARE(settings.value("sessions/groupMetadata").toMap().value("Live").toMap().value("notes").toString(),
                 QString("Production runbook"));
        const auto renamedOrder = settings.value("sessions/treeOrder").toMap();
        QVERIFY(!renamedOrder.contains("Production"));
        QCOMPARE(renamedOrder.value("Live").toStringList(), QStringList{"g:Live/Database"});
        QVERIFY(!renamedOrder.contains("Production/Database"));
        QCOMPARE(renamedOrder.value("Live/Database").toStringList(), QStringList{"s:group-management"});
        QVERIFY(runAction("Create group", "Live/Empty"));
        QVERIFY(settings.value("sessions/groups").toStringList().contains("Live/Empty"));
        QVERIFY(runAction("Delete group and its sessions", {}, true));
        QVERIFY(SessionManager::loadSessions().isEmpty());
        QCOMPARE(tree->topLevelItemCount(), 0);
        QVERIFY(settings.value("sessions/groupMetadata").toMap().isEmpty());
    }

    void testSshSessionRoundtrip() {
        Session original;
        original.id = "test-id-ssh";
        original.name = "My Server";
        original.type = SessionType::SSH;
        original.favorite = true;
        original.host = "example.com";
        original.user = "admin";
        original.port = 2222;
        original.keyPath = "/home/user/.ssh/id_rsa";
        original.remoteDirectory = "/var/log";
        original.jumpHost = "bastion.example.com";
        original.jumpUser = "jumpadmin";
        original.jumpPort = 2200;
        original.jumpKeyPath = "/home/user/.ssh/jump_rsa";
        original.jumpSessionId = "saved-bastion";
        original.x11Forwarding = true;
        original.keepAliveSeconds = 30;
        original.cryptCipher = "aes128-ctr,aes256-ctr";
        original.kexAlgo = "curve25519-sha256";
        original.macAlgo = "hmac-sha2-256";
        original.scrollback = 10000;
        original.fontFamily = "JetBrains Mono";
        original.fontSize = 12;
        original.readOnly = true;
        original.manufacturerProfile = "cisco";
        original.promptPattern = R"([#$>]\s*$)";

        QJsonObject json = original.toJson();
        QCOMPARE(json["id"].toString(), QString("test-id-ssh"));
        QCOMPARE(json["name"].toString(), QString("My Server"));
        QCOMPARE(json["type"].toString(), QString("ssh"));
        QCOMPARE(json["host"].toString(), QString("example.com"));
        QCOMPARE(json["user"].toString(), QString("admin"));
        QCOMPARE(json["port"].toInt(), 2222);
        QCOMPARE(json["keyPath"].toString(), QString("/home/user/.ssh/id_rsa"));
        QVERIFY(json["favorite"].toBool());
        QCOMPARE(json["jumpHost"].toString(), QString("bastion.example.com"));
        QVERIFY(json["x11Forwarding"].toBool());

        Session restored = Session::fromJson(json);
        QCOMPARE(restored.id, original.id);
        QCOMPARE(restored.name, original.name);
        QCOMPARE(restored.type, original.type);
        QCOMPARE(restored.host, original.host);
        QCOMPARE(restored.user, original.user);
        QCOMPARE(restored.port, original.port);
        QCOMPARE(restored.keyPath, original.keyPath);
        QCOMPARE(restored.remoteDirectory, original.remoteDirectory);
        QCOMPARE(restored.favorite, original.favorite);
        QCOMPARE(restored.jumpHost, original.jumpHost);
        QCOMPARE(restored.jumpUser, original.jumpUser);
        QCOMPARE(restored.jumpPort, original.jumpPort);
        QCOMPARE(restored.jumpKeyPath, original.jumpKeyPath);
        QCOMPARE(restored.jumpSessionId, original.jumpSessionId);
        QCOMPARE(restored.x11Forwarding, original.x11Forwarding);
        QCOMPARE(restored.keepAliveSeconds, original.keepAliveSeconds);
        QCOMPARE(restored.cryptCipher, original.cryptCipher);
        QCOMPARE(restored.kexAlgo, original.kexAlgo);
        QCOMPARE(restored.macAlgo, original.macAlgo);
        QCOMPARE(restored.scrollback, original.scrollback);
        QCOMPARE(restored.fontFamily, original.fontFamily);
        QCOMPARE(restored.fontSize, original.fontSize);
        QVERIFY(restored.readOnly);
        QCOMPARE(restored.manufacturerProfile, QString("cisco"));
        QCOMPARE(restored.promptPattern, original.promptPattern);
    }

    void testSavedBastionSelection() {
        const QList<Session> previous = SessionManager::loadSessions();
        const auto restore = qScopeGuard([&]() { SessionManager::saveSessions(previous); });
        Session bastion;
        bastion.id = "bastion-profile";
        bastion.name = "Bastion";
        bastion.type = SessionType::SSH;
        bastion.host = "bastion.example";
        bastion.port = 2222;
        bastion.user = "jump-user";
        bastion.keyPath = "jump-key";
        Session target;
        target.id = "target-profile";
        target.type = SessionType::SSH;
        target.host = "target.example";
        Session nested = target;
        nested.id = "nested-profile";
        nested.jumpHost = bastion.host;
        Session local;
        local.id = "local-profile";
        local.type = SessionType::Local;
        SessionManager::saveSessions({bastion, target, nested, local});
        {
            SessionDialog dialog(target);
            auto* selector = dialog.findChild<QComboBox*>("jumpSessionCombo");
            QVERIFY(selector);
            QCOMPARE(selector->count(), 2); // Manual and the direct bastion, excluding self.
            dialog.show();
            selector->showPopup();
            QCoreApplication::processEvents();
            for (int row = 0; row < 2; ++row) {
                const QRect item = selector->view()->visualRect(selector->model()->index(row, 0));
                QVERIFY(!item.isEmpty());
                QVERIFY(selector->view()->viewport()->rect().contains(item.center()));
            }
            selector->hidePopup();
            selector->setCurrentIndex(selector->findData(bastion.id));
            const Session selected = dialog.getSession();
            QCOMPARE(selected.jumpSessionId, bastion.id);
            QCOMPARE(selected.jumpHost, bastion.host);
            QCOMPARE(selected.jumpPort, bastion.port);
            QCOMPARE(selected.jumpUser, bastion.user);
            QCOMPARE(selected.jumpKeyPath, bastion.keyPath);
            target = selected;
        }
        bastion.host = "updated-bastion.example";
        bastion.keyPath.clear(); // Agent authentication must remain available.
        SessionManager::saveSessions({bastion, target});
        const QList<Session> updated = SessionManager::loadSessions();
        QCOMPARE(updated.at(1).jumpHost, bastion.host);
        QVERIFY(updated.at(1).jumpKeyPath.isEmpty());
        SessionManager::saveSessions({target});
        SessionDialog missing(target);
        QCOMPARE(missing.getSession().jumpSessionId, bastion.id);
        SshConnection connection;
        QSignalSpy failure(&connection, &SshConnection::connectionFailed);
        connection.connectToHost(target.host, target.port, target.user, target.keyPath, {}, {}, target.jumpHost,
                                 target.jumpPort, target.jumpUser, target.jumpKeyPath, target.id);
        QCOMPARE(failure.count(), 1); // Fail before any socket is opened; never connect directly.
    }

    void testSessionFilesArePrivate() {
#ifdef Q_OS_WIN
        QSKIP("This test verifies POSIX mode bits, not Windows ACLs.");
#endif
        Session session;
        session.id = QStringLiteral("private-file-test");
        session.name = QStringLiteral("Private");
        SessionManager::saveSessions({session});

        const QString path = SessionManager::getFilePath();
        QVERIFY2(QFileInfo::exists(path), qPrintable(path));
        const QFileDevice::Permissions permissions = QFileInfo(path).permissions();
        QVERIFY(permissions.testFlag(QFileDevice::ReadOwner));
        QVERIFY(permissions.testFlag(QFileDevice::WriteOwner));
        QVERIFY(!permissions.testFlag(QFileDevice::ReadGroup));
        QVERIFY(!permissions.testFlag(QFileDevice::WriteGroup));
        QVERIFY(!permissions.testFlag(QFileDevice::ReadOther));
        QVERIFY(!permissions.testFlag(QFileDevice::WriteOther));
    }

    void testSshSessionTunnelsRoundtrip() {
        Session original;
        original.id = "test-id-tunnels";
        original.name = "My Tunnel Server";
        original.type = SessionType::SSH;
        original.host = "example.com";
        original.user = "admin";

        TunnelConfig t1;
        t1.type = TunnelConfig::Type::Local;
        t1.localPort = 8080;
        t1.remoteHost = "10.0.0.5";
        t1.remotePort = 80;
        original.tunnels.append(t1);

        TunnelConfig t2;
        t2.type = TunnelConfig::Type::Dynamic;
        t2.localPort = 1080;
        t2.socksUsername = "proxy-user";
        t2.socksPassword = "proxy-secret";
        original.tunnels.append(t2);

        QJsonObject json = original.toJson();
        QVERIFY(json.contains("tunnels"));
        QVERIFY(json["tunnels"].isArray());

        Session restored = Session::fromJson(json);
        QCOMPARE(restored.tunnels.size(), 2);

        QCOMPARE(restored.tunnels[0].type, TunnelConfig::Type::Local);
        QCOMPARE(restored.tunnels[0].localPort, 8080);
        QCOMPARE(restored.tunnels[0].remoteHost, QString("10.0.0.5"));
        QCOMPARE(restored.tunnels[0].remotePort, 80);

        QCOMPARE(restored.tunnels[1].type, TunnelConfig::Type::Dynamic);
        QCOMPARE(restored.tunnels[1].localPort, 1080);
        QCOMPARE(restored.tunnels[1].socksUsername, QString("proxy-user"));
        QVERIFY(!json["tunnels"].toArray().at(1).toObject().contains("socksPassword"));
        QVERIFY(restored.tunnels[1].socksPassword.isEmpty());
    }

    void testLocalShellStoppedPrompt_data() {
        QTest::addColumn<QByteArray>("termination");
        QTest::newRow("normal-exit") << QByteArray("exit 0");
        QTest::newRow("nonzero-exit") << QByteArray("exit 7");
        QTest::newRow("crashed-shell") << QByteArray("kill -KILL $$");
    }

    void testLocalShellStoppedPrompt() {
#ifdef Q_OS_WIN
        QSKIP("Exercises the Unix local PTY; Windows uses ConPTY.");
#else
        QFETCH(QByteArray, termination);
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const QString program = directory.filePath("shell");
        QFile script(program);
        QVERIFY(script.open(QIODevice::WriteOnly));
        script.write("#!/bin/sh\nprintf 'local-output-café\\n'\n" + termination + "\n");
        script.close();
        QVERIFY(script.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner));
        const bool nativeDialogsDisabled = QApplication::testAttribute(Qt::AA_DontUseNativeDialogs);
        QApplication::setAttribute(Qt::AA_DontUseNativeDialogs);
        const auto restore =
            qScopeGuard([&]() { QApplication::setAttribute(Qt::AA_DontUseNativeDialogs, nativeDialogsDisabled); });
        Session session;
        session.id = "local-exit-test";
        session.name = "Local shell";
        session.type = SessionType::Local;
        session.shellPath = program;
        TerminalTab tab(session);
        QSignalSpy output(&tab, &TerminalTab::terminalDataReceived);
        QSignalSpy reconnect(&tab, &TerminalTab::reconnectRequested);
        QSignalSpy close(&tab, &TerminalTab::exitRequested);
        tab.resize(800, 500);
        tab.show();
        QTRY_VERIFY_WITH_TIMEOUT(!tab.isSessionActive(), 5000);
        auto* terminal = tab.getTerminalWidget();
        auto* display = terminal->focusProxy();
        QVERIFY(display);
        QVERIFY(display->isVisible());
        auto history = [&]() {
            QBuffer buffer;
            buffer.open(QIODevice::WriteOnly);
            terminal->saveHistory(&buffer);
            return buffer.data();
        };
        QTRY_VERIFY(history().contains("Session stopped"));
        const auto screen = history();
        QVERIFY(screen.contains("[Connection closed]"));
        QVERIFY(screen.contains("local-output-café"));
        QCOMPARE(screen.count("Session stopped"), 1);
        QCOMPARE(close.count(), 0);
        tab.activateWindow();
        display->setFocus();
        QTest::keyClick(display, Qt::Key_R);
        QCOMPARE(reconnect.count(), 1);
        QCOMPARE(qvariant_cast<Session>(reconnect.first().first()).id, session.id);
        const QString savedPath = directory.filePath("output.log");
        QTimer::singleShot(0, &tab, [&]() {
            if (auto* dialog = qobject_cast<QFileDialog*>(QApplication::activeModalWidget())) {
                dialog->selectFile(savedPath);
                QMetaObject::invokeMethod(dialog, "accept");
            }
        });
        QTest::keyClick(display, Qt::Key_S);
        QFile saved(savedPath);
        QVERIFY(saved.open(QIODevice::ReadOnly));
        const auto contents = saved.readAll();
        QVERIFY(contents.contains("local-output-café"));
        QVERIFY(contents.contains("Session stopped"));
        QByteArray captured;
        for (const auto& args : output)
            captured += args.first().toByteArray();
        QCOMPARE(captured.count("Session stopped"), 1);
        QTest::keyClick(display, Qt::Key_Return);
        QCOMPARE(close.count(), 1);
#endif
    }

    void testLocalSessionRoundtrip() {
        Session original;
        original.id = "test-id-local";
        original.name = "Local Shell";
        original.type = SessionType::Local;
        original.shellPath = "/bin/zsh";

        QJsonObject json = original.toJson();
        QCOMPARE(json["type"].toString(), QString("local"));

        Session restored = Session::fromJson(json);
        QCOMPARE(restored.type, SessionType::Local);
        QCOMPARE(restored.shellPath, original.shellPath);
        QCOMPARE(restored.port, 22); // default
    }

    void testTelnetSessionRoundtrip() {
        Session original;
        original.id = "test-id-telnet";
        original.name = "Router";
        original.type = SessionType::Telnet;
        original.host = "192.168.1.1";
        original.port = 23;

        QJsonObject json = original.toJson();
        QCOMPARE(json["type"].toString(), QString("telnet"));

        Session restored = Session::fromJson(json);
        QCOMPARE(restored.type, SessionType::Telnet);
        QCOMPARE(restored.port, 23);
        QCOMPARE(restored.host, QString("192.168.1.1"));
    }

    void testSerialSessionRoundtrip() {
        Session original;
        original.id = "test-id-serial";
        original.name = "Serial Console";
        original.type = SessionType::Serial;
        original.serialPort = "/dev/ttyUSB0";
        original.baudRate = 9600;
        original.serialCmd = "picocom";

        QJsonObject json = original.toJson();
        QCOMPARE(json["type"].toString(), QString("serial"));
        QCOMPARE(json["serialPort"].toString(), QString("/dev/ttyUSB0"));
        QCOMPARE(json["baudRate"].toInt(), 9600);
        QCOMPARE(json["serialCmd"].toString(), QString("picocom"));

        Session restored = Session::fromJson(json);
        QCOMPARE(restored.type, SessionType::Serial);
        QCOMPARE(restored.serialPort, original.serialPort);
        QCOMPARE(restored.baudRate, original.baudRate);
        QCOMPARE(restored.serialCmd, original.serialCmd);
    }

    void testFromJsonDefaults() {
        QJsonObject empty;
        Session s = Session::fromJson(empty);

        QVERIFY(!s.id.isEmpty());
        QCOMPARE(s.port, 22);
        QCOMPARE(s.type, SessionType::Local);
        QCOMPARE(s.baudRate, 115200);
        QVERIFY(!s.x11Forwarding);
    }

    void testFromJsonUnknownTypeDefaultsToLocal() {
        QJsonObject json;
        json["type"] = "nonexistent";
        json["id"] = "some-id";
        Session s = Session::fromJson(json);
        QCOMPARE(s.type, SessionType::Local);
    }

    void testOpenSshConfigImport() {
        QTemporaryFile config;
        QVERIFY(config.open());
        const QByteArray contents = "Host *\n"
                                    "    User ignored\n"
                                    "Host production\n"
                                    "    HostName prod.example.com\n"
                                    "    User deploy\n"
                                    "    Port 2222\n"
                                    "    IdentityFile ~/.ssh/prod_ed25519\n"
                                    "    ProxyJump jumpuser@bastion.example.com:2201\n"
                                    "Host staging\n"
                                    "    HostName staging.example.com\n";
        QVERIFY(config.write(contents) == contents.size());
        QVERIFY(config.flush());

        bool ok = false;
        const QList<Session> sessions = SessionManager::importOpenSshConfig(config.fileName(), &ok);
        QVERIFY(ok);
        QCOMPARE(sessions.size(), 2);

        QCOMPARE(sessions[0].name, QString("production"));
        QCOMPARE(sessions[0].type, SessionType::SSH);
        QCOMPARE(sessions[0].host, QString("prod.example.com"));
        QCOMPARE(sessions[0].user, QString("deploy"));
        QCOMPARE(sessions[0].port, 2222);
        QVERIFY(sessions[0].keyPath.endsWith(QStringLiteral("/.ssh/prod_ed25519")));
        QCOMPARE(sessions[0].jumpHost, QString("bastion.example.com"));
        QCOMPARE(sessions[0].jumpUser, QString("jumpuser"));
        QCOMPARE(sessions[0].jumpPort, 2201);

        QCOMPARE(sessions[1].name, QString("staging"));
        QCOMPARE(sessions[1].host, QString("staging.example.com"));
        QCOMPARE(sessions[1].port, 22);
    }

    void testPuTTYRegistryImport() {
        QTemporaryFile registry;
        QVERIFY(registry.open());
        const QByteArray contents = "Windows Registry Editor Version 5.00\n\n"
                                    "[HKEY_CURRENT_USER\\Software\\SimonTatham\\PuTTY\\Sessions\\Production%20SSH]\n"
                                    "\"HostName\"=\"prod.example.com\"\n"
                                    "\"PortNumber\"=dword:000008ae\n"
                                    "\"UserName\"=\"deploy\"\n"
                                    "\"PublicKeyFile\"=\"C:\\\\Users\\\\deploy\\\\.ssh\\\\id_ed25519.ppk\"\n\n"
                                    "[HKEY_CURRENT_USER\\Software\\SimonTatham\\PuTTY\\Sessions\\Staging]\n"
                                    "\"HostName\"=\"staging.example.com\"\n";
        QVERIFY(registry.write(contents) == contents.size());
        QVERIFY(registry.flush());

        bool ok = false;
        const QList<Session> sessions = SessionManager::importPuTTYRegistry(registry.fileName(), &ok);
        QVERIFY(ok);
        QCOMPARE(sessions.size(), 2);
        QCOMPARE(sessions[0].name, QString("Production SSH"));
        QCOMPARE(sessions[0].host, QString("prod.example.com"));
        QCOMPARE(sessions[0].port, 2222);
        QCOMPARE(sessions[0].user, QString("deploy"));
        QVERIFY(sessions[0].keyPath.contains(QStringLiteral("id_ed25519.ppk")));
        QCOMPARE(sessions[1].host, QString("staging.example.com"));
    }

    void testToJsonOmitsIrrelevantFields() {
        Session local;
        local.type = SessionType::Local;
        local.shellPath = "/bin/bash";
        QJsonObject json = local.toJson();

        QVERIFY(json.contains("shellPath"));
        QVERIFY(json.contains("host")); // all fields serialized
    }

    void testPortDefaults() {
        Session ssh;
        ssh.type = SessionType::SSH;
        ssh.port = 0;
        QJsonObject jsonSsh = ssh.toJson();
        QCOMPARE(jsonSsh["port"].toInt(), 0);

        Session telnet;
        telnet.type = SessionType::Telnet;
        QJsonObject jsonT = telnet.toJson();
        QCOMPARE(jsonT["port"].toInt(), 22); // struct default
    }

    void testFtpTlsOptionsRoundTrip() {
        Session ftp;
        ftp.type = SessionType::FTP;
        ftp.ftpTls = true;
        ftp.ftpTlsMinimumVersion = 13;
        ftp.ftpTlsCaFile = QStringLiteral("/etc/ssl/custom-ca.pem");
        ftp.ftpTlsAllowInvalidCertificates = true;

        const Session restored = Session::fromJson(ftp.toJson());
        QCOMPARE(restored.ftpTlsMinimumVersion, 13);
        QCOMPARE(restored.ftpTlsCaFile, ftp.ftpTlsCaFile);
        QVERIFY(restored.ftpTlsAllowInvalidCertificates);
    }

    void testFtpProtocolAgainstLocalServer_data() {
        QTest::addColumn<int>("dataAcceptDelay");
        QTest::addColumn<bool>("earlyCompletionReply");
        QTest::newRow("immediate-data-accept") << 0 << false;
        QTest::newRow("command-before-data-accept") << 100 << false;
        QTest::newRow("completion-before-data-close") << 0 << true;
    }

    void testFtpProtocolAgainstLocalServer() {
        QFETCH(int, dataAcceptDelay);
        QFETCH(bool, earlyCompletionReply);
        QTcpServer controlServer;
        QTcpServer dataServer;
        if (!controlServer.listen(QHostAddress(QStringLiteral("127.0.0.1"))))
            QSKIP(qPrintable(QStringLiteral("Local TCP sockets are unavailable: %1").arg(controlServer.errorString())));

        QTcpSocket* controlSocket = nullptr;
        QTcpSocket* dataSocket = nullptr;
        QByteArray controlBuffer;
        QByteArray uploadedData;
        QByteArray pendingDataCommand;
        bool completionSent = false;
        auto sendPendingData = [&]() {
            if (!dataSocket || pendingDataCommand.isEmpty() || pendingDataCommand.startsWith("STOR"))
                return;
            const QByteArray command = pendingDataCommand;
            pendingDataCommand.clear();
            if (command.startsWith("RETR"))
                dataSocket->write("hello");
            else if (command == "LIST /tree")
                dataSocket->write("drwxr-xr-x 1 user group 0 Jan 01 00:00 folder\r\n");
            else if (command == "LIST /tree/folder")
                dataSocket->write("drwxr-xr-x 1 user group 0 Jan 01 00:00 empty\r\n"
                                  "-rw-r--r-- 1 user group 5 Jan 01 00:00 test.txt\r\n");
            else if (command != "LIST /tree/folder/empty")
                dataSocket->write("-rw-r--r-- 1 user group 5 Jan 01 00:00 test.txt\r\n");
            if (earlyCompletionReply) {
                completionSent = true;
                controlSocket->write("226 Transfer complete\r\n");
                QTimer::singleShot(100, dataSocket, &QTcpSocket::disconnectFromHost);
            } else {
                dataSocket->disconnectFromHost();
            }
        };

        connect(&controlServer, &QTcpServer::newConnection, this, [&]() {
            controlSocket = controlServer.nextPendingConnection();
            connect(controlSocket, &QTcpSocket::readyRead, this, [&]() {
                controlBuffer += controlSocket->readAll();
                while (true) {
                    const qsizetype end = controlBuffer.indexOf("\r\n");
                    if (end < 0)
                        return;
                    const QByteArray command = controlBuffer.left(end);
                    controlBuffer.remove(0, end + 2);
                    const QByteArray upper = command.toUpper();
                    if (upper.startsWith("USER")) {
                        controlSocket->write("331 Password required\r\n");
                    } else if (upper.startsWith("PASS")) {
                        controlSocket->write("230 Logged in\r\n");
                    } else if (upper.startsWith("TYPE")) {
                        controlSocket->write("200 Type set\r\n");
                    } else if (upper.startsWith("SIZE ")) {
                        controlSocket->write("213 14\r\n");
                    } else if (upper == "PASV") {
                        completionSent = false;
                        QVERIFY(dataServer.listen(QHostAddress(QStringLiteral("127.0.0.1"))));
                        const quint16 port = dataServer.serverPort();
                        controlSocket->write(QByteArray("227 Entering Passive Mode (127,0,0,1,") +
                                             QByteArray::number(port / 256) + "," + QByteArray::number(port % 256) +
                                             ")\r\n");
                    } else if (upper.startsWith("LIST") || upper.startsWith("RETR") || upper.startsWith("STOR")) {
                        pendingDataCommand = command;
                        controlSocket->write("150 Opening data connection\r\n");
                        sendPendingData();
                    }
                }
            });
            connect(controlSocket, &QTcpSocket::disconnected, controlSocket, &QObject::deleteLater);
            controlSocket->write("220 Local test FTP server\r\n");
        });

        connect(&dataServer, &QTcpServer::newConnection, &dataServer, [&]() {
            auto* socket = dataServer.nextPendingConnection();
            connect(socket, &QTcpSocket::readyRead, &dataServer, [&, socket]() { uploadedData += socket->readAll(); });
            connect(socket, &QTcpSocket::disconnected, &dataServer, [&, socket]() {
                if (controlSocket && !completionSent)
                    controlSocket->write("226 Transfer complete\r\n");
                pendingDataCommand.clear();
                if (dataSocket == socket)
                    dataSocket = nullptr;
                socket->deleteLater();
                dataServer.close();
            });
            auto acceptDataConnection = [&, socket]() {
                dataSocket = socket;
                sendPendingData();
            };
            if (dataAcceptDelay)
                QTimer::singleShot(dataAcceptDelay, socket, acceptDataConnection);
            else
                acceptDataConnection();
        });

        FtpClient client;
        QSignalSpy connectedSpy(&client, &FtpClient::connectionSuccess);
        QSignalSpy listedSpy(&client, &FtpClient::directoryListed);
        client.connectToHost(QStringLiteral("127.0.0.1"), controlServer.serverPort(), QStringLiteral("user"),
                             QStringLiteral("password"), false);
        QTRY_COMPARE_WITH_TIMEOUT(connectedSpy.count(), 1, 2000);

        client.listDirectory(QStringLiteral("/"));
        QTRY_COMPARE_WITH_TIMEOUT(listedSpy.count(), 1, 2000);
        const QList<SftpFile> entries = qvariant_cast<QList<SftpFile>>(listedSpy.at(0).at(1));
        QCOMPARE(entries.size(), 1);
        QCOMPARE(entries.first().name, QStringLiteral("test.txt"));
        QCOMPARE(entries.first().size, 5);

        QTemporaryDir transferDir;
        QVERIFY(transferDir.isValid());
        const QString downloadPath = transferDir.filePath(QStringLiteral("download.txt"));
        QSignalSpy operationSpy(&client, &FtpClient::operationFinished);
        client.downloadFile(QStringLiteral("test.txt"), downloadPath);
        QTRY_COMPARE_WITH_TIMEOUT(operationSpy.count(), 1, 2000);
        QVERIFY(operationSpy.first().at(0).toBool());
        QFile downloaded(downloadPath);
        QVERIFY(downloaded.open(QIODevice::ReadOnly));
        QCOMPARE(downloaded.readAll(), QByteArrayLiteral("hello"));

        const QString uploadPath = transferDir.filePath(QStringLiteral("upload.txt"));
        QFile upload(uploadPath);
        QVERIFY(upload.open(QIODevice::WriteOnly));
        QVERIFY(upload.write("upload payload") == 14);
        upload.close();
        operationSpy.clear();
        client.uploadFile(uploadPath, QStringLiteral("upload.txt"));
        QTRY_COMPARE_WITH_TIMEOUT(operationSpy.count(), 1, 2000);
        QVERIFY(operationSpy.first().at(0).toBool());
        QCOMPARE(uploadedData, QByteArrayLiteral("upload payload"));

        client.transferConflict = std::make_shared<TransferConflict>();
        client.transferConflict->decision = TransferConflict::Skip;
        operationSpy.clear();
        client.uploadFile(uploadPath, QStringLiteral("upload.txt"));
        QTRY_COMPARE_WITH_TIMEOUT(operationSpy.count(), 1, 2000);
        QVERIFY(operationSpy.first().first().toBool());
        QCOMPARE(uploadedData, QByteArrayLiteral("upload payload"));
        client.transferConflict->cancelled = true;
        operationSpy.clear();
        client.uploadFile(uploadPath, QStringLiteral("upload.txt"));
        QTRY_COMPARE_WITH_TIMEOUT(operationSpy.count(), 1, 2000);
        QVERIFY(!operationSpy.first().first().toBool());
        QVERIFY(operationSpy.first().at(1).toString().startsWith("Transfer cancelled"));
        QCOMPARE(uploadedData, QByteArrayLiteral("upload payload"));

        client.disconnectFromHost();
        QTest::qWait(50);
        SftpSidebar sidebar;
        Session session;
        session.type = SessionType::FTP;
        session.host = "127.0.0.1";
        session.port = controlServer.serverPort();
        session.user = "user";
        session.remoteDirectory = "/tree";
        session.ftpTls = false;
        auto* localPath = sidebar.findChild<QLineEdit*>("localPathEdit");
        localPath->setText(transferDir.path());
        QVERIFY(QMetaObject::invokeMethod(localPath, "returnPressed"));
        sidebar.startSession(session);
        auto* tree = sidebar.findChild<QTreeWidget*>("sftpTree");
        QTRY_COMPARE_WITH_TIMEOUT(tree->findItems("folder", Qt::MatchExactly).size(), 1, 3000);
        tree->findItems("folder", Qt::MatchExactly).first()->setSelected(true);
        sidebar.findChild<QPushButton*>("localDownloadButton")->click();
        QTRY_VERIFY_WITH_TIMEOUT(QFileInfo(transferDir.filePath("folder/empty")).isDir(), 3000);
        QTRY_VERIFY_WITH_TIMEOUT(tree->isEnabled() && tree->selectedItems().isEmpty(), 3000);
        QFile recursiveDownload(transferDir.filePath("folder/test.txt"));
        QVERIFY(recursiveDownload.open(QIODevice::ReadOnly));
        QCOMPARE(recursiveDownload.readAll(), QByteArrayLiteral("hello"));
        QCOMPARE(sidebar.findChild<QLineEdit*>("remotePathEdit")->text(), QString("/tree"));
        sidebar.stopSession();
        QTest::qWait(50);
    }

    void testFtpReconnectsAfterControlLoss() {
        QTcpServer controlServer;
        if (!controlServer.listen(QHostAddress(QStringLiteral("127.0.0.1"))))
            QSKIP(qPrintable(QStringLiteral("Local TCP sockets are unavailable: %1").arg(controlServer.errorString())));

        QTcpSocket* controlSocket = nullptr;
        QByteArray controlBuffer;
        int acceptedConnections = 0;

        connect(&controlServer, &QTcpServer::newConnection, this, [&]() {
            controlSocket = controlServer.nextPendingConnection();
            ++acceptedConnections;
            controlBuffer.clear();
            connect(controlSocket, &QTcpSocket::readyRead, this, [&]() {
                controlBuffer += controlSocket->readAll();
                while (true) {
                    const qsizetype end = controlBuffer.indexOf("\r\n");
                    if (end < 0)
                        return;
                    const QByteArray command = controlBuffer.left(end).toUpper();
                    controlBuffer.remove(0, end + 2);
                    if (command.startsWith("USER"))
                        controlSocket->write("331 Password required\r\n");
                    else if (command.startsWith("PASS"))
                        controlSocket->write("230 Logged in\r\n");
                    else if (command.startsWith("TYPE"))
                        controlSocket->write("200 Type set\r\n");
                }
            });
            connect(controlSocket, &QTcpSocket::disconnected, controlSocket, &QObject::deleteLater);
            controlSocket->write("220 Reconnect test FTP server\r\n");
        });

        FtpClient client;
        QSignalSpy connectedSpy(&client, &FtpClient::connectionSuccess);
        client.connectToHost(QStringLiteral("127.0.0.1"), controlServer.serverPort(), QStringLiteral("user"),
                             QStringLiteral("password"), false);
        QTRY_COMPARE_WITH_TIMEOUT(connectedSpy.count(), 1, 2000);
        QCOMPARE(acceptedConnections, 1);

        QTimer::singleShot(50, this, [&]() {
            if (controlSocket)
                controlSocket->disconnectFromHost();
        });

        QTRY_COMPARE_WITH_TIMEOUT(connectedSpy.count(), 2, 7000);
        QCOMPARE(acceptedConnections, 2);
        client.disconnectFromHost();
    }

    void testFtpRejectsMissingCaBundle() {
        FtpClient client;
        QSignalSpy failureSpy(&client, &FtpClient::connectionFailed);
        client.connectToHost(QStringLiteral("127.0.0.1"), 1, QStringLiteral("user"), QStringLiteral("password"), true,
                             12, QStringLiteral("/path/that/does/not/exist.pem"));
        QTRY_COMPARE_WITH_TIMEOUT(failureSpy.count(), 1, 1000);
        QVERIFY(failureSpy.first().first().toString().contains(QStringLiteral("CA bundle")));
    }

    void testFtpsExplicitTlsHandshake() {
        const QString fixtureDir = QStringLiteral(BANCHO_SOURCE_DIR) + QStringLiteral("/tests/fixtures/");
        QFile certificateFile(fixtureDir + QStringLiteral("ftps-cert.pem"));
        QFile keyFile(fixtureDir + QStringLiteral("ftps-key.pem"));
        QVERIFY(certificateFile.open(QIODevice::ReadOnly));
        QVERIFY(keyFile.open(QIODevice::ReadOnly));

        const QSslCertificate certificate(certificateFile.readAll(), QSsl::Pem);
        const QSslKey privateKey(keyFile.readAll(), QSsl::Ec, QSsl::Pem);
        QVERIFY(!certificate.isNull());
        QVERIFY(!privateKey.isNull());

        QSslConfiguration serverConfiguration = QSslConfiguration::defaultConfiguration();
        serverConfiguration.setLocalCertificate(certificate);
        serverConfiguration.setPrivateKey(privateKey);
        serverConfiguration.setPeerVerifyMode(QSslSocket::VerifyNone);
        serverConfiguration.setProtocol(QSsl::TlsV1_2OrLater);

        ExplicitTlsTestServer controlServer;
        ExplicitTlsTestServer dataServer;
        if (!controlServer.listen(QHostAddress(QStringLiteral("127.0.0.1"))) ||
            !dataServer.listen(QHostAddress(QStringLiteral("127.0.0.1"))))
            QSKIP(qPrintable(QStringLiteral("Local TCP sockets are unavailable: %1")
                                 .arg(controlServer.errorString().isEmpty() ? dataServer.errorString()
                                                                            : controlServer.errorString())));

        QSslSocket* controlSocket = nullptr;
        QSslSocket* dataSocket = nullptr;
        QByteArray dataAction;
        QByteArray uploadedData;
        const auto sendData = [&]() {
            if (!dataSocket || !dataSocket->isEncrypted())
                return;
            if (dataAction == "LIST")
                dataSocket->write("-rw-r--r-- 1 user group 5 Jan 01 00:00 test.txt\r\n");
            else if (dataAction == "RETR")
                dataSocket->write("hello");
            else
                return;
            dataSocket->disconnectFromHost();
        };

        connect(&dataServer, &QTcpServer::newConnection, this, [&]() {
            dataSocket = qobject_cast<QSslSocket*>(dataServer.nextPendingConnection());
            QVERIFY(dataSocket != nullptr);
            dataSocket->setSslConfiguration(serverConfiguration);
            connect(dataSocket, &QSslSocket::encrypted, this, sendData);
            connect(dataSocket, &QTcpSocket::readyRead, this, [&]() { uploadedData += dataSocket->readAll(); });
            connect(dataSocket, &QTcpSocket::disconnected, this, [&]() {
                if (controlSocket)
                    controlSocket->write("226 Transfer complete\r\n");
                dataSocket->deleteLater();
                dataSocket = nullptr;
                dataAction.clear();
            });
            dataSocket->startServerEncryption();
        });

        connect(&controlServer, &QTcpServer::newConnection, this, [&]() {
            controlSocket = qobject_cast<QSslSocket*>(controlServer.nextPendingConnection());
            QVERIFY(controlSocket != nullptr);
            const auto commandBuffer = std::make_shared<QByteArray>();
            connect(controlSocket, &QTcpSocket::readyRead, this, [&, commandBuffer, serverConfiguration]() mutable {
                *commandBuffer += controlSocket->readAll();
                while (true) {
                    const qsizetype end = commandBuffer->indexOf("\r\n");
                    if (end < 0)
                        return;
                    const QByteArray command = commandBuffer->left(end).toUpper();
                    commandBuffer->remove(0, end + 2);
                    if (command == "AUTH TLS") {
                        controlSocket->write("234 TLS negotiation ready\r\n");
                        controlSocket->setSslConfiguration(serverConfiguration);
                        controlSocket->startServerEncryption();
                    } else if (command.startsWith("USER")) {
                        controlSocket->write("331 Password required\r\n");
                    } else if (command.startsWith("PASS")) {
                        controlSocket->write("230 Logged in\r\n");
                    } else if (command.startsWith("TYPE")) {
                        controlSocket->write("200 Type set\r\n");
                    } else if (command.startsWith("PBSZ")) {
                        controlSocket->write("200 PBSZ ok\r\n");
                    } else if (command.startsWith("PROT P")) {
                        controlSocket->write("200 PROT P ok\r\n");
                    } else if (command == "PASV") {
                        const quint16 port = dataServer.serverPort();
                        controlSocket->write(QByteArray("227 Entering Passive Mode (127,0,0,1,") +
                                             QByteArray::number(port / 256) + "," + QByteArray::number(port % 256) +
                                             ")\r\n");
                    } else if (command.startsWith("LIST")) {
                        dataAction = "LIST";
                        controlSocket->write("150 Opening secure data connection\r\n");
                        sendData();
                    } else if (command.startsWith("RETR")) {
                        dataAction = "RETR";
                        controlSocket->write("150 Opening secure data connection\r\n");
                        sendData();
                    } else if (command.startsWith("STOR")) {
                        dataAction = "STOR";
                        controlSocket->write("150 Opening secure data connection\r\n");
                    }
                }
            });
            controlSocket->write("220 Local explicit FTPS test server\r\n");
        });

        FtpClient client;
        QSignalSpy connectedSpy(&client, &FtpClient::connectionSuccess);
        QSignalSpy failureSpy(&client, &FtpClient::connectionFailed);
        client.connectToHost(QStringLiteral("127.0.0.1"), controlServer.serverPort(), QStringLiteral("user"),
                             QStringLiteral("password"), true, 12, fixtureDir + QStringLiteral("ftps-cert.pem"));
        QTRY_COMPARE_WITH_TIMEOUT(connectedSpy.count(), 1, 3000);
        QCOMPARE(failureSpy.count(), 0);

        QSignalSpy listedSpy(&client, &FtpClient::directoryListed);
        client.listDirectory(QStringLiteral("/"));
        QTRY_COMPARE_WITH_TIMEOUT(listedSpy.count(), 1, 3000);
        const QList<SftpFile> entries = qvariant_cast<QList<SftpFile>>(listedSpy.first().at(1));
        QCOMPARE(entries.size(), 1);
        QCOMPARE(entries.first().name, QStringLiteral("test.txt"));

        QTemporaryDir transferDir;
        QVERIFY(transferDir.isValid());
        QSignalSpy operationSpy(&client, &FtpClient::operationFinished);
        const QString downloadPath = transferDir.filePath(QStringLiteral("download.txt"));
        client.downloadFile(QStringLiteral("test.txt"), downloadPath);
        QTRY_COMPARE_WITH_TIMEOUT(operationSpy.count(), 1, 3000);
        QVERIFY(operationSpy.first().at(0).toBool());
        QFile downloaded(downloadPath);
        QVERIFY(downloaded.open(QIODevice::ReadOnly));
        QCOMPARE(downloaded.readAll(), QByteArrayLiteral("hello"));

        QFile upload(transferDir.filePath(QStringLiteral("upload.txt")));
        QVERIFY(upload.open(QIODevice::WriteOnly));
        QVERIFY(upload.write("upload payload") == 14);
        upload.close();
        operationSpy.clear();
        client.uploadFile(upload.fileName(), QStringLiteral("upload.txt"));
        QTRY_COMPARE_WITH_TIMEOUT(operationSpy.count(), 1, 3000);
        QVERIFY(operationSpy.first().at(0).toBool());
        QCOMPARE(uploadedData, QByteArrayLiteral("upload payload"));
        client.disconnectFromHost();
    }

    void testSshAndSftpAgainstLocalServer() {
#ifdef Q_OS_WIN
        QSKIP("The local sshd/SFTP integration test requires a Unix OpenSSH daemon.");
#endif
        QTemporaryDir serverDir;
        QVERIFY(serverDir.isValid());
        const QByteArray isolatedConfig = QDir(serverDir.filePath(QStringLiteral("config"))).absolutePath().toUtf8();
        qputenv("BANCHO_CONFIG_DIR", isolatedConfig);
        const QString hostKey = serverDir.filePath(QStringLiteral("host_key"));
        const QString clientKey = serverDir.filePath(QStringLiteral("client_key"));
        const QString authorizedKeys = serverDir.filePath(QStringLiteral("authorized_keys"));
        const QString sshdConfig = serverDir.filePath(QStringLiteral("sshd_config"));

        QTcpServer echoServer;
        if (!echoServer.listen(QHostAddress(QStringLiteral("127.0.0.1"))))
            QSKIP(qPrintable(QStringLiteral("Local TCP sockets are unavailable: %1").arg(echoServer.errorString())));
        connect(&echoServer, &QTcpServer::newConnection, this, [&]() {
            while (echoServer.hasPendingConnections()) {
                QTcpSocket* socket = echoServer.nextPendingConnection();
                connect(socket, &QTcpSocket::readyRead, socket, [socket]() {
                    socket->write(socket->readAll());
                    socket->flush();
                });
                connect(socket, &QTcpSocket::disconnected, socket, &QObject::deleteLater);
            }
        });

        std::vector<std::unique_ptr<QTcpServer>> portReservations;
        auto reservePort = [&]() {
            auto probe = std::make_unique<QTcpServer>();
            if (!probe->listen(QHostAddress(QStringLiteral("127.0.0.1"))))
                return quint16(0);
            const quint16 reserved = probe->serverPort();
            // Keep earlier ports bound while selecting the other tunnel ports.
            portReservations.push_back(std::move(probe));
            return reserved;
        };

        auto generateKey = [](const QString& path, const QString& type, const QString& bits) {
            QProcess process;
            QStringList arguments = {QStringLiteral("-q"), QStringLiteral("-t"), type};
            if (!bits.isEmpty())
                arguments << QStringLiteral("-b") << bits;
            if (type != QStringLiteral("ed25519"))
                arguments << QStringLiteral("-m") << QStringLiteral("PEM");
            arguments << QStringLiteral("-N") << QString() << QStringLiteral("-f") << path;
            process.start(QStringLiteral("ssh-keygen"), arguments);
            return process.waitForFinished(5000) && process.exitCode() == 0;
        };
        QVERIFY(generateKey(hostKey, QStringLiteral("rsa"), QStringLiteral("2048")));
        QVERIFY(generateKey(clientKey, QStringLiteral("ed25519"), QString()));
        QVERIFY(QFile::copy(clientKey + QStringLiteral(".pub"), authorizedKeys));
        QVERIFY(QFile::setPermissions(hostKey, QFileDevice::ReadOwner | QFileDevice::WriteOwner));
        QVERIFY(QFile::setPermissions(clientKey, QFileDevice::ReadOwner | QFileDevice::WriteOwner));
        QVERIFY(QFile::setPermissions(authorizedKeys, QFileDevice::ReadOwner | QFileDevice::WriteOwner));

        QFile configFile(sshdConfig);
        QVERIFY(configFile.open(QIODevice::WriteOnly | QIODevice::Text));
        const QByteArray config = QStringLiteral("HostKey %1\n"
                                                 "AuthorizedKeysFile %2\n"
                                                 "PubkeyAuthentication yes\n"
                                                 "PasswordAuthentication no\n"
                                                 "KbdInteractiveAuthentication no\n"
                                                 "UsePAM no\n"
                                                 "StrictModes no\n"
                                                 "PermitRootLogin no\n"
                                                 "AcceptEnv COLORTERM TERM_PROGRAM\n"
                                                 "Subsystem sftp internal-sftp\n"
                                                 "LogLevel VERBOSE\n")
                                      .arg(hostKey, authorizedKeys)
                                      .toUtf8();
        QVERIFY(configFile.write(config) == config.size());
        configFile.close();
        QVERIFY(QFile::setPermissions(sshdConfig, QFileDevice::ReadOwner | QFileDevice::WriteOwner));

        QTcpServer portProbe;
        if (!portProbe.listen(QHostAddress(QStringLiteral("127.0.0.1"))))
            QSKIP(qPrintable(QStringLiteral("Local TCP sockets are unavailable: %1").arg(portProbe.errorString())));
        const quint16 port = portProbe.serverPort();
        portProbe.close();

        QProcess sshd;
        sshd.setProcessChannelMode(QProcess::MergedChannels);
        sshd.start(QStringLiteral("sshd"), {QStringLiteral("-D"), QStringLiteral("-e"), QStringLiteral("-f"),
                                            sshdConfig, QStringLiteral("-p"), QString::number(port)});
        QVERIFY(sshd.waitForStarted(3000));
        QTest::qWait(500);
        QVERIFY2(sshd.state() == QProcess::Running, qPrintable(sshd.readAll()));

        QProcess keyscan;
        keyscan.start(QStringLiteral("ssh-keyscan"), {QStringLiteral("-T"), QStringLiteral("3"), QStringLiteral("-p"),
                                                      QString::number(port), QStringLiteral("127.0.0.1")});
        QVERIFY(keyscan.waitForFinished(5000));
        const QByteArray knownHostData = keyscan.readAllStandardOutput();
        QVERIFY(!knownHostData.isEmpty());
        const QString knownHostsPath = QDir(AppPaths::configDir()).filePath(QStringLiteral("known_hosts"));
        QVERIFY(QDir().mkpath(QFileInfo(knownHostsPath).absolutePath()));
        QFile knownHosts(knownHostsPath);
        QVERIFY(knownHosts.open(QIODevice::WriteOnly));
        QVERIFY(knownHosts.write(knownHostData) > 0);
        knownHosts.close();

        QString user = qEnvironmentVariable("USER");
        if (user.isEmpty())
            user = qEnvironmentVariable("USERNAME");
        QVERIFY(!user.isEmpty());

        TunnelConfig localTunnel;
        localTunnel.type = TunnelConfig::Type::Local;
        localTunnel.localPort = reservePort();
        QVERIFY(localTunnel.localPort != 0);
        localTunnel.remoteHost = QStringLiteral("127.0.0.1");
        localTunnel.remotePort = echoServer.serverPort();

        TunnelConfig remoteTunnel;
        remoteTunnel.type = TunnelConfig::Type::Remote;
        remoteTunnel.localPort = echoServer.serverPort();
        remoteTunnel.remoteHost = QStringLiteral("127.0.0.1");
        remoteTunnel.remotePort = reservePort();
        QVERIFY(remoteTunnel.remotePort != 0);

        TunnelConfig socksTunnel;
        socksTunnel.type = TunnelConfig::Type::Dynamic;
        socksTunnel.localPort = reservePort();
        QVERIFY(socksTunnel.localPort != 0);
        socksTunnel.socksUsername = QStringLiteral("proxy-user");
        socksTunnel.socksPassword = QStringLiteral("proxy-password");
        const QList<TunnelConfig> tunnels = {localTunnel, remoteTunnel, socksTunnel};
        portReservations.clear();

        SshConnection connection;
        QSettings monitoringSettings;
        const QVariant previousMonitoring = monitoringSettings.value("ssh/monitoringEnabled");
        const QVariant previousInterval = monitoringSettings.value("ssh/monitoringIntervalSeconds");
        const QVariant previousIntegration = monitoringSettings.value("terminal/shellIntegration");
        const auto restoreMonitoring = qScopeGuard([&]() {
            for (const auto& entry : {qMakePair(QString("ssh/monitoringEnabled"), previousMonitoring),
                                      qMakePair(QString("ssh/monitoringIntervalSeconds"), previousInterval),
                                      qMakePair(QString("terminal/shellIntegration"), previousIntegration)}) {
                if (entry.second.isValid())
                    monitoringSettings.setValue(entry.first, entry.second);
                else
                    monitoringSettings.remove(entry.first);
            }
        });
        monitoringSettings.setValue("ssh/monitoringEnabled", false);
        monitoringSettings.setValue("terminal/shellIntegration", true);
        monitoringSettings.setValue("ssh/monitoringIntervalSeconds", 1);
        QSignalSpy statsSpy(&connection, &SshConnection::remoteStatsUpdated);
        QSignalSpy connectedSpy(&connection, &SshConnection::connectionSuccess);
        QSignalSpy failureSpy(&connection, &SshConnection::connectionFailed);
        QSignalSpy tunnelStateSpy(&connection, &SshConnection::tunnelStateChanged);
        QSignalSpy tunnelStatusSpy(&connection, &SshConnection::tunnelStatus);
        QSignalSpy directorySpy(&connection, &SshConnection::directoryListed);
        QSignalSpy workingDirectorySpy(&connection, &SshConnection::remoteDirectoryChanged);
        connection.connectToHost(QStringLiteral("127.0.0.1"), port, user, clientKey, QString(), tunnels);
        QTRY_VERIFY_WITH_TIMEOUT(connectedSpy.count() == 1 || failureSpy.count() > 0, 5000);
        if (connectedSpy.isEmpty()) {
            connection.disconnectFromHost();
            sshd.terminate();
            sshd.waitForFinished(3000);
            QSKIP("libssh2 authentication is unavailable with the local sshd configuration");
        }
        QCOMPARE(failureSpy.count(), 0);

        // Plain Bash emits no OSC 7. Track the real PTY process even with
        // monitoring disabled, preserving spaces, percent signs and UTF-8.
        connection.sendToShell("exec /bin/bash --noprofile --norc\r");
        const QString trackedPath = serverDir.filePath(QString::fromUtf8("cwd with spaces % ñ"));
        QVERIFY(QDir().mkpath(trackedPath));
        connection.sendToShell(("cd -- '" + trackedPath + "'\r").toUtf8());
        QTRY_VERIFY_WITH_TIMEOUT(!workingDirectorySpy.isEmpty(), 5000);
        QTRY_COMPARE_WITH_TIMEOUT(workingDirectorySpy.last().first().toString(), trackedPath, 5000);
        const QString browserPath = serverDir.filePath("SFTP's folder $literal");
        QVERIFY(QDir().mkpath(browserPath));
        {
            SftpSidebar browser;
            browser.setConnection(&connection);
            Session browserSession;
            browserSession.type = SessionType::SSH;
            browserSession.remoteDirectory = trackedPath;
            browser.startSession(browserSession);
            auto* path = browser.findChild<QLineEdit*>("remotePathEdit");
            path->setText(browserPath);
            QVERIFY(QMetaObject::invokeMethod(path, "returnPressed", Qt::DirectConnection));
            QTRY_COMPARE_WITH_TIMEOUT(workingDirectorySpy.last().first().toString(), browserPath, 5000);
            // A foreground program must not receive the navigation command.
            QSignalSpy foregroundOutput(&connection, &SshConnection::shellDataReceived);
            connection.sendToShell("sh -c 'echo BANCHO_WAITING; read line; echo BANCHO_RELEASED'\r");
            auto output = [&]() {
                QByteArray text;
                for (const auto& event : foregroundOutput)
                    text += event.first().toByteArray();
                return text;
            };
            // The marker occurs once in the echoed command and once in its
            // output; shell prompt escape sequences can precede the latter.
            QTRY_VERIFY2_WITH_TIMEOUT(output().count("BANCHO_WAITING") >= 2, output().constData(), 3000);
            path->setText(trackedPath);
            QVERIFY(QMetaObject::invokeMethod(path, "returnPressed", Qt::DirectConnection));
            QTest::qWait(1200);
            QCOMPARE(workingDirectorySpy.last().first().toString(), browserPath);
            QVERIFY(output().count("BANCHO_RELEASED") < 2);
            connection.sendToShell("\r");
            QTRY_COMPARE_WITH_TIMEOUT(workingDirectorySpy.last().first().toString(), trackedPath, 5000);
        }
        QSignalSpy shellOutput(&connection, &SshConnection::shellDataReceived);
        auto shellText = [&]() {
            QByteArray text;
            for (const auto& output : shellOutput)
                text += output.first().toByteArray();
            return text;
        };
        connection.sendToShell("exec /bin/dash\r");
        // Wait for the new prompt: interactive shell startup may flush input
        // queued before its terminal modes have been initialized.
        QTRY_VERIFY_WITH_TIMEOUT(shellText().contains("exec /bin/dash") && shellText().endsWith("$ "), 3000);
        connection.sendToShell(("cd -- '" + serverDir.path() + "'\r").toUtf8());
        QTRY_COMPARE_WITH_TIMEOUT(workingDirectorySpy.last().first().toString(), serverDir.path(), 5000);

        QTest::qWait(1200);
        QCOMPARE(statsSpy.count(), 0);
        QByteArray monitoringLog = sshd.readAll();
        QCOMPARE(monitoringLog.count("Starting session: command"), 0);
        monitoringSettings.setValue("ssh/monitoringEnabled", true);
        connection.reloadMonitoringSettings();
        QTRY_VERIFY_WITH_TIMEOUT(statsSpy.count() >= 3, 5000);
        const auto sample = statsSpy.last();
        for (int i = 0; i < 3; ++i) {
            QVERIFY(sample[i].toDouble() >= 0);
            QVERIFY(sample[i].toDouble() <= 100);
        }
        QVERIFY(sample[3].toDouble() > 0);
        monitoringLog += sshd.readAll();
        QCOMPARE(monitoringLog.count("Starting session: command"), 1);

        monitoringSettings.setValue("ssh/monitoringIntervalSeconds", 3600);
        connection.reloadMonitoringSettings();
        const int slowCount = statsSpy.count();
        QTest::qWait(1400);
        QCOMPARE(statsSpy.count(), slowCount);
        monitoringSettings.setValue("ssh/monitoringIntervalSeconds", 1);
        connection.reloadMonitoringSettings();
        QTRY_VERIFY_WITH_TIMEOUT(statsSpy.count() > slowCount, 2000);
        monitoringLog += sshd.readAll();
        QCOMPARE(monitoringLog.count("Starting session: command"), 1);

        monitoringSettings.setValue("ssh/monitoringEnabled", false);
        connection.reloadMonitoringSettings();
        const int disabledCount = statsSpy.count();
        QTest::qWait(1400);
        QCOMPARE(statsSpy.count(), disabledCount);
        monitoringLog += sshd.readAll();
        QVERIFY(monitoringLog.contains("Close session: user"));
        monitoringSettings.setValue("ssh/monitoringEnabled", true);
        connection.reloadMonitoringSettings();
        QTRY_VERIFY_WITH_TIMEOUT(statsSpy.count() > disabledCount, 2000);
        monitoringLog += sshd.readAll();
        QCOMPARE(monitoringLog.count("Starting session: command"), 2);

        // Exercise the sidebar queue with actual nested files and an empty directory.
        const QString sourceFolder = serverDir.filePath("sidebar-source");
        QVERIFY(QDir().mkpath(sourceFolder + "/nested/empty"));
        QFile sourceFile(sourceFolder + "/nested/payload.txt");
        QVERIFY(sourceFile.open(QIODevice::WriteOnly));
        const QByteArray sidebarPayload("recursive sidebar download\n");
        QCOMPARE(sourceFile.write(sidebarPayload), sidebarPayload.size());
        sourceFile.close();
        QTemporaryDir destination;
        QVERIFY(destination.isValid());
        {
            SftpSidebar sidebar;
            sidebar.setConnection(&connection);
            Session sidebarSession;
            sidebarSession.type = SessionType::SSH;
            sidebarSession.id = "sidebar-download-test";
            sidebarSession.host = "127.0.0.1";
            sidebarSession.port = port;
            sidebarSession.user = user;
            sidebarSession.keyPath = clientKey;
            sidebarSession.remoteDirectory = serverDir.path();
            sidebar.startSession(sidebarSession);
            auto* localPath = sidebar.findChild<QLineEdit*>("localPathEdit");
            localPath->setText(destination.path());
            QVERIFY(QMetaObject::invokeMethod(localPath, "returnPressed"));
            auto* remoteTree = sidebar.findChild<QTreeWidget*>("sftpTree");
            const auto matches = remoteTree->findItems("sidebar-source", Qt::MatchExactly);
            QCOMPARE(matches.size(), 1);
            matches.first()->setSelected(true);
            sidebar.findChild<QPushButton*>("localDownloadButton")->click();
            const QString downloadedPath = destination.filePath("sidebar-source/nested/payload.txt");
            QTRY_VERIFY_WITH_TIMEOUT(QFileInfo::exists(downloadedPath), 10000);
            QTRY_VERIFY_WITH_TIMEOUT(QFileInfo(destination.filePath("sidebar-source/nested/empty")).isDir(), 10000);
            QTRY_VERIFY_WITH_TIMEOUT(sidebar.findChild<QPushButton*>("localDownloadButton")->isEnabled() == false &&
                                         remoteTree->isEnabled(),
                                     10000);
            QFile downloaded(downloadedPath);
            QVERIFY(downloaded.open(QIODevice::ReadOnly));
            QTRY_COMPARE_WITH_TIMEOUT(downloaded.size(), sidebarPayload.size(), 10000);
            QCOMPARE(downloaded.readAll(), sidebarPayload);
            QCOMPARE(sidebar.findChild<QLineEdit*>("remotePathEdit")->text(), serverDir.path());
        }

        directorySpy.clear();
        QSignalSpy colorEnvironmentSpy(&connection, &SshConnection::shellDataReceived);
        connection.sendToShell(QByteArrayLiteral("printf '\\nBANCHO_COLOR=%s\\n' \"$COLORTERM\"\r"));
        auto trueColorAnnounced = [&]() {
            QByteArray received;
            for (const auto& event : colorEnvironmentSpy)
                received += event.at(0).toByteArray();
            return received.contains("BANCHO_COLOR=truecolor");
        };
        QTRY_VERIFY_WITH_TIMEOUT(trueColorAnnounced(), 3000);

        connection.listDirectory(QStringLiteral("."));
        QTRY_COMPARE_WITH_TIMEOUT(directorySpy.count(), 1, 3000);
        QVERIFY(!qvariant_cast<QList<SftpFile>>(directorySpy.first().at(1)).isEmpty());

        const QString uploadSource = serverDir.filePath(QStringLiteral("folder-upload-source"));
        const QString nestedUploadSource = QDir(uploadSource).filePath(QStringLiteral("nested"));
        QVERIFY(QDir().mkpath(nestedUploadSource));
        const QList<QPair<QString, QByteArray>> folderFiles = {
            {QStringLiteral("first.txt"), QByteArrayLiteral("first")},
            {QStringLiteral("second.txt"), QByteArrayLiteral("second")},
            {QStringLiteral("third.txt"), QByteArrayLiteral("third")},
            {QStringLiteral("fourth.txt"), QByteArrayLiteral("fourth")},
            {QStringLiteral("nested/fifth.txt"), QByteArrayLiteral("fifth")},
        };
        for (const auto& file : folderFiles) {
            QFile output(QDir(uploadSource).filePath(file.first));
            QVERIFY(output.open(QIODevice::WriteOnly));
            QCOMPARE(output.write(file.second), file.second.size());
        }

        const QString uploadDestination = serverDir.filePath(QStringLiteral("folder-upload-destination"));
        QVERIFY(QDir().mkpath(uploadDestination));
        QSignalSpy folderUploadSpy(&connection, &SshConnection::operationFinished);
        connection.uploadDirectory(uploadSource, uploadDestination);
        QTRY_COMPARE_WITH_TIMEOUT(folderUploadSpy.count(), 1, 10000);
        QVERIFY2(folderUploadSpy.first().at(0).toBool(), qPrintable(folderUploadSpy.first().at(1).toString()));
        const QString uploadedRoot = QDir(uploadDestination).filePath(QFileInfo(uploadSource).fileName());
        for (const auto& file : folderFiles) {
            QFile uploaded(QDir(uploadedRoot).filePath(file.first));
            QVERIFY(uploaded.open(QIODevice::ReadOnly));
            QCOMPARE(uploaded.readAll(), file.second);
        }

        auto allTunnelsActive = [&]() {
            bool active[3] = {false, false, false};
            for (const auto& signal : tunnelStateSpy) {
                const int index = signal.at(0).toInt();
                if (index >= 0 && index < 3 && signal.at(1).toBool())
                    active[index] = true;
            }
            return active[0] && active[1] && active[2];
        };
        QElapsedTimer tunnelTimer;
        tunnelTimer.start();
        while (!allTunnelsActive() && tunnelTimer.elapsed() < 5000)
            QTest::qWait(50);
        QVERIFY2(allTunnelsActive(), "All three SSH tunnels must be listening before testing their protocols");

        auto waitForBytes = [&](QTcpSocket& socket, qint64 bytes) {
            QElapsedTimer timer;
            timer.start();
            while (socket.bytesAvailable() < bytes && timer.elapsed() < 3000)
                QTest::qWait(25);
            return socket.bytesAvailable() >= bytes;
        };
        auto verifyEcho = [&](QTcpSocket& socket, const QByteArray& payload) {
            QElapsedTimer timer;
            timer.start();
            while (socket.state() != QAbstractSocket::ConnectedState && timer.elapsed() < 3000)
                QTest::qWait(25);
            if (socket.state() != QAbstractSocket::ConnectedState)
                return false;
            if (socket.write(payload) != payload.size() || !socket.waitForBytesWritten(1000))
                return false;
            if (!waitForBytes(socket, payload.size()))
                return false;
            return socket.read(payload.size()) == payload;
        };

        QTcpSocket localSocket;
        localSocket.connectToHost(QStringLiteral("127.0.0.1"), localTunnel.localPort);
        QVERIFY(verifyEcho(localSocket, QByteArrayLiteral("local-tunnel")));

        QTcpSocket remoteSocket;
        remoteSocket.connectToHost(QStringLiteral("127.0.0.1"), remoteTunnel.remotePort);
        QVERIFY(verifyEcho(remoteSocket, QByteArrayLiteral("remote-tunnel")));

        QTcpSocket socksSocket;
        socksSocket.connectToHost(QStringLiteral("127.0.0.1"), socksTunnel.localPort);
        QElapsedTimer socksTimer;
        socksTimer.start();
        while (socksSocket.state() != QAbstractSocket::ConnectedState && socksTimer.elapsed() < 3000)
            QTest::qWait(25);
        QVERIFY(socksSocket.state() == QAbstractSocket::ConnectedState);
        socksSocket.write(QByteArray::fromHex("050102"));
        QVERIFY(socksSocket.waitForBytesWritten(1000));
        QVERIFY(waitForBytes(socksSocket, 2));
        QCOMPARE(socksSocket.read(2), QByteArray::fromHex("0502"));

        QByteArray socksAuth;
        socksAuth.append(char(0x01));
        socksAuth.append(char(socksTunnel.socksUsername.toUtf8().size()));
        socksAuth.append(socksTunnel.socksUsername.toUtf8());
        socksAuth.append(char(socksTunnel.socksPassword.toUtf8().size()));
        socksAuth.append(socksTunnel.socksPassword.toUtf8());
        socksSocket.write(socksAuth);
        QVERIFY(socksSocket.waitForBytesWritten(1000));
        QVERIFY(waitForBytes(socksSocket, 2));
        QCOMPARE(socksSocket.read(2), QByteArray::fromHex("0100"));

        QByteArray socksRequest = QByteArray::fromHex("05010001");
        socksRequest.append(char(127));
        socksRequest.append(char(0));
        socksRequest.append(char(0));
        socksRequest.append(char(1));
        socksRequest.append(char(echoServer.serverPort() >> 8));
        socksRequest.append(char(echoServer.serverPort() & 0xff));
        socksSocket.write(socksRequest);
        QVERIFY(socksSocket.waitForBytesWritten(1000));
        QVERIFY(waitForBytes(socksSocket, 10));
        QCOMPARE(static_cast<unsigned char>(socksSocket.read(2).at(1)), 0);
        QCOMPARE(socksSocket.read(8).size(), 8);
        QVERIFY(verifyEcho(socksSocket, QByteArrayLiteral("socks5-tunnel")));

        const QString localSourceA = serverDir.filePath(QStringLiteral("parallel-source-a.txt"));
        const QString localSourceB = serverDir.filePath(QStringLiteral("parallel-source-b.txt"));
        const QByteArray parallelPayloadA(1024 * 128, 'A');
        const QByteArray parallelPayloadB(1024 * 128, 'B');
        for (const auto& file :
             {qMakePair(localSourceA, parallelPayloadA), qMakePair(localSourceB, parallelPayloadB)}) {
            QFile output(file.first);
            QVERIFY(output.open(QIODevice::WriteOnly));
            QCOMPARE(output.write(file.second), file.second.size());
        }

        Session parallelSession;
        parallelSession.id = QStringLiteral("parallel-sftp-test");
        parallelSession.type = SessionType::SSH;
        parallelSession.host = QStringLiteral("127.0.0.1");
        parallelSession.port = port;
        parallelSession.user = user;
        parallelSession.keyPath = clientKey;

        QList<SftpTransferWorker*> workers;
        QList<QThread*> workerThreads;
        std::atomic_bool uploadPaused{false};
        const auto cleanupWorkers = qScopeGuard([&]() {
            for (int i = 0; i < workerThreads.size(); ++i) {
                workers.at(i)->cancel();
                workerThreads.at(i)->quit();
                workerThreads.at(i)->wait();
                delete workers.at(i);
                delete workerThreads.at(i);
            }
        });
        QSignalSpy shellDuringTransfer(&connection, &SshConnection::shellDataReceived);
        QSignalSpy parallelFinishedSpy(this, &TestSession::parallelTransferFinished);
        const QList<QPair<QString, QString>> parallelTransfers = {
            {localSourceA, QStringLiteral("/tmp/banchoxterm-parallel-a.txt")},
            {localSourceB, QStringLiteral("/tmp/banchoxterm-parallel-b.txt")},
        };
        for (int i = 0; i < parallelTransfers.size(); ++i) {
            SftpTransferRequest request;
            request.id = QStringLiteral("parallel-%1").arg(i);
            request.localPath = parallelTransfers.at(i).first;
            request.remotePath = parallelTransfers.at(i).second;
            request.isUpload = true;

            auto* worker = new SftpTransferWorker(parallelSession, request);
            if (i == 0) {
                connect(
                    worker, &SftpTransferWorker::progress, worker,
                    [worker, &uploadPaused](const QString&, const QString&, qint64 done, qint64) {
                        if (done > 0 && !uploadPaused.exchange(true))
                            worker->pause();
                    },
                    Qt::DirectConnection);
            }
            auto* thread = new QThread;
            worker->moveToThread(thread);
            connect(thread, &QThread::started, worker, &SftpTransferWorker::start);
            connect(worker, &SftpTransferWorker::finished, this,
                    [this, thread](const QString&, bool success, const QString& message) {
                        emit parallelTransferFinished(success, message);
                        thread->quit();
                    });
            workers.append(worker);
            workerThreads.append(thread);
            thread->start();
        }
        QTRY_VERIFY_WITH_TIMEOUT(uploadPaused.load(), 10000);
        connection.sendToShell(QByteArrayLiteral("printf '%s%s\\n' '__SFTP_' 'INTERACTIVE__'\n"));
        auto receivedShellMarker = [&]() {
            QByteArray output;
            for (const auto& event : shellDuringTransfer)
                output += event.first().toByteArray();
            return output.contains("__SFTP_INTERACTIVE__");
        };
        QTRY_VERIFY_WITH_TIMEOUT(receivedShellMarker(), 5000);
        workers.first()->resume();
        QTRY_COMPARE_WITH_TIMEOUT(parallelFinishedSpy.count(), 2, 10000);
        for (int i = 0; i < workerThreads.size(); ++i) {
            QVERIFY(workerThreads.at(i)->wait(3000));
        }
        QSignalSpy downloadSpy(&connection, &SshConnection::operationFinished);
        const QString downloadedA = serverDir.filePath(QStringLiteral("parallel-downloaded-a.txt"));
        const QString downloadedB = serverDir.filePath(QStringLiteral("parallel-downloaded-b.txt"));
        connection.downloadFile(QStringLiteral("/tmp/banchoxterm-parallel-a.txt"), downloadedA);
        QTRY_COMPARE_WITH_TIMEOUT(downloadSpy.count(), 1, 3000);
        QVERIFY(downloadSpy.first().at(0).toBool());
        connection.downloadFile(QStringLiteral("/tmp/banchoxterm-parallel-b.txt"), downloadedB);
        QTRY_COMPARE_WITH_TIMEOUT(downloadSpy.count(), 2, 3000);
        QVERIFY(downloadSpy.last().at(0).toBool());
        for (const auto& file : {qMakePair(downloadedA, parallelPayloadA), qMakePair(downloadedB, parallelPayloadB)}) {
            QFile downloaded(file.first);
            QVERIFY(downloaded.open(QIODevice::ReadOnly));
            QCOMPARE(downloaded.readAll(), file.second);
        }
        connection.deleteFile(QStringLiteral("/tmp/banchoxterm-parallel-a.txt"), false);
        connection.deleteFile(QStringLiteral("/tmp/banchoxterm-parallel-b.txt"), false);

        connection.disconnectFromHost();
        QTRY_VERIFY_WITH_TIMEOUT(!connection.isConnected(), 3000);
        connection.connectToHost(QStringLiteral("127.0.0.1"), port, user, clientKey, QString(), {});
        QTRY_COMPARE_WITH_TIMEOUT(connectedSpy.count(), 2, 5000);
        connection.listDirectory(QStringLiteral("."));
        QTRY_COMPARE_WITH_TIMEOUT(directorySpy.count(), 2, 3000);
        QVERIFY(!qvariant_cast<QList<SftpFile>>(directorySpy.last().at(1)).isEmpty());

        localSocket.disconnectFromHost();
        remoteSocket.disconnectFromHost();
        socksSocket.disconnectFromHost();
        connection.disconnectFromHost();

        // Use the local server as both bastion and target to exercise the
        // nested SSH transport, including its callback context and SFTP.
        failureSpy.clear();
        connection.connectToHost(QStringLiteral("127.0.0.1"), port, user, clientKey, QString(), {},
                                 QStringLiteral("127.0.0.1"), port, user, clientKey);
        QTRY_VERIFY_WITH_TIMEOUT(connectedSpy.count() == 3 || !failureSpy.isEmpty(), 5000);
        QVERIFY2(failureSpy.isEmpty(), qPrintable(failureSpy.value(0).value(0).toString()));
        QCOMPARE(connectedSpy.count(), 3);
        connection.listDirectory(QStringLiteral("."));
        QTRY_COMPARE_WITH_TIMEOUT(directorySpy.count(), 3, 3000);
        QVERIFY(!qvariant_cast<QList<SftpFile>>(directorySpy.last().at(1)).isEmpty());
        connection.disconnectFromHost();

        const QList<Session> previousProfiles = SessionManager::loadSessions();
        const auto restoreProfiles = qScopeGuard([&]() { SessionManager::saveSessions(previousProfiles); });
        Session bastionProfile;
        bastionProfile.id = "integration-bastion";
        bastionProfile.type = SessionType::SSH;
        bastionProfile.host = "127.0.0.1";
        bastionProfile.port = port;
        bastionProfile.user = user;
        bastionProfile.keyPath = clientKey;
        Session targetProfile;
        targetProfile.id = "integration-target";
        targetProfile.type = SessionType::SSH;
        targetProfile.jumpSessionId = bastionProfile.id;
        SessionManager::saveSessions({bastionProfile, targetProfile});
        // A stale snapshot must not override the current saved bastion data.
        connection.connectToHost(QStringLiteral("127.0.0.1"), port, user, clientKey, QString(), {},
                                 QStringLiteral("stale.invalid"), 1, QString(), QString(), targetProfile.id);
        QTRY_VERIFY_WITH_TIMEOUT(connectedSpy.count() == 4 || !failureSpy.isEmpty(), 5000);
        QVERIFY2(failureSpy.isEmpty(), qPrintable(failureSpy.value(0).value(0).toString()));
        QCOMPARE(connectedSpy.count(), 4);
        connection.listDirectory(QStringLiteral("."));
        QTRY_COMPARE_WITH_TIMEOUT(directorySpy.count(), 4, 3000);
        QVERIFY(!qvariant_cast<QList<SftpFile>>(directorySpy.last().at(1)).isEmpty());
        connection.disconnectFromHost();
        sshd.terminate();
        QVERIFY(sshd.waitForFinished(3000));
    }

    void testTerminalShiftTab() {
        QWidget host;
        auto* layout = new QVBoxLayout(&host);
        auto* previous = new QLineEdit(&host);
        auto* terminal = new QTermWidget(0, &host);
        layout->addWidget(previous);
        layout->addWidget(terminal);
        terminal->startExternal();
        host.show();
        auto* display = terminal->focusProxy();
        QVERIFY(display);
        display->setFocus();
        QTRY_VERIFY(display->hasFocus());
        QByteArray sent;
        connect(terminal, &QTermWidget::sendData, &host,
                [&sent](const char* data, int size) { sent.append(data, size); });
        for (auto key : {Qt::Key_Tab, Qt::Key_Backtab}) {
            sent.clear();
            QTest::keyClick(display, key, Qt::ShiftModifier);
            QCOMPARE(sent, QByteArray("\x1b[Z"));
            QVERIFY(display->hasFocus());
        }
        sent.clear();
        QTest::keyClick(display, Qt::Key_Tab);
        QCOMPARE(sent, QByteArray("\t"));
        QVERIFY(display->hasFocus());
    }

    void testTerminalUnicodeFileDrop() {
        QTermWidget terminal(0);
        terminal.startExternal();
        terminal.setCodec(QStringLiteral("UTF-8"));
        terminal.resize(800, 400);
        terminal.show();
        auto* display = terminal.focusProxy();
        QVERIFY(display);
        QByteArray sent;
        connect(&terminal, &QTermWidget::sendData, &terminal,
                [&sent](const char* data, int size) { sent.append(data, size); });
#ifdef Q_OS_WIN
        const QString prefix = QStringLiteral("E:/@home/gargadon/");
#else
        const QString prefix = QStringLiteral("/home/gargadon/");
#endif
        const QString path = prefix + QStringLiteral("V\u00eddeos/Proclama/M\u00fasica \u2014 O'Brien [LETRA].mp4");
        QMimeData files;
        files.setUrls({QUrl::fromLocalFile(path), QUrl::fromLocalFile(prefix + QStringLiteral("otro.mp4"))});
        const auto drop = [&](QMimeData* mime) {
            QDragEnterEvent enter(QPoint(10, 10), Qt::CopyAction, mime, Qt::LeftButton, Qt::NoModifier);
            QApplication::sendEvent(display, &enter);
            QVERIFY(enter.isAccepted());
            QDropEvent event(QPointF(10, 10), Qt::CopyAction, mime, Qt::LeftButton, Qt::NoModifier);
            QApplication::sendEvent(display, &event);
            QVERIFY(event.isAccepted());
        };
        QString quoted = path;
        quoted.replace('\'', QStringLiteral("'\\''"));
        const QByteArray expected = ("'" + quoted + "' '" + prefix + "otro.mp4' ").toUtf8();
        drop(&files);
        QCOMPARE(sent, expected);
        sent.clear();
        terminal.feedData(QByteArrayLiteral("\x1b[?2004h"));
        drop(&files);
        QCOMPARE(sent, QByteArray("\x1b[200~") + expected + QByteArray("\x1b[201~"));
        sent.clear();
        QMimeData text;
        text.setText(QStringLiteral("V\u00eddeos y M\u00fasica"));
        drop(&text);
        QCOMPARE(sent, QByteArray("\x1b[200~") + text.text().toUtf8() + QByteArray("\x1b[201~"));
    }

    void testTerminalSearchApi() {
        QTermWidget terminal(0);
        terminal.startExternal();
        terminal.setCodec(QStringLiteral("UTF-8"));
        terminal.feedData(QString::fromUtf8("Alpha café a.b\r\nalpha second\r\n").toUtf8());
        QTest::qWait(50);
        QSignalSpy results(&terminal, &QTermWidget::searchResult);
        terminal.searchText(QStringLiteral("café"), true, true, true);
        QCOMPARE(results.count(), 1);
        QCOMPARE(results.takeFirst().at(0).toBool(), true);
        QCOMPARE(terminal.selectedText(), QStringLiteral("café"));
        terminal.searchText(QStringLiteral("a.b"), true, true, true);
        QCOMPARE(results.count(), 1);
        QCOMPARE(results.takeFirst().at(0).toBool(), true);
        QCOMPARE(terminal.selectedText(), QStringLiteral("a.b"));
        terminal.searchText(QStringLiteral("ALPHA"), true, true, true);
        QCOMPARE(results.count(), 1);
        QCOMPARE(results.takeFirst().at(0).toBool(), false);
        terminal.searchText(QStringLiteral("ALPHA"), true, true, false);
        QCOMPARE(results.count(), 1);
        QCOMPARE(results.takeFirst().at(0).toBool(), true);
        terminal.searchText(QStringLiteral("missing"), false, false, false);
        QCOMPARE(results.count(), 1);
        QCOMPARE(results.takeFirst().at(0).toBool(), false);
        terminal.searchText(QString(), true, true, false);
        QCOMPARE(results.count(), 1);
        QCOMPARE(results.takeFirst().at(0).toBool(), false);
    }

    void testNaturalTerminalShortcuts() {
        QMap<QString, QKeySequence> original;
        QMap<QString, QKeySequence> defaults;
        QSet<QString> assigned;
        for (const auto& definition : KeyboardShortcuts::definitions()) {
            original.insert(definition.id, KeyboardShortcuts::sequence(definition.id));
            defaults.insert(definition.id, definition.defaultSequence);
            const QString sequence = definition.defaultSequence.toString(QKeySequence::PortableText);
            QVERIFY(!sequence.isEmpty());
            QVERIFY(!assigned.contains(sequence));
            assigned.insert(sequence);
        }
        const auto restore = qScopeGuard([&]() { KeyboardShortcuts::save(original); });
        KeyboardShortcuts::save(defaults);
        MainWindow window;
        window.resize(1000, 650);
        window.show();
        window.activateWindow();
        Session session;
        session.type = SessionType::Serial;
        session.id = "natural-shortcuts";
        session.name = session.id;
        session.serialPort = "banchoxterm-test-missing-port";
        QVERIFY(QMetaObject::invokeMethod(&window, "onConnectSession", Qt::DirectConnection, Q_ARG(Session, session)));
        QPointer<TerminalTab> tab = window.findChild<TerminalTab*>();
        QVERIFY(tab);
        QVERIFY(!tab->capturesApplicationShortcuts());
        auto* terminal = tab->getTerminalWidget();
        auto* display = terminal->focusProxy();
        display->setFocus();
        QTRY_VERIFY(display->hasFocus());
        QByteArray sent;
        connect(terminal, &QTermWidget::sendData, &window,
                [&sent](const char* bytes, int size) { sent.append(bytes, size); });
        QTest::keyClick(display, Qt::Key_Escape);
        QCOMPARE(sent, QByteArrayLiteral("\x1b"));
        for (const auto& pair :
             {qMakePair(Qt::Key_W, char(0x17)), qMakePair(Qt::Key_F, char(0x06)), qMakePair(Qt::Key_Q, char(0x11)),
              qMakePair(Qt::Key_K, char(0x0b)), qMakePair(Qt::Key_C, char(0x03)), qMakePair(Qt::Key_D, char(0x04)),
              qMakePair(Qt::Key_Z, char(0x1a))}) {
            sent.clear();
            QTest::keyClick(display, pair.first, Qt::ControlModifier);
            QCOMPARE(sent, QByteArray(1, pair.second));
            QVERIFY(window.isVisible());
            QVERIFY(tab);
        }
        for (const auto& pair : {qMakePair(Qt::Key_B, QByteArray("\x1b"
                                                                 "b")),
                                 qMakePair(Qt::Key_F, QByteArray("\x1b"
                                                                 "f"))}) {
            sent.clear();
            QTest::keyClick(display, pair.first, Qt::AltModifier);
            QCOMPARE(sent, pair.second);
        }
        QCOMPARE(window.findChildren<QAction*>("shortcut_findAll").size(), 1);
        QTest::keyClick(display, Qt::Key_F, Qt::ControlModifier | Qt::ShiftModifier);
        auto* search = terminal->findChild<QLineEdit*>("searchTextEdit");
        QVERIFY(search);
        QVERIFY(search->isVisible());
        QTest::keyClick(search, Qt::Key_Escape);
        QTRY_VERIFY(!search->isVisible());
        display->setFocus();
        const QString oldClipboard = QApplication::clipboard()->text();
        const auto restoreClipboard = qScopeGuard([&]() { QApplication::clipboard()->setText(oldClipboard); });
        QApplication::clipboard()->setText("abcXYZ");
        terminal->feedData("\x1b[?2004h");
        sent.clear();
        QTest::keyClick(display, Qt::Key_V, Qt::ControlModifier | Qt::ShiftModifier);
        QCOMPARE(sent, QByteArray("\x1b[200~abcXYZ\x1b[201~")); // Exactly one bracketed paste.
        QTest::keyClick(display, Qt::Key_W, Qt::ControlModifier | Qt::ShiftModifier);
        QTRY_VERIFY(tab.isNull());
    }

    void testShortcutCustomization() {
        QMap<QString, QKeySequence> original;
        QMap<QString, QKeySequence> defaults;
        for (const auto& definition : KeyboardShortcuts::definitions()) {
            original.insert(definition.id, KeyboardShortcuts::sequence(definition.id));
            defaults.insert(definition.id, definition.defaultSequence);
        }
        const auto restore = qScopeGuard([&]() { KeyboardShortcuts::save(original); });
        KeyboardShortcuts::save(defaults);
        MainWindow window;
        window.show();
        window.activateWindow();
        auto* closeAction = window.findChild<QAction*>("shortcut_closeTab");
        QVERIFY(closeAction);
        QCOMPARE(closeAction->shortcut(), QKeySequence("Ctrl+Shift+W"));
        {
            SettingsDialog dialog(&window);
            auto* editor = dialog.findChild<QKeySequenceEdit*>("shortcutEdit_closeTab");
            editor->setKeySequence(QKeySequence("F8, F9"));
            QCOMPARE(editor->keySequence(), QKeySequence("F8"));
            editor->clear();
            QVERIFY(editor->keySequence().isEmpty());
            editor->setKeySequence(QKeySequence("F8"));
            dialog.reject();
            QCOMPARE(closeAction->shortcut(), QKeySequence("Ctrl+Shift+W"));
        }
        {
            SettingsDialog dialog(&window);
            dialog.findChild<QKeySequenceEdit*>("shortcutEdit_closeTab")->setKeySequence(QKeySequence("F8"));
            dialog.findChild<QKeySequenceEdit*>("shortcutEdit_findTerminal")->setKeySequence(QKeySequence("F7"));
            QVERIFY(QMetaObject::invokeMethod(&dialog, "onAccept", Qt::DirectConnection));
            QCOMPARE(dialog.result(), int(QDialog::Accepted));
        }
        QCOMPARE(closeAction->shortcut(), QKeySequence("F8"));
        QCOMPARE(QSettings().value("shortcuts/closeTab").toString(), QString("F8"));
        Session session;
        session.type = SessionType::Serial;
        session.id = "custom-shortcuts";
        session.name = session.id;
        session.serialPort = "banchoxterm-test-missing-port";
        QVERIFY(QMetaObject::invokeMethod(&window, "onConnectSession", Qt::DirectConnection, Q_ARG(Session, session)));
        QPointer<TerminalTab> tab = window.findChild<TerminalTab*>();
        auto* terminal = tab->getTerminalWidget();
        auto* display = terminal->focusProxy();
        QCOMPARE(terminal->findChild<QAction*>("shortcut_findTerminal")->shortcut(), QKeySequence("F7"));
        display->setFocus();
        QTRY_VERIFY(display->hasFocus());
        QTest::keyClick(display, Qt::Key_F7);
        auto* search = terminal->findChild<QLineEdit*>("searchTextEdit");
        QVERIFY(search->isVisible());
        QTest::keyClick(search, Qt::Key_Escape);
        // Clearing a field disables the binding and updates an already-open terminal.
        {
            SettingsDialog dialog(&window);
            dialog.findChild<QKeySequenceEdit*>("shortcutEdit_closeTab")->clear();
            dialog.findChild<QKeySequenceEdit*>("shortcutEdit_findTerminal")->setKeySequence(QKeySequence("F6"));
            QVERIFY(QMetaObject::invokeMethod(&dialog, "onAccept", Qt::DirectConnection));
            QCOMPARE(dialog.result(), int(QDialog::Accepted));
        }
        QVERIFY(closeAction->shortcut().isEmpty());
        QVERIFY(QSettings().contains("shortcuts/closeTab"));
        QCOMPARE(terminal->findChild<QAction*>("shortcut_findTerminal")->shortcut(), QKeySequence("F6"));
        display->setFocus();
        QTest::keyClick(display, Qt::Key_F8);
        QVERIFY(tab);
        {
            SettingsDialog dialog(&window);
            dialog.findChild<QPushButton*>("restoreDefaultShortcutsButton")->click();
            QVERIFY(closeAction->shortcut().isEmpty()); // Reset is not applied until OK.
            QVERIFY(QMetaObject::invokeMethod(&dialog, "onAccept", Qt::DirectConnection));
        }
        QCOMPARE(closeAction->shortcut(), QKeySequence("Ctrl+Shift+W"));
        QCOMPARE(terminal->findChild<QAction*>("shortcut_findTerminal")->shortcut(), QKeySequence("Ctrl+Shift+F"));
        QVERIFY(!QSettings().contains("shortcuts/closeTab"));
        {
            SettingsDialog dialog(&window);
            dialog.findChild<QKeySequenceEdit*>("shortcutEdit_closeTab")->setKeySequence(QKeySequence("F8"));
            dialog.findChild<QKeySequenceEdit*>("shortcutEdit_findAll")->setKeySequence(QKeySequence("F8"));
            QTimer::singleShot(0, &dialog, []() {
                auto* warning = qobject_cast<QMessageBox*>(QApplication::activeModalWidget());
                QVERIFY(warning);
                warning->accept();
            });
            QVERIFY(QMetaObject::invokeMethod(&dialog, "onAccept", Qt::DirectConnection));
            QVERIFY(dialog.result() != QDialog::Accepted);
            QCOMPARE(closeAction->shortcut(), QKeySequence("Ctrl+Shift+W"));
        }
        // Reopened editors read the saved mappings, not stale widget state.
        SettingsDialog reopened(&window);
        QCOMPARE(reopened.findChild<QKeySequenceEdit*>("shortcutEdit_closeTab")->keySequence(),
                 QKeySequence("Ctrl+Shift+W"));
    }

    void testTerminalBellNotification() {
        Session session;
        session.type = SessionType::Serial;
        session.serialPort = "banchoxterm-test-missing-port";
        TerminalTab tab(session);
        auto* terminal = tab.getTerminalWidget();
        QVERIFY(terminal);
        auto* notice = terminal->findChild<QLabel*>("terminalBellNotice");
        QVERIFY(notice);
        QSignalSpy bells(&tab, &TerminalTab::bell);
        QVERIFY(notice->isHidden());
        // OSC terminators are not terminal alerts, even across reads.
        terminal->feedData(QByteArrayLiteral("\x1b]0;title"));
        terminal->feedData(QByteArrayLiteral("\x07"));
        QCOMPARE(bells.count(), 0);
        terminal->feedData(QByteArrayLiteral("\x07"));
        QCOMPARE(bells.count(), 1);
        QVERIFY(!notice->isHidden());
        // The emulation already limits bell storms to one per 500 ms.
        terminal->feedData(QByteArray(20, '\x07'));
        QCOMPARE(bells.count(), 1);
        QTRY_VERIFY_WITH_TIMEOUT(notice->isHidden(), 2500);
        terminal->feedData(QByteArrayLiteral("\x07"));
        QCOMPARE(bells.count(), 2);
        QVERIFY(!notice->isHidden());
    }

    void testTerminalShortcutCapture() {
        MainWindow window;
        window.resize(1000, 650);
        window.show();
        window.activateWindow();
        Session session;
        session.type = SessionType::Serial;
        session.id = "shortcut-capture-first";
        session.name = session.id;
        session.serialPort = "banchoxterm-test-missing-port";
        QVERIFY(QMetaObject::invokeMethod(&window, "onConnectSession", Qt::DirectConnection, Q_ARG(Session, session)));
        QPointer<TerminalTab> first = window.findChild<TerminalTab*>();
        QVERIFY(first);
        auto* terminal = first->getTerminalWidget();
        QVERIFY(terminal);
        auto* display = terminal->focusProxy();
        QVERIFY(display);
        QByteArray sent;
        connect(terminal, &QTermWidget::sendData, &window,
                [&sent](const char* bytes, int size) { sent.append(bytes, size); });
        auto* capture = window.findChild<QAction*>("captureShortcutsAction");
        QVERIFY(capture);
        QVERIFY(capture->isEnabled());
        capture->trigger();
        QVERIFY(first->capturesApplicationShortcuts());
        QVERIFY(capture->isChecked());
        QVERIFY(first->findChild<QWidget*>("shortcutCaptureBar")->isVisible());
        display->setFocus();
        QTRY_VERIFY(display->hasFocus());
        QTRY_VERIFY(first->findChild<QWidget*>("shortcutCaptureBar")->height() < terminal->height() / 3);
        sent.clear();
        QTest::keyClick(display, Qt::Key_W, Qt::ControlModifier);
        QCOMPARE(sent, QByteArray("\x17"));
        QTest::qWait(10);
        QVERIFY(first); // Ctrl+W must reach the terminal.
        sent.clear();
        QTest::keyClick(display, Qt::Key_W, Qt::ControlModifier | Qt::ShiftModifier);
        QCOMPARE(sent, QByteArray("\x17"));
        QVERIFY(first); // The new application close shortcut is also captured in this mode.
        sent.clear();
        QTest::keyClick(display, Qt::Key_F, Qt::ControlModifier);
        QCOMPARE(sent, QByteArray("\x06"));
        const QString oldClipboard = QApplication::clipboard()->text();
        const auto restoreClipboard = qScopeGuard([&]() { QApplication::clipboard()->setText(oldClipboard); });
        QApplication::clipboard()->setText("must-not-be-pasted");
        sent.clear();
        QTest::keyClick(display, Qt::Key_C, Qt::ControlModifier | Qt::ShiftModifier);
        QCOMPARE(sent, QByteArray("\x03"));
        sent.clear();
        QTest::keyClick(display, Qt::Key_V, Qt::ControlModifier | Qt::ShiftModifier);
        QCOMPARE(sent, QByteArray("\x16"));

        // The mouse menu can restore shortcuts while keyboard capture is active.
        QTimer::singleShot(0, &window, [&]() {
            auto* menu = qobject_cast<QMenu*>(QApplication::activePopupWidget());
            QVERIFY(menu);
            const auto close = qScopeGuard([&]() { menu->close(); });
            auto* toggle = menu->actions().first();
            QVERIFY(toggle->isChecked());
            QTest::mouseClick(menu, Qt::LeftButton, Qt::NoModifier, menu->actionGeometry(toggle).center());
        });
        QVERIFY(QMetaObject::invokeMethod(first, "showTerminalContextMenu", Qt::DirectConnection,
                                          Q_ARG(QPoint, QPoint(20, 20))));
        QVERIFY(!first->capturesApplicationShortcuts());
        QVERIFY(!capture->isChecked());
        capture->trigger();
        QVERIFY(first->capturesApplicationShortcuts());

        // A field outside the terminal viewport retains its own shortcuts.
        window.activateWindow();
        auto* quickConnect = window.findChild<QLineEdit*>("quickConnectEdit");
        QVERIFY(quickConnect);
        {
            QLineEdit embeddedField(terminal); // Same hierarchy as QTermWidget's search field.
            embeddedField.show();
            for (auto* field : {&embeddedField, quickConnect}) {
                QShortcut fieldShortcut(QKeySequence(Qt::Key_F12), field);
                fieldShortcut.setContext(Qt::WidgetShortcut);
                QSignalSpy fieldShortcutSpy(&fieldShortcut, &QShortcut::activated);
                field->setFocus();
                QTRY_VERIFY(field->hasFocus());
                QTest::keyClick(field, Qt::Key_F12);
                QCOMPARE(fieldShortcutSpy.count(), 1);
            }
        }

        // The mode belongs to its tab, and Ctrl+Shift+W still closes other tabs.
        session.id = "shortcut-capture-second";
        session.name = session.id;
        QVERIFY(QMetaObject::invokeMethod(&window, "onConnectSession", Qt::DirectConnection, Q_ARG(Session, session)));
        QPointer<TerminalTab> second;
        for (auto* tab : window.findChildren<TerminalTab*>())
            if (tab != first)
                second = tab;
        QVERIFY(second);
        QVERIFY(!capture->isChecked());
        QVERIFY(!second->capturesApplicationShortcuts());
        QVERIFY(!second->isSessionActive());
        second->getTerminalWidget()->setFocus();
        QTest::keyClick(second->getTerminalWidget()->focusProxy(), Qt::Key_W, Qt::ControlModifier | Qt::ShiftModifier);
        QTRY_VERIFY(second.isNull());
        QVERIFY(first);
        QVERIFY(capture->isChecked());
        first->findChild<QPushButton*>("restoreShortcutsButton")->click();
        QVERIFY(!first->capturesApplicationShortcuts());
        QVERIFY(!capture->isChecked());
        QVERIFY(!first->findChild<QWidget*>("shortcutCaptureBar")->isVisible());
        QTest::keyClick(display, Qt::Key_W, Qt::ControlModifier | Qt::ShiftModifier);
        QTRY_VERIFY(first.isNull());
    }

    void testSensitiveDataFilter() {
        const QByteArray secret = QString::fromUtf8("secreto-pingüino").toUtf8();
        const QByteArray stream = "before " + secret + " after\n";
        for (qsizetype split = 0; split <= stream.size(); ++split) {
            SensitiveDataFilter filter;
            filter.addValue(secret);
            QByteArray result = filter.process(stream.left(split));
            result += filter.process(stream.mid(split));
            QCOMPARE(result, QByteArray("before [redacted] after\n"));
        }
        SensitiveDataFilter filter;
        filter.addValue("pass");
        filter.addValue("password");
        QCOMPARE(filter.process("prefix pa"), QByteArray("prefix "));
        QCOMPARE(filter.process("ss"), QByteArray());
        QCOMPARE(filter.process("word! pass!\n"), QByteArray("[redacted]! [redacted]!\n"));
        QVERIFY(filter.containsValue("a password here"));
        QVERIFY(!filter.containsValue("ordinary command"));
        SensitiveDataFilter ordinary;
        ordinary.addValue(QByteArray());
        QCOMPARE(ordinary.process("normal output"), QByteArray("normal output"));
    }

    void testSensitiveSessionLog() {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        QSettings settings;
        const QVariant oldEnabled = settings.value("terminal/loggingEnabled");
        const QVariant oldDirectory = settings.value("terminal/logDirectory");
        const auto restore = qScopeGuard([&]() {
            settings.setValue("terminal/loggingEnabled", oldEnabled);
            settings.setValue("terminal/logDirectory", oldDirectory);
        });
        settings.setValue("terminal/loggingEnabled", true);
        settings.setValue("terminal/logDirectory", dir.path());
        const QByteArray secret("temporary-sensitive-value");
        {
            Session session;
            session.type = SessionType::SSH;
            session.id = "sensitive-log-test";
            session.name = session.id;
            session.host = "127.0.0.1";
            session.port = 1;
            TerminalTab tab(session);
            QVERIFY(tab.connection());
            tab.sendSensitiveInputText(QString::fromUtf8(secret));
            // Inject output through the real SSH-to-terminal/log signal path.
            for (const QByteArray& chunk :
                 {QByteArray("before temporary-"), QByteArray("sensitive-"), QByteArray("value after\n")})
                QVERIFY(QMetaObject::invokeMethod(tab.connection(), "shellDataReceived", Qt::DirectConnection,
                                                  Q_ARG(QByteArray, chunk)));
        }
        const QStringList logs = QDir(dir.path()).entryList({"*.log"}, QDir::Files);
        QCOMPARE(logs.size(), 1);
        QFile log(dir.filePath(logs.first()));
        QVERIFY(log.open(QIODevice::ReadOnly));
        const QByteArray data = log.readAll();
        QVERIFY(data.contains("before [redacted] after\n"));
        QVERIFY(!data.contains(secret));
        QVERIFY(!data.contains("temporary-"));
        QVERIFY(!data.contains("sensitive-"));
        QVERIFY(!settings.value("commandHistory/global").toStringList().contains(QString::fromUtf8(secret)));
        QVERIFY(!settings.value("commandHistory/session/sensitive-log-test")
                     .toStringList()
                     .contains(QString::fromUtf8(secret)));
    }

    void testSensitiveMultiInput() {
#ifdef Q_OS_WIN
        QSKIP("This integration test uses /bin/sh.");
#else
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString outputPath = dir.filePath("sensitive-output.txt");
        const QString secret = QString("  printf x >> '%1'  ").arg(outputPath);
        QSettings settings;
        const QStringList historyKeys = {"multiinput/history", "commandHistory/global",
                                         "commandHistory/session/sensitive-first",
                                         "commandHistory/session/sensitive-second"};
        QMap<QString, QVariant> oldValues;
        for (const QString& key : historyKeys) {
            oldValues.insert(key, settings.value(key));
            settings.setValue(key, QStringList{"existing"});
        }
        const auto restore = qScopeGuard([&]() {
            for (auto it = oldValues.cbegin(); it != oldValues.cend(); ++it)
                settings.setValue(it.key(), it.value());
        });
        {
            MainWindow window;
            for (int i = 0; i < 3; ++i) {
                Session session;
                session.type = SessionType::Local;
                session.id = i == 0 ? "sensitive-first" : i == 1 ? "sensitive-second" : "sensitive-readonly";
                session.name = session.id;
                session.shellPath = "/bin/sh";
                session.readOnly = i == 2;
                QVERIFY(QMetaObject::invokeMethod(&window, "onConnectSession", Qt::DirectConnection,
                                                  Q_ARG(Session, session)));
            }
            auto* action = window.findChild<QAction*>("sensitiveMultiInputAction");
            QVERIFY(action);
            QTimer::singleShot(0, &window, [&]() {
                auto* dialog = qobject_cast<QDialog*>(QApplication::activeModalWidget());
                QVERIFY(dialog);
                const auto close = qScopeGuard([&]() { dialog->reject(); });
                auto* value = dialog->findChild<QLineEdit*>("sensitiveInputValue");
                auto* list = dialog->findChild<QListWidget*>("sensitiveInputList");
                QVERIFY(value);
                QCOMPARE(value->echoMode(), QLineEdit::Password);
                dialog->findChild<QLineEdit*>("sensitiveInputName")->setText("Proxy credentials");
                value->setText(secret);
                dialog->findChild<QPushButton*>("sensitiveInputAddButton")->click();
                QCOMPARE(list->count(), 1);
                QCOMPARE(list->item(0)->text(), QString("Proxy credentials"));
                QTimer::singleShot(0, &window, [&]() {
                    auto* targets = qobject_cast<QDialog*>(QApplication::activeModalWidget());
                    QVERIFY(targets);
                    auto cancel = qScopeGuard([&]() { targets->reject(); });
                    QCOMPARE(targets->objectName(), QString("multiInputTargetDialog"));
                    auto* targetList = targets->findChild<QListWidget*>();
                    QCOMPARE(targetList->count(), 2); // Read-only session is excluded.
                    for (int row = 0; row < targetList->count(); ++row)
                        targetList->item(row)->setCheckState(
                            targetList->item(row)->text().contains("first") ? Qt::Checked : Qt::Unchecked);
                    for (auto* label : targets->findChildren<QLabel*>())
                        QVERIFY(!label->text().contains(secret.trimmed()));
                    targets->accept();
                    cancel.dismiss();
                });
                dialog->findChild<QPushButton*>("sensitiveInputSendButton")->click();
            });
            action->trigger();
            auto contents = [&]() {
                QFile file(outputPath);
                return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
            };
            QTRY_COMPARE_WITH_TIMEOUT(contents(), QByteArray("x"), 3000);
            for (const QString& key : historyKeys)
                QCOMPARE(settings.value(key).toStringList(), QStringList{"existing"});
            for (const QString& key : settings.allKeys())
                QVERIFY(!settings.value(key).toStringList().join('\n').contains(secret.trimmed()));

            // Entries survive reopening the dialog, but cancelling a send writes nothing.
            QTimer::singleShot(0, &window, [&]() {
                auto* dialog = qobject_cast<QDialog*>(QApplication::activeModalWidget());
                QVERIFY(dialog);
                const auto close = qScopeGuard([&]() { dialog->reject(); });
                auto* list = dialog->findChild<QListWidget*>("sensitiveInputList");
                QCOMPARE(list->count(), 1);
                list->setCurrentRow(0);
                QCOMPARE(dialog->findChild<QLineEdit*>("sensitiveInputValue")->text(), secret);
                QTimer::singleShot(0, &window, []() {
                    auto* targets = qobject_cast<QDialog*>(QApplication::activeModalWidget());
                    QVERIFY(targets);
                    targets->reject();
                });
                dialog->findChild<QPushButton*>("sensitiveInputSendButton")->click();
            });
            action->trigger();
            QTest::qWait(150);
            QCOMPARE(contents(), QByteArray("x"));
            for (auto* tab : window.findChildren<TerminalTab*>())
                if (tab->session().id == "sensitive-first")
                    tab->sendInputText("printf normal > /dev/null");
            QCOMPARE(settings.value("commandHistory/global").toStringList().first(),
                     QString("printf normal > /dev/null"));
        }
        // A new application window does not recover any sensitive entries from settings.
        MainWindow freshWindow;
        QTimer::singleShot(0, &freshWindow, []() {
            auto* dialog = qobject_cast<QDialog*>(QApplication::activeModalWidget());
            QVERIFY(dialog);
            const auto close = qScopeGuard([&]() { dialog->reject(); });
            QCOMPARE(dialog->findChild<QListWidget*>("sensitiveInputList")->count(), 0);
            QVERIFY(dialog->findChild<QLineEdit*>("sensitiveInputValue")->text().isEmpty());
        });
        QVERIFY(QMetaObject::invokeMethod(&freshWindow, "onSensitiveMultiInput", Qt::DirectConnection));
#endif
    }

    void testMacroAsyncContinuation_data() {
        QTest::addColumn<QString>("pause");
        QTest::addColumn<bool>("expect");
        QTest::addColumn<QString>("lineBreak");
        QTest::addColumn<bool>("firstEnter");
        QTest::newRow("compact-pause") << "{{PAUSE:80}}" << false << "" << true;
        QTest::newRow("spaced-pause") << "{{PAUSE: 80 }}" << false << "" << true;
        QTest::newRow("pause-and-expect") << "{{PAUSE: 80}}" << true << "" << true;
        QTest::newRow("formatted-lf") << "{{PAUSE:80}}" << false << "\n" << true;
        QTest::newRow("formatted-crlf") << "{{PAUSE:80}}" << false << "\r\n" << true;
        QTest::newRow("no-implicit-enter") << "{{PAUSE:80}}" << false << "\n" << false;
    }

    void testMacroAsyncContinuation() {
#ifdef Q_OS_WIN
        QSKIP("This integration test uses /bin/sh.");
#else
        QFETCH(QString, pause);
        QFETCH(bool, expect);
        QFETCH(QString, lineBreak);
        QFETCH(bool, firstEnter);
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString outputPath = dir.filePath("macro-output.txt");
        const QString name = "macro-async-regression";
        QSettings settings;
        const QVariant oldNames = settings.value("macros/names");
        const QVariant oldTexts = settings.value("macros/texts");
        const auto restore = qScopeGuard([&]() {
            settings.setValue("macros/names", oldNames);
            settings.setValue("macros/texts", oldTexts);
        });
        const QString wait = expect ? "{{EXPECT:READY_TO_CONTINUE}}" : QString();
        const QString macro = QString("printf first > '%1'").arg(outputPath) + (firstEnter ? "{{ENTER}}" : "") +
                              lineBreak + pause + lineBreak + wait + (firstEnter ? "" : "{{ENTER}}") + "{{PAUSE:80}}" +
                              lineBreak + QString("printf second >> '%1'{{ENTER}}").arg(outputPath) + lineBreak +
                              pause + lineBreak + QString("printf third >> '%1'{{ENTER}}").arg(outputPath);
        settings.setValue("macros/names", QStringList{name});
        settings.setValue("macros/texts", QStringList{macro});
        MainWindow window;
        Session session;
        session.id = name;
        session.name = name;
        session.type = SessionType::Local;
        session.shellPath = "/bin/sh";
        QVERIFY(QMetaObject::invokeMethod(&window, "onConnectSession", Qt::DirectConnection, Q_ARG(Session, session)));
        auto* tab = window.findChild<TerminalTab*>();
        QVERIFY(tab);
        QAction* action = nullptr;
        for (auto* candidate : window.findChildren<QAction*>())
            if (candidate->text() == name)
                action = candidate;
        QVERIFY(action);
        auto contents = [&]() {
            QFile file(outputPath);
            return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
        };
        action->trigger();
        if (!firstEnter) {
            QTest::qWait(30);
            QVERIFY(!QFileInfo::exists(outputPath));
        }
        QTRY_COMPARE_WITH_TIMEOUT(contents(), QByteArray("first"), 2000);
        if (expect) {
            QTest::qWait(150);
            QCOMPARE(contents(), QByteArray("first"));
            QVERIFY(QMetaObject::invokeMethod(tab, "terminalDataReceived", Qt::DirectConnection,
                                              Q_ARG(QByteArray, QByteArray("READY_TO_CONTINUE"))));
        }
        QTRY_COMPARE_WITH_TIMEOUT(contents(), QByteArray("firstsecondthird"), 3000);
        const auto log = QSettings().value("macros/log").toStringList();
        QVERIFY(!log.isEmpty());
        QVERIFY(log.first().contains("Macro completed"));
#endif
    }

    void testTerminalTabLifecycle() {
        Session session;
        session.id = QStringLiteral("terminal-tab-lifecycle");
        session.name = QStringLiteral("Lifecycle test");
        session.type = SessionType::Local;
        session.shellPath = QStringLiteral("/bin/sh");

        auto* tab = new TerminalTab(session);
        tab->show();
        QTest::qWait(100);
        QVERIFY(tab->getTerminalWidget() != nullptr);
        QCOMPARE(tab->session().id, session.id);
        QVERIFY(tab->isSessionActive());

        delete tab;
        QCoreApplication::processEvents();
    }

    void testFlexiblePanelLayout() {
        QSettings settings;
        const QStringList keys = {"window/paneTree", "window/openTabs", "window/tabLayoutMode", "window/sidebarHidden"};
        QMap<QString, QVariant> previous;
        for (const auto& key : keys) {
            previous[key] = settings.value(key);
            settings.remove(key);
        }
        settings.setValue("window/openTabs", QByteArray("[]"));
        const auto restore = qScopeGuard([&]() {
            for (auto it = previous.cbegin(); it != previous.cend(); ++it)
                if (it.value().isValid())
                    settings.setValue(it.key(), it.value());
                else
                    settings.remove(it.key());
        });
        MainWindow window;
        window.resize(1400, 900);
        window.show();
        auto visible = [&]() {
            QList<QTabWidget*> result;
            for (auto* pane : window.findChildren<QTabWidget*>("terminalTabs"))
                if (!pane->isHidden())
                    result.append(pane);
            return result;
        };
        QCOMPARE(visible().size(), 1);
        Session session;
        session.type = SessionType::Local;
        session.name = "Panel lifecycle";
#ifdef Q_OS_WIN
        session.shellPath = "cmd.exe";
#else
        session.shellPath = "/bin/sh";
#endif
        QVERIFY(QMetaObject::invokeMethod(&window, "onConnectSession", Q_ARG(Session, session)));
        QPointer<TerminalTab> tab = window.findChild<TerminalTab*>();
        QVERIFY(tab);
        for (int i = 0; i < 5; ++i) {
            QVERIFY(QMetaObject::invokeMethod(&window, i % 2 ? "splitPaneVertically" : "splitPaneHorizontally"));
            QCoreApplication::processEvents();
            QCOMPARE(visible().size(), i + 2);
            QVERIFY(tab);
            QVERIFY(tab->isVisible());
        }
        // Keep a second session in a dynamically created pane when switching
        // to a preset; it must migrate into a visible pane, not disappear.
        QVERIFY(QMetaObject::invokeMethod(&window, "onConnectSession", Q_ARG(Session, session)));
        auto* root = window.findChild<QSplitter*>("terminalPaneRoot");
        QVERIFY(root);
        auto splits = root->findChildren<QSplitter*>();
        QVERIFY(splits.size() >= 4);
        bool horizontal = false, vertical = false;
        for (auto* split : splits) {
            horizontal |= split->orientation() == Qt::Horizontal;
            vertical |= split->orientation() == Qt::Vertical;
            QVERIFY(!split->childrenCollapsible());
        }
        QVERIFY(horizontal && vertical);
        QVERIFY(QMetaObject::invokeMethod(&window, "toggleGridView"));
        QCoreApplication::processEvents();
        QCOMPARE(visible().size(), 2);
        int visibleTabs = 0;
        for (auto* pane : visible())
            visibleTabs += pane->count();
        QCOMPARE(visibleTabs, 2);
        QVERIFY(QMetaObject::invokeMethod(&window, "splitPaneHorizontally"));
        QCoreApplication::processEvents();
        QCOMPARE(visible().size(), 3);
        QVERIFY(QMetaObject::invokeMethod(&window, "closePane"));
        QCoreApplication::processEvents();
        QCOMPARE(visible().size(), 2);
        QVERIFY(tab && tab->isSessionActive());
        QVERIFY(QMetaObject::invokeMethod(&window, "toggleGridView"));
        QCoreApplication::processEvents();
        QCOMPARE(visible().size(), 2);
        while (visible().size() > 1) {
            QVERIFY(QMetaObject::invokeMethod(&window, "closePane"));
            QCoreApplication::processEvents();
        }
        QCOMPARE(visible().size(), 1);
        QVERIFY(tab);
        QVERIFY(visible().first()->indexOf(tab) >= 0);
        auto* sidebar = window.findChild<QFrame*>("sidebarContainer");
        QVERIFY(sidebar);
        const int width = sidebar->width();
        auto* toggle = window.findChild<QToolButton*>("sidebarToggleButton");
        QVERIFY(toggle);
        QVERIFY(toggle->isVisible());
        QVERIFY(!sidebar->isAncestorOf(toggle));
        QTest::mouseClick(toggle, Qt::LeftButton);
        QVERIFY(sidebar->isHidden());
        QVERIFY(toggle->isVisible());
        QTest::mouseClick(toggle, Qt::LeftButton);
        QCoreApplication::processEvents();
        QVERIFY(!sidebar->isHidden());
        QVERIFY(qAbs(sidebar->width() - width) <= 5);
    }

    void testFreeSplitControlsAndDragging() {
        QSettings settings;
        const QStringList keys = {"window/paneTree", "window/openTabs", "window/tabLayoutMode"};
        QMap<QString, QVariant> previous;
        for (const auto& key : keys) {
            previous[key] = settings.value(key);
            settings.remove(key);
        }
        settings.setValue("window/openTabs", QByteArray("[]"));
        const auto restore = qScopeGuard([&]() {
            for (auto it = previous.cbegin(); it != previous.cend(); ++it)
                if (it.value().isValid())
                    settings.setValue(it.key(), it.value());
                else
                    settings.remove(it.key());
        });
        MainWindow window;
        window.resize(1600, 1000);
        window.show();
        auto panes = [&]() {
            QList<QTabWidget*> result;
            for (auto* pane : window.findChildren<QTabWidget*>("terminalTabs"))
                if (!pane->isHidden())
                    result.append(pane);
            return result;
        };
        Session session;
        session.type = SessionType::Local;
#ifdef Q_OS_WIN
        session.shellPath = "cmd.exe";
#else
        session.shellPath = "/bin/sh";
#endif
        for (int i = 0; i < 6; ++i) {
            session.name = QString("Free split %1").arg(i);
            QVERIFY(QMetaObject::invokeMethod(&window, "onConnectSession", Q_ARG(Session, session)));
        }
        const auto terminals = window.findChildren<TerminalTab*>();
        auto* lateral = window.findChild<QAction*>("shortcut_toggleSplit");
        auto* horizontal = window.findChild<QAction*>("splitPaneVertically");
        auto* equalize = window.findChild<QAction*>("equalizePaneSizes");
        QVERIFY(lateral && horizontal && equalize);
        lateral->trigger();
        lateral->trigger();
        QCoreApplication::processEvents();
        QCOMPARE(panes().size(), 3);
        auto* root = window.findChild<QSplitter*>("terminalPaneRoot");
        QCOMPARE(root->count(), 3);
        QCOMPARE(root->orientation(), Qt::Horizontal);
        for (auto* pane : panes())
            QVERIFY(pane->currentWidget());
        QTest::qWait(50);
        const int before = root->sizes()[0];
        auto* handle = root->handle(1);
        const QPoint center = handle->rect().center();
        const QPoint global = handle->mapToGlobal(center);
        QMouseEvent press(QEvent::MouseButtonPress, QPointF(center), QPointF(global), Qt::LeftButton, Qt::LeftButton,
                          Qt::NoModifier);
        QApplication::sendEvent(handle, &press);
        // Send a drag with the left button held explicitly. The offscreen
        // backend does not synthesize native pointer motion from QCursor.
        QMouseEvent drag(QEvent::MouseMove, QPointF(center + QPoint(80, 0)), QPointF(global + QPoint(80, 0)),
                         Qt::NoButton, Qt::LeftButton, Qt::NoModifier);
        QApplication::sendEvent(handle, &drag);
        QMouseEvent release(QEvent::MouseButtonRelease, QPointF(handle->mapFromGlobal(global + QPoint(80, 0))),
                            QPointF(global + QPoint(80, 0)), Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
        QApplication::sendEvent(handle, &release);
        QVERIFY(qAbs(root->sizes()[0] - before) > 20);
        equalize->trigger();
        QCoreApplication::processEvents();
        const auto equal = root->sizes();
        QVERIFY(qAbs(equal[0] - equal[1]) <= 2);
        QVERIFY(qAbs(equal[1] - equal[2]) <= 2);
        horizontal->trigger();
        horizontal->trigger();
        lateral->trigger();
        QCoreApplication::processEvents();
        QCOMPARE(panes().size(), 6);
        for (auto* pane : panes())
            QCOMPARE(pane->count(), 1);
        for (auto* terminal : terminals)
            QVERIFY(terminal->isVisible());
        QCOMPARE(window.findChildren<TerminalTab*>().size(), 6);
        bool vertical = false;
        for (auto* split : root->findChildren<QSplitter*>())
            vertical |= split->orientation() == Qt::Vertical && split->count() >= 2;
        QVERIFY(vertical);
    }

    void testGridDistributesAllTabsWithTransparency() {
        QSettings settings;
        const QStringList keys = {"window/paneTree", "window/openTabs", "window/tabLayoutMode",
                                  "terminal/backgroundOpacity", "terminal/backgroundBlur"};
        QMap<QString, QVariant> previous;
        for (const auto& key : keys) {
            previous[key] = settings.value(key);
            settings.remove(key);
        }
        const auto restore = qScopeGuard([&]() {
            for (auto it = previous.cbegin(); it != previous.cend(); ++it)
                if (it.value().isValid())
                    settings.setValue(it.key(), it.value());
                else
                    settings.remove(it.key());
        });
        settings.setValue("window/openTabs", QByteArray("[]"));
        settings.setValue("terminal/backgroundOpacity", 50);
        settings.setValue("terminal/backgroundBlur", true);
        MainWindow window;
        window.resize(1600, 1000);
        window.show();
        QTest::qWait(50);
        QVERIFY(window.testAttribute(Qt::WA_TranslucentBackground));
        auto* welcome = window.findChild<QWidget*>("welcomeScreen");
        QVERIFY(welcome && welcome->isVisible());
        const auto welcomeImage = window.grab().toImage();
        const auto welcomePixel = welcomeImage.pixelColor(welcome->mapTo(&window, QPoint(10, 10)));
        QCOMPARE(welcomePixel.alpha(), 255);
        Session session;
        session.type = SessionType::Local;
#ifdef Q_OS_WIN
        session.shellPath = "cmd.exe";
#else
        session.shellPath = "/bin/sh";
#endif
        for (int i = 0; i < 7; ++i) {
            session.name = QString("Grid session %1").arg(i);
            QVERIFY(QMetaObject::invokeMethod(&window, "onConnectSession", Q_ARG(Session, session)));
        }
        const auto originalTabs = window.findChildren<TerminalTab*>();
        QCOMPARE(originalTabs.size(), 7);
        auto verifyAlpha = [&]() {
            const auto image = window.grab().toImage();
            for (auto* tab : originalTabs) {
                if (!tab->isVisible())
                    continue;
                auto* display = tab->getTerminalWidget()->findChild<Konsole::TerminalDisplay*>();
                if (!display || display->height() < 10)
                    return false;
                const auto pixel =
                    image.pixelColor(display->mapTo(&window, QPoint(display->width() / 2, display->height() - 5)));
                if (pixel.alpha() == 0 || pixel.alpha() == 255)
                    return false;
            }
            return true;
        };
        QTest::qWait(50);
        QVERIFY(verifyAlpha());
        QVERIFY(QMetaObject::invokeMethod(&window, "splitPaneHorizontally"));
        QTest::qWait(50);
        QVERIFY(verifyAlpha());
        QVERIFY(QMetaObject::invokeMethod(&window, "toggleGridView"));
        QTest::qWait(50);
        int panes = 0;
        for (auto* pane : window.findChildren<QTabWidget*>("terminalTabs")) {
            if (pane->isHidden())
                continue;
            ++panes;
            QCOMPARE(pane->count(), 1);
        }
        QCOMPARE(panes, 7);
        for (auto* tab : originalTabs)
            QVERIFY(tab->isVisible());
        QVERIFY(verifyAlpha());
        // A second click arranges newly opened tabs too, rather than collapsing the grid.
        session.name = "Eighth session";
        QVERIFY(QMetaObject::invokeMethod(&window, "onConnectSession", Q_ARG(Session, session)));
        QVERIFY(QMetaObject::invokeMethod(&window, "toggleGridView"));
        QTest::qWait(50);
        panes = 0;
        for (auto* pane : window.findChildren<QTabWidget*>("terminalTabs"))
            if (!pane->isHidden()) {
                ++panes;
                QCOMPARE(pane->count(), 1);
            }
        QCOMPARE(panes, 8);
        QCOMPARE(window.findChildren<TerminalTab*>().size(), 8);
        QVERIFY(verifyAlpha());
        QVERIFY(settings.value("terminal/backgroundBlur").toBool());
        settings.setValue("terminal/backgroundOpacity", 100);
        TerminalAppearance::refreshWindow(&window);
        for (auto* tab : originalTabs)
            QVERIFY(tab->getTerminalWidget()->findChild<Konsole::TerminalDisplay*>()->testAttribute(
                Qt::WA_OpaquePaintEvent));
    }

    void testPanelGroupsAndRestoration() {
        QSettings settings;
        const auto previous = settings.value("panels/groups");
        const auto oldTree = settings.value("window/paneTree");
        const auto oldWorkspaces = settings.value("window/panelWorkspaces");
        settings.remove("window/panelWorkspaces");
        const auto oldMode = settings.value("window/tabLayoutMode");
        const auto oldTabs = settings.value("window/openTabs");
        const auto restore = qScopeGuard([&]() {
            settings.setValue("panels/groups", previous);
            settings.setValue("window/paneTree", oldTree);
            settings.setValue("window/panelWorkspaces", oldWorkspaces);
            settings.setValue("window/tabLayoutMode", oldMode);
            settings.setValue("window/openTabs", oldTabs);
        });
        settings.remove("panels/groups");
        settings.remove("window/paneTree");
        settings.setValue("window/tabLayoutMode", 0);
        settings.setValue("window/openTabs", QByteArray("[]"));
        {
            MainWindow window;
            window.show();
            Session session;
            session.type = SessionType::Local;
            session.name = "Saved group session";
#ifdef Q_OS_WIN
            session.shellPath = "cmd.exe";
#else
            session.shellPath = "/bin/sh";
#endif
            QVERIFY(QMetaObject::invokeMethod(&window, "onConnectSession", Q_ARG(Session, session)));
            QVERIFY(QMetaObject::invokeMethod(&window, "splitPaneVertically"));
            QTimer::singleShot(0, &window, [&]() {
                auto* dialog = qobject_cast<QDialog*>(QApplication::activeModalWidget());
                QVERIFY(dialog);
                dialog->findChild<QLineEdit*>("panelGroupName")->setText("Databases");
                dialog->findChild<QPushButton*>("savePanelGroup")->click();
                QCOMPARE(dialog->findChild<QListWidget*>("panelGroupsList")->count(), 1);
                dialog->accept();
            });
            QVERIFY(QMetaObject::invokeMethod(&window, "managePanelGroups"));
            const auto groups = QJsonDocument::fromJson(settings.value("panels/groups").toByteArray()).object();
            QCOMPARE(groups.value("Databases").toObject().value("tabs").toArray().size(), 1);
            QTimer::singleShot(0, &window, [&]() {
                auto* dialog = qobject_cast<QDialog*>(QApplication::activeModalWidget());
                QVERIFY(dialog);
                dialog->findChild<QListWidget*>("panelGroupsList")->setCurrentRow(0);
                dialog->findChild<QPushButton*>("openPanelGroup")->click();
            });
            QVERIFY(QMetaObject::invokeMethod(&window, "managePanelGroups"));
            QCOMPARE(window.findChildren<TerminalTab*>().size(), 2);
            int leaves = 0;
            for (auto* pane : window.findChildren<QTabWidget*>("terminalTabs"))
                leaves += pane->isVisible();
            QCOMPARE(leaves, 2); // Only the selected group is visible.
            QTimer::singleShot(0, &window, []() {
                if (auto* box = qobject_cast<QMessageBox*>(QApplication::activeModalWidget()))
                    box->done(QMessageBox::Yes);
            });
            QVERIFY(window.close());
            QVERIFY(!settings.value("window/paneTree").toByteArray().isEmpty());
        }
        MainWindow restored;
        restored.show();
        QCOMPARE(restored.findChildren<TerminalTab*>().size(), 2);
        int leaves = 0;
        for (auto* pane : restored.findChildren<QTabWidget*>("terminalTabs"))
            leaves += pane->isVisible();
        QCOMPARE(leaves, 2);
    }

    void testLivePanelGroups() {
        QSettings settings;
        const QStringList keys = {"panels/groups",   "window/panelWorkspaces", "window/activePanelGroup",
                                  "window/openTabs", "window/paneTree",        "window/tabLayoutMode",
                                  "theme/mode"};
        QMap<QString, QVariant> previous;
        for (const auto& key : keys) {
            previous[key] = settings.value(key);
            settings.remove(key);
        }
        settings.setValue("window/openTabs", QByteArray("[]"));
        settings.setValue("theme/mode", "dark");
        const auto restore = qScopeGuard([&]() {
            for (auto it = previous.cbegin(); it != previous.cend(); ++it)
                if (it.value().isValid())
                    settings.setValue(it.key(), it.value());
                else
                    settings.remove(it.key());
        });
        {
            MainWindow window;
            window.resize(1500, 950);
            window.show();
            Session profile;
            profile.type = SessionType::Local;
            profile.group = "Saved-session folder";
#ifdef Q_OS_WIN
            profile.shellPath = "cmd.exe";
            profile.fontFamily = "Consolas";
#else
            profile.shellPath = "/bin/sh";
#endif
            for (int i = 0; i < 3; ++i) {
                profile.name = QString("Database %1").arg(i + 1);
                QVERIFY(QMetaObject::invokeMethod(&window, "onConnectSession", Q_ARG(Session, profile)));
            }
            const auto original = window.findChildren<TerminalTab*>();
            QList<QPointer<TerminalTab>> tracked;
            for (auto* tab : original)
                tracked.append(tab);
            QVERIFY(QMetaObject::invokeMethod(&window, "toggleGridView"));
            QTimer::singleShot(0, &window, [&]() {
                auto* dialog = qobject_cast<QDialog*>(QApplication::activeModalWidget());
                QVERIFY(dialog);
                QCOMPARE(dialog->objectName(), QString("createPanelGroupDialog"));
                dialog->findChild<QLineEdit*>("newPanelGroupName")->setText("Databases");
                auto* sessions = dialog->findChild<QListWidget*>("newPanelGroupSessions");
                QCOMPARE(sessions->count(), 3);
                sessions->item(2)->setCheckState(Qt::Unchecked);
                dialog->findChild<QPushButton*>("createPanelGroupSave")->click();
            });
            window.findChild<QAction*>("newPanelGroup")->trigger();
            QCoreApplication::processEvents();
            auto* groups = window.findChild<QTabBar*>("panelGroupTabBar");
            QVERIFY(groups);
            QCOMPARE(groups->count(), 2);
            QCOMPARE(groups->currentIndex(), 1);
            QCOMPARE(groups->tabText(1), QString("Databases"));
            QVERIFY(groups->isVisible());
            QCOMPARE(window.findChildren<TerminalTab*>().size(), 3);
            QVERIFY(original[0]->isVisible() && original[1]->isVisible());
            QVERIFY(!original[2]->isVisible());
            for (auto* tab : original)
                QCOMPARE(tab->session().group, profile.group);
            const auto saved = QJsonDocument::fromJson(settings.value("panels/groups").toByteArray()).object();
            QCOMPARE(saved.value("Databases").toObject().value("tabs").toArray().size(), 2);
            QTimer::singleShot(0, &window, []() {
                auto* dialog = qobject_cast<QDialog*>(QApplication::activeModalWidget());
                QVERIFY(dialog);
                QCOMPARE(dialog->findChild<QListWidget*>()->count(), 2);
                dialog->accept();
            });
            auto* synchronize = window.findChild<QAction*>("synchronizePanels");
            synchronize->trigger();
            QVERIFY(synchronize->isChecked());
            groups->setCurrentIndex(0);
            QCoreApplication::processEvents();
            QVERIFY(!synchronize->isChecked()); // Hidden groups must not receive synchronized typing.
            QVERIFY(original[2]->isVisible());
            QVERIFY(!original[0]->isVisible() && !original[1]->isVisible());
            auto openSaved = [&]() {
                auto* menu = window.findChild<QAction*>("openSavedPanelGroup")->menu();
                for (auto* action : menu->actions())
                    if (action->text() == "Databases") {
                        action->trigger();
                        return true;
                    }
                return false;
            };
            QVERIFY(openSaved());
            QCOMPARE(groups->currentIndex(), 1);
            QCOMPARE(groups->count(), 2);
            QCOMPARE(window.findChildren<TerminalTab*>().size(), 3); // Reuse the open group, not duplicate connections.
            profile.name = "Database 4";
            QVERIFY(QMetaObject::invokeMethod(&window, "onConnectSession", Q_ARG(Session, profile)));
            QVERIFY(QMetaObject::invokeMethod(&window, "saveActivePanelGroup"));
            QCOMPARE(QJsonDocument::fromJson(settings.value("panels/groups").toByteArray())
                         .object()
                         .value("Databases")
                         .toObject()
                         .value("tabs")
                         .toArray()
                         .size(),
                     3);
            TerminalTab* extra = nullptr;
            for (auto* tab : window.findChildren<TerminalTab*>())
                if (tab->session().name == "Database 4")
                    extra = tab;
            QVERIFY(extra);
            const QString owner = extra->property("panelGroupId").toString();
            auto moveToGroup = [&](int index) {
                auto* source = qobject_cast<QTabWidget*>(extra->parentWidget()->parentWidget());
                PaneTabMimeData payload(source, extra);
                const QPoint point = groups->tabRect(index).center();
                QDragEnterEvent enter(point, Qt::MoveAction, &payload, Qt::LeftButton, Qt::NoModifier);
                QApplication::sendEvent(groups, &enter);
                QDropEvent drop(QPointF(point), Qt::MoveAction, &payload, Qt::LeftButton, Qt::NoModifier);
                QApplication::sendEvent(groups, &drop);
                return enter.isAccepted() && drop.isAccepted();
            };
            QVERIFY(moveToGroup(0));
            QVERIFY(extra->property("panelGroupId").toString() != owner);
            QVERIFY(moveToGroup(1));
            QCOMPARE(extra->property("panelGroupId").toString(), owner);
            window.findChild<QTabWidget*>("ribbonTabs")->setCurrentIndex(1);
            QTest::qWait(100);
            const QString screenshot = qEnvironmentVariable("BANCHO_PANEL_GROUP_SCREENSHOT");
            if (!screenshot.isEmpty())
                QVERIFY(window.grab().save(screenshot));
            groups->setCurrentIndex(0);
            QTimer::singleShot(0, &window, []() {
                if (auto* box = qobject_cast<QMessageBox*>(QApplication::activeModalWidget()))
                    box->done(QMessageBox::Yes);
            });
            QVERIFY(QMetaObject::invokeMethod(&window, "closePanelGroup", Q_ARG(int, 1)));
            QCOMPARE(groups->count(), 1);
            QVERIFY(!groups->isVisible()); // Only the ungrouped workspace remains.
            QVERIFY(!tracked[0] && !tracked[1] && tracked[2]);
            QCOMPARE(window.findChildren<TerminalTab*>().size(), 1);
            QVERIFY(openSaved());
            QCOMPARE(groups->count(), 2);
            QCOMPARE(window.findChildren<TerminalTab*>().size(), 4);
            QTimer::singleShot(0, &window, []() {
                if (auto* box = qobject_cast<QMessageBox*>(QApplication::activeModalWidget()))
                    box->done(QMessageBox::Yes);
            });
            QVERIFY(window.close());
        }
        MainWindow restored;
        restored.show();
        auto* groups = restored.findChild<QTabBar*>("panelGroupTabBar");
        QCOMPARE(groups->count(), 2);
        QCOMPARE(groups->currentIndex(), 1);
        QCOMPARE(groups->tabText(1), QString("Databases"));
        QCOMPARE(restored.findChildren<TerminalTab*>().size(), 4);
        groups->setCurrentIndex(0);
        QCoreApplication::processEvents();
        int visible = 0;
        for (auto* tab : restored.findChildren<TerminalTab*>())
            visible += tab->isVisible();
        QCOMPARE(visible, 1);
    }

    void testGroupFromAllOpenTabs() {
        QSettings settings;
        const QStringList keys = {"panels/groups", "window/panelWorkspaces", "window/openTabs", "window/paneTree",
                                  "window/tabLayoutMode"};
        QMap<QString, QVariant> previous;
        for (const auto& key : keys) {
            previous[key] = settings.value(key);
            settings.remove(key);
        }
        settings.setValue("window/openTabs", QByteArray("[]"));
        const auto restore = qScopeGuard([&]() {
            for (auto it = previous.cbegin(); it != previous.cend(); ++it)
                if (it.value().isValid())
                    settings.setValue(it.key(), it.value());
                else
                    settings.remove(it.key());
        });
        MainWindow window;
        window.show();
        Session profile;
        profile.type = SessionType::Local;
#ifdef Q_OS_WIN
        profile.shellPath = "cmd.exe";
#else
        profile.shellPath = "/bin/sh";
#endif
        for (int i = 0; i < 2; ++i) {
            profile.name = QString("Home %1").arg(i);
            QVERIFY(QMetaObject::invokeMethod(&window, "onConnectSession", Q_ARG(Session, profile)));
        }
        const auto original = window.findChildren<TerminalTab*>();
        QTimer::singleShot(0, &window, []() {
            auto* dialog = qobject_cast<QDialog*>(QApplication::activeModalWidget());
            QVERIFY(dialog);
            dialog->findChild<QLineEdit*>("newPanelGroupName")->setText("Home");
            dialog->findChild<QPushButton*>("createPanelGroupSave")->click();
        });
        window.findChild<QAction*>("newPanelGroup")->trigger();
        QCoreApplication::processEvents();
        auto* groups = window.findChild<QTabBar*>("panelGroupTabBar");
        QCOMPARE(groups->count(), 1);
        QCOMPARE(groups->tabText(0), QString("Home"));
        QVERIFY(groups->isVisible());
        QCOMPARE(window.findChildren<TerminalTab*>().size(), 2);
        for (auto* tab : original)
            QVERIFY(window.findChildren<TerminalTab*>().contains(tab));
        QTimer::singleShot(0, &window, []() {
            auto* dialog = qobject_cast<QDialog*>(QApplication::activeModalWidget());
            QVERIFY(dialog);
            auto* list = dialog->findChild<QListWidget*>("panelGroupsList");
            list->setCurrentRow(0);
            QCOMPARE(dialog->findChild<QListWidget*>("savedPanelGroupSessions")->count(), 2);
            dialog->findChild<QLineEdit*>("panelGroupName")->setText("Casa");
            dialog->findChild<QPushButton*>("renamePanelGroup")->click();
            dialog->accept();
        });
        QVERIFY(QMetaObject::invokeMethod(&window, "managePanelGroups"));
        QCOMPARE(groups->tabText(0), QString("Casa"));
        auto saved = QJsonDocument::fromJson(settings.value("panels/groups").toByteArray()).object();
        QVERIFY(saved.contains("Casa") && !saved.contains("Home"));
        QTimer::singleShot(0, &window, []() {
            auto* dialog = qobject_cast<QDialog*>(QApplication::activeModalWidget());
            QVERIFY(dialog);
            dialog->findChild<QListWidget*>("panelGroupsList")->setCurrentRow(0);
            dialog->findChild<QPushButton*>("deletePanelGroup")->click();
            dialog->accept();
        });
        QVERIFY(QMetaObject::invokeMethod(&window, "managePanelGroups"));
        QVERIFY(QJsonDocument::fromJson(settings.value("panels/groups").toByteArray()).object().isEmpty());
        QCOMPARE(window.findChildren<TerminalTab*>().size(), 2); // Deleting a saved definition keeps the live group.
    }

    void testPaneTabDropsAndSinglePane() {
        QSettings settings;
        const auto oldTabs = settings.value("window/openTabs");
        const auto oldTree = settings.value("window/paneTree");
        const auto oldMode = settings.value("window/tabLayoutMode");
        settings.setValue("window/openTabs", QByteArray("[]"));
        settings.remove("window/paneTree");
        settings.setValue("window/tabLayoutMode", 0);
        const auto restore = qScopeGuard([&]() {
            settings.setValue("window/openTabs", oldTabs);
            settings.setValue("window/paneTree", oldTree);
            settings.setValue("window/tabLayoutMode", oldMode);
        });
        MainWindow window;
        window.resize(1400, 900);
        window.show();
        Session session;
        session.type = SessionType::Local;
#ifdef Q_OS_WIN
        session.shellPath = "cmd.exe";
#else
        session.shellPath = "/bin/sh";
#endif
        for (int i = 0; i < 3; ++i) {
            session.name = QString("Drag session %1").arg(i);
            QVERIFY(QMetaObject::invokeMethod(&window, "onConnectSession", Q_ARG(Session, session)));
        }
        const auto tabs = window.findChildren<TerminalTab*>();
        for (int i = 0; i < 3; ++i)
            QVERIFY(QMetaObject::invokeMethod(&window, "splitPaneHorizontally"));
        QCoreApplication::processEvents();
        QTabWidget* empty = nullptr;
        QTabWidget* source = nullptr;
        QTabWidget* occupied = nullptr;
        auto* tab = tabs.first();
        for (auto* pane : window.findChildren<QTabWidget*>("terminalTabs")) {
            if (pane->isHidden())
                continue;
            if (pane->count() == 0)
                empty = pane;
            else if (pane->indexOf(tab) >= 0)
                source = pane;
            else
                occupied = pane;
        }
        QVERIFY(empty && source && occupied);
        const QString tooltip = source->tabToolTip(source->indexOf(tab));
        auto drop = [&](QTabWidget* from, QWidget* target) {
            PaneTabMimeData payload(from, tab);
            // Native drag backends can wrap MIME data; the live token must survive that transport.
            QMimeData transport;
            transport.setData("application/x-banchoxterm-pane-tab", payload.data("application/x-banchoxterm-pane-tab"));
            const QPoint point = target->rect().center();
            QDragEnterEvent enter(point, Qt::MoveAction, &transport, Qt::LeftButton, Qt::NoModifier);
            QApplication::sendEvent(target, &enter);
            if (!enter.isAccepted())
                return false;
            QDropEvent event(QPointF(point), Qt::MoveAction, &transport, Qt::LeftButton, Qt::NoModifier);
            QApplication::sendEvent(target, &event);
            return event.isAccepted() && event.dropAction() == Qt::MoveAction;
        };
        QVERIFY(drop(source, empty));
        QCOMPARE(source->count(), 0);
        QCOMPARE(empty->currentWidget(), tab);
        QCOMPARE(empty->tabToolTip(0), tooltip);
        // Dropping over terminal content must also work, not only over a tab bar.
        auto* content = qobject_cast<TerminalTab*>(occupied->currentWidget());
        QVERIFY(content);
        QVERIFY(drop(empty, content->getTerminalWidget()->findChild<Konsole::TerminalDisplay*>()));
        QCOMPARE(empty->count(), 0);
        QCOMPARE(occupied->currentWidget(), tab);
        QCOMPARE(occupied->count(), 2);
        // A foreign MIME payload cannot impersonate one of our in-process tabs.
        QMimeData foreign;
        foreign.setData("application/x-banchoxterm-pane-tab", "1");
        QDragEnterEvent invalid(QPoint(10, 10), Qt::MoveAction, &foreign, Qt::LeftButton, Qt::NoModifier);
        QApplication::sendEvent(empty, &invalid);
        QVERIFY(!invalid.isAccepted());
        QVERIFY(QMetaObject::invokeMethod(&window, "detachCurrentTab"));
        auto* single = window.findChild<QAction*>("restoreSinglePane");
        QVERIFY(single);
        single->trigger();
        QCoreApplication::processEvents();
        int visible = 0;
        for (auto* pane : window.findChildren<QTabWidget*>("terminalTabs"))
            if (!pane->isHidden()) {
                ++visible;
                QCOMPARE(pane->count(), 3);
                for (auto* original : tabs)
                    QVERIFY(pane->indexOf(original) >= 0);
            }
        QCOMPARE(visible, 1);
        QCOMPARE(window.findChildren<TerminalTab*>().size(), 3);
    }

    void testRealtimeSynchronizationSelection() {
        QSettings settings;
        const auto oldTabs = settings.value("window/openTabs");
        settings.setValue("window/openTabs", QByteArray("[]"));
        const auto restore = qScopeGuard([&]() { settings.setValue("window/openTabs", oldTabs); });
        MainWindow window;
        window.show();
        QList<TerminalTab*> tabs;
        for (int i = 0; i < 4; ++i) {
            Session session;
            session.type = SessionType::Local;
            session.name = QString("RT %1").arg(i);
            session.readOnly = i == 3;
#ifdef Q_OS_WIN
            session.shellPath = "cmd.exe";
#else
            session.shellPath = "/bin/sh";
#endif
            QVERIFY(QMetaObject::invokeMethod(&window, "onConnectSession", Q_ARG(Session, session)));
            for (auto* tab : window.findChildren<TerminalTab*>())
                if (!tabs.contains(tab))
                    tabs.append(tab);
        }
        QTest::qWait(200);
        auto* sync = window.findChild<QAction*>("synchronizePanels");
        auto* choose = window.findChild<QAction*>("chooseSynchronizationTargets");
        QVERIFY(sync && choose);
        auto select = [&](int other, bool accept) {
            QTimer::singleShot(0, &window, [&, other, accept]() {
                auto* dialog = qobject_cast<QDialog*>(QApplication::activeModalWidget());
                QVERIFY(dialog);
                auto* list = dialog->findChild<QListWidget*>();
                QCOMPARE(list->count(), 3); // Read-only sessions are excluded.
                for (int i = 0; i < list->count(); ++i)
                    list->item(i)->setCheckState(i == 0 || i == other ? Qt::Checked : Qt::Unchecked);
                if (accept)
                    dialog->accept();
                else
                    dialog->reject();
            });
        };
        select(1, true);
        sync->trigger();
        QVERIFY(sync->isChecked());
        QList<QByteArray> writes(4);
        for (int i = 0; i < tabs.size(); ++i) {
            auto* session = tabs[i]->getTerminalWidget()->findChild<Konsole::Session*>();
            QVERIFY(session);
            connect(session->emulation(), &Konsole::Emulation::sendData, &window,
                    [&, i](const char* data, int size) { writes[i].append(data, size); });
        }
        const auto disconnectWrites = qScopeGuard([&]() {
            for (auto* tab : tabs)
                disconnect(tab->getTerminalWidget()->findChild<Konsole::Session*>()->emulation(), nullptr, &window,
                           nullptr);
        });
        auto hasBadge = [](TerminalTab* tab) {
            auto* pane = qobject_cast<QTabWidget*>(tab->parentWidget()->parentWidget());
            QWidget* badge = pane->tabBar()->tabButton(pane->indexOf(tab), QTabBar::LeftSide);
            return badge && badge->objectName() == "synchronizedTabBadge";
        };
        QVERIFY(hasBadge(tabs[0]) && hasBadge(tabs[1]));
        QVERIFY(!hasBadge(tabs[2]) && !hasBadge(tabs[3]));
        auto* pane = qobject_cast<QTabWidget*>(tabs[0]->parentWidget()->parentWidget());
        QVERIFY(pane);
        pane->setCurrentWidget(tabs[0]);
        auto* display = tabs[0]->getTerminalWidget()->findChild<Konsole::TerminalDisplay*>();
        QTest::keyClicks(display, "xy"); // No Enter: synchronization is per keystroke.
        QCOMPARE(writes[0], QByteArray("xy"));
        QCOMPARE(writes[1], QByteArray("xy"));
        QVERIFY(writes[2].isEmpty() && writes[3].isEmpty());
        QApplication::clipboard()->setText("pasted");
        tabs[0]->pasteSelection();
        QCOMPARE(writes[0], QByteArray("xypasted"));
        QCOMPARE(writes[1], writes[0]);
        select(2, false);
        choose->trigger();
        QTest::keyClicks(display, "a");
        QCOMPARE(writes[1], writes[0]); // Cancelling preserves the previous selection.
        QVERIFY(writes[2].isEmpty());
        select(2, true);
        choose->trigger();
        QVERIFY(hasBadge(tabs[0]) && hasBadge(tabs[2]));
        QVERIFY(!hasBadge(tabs[1]));
        // Selection follows live sessions when they are rearranged or merged.
        QVERIFY(QMetaObject::invokeMethod(&window, "toggleGridView"));
        QCoreApplication::processEvents();
        window.findChild<QAction*>("restoreSinglePane")->trigger();
        QCoreApplication::processEvents();
        QVERIFY(hasBadge(tabs[0]) && hasBadge(tabs[2]));
        QVERIFY(!hasBadge(tabs[1]));
        for (auto& write : writes)
            write.clear();
        QTest::keyClicks(display, "z");
        QCOMPARE(writes[0], QByteArray("z"));
        QVERIFY(writes[1].isEmpty());
        QCOMPARE(writes[2], QByteArray("z"));
        QVERIFY(writes[3].isEmpty());
        sync->trigger();
        for (auto* tab : tabs)
            QVERIFY(!hasBadge(tab));
        for (auto& write : writes)
            write.clear();
        QTest::keyClicks(display, "q");
        QCOMPARE(writes[0], QByteArray("q"));
        QVERIFY(writes[1].isEmpty() && writes[2].isEmpty() && writes[3].isEmpty());
    }

    void testSynchronizedPanelInput() {
#ifdef Q_OS_WIN
        QSKIP("Uses /bin/sh to verify shell input end to end.");
#else
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        QSettings settings;
        const auto oldTabs = settings.value("window/openTabs");
        settings.setValue("window/openTabs", QByteArray("[]"));
        const auto restore = qScopeGuard([&]() { settings.setValue("window/openTabs", oldTabs); });
        MainWindow window;
        window.show();
        QList<TerminalTab*> tabs;
        for (int i = 0; i < 3; ++i) {
            Session session;
            session.type = SessionType::Local;
            session.name = QString("Sync %1").arg(i);
            session.shellPath = "/bin/sh";
            QVERIFY(QMetaObject::invokeMethod(&window, "onConnectSession", Q_ARG(Session, session)));
            for (auto* tab : window.findChildren<TerminalTab*>())
                if (!tabs.contains(tab))
                    tabs.append(tab);
        }
        QTRY_VERIFY(tabs[0]->isSessionActive() && tabs[1]->isSessionActive() && tabs[2]->isSessionActive());
        auto* sync = window.findChild<QAction*>("synchronizePanels");
        QVERIFY(sync);
        QTimer::singleShot(0, &window, [&]() {
            auto* dialog = qobject_cast<QDialog*>(QApplication::activeModalWidget());
            QVERIFY(dialog);
            auto* list = dialog->findChild<QListWidget*>();
            QCOMPARE(list->count(), 3);
            for (int i = 0; i < list->count(); ++i)
                list->item(i)->setCheckState(i < 2 ? Qt::Checked : Qt::Unchecked);
            dialog->accept();
        });
        sync->trigger();
        QVERIFY(sync->isChecked());
        auto* pane = qobject_cast<QTabWidget*>(tabs[0]->parentWidget()->parentWidget());
        QVERIFY(pane);
        pane->setCurrentWidget(tabs[0]);
        auto* display = tabs[0]->getTerminalWidget()->findChild<Konsole::TerminalDisplay*>();
        QVERIFY(display);
        const QString path = directory.filePath("broadcast.txt");
        const QString command = QString("printf x >> '%1'").arg(path);
        QTest::keyClicks(display, command);
        QTest::keyClick(display, Qt::Key_Return);
        auto contents = [&]() {
            QFile file(path);
            return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
        };
        QTRY_COMPARE_WITH_TIMEOUT(contents(), QByteArray("xx"), 3000);
        QApplication::clipboard()->setText(command);
        tabs[0]->pasteSelection();
        QTest::keyClick(display, Qt::Key_Return);
        QTRY_COMPARE_WITH_TIMEOUT(contents(), QByteArray("xxxx"), 3000);
        sync->trigger();
        QVERIFY(!sync->isChecked());
        QTest::keyClicks(display, command);
        QTest::keyClick(display, Qt::Key_Return);
        QTRY_COMPARE_WITH_TIMEOUT(contents(), QByteArray("xxxxx"), 3000);
#endif
    }

    void testMainWindowStartupShutdown() {
        QTemporaryDir configDir;
        QVERIFY(configDir.isValid());

        QProcess application;
        QProcessEnvironment environment = QProcessEnvironment::systemEnvironment();
        environment.insert(QStringLiteral("QT_QPA_PLATFORM"), QStringLiteral("offscreen"));
        environment.insert(QStringLiteral("BANCHO_CONFIG_DIR"), configDir.path());
        application.setProcessEnvironment(environment);
        application.setProgram(QDir(QCoreApplication::applicationDirPath()).filePath(QStringLiteral("banchoxterm")));
        application.start();
        QVERIFY2(application.waitForStarted(3000), qPrintable(application.errorString()));
        QTest::qWait(300);
        QVERIFY(application.state() == QProcess::Running);

#ifdef Q_OS_WIN
        // Offscreen windows cannot be relied on to handle WM_CLOSE from
        // QProcess::terminate(). This smoke test only checks process startup.
        application.kill();
#else
        application.terminate();
#endif
        QVERIFY(application.waitForFinished(3000));
    }

    void testWelcomeScreenLayout() {
        MainWindow window;
        window.resize(1280, 800);
        window.show();
        QTest::qWait(100);

        auto* welcome = window.findChild<QWidget*>(QStringLiteral("welcomeScreen"));
        QVERIFY(welcome != nullptr);
        QVERIFY(welcome->isVisible());
        const auto cards = welcome->findChildren<QFrame*>(QStringLiteral("welcomeCard"));
        QCOMPARE(cards.size(), 2);
        QVERIFY(cards.at(0)->geometry().right() < cards.at(1)->geometry().left());
        QVERIFY(welcome->findChild<QPushButton*>(QStringLiteral("welcomeSecondaryButton")) != nullptr);
        QVERIFY(welcome->findChild<QPushButton*>(QStringLiteral("welcomeLinkButton")) != nullptr);

        const QString screenshotPath = qEnvironmentVariable("BANCHO_WELCOME_SCREENSHOT");
        if (!screenshotPath.isEmpty())
            QVERIFY(window.grab().save(screenshotPath));

        auto* local = welcome->findChild<QPushButton*>("welcomeSecondaryButton");
        auto verifyHitTarget = [&]() {
            QWidget* hit = QApplication::widgetAt(local->mapToGlobal(local->rect().center()));
            return hit == local || (hit && local->isAncestorOf(hit));
        };
        QVERIFY(verifyHitTarget());
        // Rebuilding an empty layout must not put its splitter over the welcome actions.
        QVERIFY(QMetaObject::invokeMethod(&window, "toggleGridView"));
        QCoreApplication::processEvents();
        QVERIFY(verifyHitTarget());
        QTest::mouseClick(window.windowHandle(), Qt::LeftButton, Qt::NoModifier,
                          local->mapTo(&window, local->rect().center()));
        QTRY_VERIFY(!welcome->isVisible());
        QCOMPARE(window.findChildren<TerminalTab*>().size(), 1);
    }

    void testWelcomeViewAllSessions() {
#ifdef Q_OS_WIN
        QSKIP("The saved local session in this test uses /bin/sh.");
#else
        const auto originalSessions = SessionManager::loadSessions();
        QSettings settings;
        const QVariant oldRecent = settings.value("sessions/recent");
        const auto restore = qScopeGuard([&]() {
            SessionManager::saveSessions(originalSessions);
            settings.setValue("sessions/recent", oldRecent);
        });
        Session recent;
        recent.id = "view-all-recent";
        recent.name = "Recent local session";
        recent.type = SessionType::Local;
        recent.shellPath = "/bin/sh";
        recent.group = "Test group";
        Session other = recent;
        other.id = "view-all-other";
        other.name = "Other saved session";
        other.group.clear();
        Session added = other;
        added.id = "view-all-added";
        added.name = "Newly added session";
        SessionManager::saveSessions({recent, other});
        settings.setValue("sessions/recent", QStringList{recent.name});
        MainWindow window;
        window.show();
        auto* sidebar = window.findChild<SessionsSidebar*>();
        QVERIFY(sidebar);
        auto* welcome = window.findChild<QWidget*>("welcomeScreen");
        QVERIFY(welcome);
        auto* viewAll = welcome->findChild<QPushButton*>("welcomeLinkButton");
        QVERIFY(viewAll);
        QTimer::singleShot(0, &window, [&]() {
            auto* dialog = qobject_cast<QDialog*>(QApplication::activeModalWidget());
            QVERIFY(dialog);
            const auto close = qScopeGuard([&]() { dialog->reject(); });
            QCOMPARE(dialog->objectName(), QString("allSessionsDialog"));
            auto* manager = dialog->findChild<SessionsSidebar*>();
            QVERIFY(manager);
            auto* tree = manager->findChild<QTreeWidget*>("sessionTree");
            QVERIFY(tree);
            QCOMPARE(tree->findItems(recent.name, Qt::MatchExactly | Qt::MatchRecursive).size(), 1);
            QCOMPARE(tree->findItems(other.name, Qt::MatchExactly | Qt::MatchRecursive).size(), 1);
            // Exercise the sidebar refresh after changes to the saved session list.
            SessionManager::saveSessions({recent, other, added});
        });
        viewAll->click();
        auto* sidebarTree = sidebar->findChild<QTreeWidget*>("sessionTree");
        QCOMPARE(sidebarTree->findItems(added.name, Qt::MatchExactly | Qt::MatchRecursive).size(), 1);
        QTimer::singleShot(0, &window, [&]() {
            auto* dialog = qobject_cast<QDialog*>(QApplication::activeModalWidget());
            QVERIFY(dialog);
            auto close = qScopeGuard([&]() { dialog->reject(); });
            auto* manager = dialog->findChild<SessionsSidebar*>();
            auto* tree = manager->findChild<QTreeWidget*>("sessionTree");
            const auto matches = tree->findItems(other.name, Qt::MatchExactly);
            QCOMPARE(matches.size(), 1);
            QVERIFY(QMetaObject::invokeMethod(manager, "onItemDoubleClicked", Qt::DirectConnection,
                                              Q_ARG(QTreeWidgetItem*, matches.first()), Q_ARG(int, 0)));
            close.dismiss();
        });
        viewAll->click();
        auto* terminal = window.findChild<TerminalTab*>();
        QVERIFY(terminal);
        QCOMPARE(terminal->session().id, other.id);
#endif
    }

    void testMainWindowTabLifecycle() {
#ifdef Q_OS_WIN
        QSKIP("The local process lifecycle test uses the Unix /bin/true command.");
#endif
        MainWindow window;
        window.show();
        QTest::qWait(100);

        Session session;
        session.id = QStringLiteral("mainwindow-tab-lifecycle");
        session.name = QStringLiteral("Short-lived local tab");
        session.type = SessionType::Local;
        session.shellPath = QStringLiteral("/bin/true");
        QVERIFY(QMetaObject::invokeMethod(&window, "onConnectSession", Qt::DirectConnection, Q_ARG(Session, session)));

        auto* tab = window.findChild<TerminalTab*>();
        QVERIFY(tab != nullptr);
        QTRY_VERIFY_WITH_TIMEOUT(!tab->isSessionActive(), 3000);

        QTabWidget* pane = nullptr;
        for (auto* candidate : window.findChildren<QTabWidget*>()) {
            if (candidate->indexOf(tab) >= 0) {
                pane = candidate;
                break;
            }
        }
        QVERIFY(pane != nullptr);
        const int tabIndex = pane->indexOf(tab);
        QVERIFY(tabIndex >= 0);
        QVERIFY(QMetaObject::invokeMethod(&window, "onTabCloseRequested", Qt::DirectConnection,
                                          Q_ARG(QTabWidget*, pane), Q_ARG(int, tabIndex)));
        QTRY_VERIFY_WITH_TIMEOUT(window.findChild<TerminalTab*>() == nullptr, 3000);
    }

#ifdef BANCHO_HAVE_VNC
    void testVncReconnectRequestAfterFailure() {
        Session session;
        session.id = QStringLiteral("vnc-reconnect-test");
        session.name = QStringLiteral("Unavailable VNC");
        session.type = SessionType::VNC;
        session.host = QStringLiteral("127.0.0.1");
        session.port = 1;
        session.autoReconnect = true;

        MainWindow window;
        window.show();
        QVERIFY(QMetaObject::invokeMethod(&window, "onConnectSession", Qt::DirectConnection, Q_ARG(Session, session)));
        const auto tabs = window.findChildren<TerminalTab*>();
        QCOMPARE(tabs.size(), 1);
        QPointer<TerminalTab> firstTab = tabs.first();
        // Windows may take several seconds to report a refused connection
        // when no VNC server is listening.  Allow the client timeout plus
        // the automatic reconnect delay before declaring the test failed.
        QTRY_VERIFY_WITH_TIMEOUT(firstTab.isNull(), 20000);
        QCOMPARE(window.findChildren<TerminalTab*>().size(), 1);
    }
#endif

#ifndef Q_OS_WIN
    void testRdpReconnectRequestAfterClientFailure() {
#ifndef BANCHO_HAVE_FREERDP
        QTemporaryDir emptyPath;
        QVERIFY(emptyPath.isValid());
        const QByteArray oldPath = qgetenv("PATH");
        qputenv("PATH", emptyPath.path().toUtf8());
#endif

        Session session;
        session.id = QStringLiteral("rdp-reconnect-test");
        session.name = QStringLiteral("Unavailable RDP");
        session.type = SessionType::RDP;
        session.host = QStringLiteral("127.0.0.1");
        session.port = 1;
        session.user = QStringLiteral("test-user");
        session.autoReconnect = true;

#ifdef BANCHO_HAVE_FREERDP
        // The embedded client collects credentials in Qt rather than failing
        // to find xfreerdp in PATH. Answer every reconnect's modal dialog.
        QTimer credentials;
        connect(&credentials, &QTimer::timeout, this, [] {
            auto* dialog = qobject_cast<QDialog*>(QApplication::activeModalWidget());
            if (!dialog || dialog->windowTitle() != QStringLiteral("RDP credentials"))
                return;
            for (auto* edit : dialog->findChildren<QLineEdit*>())
                if (edit->echoMode() == QLineEdit::Password)
                    edit->setText(QStringLiteral("test-password"));
            dialog->accept();
        });
        credentials.start(10);
#endif
        MainWindow window;
        window.show();
        QVERIFY(QMetaObject::invokeMethod(&window, "onConnectSession", Qt::DirectConnection, Q_ARG(Session, session)));
        const auto tabs = window.findChildren<TerminalTab*>();
        QCOMPARE(tabs.size(), 1);
        QPointer<TerminalTab> firstTab = tabs.first();
        QTRY_VERIFY_WITH_TIMEOUT(firstTab.isNull(), 8000);
        QCOMPARE(window.findChildren<TerminalTab*>().size(), 1);
#ifndef BANCHO_HAVE_FREERDP
        qputenv("PATH", oldPath);
#endif
    }
#endif

    void testMasterPasswordEncryption() {
        MasterPasswordManager& mpm = MasterPasswordManager::instance();

        mpm.lock();
        QVERIFY(!mpm.isUnlocked());

        QVERIFY(mpm.setMasterPassword("SuperSecure123!"));
        QVERIFY(mpm.isEnabled());
        QVERIFY(mpm.isUnlocked());

        QString plaintext = "MySshSecretPassword";
        QString encrypted = mpm.encryptPassword(plaintext);
        QVERIFY(encrypted.startsWith("BANCHO2:"));
        QVERIFY(encrypted != plaintext);

        QString decrypted = mpm.decryptPassword(encrypted);
        QCOMPARE(decrypted, plaintext);

        QTemporaryDir exportDir;
        QVERIFY(exportDir.isValid());
        Session encryptedSession;
        encryptedSession.id = QStringLiteral("encrypted-session");
        encryptedSession.name = QStringLiteral("Encrypted server");
        const QString encryptedPath = QDir(exportDir.path()).filePath(QStringLiteral("sessions.bancho.enc"));
        QVERIFY(SessionManager::exportEncryptedSessions({encryptedSession}, encryptedPath));
        QFile encryptedFile(encryptedPath);
        QVERIFY(encryptedFile.open(QIODevice::ReadOnly));
        QVERIFY(encryptedFile.readAll().startsWith("BANCHO2:"));
        bool importOk = false;
        const QList<Session> imported = SessionManager::importSessions(encryptedPath, &importOk);
        QVERIFY(importOk);
        QCOMPARE(imported.size(), 1);
        QCOMPARE(imported.first().name, encryptedSession.name);

        mpm.lock();
        QVERIFY(!mpm.isUnlocked());

        QVERIFY(mpm.unlock("SuperSecure123!"));
        QVERIFY(mpm.isUnlocked());
        QCOMPARE(mpm.decryptPassword(encrypted), plaintext);
        QString tampered = encrypted;
        tampered[tampered.size() - 1] = tampered.at(tampered.size() - 1) == QChar('A') ? QChar('B') : QChar('A');
        QVERIFY(mpm.decryptPassword(tampered).isEmpty());

        QVERIFY(mpm.disableMasterPassword("SuperSecure123!"));
        QVERIFY(!mpm.isEnabled());
    }

    void testMobaXtermImport() {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString path = QDir(dir.path()).filePath(QStringLiteral("bookmarks.mxtsessions"));
        QFile file(path);
        QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Text));
        file.write("[Bookmarks\\Network\\router]\nHostName=router.example\nUserName=admin\nPort=2222\nProtocol=ssh\n");
        file.close();

        bool ok = false;
        const QList<Session> sessions = SessionManager::importSessions(path, &ok);
        QVERIFY(ok);
        QCOMPARE(sessions.size(), 1);
        QCOMPARE(sessions.first().name, QStringLiteral("Network/router"));
        QCOMPARE(sessions.first().host, QStringLiteral("router.example"));
        QCOMPARE(sessions.first().user, QStringLiteral("admin"));
        QCOMPARE(sessions.first().port, 2222);
        QCOMPARE(sessions.first().type, SessionType::SSH);
    }

    void testSecureCrtImport() {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString path = QDir(dir.path()).filePath(QStringLiteral("router.ini"));
        QFile file(path);
        QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Text));
        file.write("S:\"Hostname\"=router.example\nS:\"Username\"=admin\nD:\"Port\"=00000016\n");
        file.close();

        bool ok = false;
        const QList<Session> sessions = SessionManager::importSessions(path, &ok);
        QVERIFY(ok);
        QCOMPARE(sessions.size(), 1);
        QCOMPARE(sessions.first().name, QStringLiteral("router"));
        QCOMPARE(sessions.first().host, QStringLiteral("router.example"));
        QCOMPARE(sessions.first().user, QStringLiteral("admin"));
        QCOMPARE(sessions.first().port, 22);
        QCOMPARE(sessions.first().type, SessionType::SSH);
    }

    void testRoyalTsImport() {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString path = QDir(dir.path()).filePath(QStringLiteral("connections.rtsx"));
        QFile file(path);
        QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Text));
        file.write(R"xml(<?xml version="1.0" encoding="utf-8"?>
<RoyalDocument>
  <RoyalFolder><Name>Network</Name></RoyalFolder>
  <RoyalSSHConnection>
    <ID>123</ID>
    <Name>Router</Name>
    <URI>router.example</URI>
    <Port>2200</Port>
    <CredentialUsername>admin</CredentialUsername>
    <CredentialPassword>must-not-be-imported</CredentialPassword>
  </RoyalSSHConnection>
</RoyalDocument>)xml");
        file.close();

        bool ok = false;
        const QList<Session> sessions = SessionManager::importSessions(path, &ok);
        QVERIFY(ok);
        QCOMPARE(sessions.size(), 1);
        QCOMPARE(sessions.first().name, QStringLiteral("Router"));
        QCOMPARE(sessions.first().host, QStringLiteral("router.example"));
        QCOMPARE(sessions.first().user, QStringLiteral("admin"));
        QCOMPARE(sessions.first().port, 2200);
        QVERIFY(sessions.first().type == SessionType::SSH);
    }

    void testRoyalTsProtocolImport() {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString path = QDir(dir.path()).filePath(QStringLiteral("protocols.rtsx"));
        QFile file(path);
        QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Text));
        file.write(R"xml(<RoyalDocument>
  <RoyalSSHConnection><Name>Telnet device</Name><URI>telnet.example</URI><ConnectionType>telnet</ConnectionType></RoyalSSHConnection>
  <RoyalRDSConnection><Name>Windows host</Name><URI>rdp.example</URI><Port>3390</Port></RoyalRDSConnection>
  <RoyalVNCConnection><Name>Console</Name><URI>vnc.example</URI></RoyalVNCConnection>
</RoyalDocument>)xml");
        file.close();

        bool ok = false;
        const QList<Session> sessions = SessionManager::importSessions(path, &ok);
        QVERIFY(ok);
        QCOMPARE(sessions.size(), 3);
        QCOMPARE(sessions.at(0).type, SessionType::Telnet);
        QCOMPARE(sessions.at(0).port, 22);
        QCOMPARE(sessions.at(1).type, SessionType::RDP);
        QCOMPARE(sessions.at(1).port, 3390);
        QCOMPARE(sessions.at(2).type, SessionType::VNC);
        QCOMPARE(sessions.at(2).port, 5900);
    }

    void testImporterFixtures() {
        const QString fixtureDir = QStringLiteral(BANCHO_SOURCE_DIR) + QStringLiteral("/tests/fixtures/");
        const QList<QPair<QString, QString>> fixtures = {
            {QStringLiteral("openssh.conf"), QStringLiteral("fixture.example")},
            {QStringLiteral("putty.reg"), QStringLiteral("fixture.example")},
            {QStringLiteral("mobaxterm.mxtsessions"), QStringLiteral("fixture.example")},
            {QStringLiteral("securecrt.ini"), QStringLiteral("fixture.example")},
            {QStringLiteral("royalts.rtsx"), QStringLiteral("fixture.example")},
        };
        for (const auto& fixture : fixtures) {
            bool ok = false;
            const QList<Session> sessions =
                SessionManager::importSessions(QDir(fixtureDir).filePath(fixture.first), &ok);
            QVERIFY2(ok, qPrintable(fixture.first));
            QCOMPARE(sessions.size(), 1);
            QCOMPARE(sessions.first().host, fixture.second);
        }
    }

    void testKeyringRoundtrip() {
#ifndef Q_OS_WIN
        if (QStandardPaths::findExecutable(QStringLiteral("secret-tool")).isEmpty())
            QSKIP("secret-tool is not available in this environment");
#endif
        const QString id = QStringLiteral("test-keyring-%1").arg(QUuid::createUuid().toString(QUuid::WithoutBraces));
        const QString password = QStringLiteral("p ässword with spaces");
        if (!Keyring::storePassword(id, password)) {
            Keyring::deletePassword(id);
            QSKIP("The system keyring provider is not available in this environment");
        }
        QCOMPARE(Keyring::lookupPassword(id), password);
        QVERIFY(Keyring::deletePassword(id));
        QVERIFY(Keyring::lookupPassword(id).isEmpty());
    }

    void testVtTerminalWidgetSearch() {
        VtTerminalWidget widget;
        widget.resize(800, 600); // Trigger resize to allocate buffer

        widget.writeData("Hello World\r\nThis is a test line\r\nError: connection failed\r\n");

        QVERIFY(widget.findText("test", true, true));
        QVERIFY(!widget.findText("ERROR", true, true));
        QVERIFY(widget.findText("ERROR", true, false));
    }

    void testSpecialCharacterBytes() {
        Session session;
        session.type = SessionType::SSH;
        session.ciscoBreakSequence = "1E80FF00";
        session.enterSequence = "\r\n";
        session.backspaceSequence = "\b";
        TerminalTab tab(session);
        auto* emulator = tab.getTerminalWidget()->findChild<Konsole::Session*>()->emulation();
        QByteArray received;
        connect(emulator, &Konsole::Emulation::sendData, this,
                [&](const char* bytes, int size) { received.append(bytes, size); });
        const QByteArray controls = QByteArray::fromHex("03041A1B0D097F1D1E0080FF");
        tab.sendBytes(controls);
        QCOMPARE(received, controls);
        received.clear();
        tab.sendTelnetEscape(); // Never send a Telnet client command to an SSH shell.
        QVERIFY(received.isEmpty());
        tab.sendBreak(); // A protocol request must not turn into Ctrl+^ shell input.
        QVERIFY(received.isEmpty());
        tab.sendCiscoEscape();
        QCOMPARE(received, QByteArray::fromHex("1E80FF00"));
        received.clear();
        tab.sendSpecialCharacter("\r");
        tab.sendSpecialCharacter(QByteArrayLiteral("\x7f"));
        QCOMPARE(received, QByteArrayLiteral("\r\n\b"));
        session.readOnly = true;
        TerminalTab locked(session);
        QByteArray blocked;
        connect(locked.getTerminalWidget()->findChild<Konsole::Session*>()->emulation(), &Konsole::Emulation::sendData,
                this, [&](const char* bytes, int size) { blocked.append(bytes, size); });
        locked.sendBytes(controls);
        locked.sendCiscoEscape();
        locked.sendBreak();
        QVERIFY(blocked.isEmpty());
    }

    void testSshBreakPacket() {
        unsigned char packet[19];
        bancho_encode_ssh_break(packet, 0x12345678, 500);
        QCOMPARE(QByteArray(reinterpret_cast<const char*>(packet), 19),
                 QByteArray::fromHex("621234567800000005627265616B00000001F4"));
        QCOMPARE(bancho_ssh_channel_break(nullptr, 500), LIBSSH2_ERROR_BAD_USE);
    }

    void testConPtyControlPunctuation() {
#ifdef Q_OS_WIN
        ConPty console;
        QVERIFY(console.start(QDir(QCoreApplication::applicationDirPath()).filePath("conpty_input_probe.exe"), {}, 80, 25));
        QByteArray output;
        QTRY_VERIFY_WITH_TIMEOUT((output += console.read()).contains("INPUT_READY"), 5000);
        for (int character = 0x1c; character <= 0x1f; ++character) {
            output.clear();
            QVERIFY(console.write(QByteArray(1, char(character))));
            const QByteArray expected = "CHAR=" + QByteArray::number(character) + " CTRL=1 ALT=0";
            QTRY_VERIFY_WITH_TIMEOUT((output += console.read()).contains(expected), 2000);
        }
        output.clear();
        QVERIFY(console.write(QByteArrayLiteral("a\x1d\x1e")));
        QTRY_VERIFY_WITH_TIMEOUT((output += console.read()).contains("CHAR=29 CTRL=1 ALT=0") && output.contains("CHAR=30 CTRL=1 ALT=0"), 2000);
        QVERIFY(console.write("\r"));
#else
        QSKIP("ConPTY is only available on Windows.");
#endif
    }

    void testVtTerminalSpecialKeys() {
        VtTerminalWidget widget;
        widget.resize(800, 600);
        widget.show();
        widget.setFocus();
        QSignalSpy dataSpy(&widget, &VtTerminalWidget::dataReady);

        QTest::keyClick(&widget, Qt::Key_C, Qt::ControlModifier);
        QTest::keyClick(&widget, Qt::Key_Return);
        QTest::keyClick(&widget, Qt::Key_Backspace);
        QTest::keyClick(&widget, Qt::Key_Tab);
        QTest::keyClick(&widget, Qt::Key_Up);

        QCOMPARE(dataSpy.size(), 5);
        QCOMPARE(dataSpy.at(0).at(0).toByteArray(), QByteArray("\x03", 1));
        QCOMPARE(dataSpy.at(1).at(0).toByteArray(), QByteArray("\r", 1));
        QCOMPARE(dataSpy.at(2).at(0).toByteArray(), QByteArray("\x7f", 1));
        QCOMPARE(dataSpy.at(3).at(0).toByteArray(), QByteArray("\t", 1));
        QCOMPARE(dataSpy.at(4).at(0).toByteArray(), QByteArray("\x1b[A", 3));
    }

    void testRestoredTerminalSize() {
#ifdef Q_OS_WIN
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const QString path = directory.filePath("console-size.txt");
        const QByteArray previous = qgetenv("BANCHO_CONSOLE_SIZE_PROBE");
        const auto restore = qScopeGuard([&]() {
            if (previous.isNull())
                qunsetenv("BANCHO_CONSOLE_SIZE_PROBE");
            else
                qputenv("BANCHO_CONSOLE_SIZE_PROBE", previous);
        });
        qputenv("BANCHO_CONSOLE_SIZE_PROBE", path.toLocal8Bit());
        Session session;
        session.type = SessionType::Local;
        session.shellPath = QDir(QCoreApplication::applicationDirPath()).filePath("conpty_input_probe.exe");
        QTabWidget tabs;
        tabs.resize(960, 640);
        auto* tab = new TerminalTab(session);
        tabs.addTab(tab, "Restored");
        tabs.addTab(new QWidget, "Active");
        tabs.setCurrentIndex(1);
        tabs.show();
        QTest::qWait(500);
        auto* terminal = tab->getTerminalWidget();
        QVERIFY(terminal);
        tabs.setCurrentIndex(0);
        QTest::qWait(100);
        QKeyEvent key(QEvent::KeyPress, Qt::Key_A, Qt::NoModifier, "a");
        terminal->sendKeyEvent(&key);
        const QByteArray expected =
            QByteArray::number(terminal->screenColumnsCount()) + " " + QByteArray::number(terminal->screenLinesCount());
        auto consoleSizeMatches = [&]() {
            QFile file(path);
            return file.open(QIODevice::ReadOnly) && file.readAll() == expected;
        };
        QTRY_VERIFY_WITH_TIMEOUT(consoleSizeMatches(), 5000);
#else
        QSKIP("ConPTY is only available on Windows.");
#endif
    }

    void testConPtyEscape() {
#ifdef Q_OS_WIN
        ConPty console;
        QVERIFY(
            console.start(QDir(QCoreApplication::applicationDirPath()).filePath("conpty_input_probe.exe"), {}, 80, 25));
        QByteArray output;
        QTRY_VERIFY_WITH_TIMEOUT((output += console.read()).contains("INPUT_READY"), 5000);
        QVERIFY(console.write(QByteArrayLiteral("\x1b")));
        // Escape must arrive without needing another key to finish an ANSI sequence.
        QTRY_VERIFY_WITH_TIMEOUT((output += console.read()).contains("KEY=27 SHIFT=0 CHAR=27 CTRL=0 ALT=0"), 2000);
        output.clear();
        QVERIFY(console.write(QByteArrayLiteral("\x1b")));
        QVERIFY(console.write(QByteArrayLiteral("\x1b")));
        QTRY_VERIFY_WITH_TIMEOUT((output += console.read()).count("KEY=27 SHIFT=0 CHAR=27 CTRL=0 ALT=0") == 2, 2000);
        QVERIFY(console.write(QByteArrayLiteral("\r")));
        QTRY_VERIFY_WITH_TIMEOUT((output += console.read()).contains("KEY=13 SHIFT=0"), 5000);
#else
        QSKIP("ConPTY is only available on Windows.");
#endif
    }

    void testConPtyShiftEnter() {
#ifdef Q_OS_WIN
        ConPty console;
        QVERIFY(
            console.start(QDir(QCoreApplication::applicationDirPath()).filePath("conpty_input_probe.exe"), {}, 80, 25));
        QByteArray output;
        QTRY_VERIFY_WITH_TIMEOUT((output += console.read()).contains("INPUT_READY"), 5000);
        QTRY_VERIFY_WITH_TIMEOUT((output += console.read()).contains("COLORTERM=truecolor"), 5000);
        QVERIFY(console.write(QByteArrayLiteral("\x1b[Z")));
        QTRY_VERIFY_WITH_TIMEOUT((output += console.read()).contains("KEY=9 SHIFT=1 CHAR=9 CTRL=0 ALT=0"), 5000);
        QVERIFY(console.write(QByteArrayLiteral("\x1b[1;2D")));
        QTRY_VERIFY_WITH_TIMEOUT((output += console.read()).contains("KEY=37 SHIFT=1"), 5000);
        QVERIFY(console.write(QByteArrayLiteral("\x1b[13;2u")));
        QTRY_VERIFY_WITH_TIMEOUT((output += console.read()).contains("KEY=13 SHIFT=1"), 5000);
#else
        QSKIP("ConPTY is only available on Windows.");
#endif
    }

    void testConPtyColorQueryRoundtrip() {
#ifdef Q_OS_WIN
        ConPty console;
        QVERIFY(console.start(QDir(QCoreApplication::applicationDirPath()).filePath("conpty_input_probe.exe"),
                              {"--query"}, 80, 25));
        QTermWidget terminal(0);
        terminal.startExternal();
        QByteArray replies;
        connect(&terminal, &QTermWidget::sendData, &terminal, [&console, &replies](const char* bytes, int size) {
            replies.append(bytes, size);
            console.write(QByteArray(bytes, size));
        });
        QByteArray output;
        auto received = [&]() {
            const QByteArray data = console.read();
            output += data;
            terminal.feedData(data);
            return output.contains("QUERY_REPLY_RECEIVED");
        };
        QTRY_VERIFY2_WITH_TIMEOUT(received(), ("output=" + output.toHex() + " replies=" + replies.toHex()).constData(),
                                  5000);
#else
        QSKIP("ConPTY is only available on Windows.");
#endif
    }

    void testConPtyRgbRendering() {
#ifdef Q_OS_WIN
        ConPty console;
        QVERIFY(console.start(QDir(QCoreApplication::applicationDirPath()).filePath("conpty_input_probe.exe"),
                              {"--colors"}, 80, 25));
        QByteArray output;
        QTRY_VERIFY_WITH_TIMEOUT((output += console.read()).contains("INPUT_READY"), 5000);
        QTermWidget terminal(0);
        terminal.resize(800, 600);
        terminal.startExternal();
        terminal.show();
        terminal.feedData(output);
        QTest::qWait(50);
        const QImage image = terminal.grab().toImage();
        int matchingPixels = 0;
        for (int y = 0; y < image.height(); ++y)
            for (int x = 0; x < image.width(); ++x)
                if (image.pixelColor(x, y) == QColor(17, 39, 61))
                    ++matchingPixels;
        QVERIFY2(matchingPixels > 100, output.toHex().constData());
#else
        QSKIP("ConPTY is only available on Windows.");
#endif
    }

    void testTerminalAppearanceSettings() {
        QSettings settings;
        const QVariant oldOpacity = settings.value("terminal/backgroundOpacity");
        const QVariant oldBlur = settings.value("terminal/backgroundBlur");
        const auto restore = qScopeGuard([&]() {
            for (const auto& pair : {qMakePair(QString("terminal/backgroundOpacity"), oldOpacity),
                                     qMakePair(QString("terminal/backgroundBlur"), oldBlur)}) {
                if (pair.second.isValid())
                    settings.setValue(pair.first, pair.second);
                else
                    settings.remove(pair.first);
            }
        });
        settings.setValue("terminal/backgroundOpacity", 100);
        settings.setValue("terminal/backgroundBlur", false);
        {
            SettingsDialog dialog;
            auto* opacity = dialog.findChild<QSpinBox*>("terminalBackgroundOpacity");
            auto* blur = dialog.findChild<QCheckBox*>("terminalBackgroundBlur");
            QVERIFY(opacity && blur);
            QCOMPARE(opacity->value(), 100);
            QVERIFY(!blur->isEnabled());
            opacity->setValue(60);
            QCOMPARE(blur->isEnabled(), TerminalAppearance::blurSupported());
            dialog.reject();
            QCOMPARE(settings.value("terminal/backgroundOpacity").toInt(), 100);
        }
        {
            SettingsDialog dialog;
            dialog.findChild<QSpinBox*>("terminalBackgroundOpacity")->setValue(0);
            QVERIFY(QMetaObject::invokeMethod(&dialog, "onAccept", Qt::DirectConnection));
            QCOMPARE(settings.value("terminal/backgroundOpacity").toInt(), 0);
            QCOMPARE(TerminalAppearance::opacityPercent(), 0);
        }
        SettingsDialog reopened;
        QCOMPARE(reopened.findChild<QSpinBox*>("terminalBackgroundOpacity")->value(), 0);
    }

    void testMonitoringSettings() {
        QSettings settings;
        const QVariant oldEnabled = settings.value("ssh/monitoringEnabled");
        const QVariant oldInterval = settings.value("ssh/monitoringIntervalSeconds");
        const auto restore = qScopeGuard([&]() {
            for (const auto& entry : {qMakePair(QString("ssh/monitoringEnabled"), oldEnabled),
                                      qMakePair(QString("ssh/monitoringIntervalSeconds"), oldInterval)}) {
                if (entry.second.isValid())
                    settings.setValue(entry.first, entry.second);
                else
                    settings.remove(entry.first);
            }
        });
        settings.remove("ssh/monitoringEnabled");
        settings.remove("ssh/monitoringIntervalSeconds");
        {
            SettingsDialog dialog;
            auto* enabled = dialog.findChild<QCheckBox*>("sshMonitoringEnabled");
            auto* interval = dialog.findChild<QSpinBox*>("sshMonitoringInterval");
            QVERIFY(enabled && interval);
            QVERIFY(enabled->isChecked());
            QCOMPARE(interval->value(), 3);
            QCOMPARE(interval->minimum(), 1);
            QCOMPARE(interval->maximum(), 3600);
            enabled->setChecked(false);
            QVERIFY(!interval->isEnabled());
            interval->setValue(3600);
            dialog.reject();
            QVERIFY(!settings.contains("ssh/monitoringEnabled"));
        }
        {
            SettingsDialog dialog;
            dialog.findChild<QCheckBox*>("sshMonitoringEnabled")->setChecked(false);
            dialog.findChild<QSpinBox*>("sshMonitoringInterval")->setValue(3600);
            QVERIFY(QMetaObject::invokeMethod(&dialog, "onAccept", Qt::DirectConnection));
            QCOMPARE(settings.value("ssh/monitoringEnabled").toBool(), false);
            QCOMPARE(settings.value("ssh/monitoringIntervalSeconds").toInt(), 3600);
        }
        SettingsDialog reopened;
        QVERIFY(!reopened.findChild<QCheckBox*>("sshMonitoringEnabled")->isChecked());
        QCOMPARE(reopened.findChild<QSpinBox*>("sshMonitoringInterval")->value(), 3600);
    }

    void testSettingsTypographyAccessible() {
        SettingsDialog dialog;
        dialog.resize(640, 480);
        dialog.show();
        auto* scroll = dialog.findChild<QScrollArea*>("appearanceScrollArea");
        auto* typography = dialog.findChild<QGroupBox*>("terminalTypography");
        QVERIFY(scroll && typography);
        QTRY_VERIFY(scroll->verticalScrollBar()->maximum() > 0);
        scroll->ensureWidgetVisible(typography);
        QCoreApplication::processEvents();
        const QRect typographyRect(typography->mapTo(scroll->viewport(), QPoint()), typography->size());
        QVERIFY(scroll->viewport()->rect().contains(typographyRect));
    }

    void testLinuxTerminalBlurToggle() {
        if (QApplication::platformName() != "wayland" && QApplication::platformName() != "xcb")
            QSKIP("Requires a Linux compositor with blur support.");
        QElapsedTimer timer;
        timer.start();
        while (!TerminalAppearance::blurSupported() && timer.elapsed() < 1000)
            QTest::qWait(10);
        if (!TerminalAppearance::blurSupported())
            QSKIP("Requires a Linux compositor with blur support.");
        QSettings settings;
        const QVariant oldOpacity = settings.value("terminal/backgroundOpacity");
        const QVariant oldBlur = settings.value("terminal/backgroundBlur");
        const auto restore = qScopeGuard([&]() {
            for (const auto& pair : {qMakePair(QString("terminal/backgroundOpacity"), oldOpacity),
                                     qMakePair(QString("terminal/backgroundBlur"), oldBlur)}) {
                if (pair.second.isValid())
                    settings.setValue(pair.first, pair.second);
                else
                    settings.remove(pair.first);
            }
        });
        settings.setValue("terminal/backgroundOpacity", 50);
        settings.setValue("terminal/backgroundBlur", true);
        QMainWindow window;
        TerminalAppearance::initializeWindow(&window);
        auto* terminal = new QTermWidget(0, &window);
        window.setCentralWidget(terminal);
        window.resize(640, 320);
        window.show();
        QTRY_VERIFY(window.property("terminalBlurActive").toBool());
        settings.setValue("terminal/backgroundBlur", false);
        TerminalAppearance::refreshWindow(&window);
        QVERIFY(!window.property("terminalBlurActive").toBool());
        settings.setValue("terminal/backgroundBlur", true);
        TerminalAppearance::refreshWindow(&window);
        QVERIFY(window.property("terminalBlurActive").toBool());
        terminal->hide();
        QTRY_VERIFY(!window.property("terminalBlurActive").toBool());
        terminal->show();
        QTRY_VERIFY(window.property("terminalBlurActive").toBool());
        settings.setValue("terminal/backgroundOpacity", 100);
        TerminalAppearance::refreshWindow(&window);
        QVERIFY(!window.property("terminalBlurActive").toBool());
    }

    void testNativeReorganizedTerminalGlass() {
#ifdef Q_OS_WIN
        if (QApplication::platformName() != "windows")
            QSKIP("Requires the native Windows compositor.");
        QSettings settings;
        const QStringList keys = {
            "window/panelWorkspaces",     "window/openTabs",         "window/paneTree", "window/tabLayoutMode",
            "terminal/backgroundOpacity", "terminal/backgroundBlur", "theme/mode"};
        QMap<QString, QVariant> previous;
        for (const auto& key : keys) {
            previous[key] = settings.value(key);
            settings.remove(key);
        }
        const auto restore = qScopeGuard([&]() {
            for (auto it = previous.cbegin(); it != previous.cend(); ++it)
                if (it.value().isValid())
                    settings.setValue(it.key(), it.value());
                else
                    settings.remove(it.key());
        });
        settings.setValue("window/openTabs", QByteArray("[]"));
        settings.setValue("terminal/backgroundOpacity", 50);
        settings.setValue("terminal/backgroundBlur", false);
        settings.setValue("theme/mode", "dark");
        QWidget background;
        const QRect available = QApplication::primaryScreen()->availableGeometry();
        background.setWindowFlag(Qt::WindowStaysOnTopHint);
        background.setGeometry(available);
        background.setAutoFillBackground(true);
        background.show();
        MainWindow window;
        window.setWindowFlag(Qt::WindowStaysOnTopHint);
        window.setGeometry(available.adjusted(20, 50, -20, -20));
        window.show();
        window.raise();
        window.activateWindow();
        window.findChild<QAction*>("toggleSidebar")->trigger();
        Session profile;
        profile.type = SessionType::Local;
        profile.shellPath = "cmd.exe";
        profile.fontFamily = "Consolas";
        for (int i = 0; i < 3; ++i) {
            profile.name = QString("Glass %1").arg(i);
            QVERIFY(QMetaObject::invokeMethod(&window, "onConnectSession", Q_ARG(Session, profile)));
        }
        auto contrast = [&](const char* stage) {
            QTest::qWait(300);
            QList<QRect> areas;
            for (auto* tab : window.findChildren<TerminalTab*>()) {
                if (!tab->isVisible())
                    continue;
                auto* display = tab->getTerminalWidget()->findChild<Konsole::TerminalDisplay*>();
                const QRect rect(display->mapToGlobal(QPoint()), display->size());
                const QRect clipped = rect.intersected(available);
                if (clipped.width() > 80 && clipped.height() > 100)
                    areas.append(QRect(clipped.topLeft() + QPoint(20, qMin(150, clipped.height() / 2)), QSize(20, 20)));
            }
            if (areas.isEmpty())
                return false;
            auto capture = [&](const QColor& color) {
                QPalette palette = background.palette();
                palette.setColor(QPalette::Window, color);
                background.setPalette(palette);
                background.update();
                QTest::qWait(250);
                QList<QColor> samples;
                for (const auto& area : areas) {
                    const auto image =
                        window.screen()->grabWindow(0, area.x(), area.y(), area.width(), area.height()).toImage();
                    if (image.isNull())
                        return QList<QColor>{};
                    samples.append(image.pixelColor(image.width() / 2, image.height() / 2));
                }
                return samples;
            };
            const auto red = capture(Qt::red);
            const auto blue = capture(Qt::blue);
            if (red.size() != areas.size() || blue.size() != areas.size())
                return false;
            for (int i = 0; i < red.size(); ++i) {
                qDebug() << stage << i << red[i] << blue[i];
                if (red[i].red() - blue[i].red() < 20 || blue[i].blue() - red[i].blue() < 20)
                    return false;
            }
            return true;
        };
        QVERIFY(contrast("initial"));
        QVERIFY(QMetaObject::invokeMethod(&window, "splitPaneHorizontally"));
        for (auto* tab : window.findChildren<TerminalTab*>())
            tab->getTerminalWidget()->feedData(QByteArrayLiteral("\x1b[48;2;12;12;12m\x1b[2J"));
        QVERIFY(contrast("split"));
        QVERIFY(QMetaObject::invokeMethod(&window, "toggleGridView"));
        QVERIFY(contrast("grid"));
        window.findChild<QAction*>("restoreSinglePane")->trigger();
        QVERIFY(contrast("single"));
        window.showMaximized();
        QVERIFY(contrast("maximized"));
        if (TerminalAppearance::blurSupported()) {
            settings.setValue("terminal/backgroundBlur", true);
            TerminalAppearance::refreshWindow(&window);
            QVERIFY(window.property("terminalBlurActive").toBool());
            QVERIFY(QMetaObject::invokeMethod(&window, "toggleGridView"));
            QTest::qWait(300);
            QVERIFY(window.property("terminalBlurActive").toBool());
            int backdrop = 0;
            const HRESULT result =
                DwmGetWindowAttribute(reinterpret_cast<HWND>(window.winId()), 38, &backdrop, sizeof(backdrop));
            QVERIFY(SUCCEEDED(result));
            QCOMPARE(backdrop, 3);
        }
#else
        QSKIP("Windows-specific native compositor regression.");
#endif
    }

    void testNativeTerminalBlur() {
        if (QApplication::platformName() != "windows" || !TerminalAppearance::blurSupported())
            QSKIP("Requires a visible Windows 11 desktop compositor.");
        QSettings settings;
        const QVariant oldOpacity = settings.value("terminal/backgroundOpacity");
        const QVariant oldBlur = settings.value("terminal/backgroundBlur");
        const auto restore = qScopeGuard([&]() {
            for (const auto& pair : {qMakePair(QString("terminal/backgroundOpacity"), oldOpacity),
                                     qMakePair(QString("terminal/backgroundBlur"), oldBlur)}) {
                if (pair.second.isValid())
                    settings.setValue(pair.first, pair.second);
                else
                    settings.remove(pair.first);
            }
        });
        settings.setValue("terminal/backgroundOpacity", 50);
        settings.setValue("terminal/backgroundBlur", false);
        QPixmap pattern(20, 20);
        pattern.fill(Qt::red);
        {
            QPainter painter(&pattern);
            painter.fillRect(0, 0, 10, 10, Qt::blue);
            painter.fillRect(10, 10, 10, 10, Qt::blue);
        }
        QWidget background;
        QPalette palette = background.palette();
        palette.setBrush(QPalette::Window, pattern);
        background.setPalette(palette);
        background.setAutoFillBackground(true);
        background.setGeometry(100, 100, 900, 600);
        background.show();
        QMainWindow foreground;
        foreground.setWindowTitle("BanchoXterm - native title bar");
        TerminalAppearance::initializeWindow(&foreground);
        auto* terminal = new QTermWidget(0, &foreground);
        foreground.setCentralWidget(terminal);
        terminal->startExternal();
        foreground.setGeometry(180, 180, 640, 320);
        foreground.show();
        foreground.raise();
        foreground.activateWindow();
        TerminalAppearance::refreshWindow(&foreground);
        QVERIFY(!foreground.windowFlags().testFlag(Qt::FramelessWindowHint));
#ifdef Q_OS_WIN
        const HWND hwnd = reinterpret_cast<HWND>(foreground.winId());
        const auto style = GetWindowLongPtr(hwnd, GWL_STYLE);
        const LONG_PTR nativeFrame = WS_CAPTION | WS_SYSMENU | WS_THICKFRAME | WS_MINIMIZEBOX | WS_MAXIMIZEBOX;
        QCOMPARE(style & nativeFrame, nativeFrame);
        RECT outer{}, client{};
        QVERIFY(GetWindowRect(hwnd, &outer));
        QVERIFY(GetClientRect(hwnd, &client));
        QVERIFY(outer.bottom - outer.top > client.bottom - client.top);
#endif
        foreground.resize(700, 500);
        auto capture = [&]() {
            QTest::qWait(600);
            const QPoint position = terminal->mapToGlobal(QPoint(100, 100));
            return foreground.screen()->grabWindow(0, position.x(), position.y(), 120, 120).toImage();
        };
        const QImage clear = capture();
        settings.setValue("terminal/backgroundBlur", true);
        TerminalAppearance::refreshWindow(&foreground);
        QVERIFY(foreground.property("terminalBlurActive").toBool());
        const QImage blurred = capture();
        clear.save("build/terminal-clear.png");
        blurred.save("build/terminal-blur.png");
        QVERIFY(!clear.isNull() && !blurred.isNull());
        QVERIFY(clear != blurred);
        auto caption = [&]() {
            QTest::qWait(300);
            const QPoint position = foreground.mapToGlobal(QPoint(150, -16));
            return foreground.screen()->grabWindow(0, position.x(), position.y(), 120, 6).toImage();
        };
        const QImage titleBefore = caption();
        auto luminance = [](const QColor& color) {
            auto linear = [](double value) {
                return value <= 0.04045 ? value / 12.92 : std::pow((value + 0.055) / 1.055, 2.4);
            };
            return 0.2126 * linear(color.redF()) + 0.7152 * linear(color.greenF()) + 0.0722 * linear(color.blueF());
        };
        double darkest = 1.0, lightest = 0.0;
        for (int y = 0; y < titleBefore.height(); ++y)
            for (int x = 0; x < titleBefore.width(); ++x) {
                const double value = luminance(titleBefore.pixelColor(x, y));
                darkest = qMin(darkest, value);
                lightest = qMax(lightest, value);
            }
        QVERIFY2((lightest + 0.05) / (darkest + 0.05) >= 4.5,
                 "Native title text must contrast with its opaque caption background.");

        palette.setColor(QPalette::Window, Qt::green);
        background.setPalette(palette);
        background.update();
        const QImage titleAfter = caption();
        titleBefore.save("build/title-before.png");
        titleAfter.save("build/title-after.png");
        QCOMPARE(titleBefore, titleAfter);
        settings.setValue("terminal/backgroundBlur", false);
        TerminalAppearance::refreshWindow(&foreground);
        QCOMPARE(caption(), titleBefore);
        foreground.showMaximized();
        QTRY_VERIFY(foreground.isMaximized());
        foreground.showNormal();
        QTRY_VERIFY(!foreground.isMaximized());
    }

    void testDetachedWindowKeepsItsOwnOpacity() {
        QSettings settings;
        const auto oldOpacity = settings.value("terminal/backgroundOpacity");
        const auto restore = qScopeGuard([&]() {
            if (oldOpacity.isValid())
                settings.setValue("terminal/backgroundOpacity", oldOpacity);
            else
                settings.remove("terminal/backgroundOpacity");
        });
        settings.setValue("terminal/backgroundOpacity", 100);
        QMainWindow owner;
        TerminalAppearance::initializeWindow(&owner);
        settings.setValue("terminal/backgroundOpacity", 50);
        QMainWindow detached(&owner);
        TerminalAppearance::initializeWindow(&detached);
        auto* terminal = new QTermWidget(0, &detached);
        detached.setCentralWidget(terminal);
        terminal->startExternal();
        TerminalAppearance::refreshWindow(&detached);
        auto* display = terminal->findChild<Konsole::TerminalDisplay*>();
        QVERIFY(display);
        QVERIFY(!display->testAttribute(Qt::WA_OpaquePaintEvent));
        TerminalAppearance::refreshWindow(&owner);
        QVERIFY(!display->testAttribute(Qt::WA_OpaquePaintEvent));
    }

    void testTerminalResizeRefreshesImage() {
        QTermWidget terminal(0);
        terminal.startExternal();
        terminal.setBlinkingCursor(false);
        terminal.resize(420, 210);
        terminal.show();
        QTest::qWait(50);
        auto* display = terminal.findChild<Konsole::TerminalDisplay*>();
        QVERIFY(display);
        terminal.feedData(QByteArrayLiteral("\x1b[?25l\x1b[48;2;25;60;90m\x1b[2J\x1b[Hresize"));
        QTest::qWait(50);
        for (const QSize size : {QSize(790, 460), QSize(310, 170), QSize(655, 335)}) {
            terminal.resize(size);
            terminal.layout()->activate();
            // Capturing paints the widget, but must not be responsible for
            // synchronizing its cached character image after a grid resize.
            const QImage resized = display->grab().toImage();
            display->updateImage();
            const QImage refreshed = display->grab().toImage();
            QCOMPARE(resized, refreshed);
        }
    }

    void testTerminalTransparency() {
        QSettings settings;
        const QVariant previous = settings.value("terminal/backgroundOpacity");
        const auto restore = qScopeGuard([&]() {
            if (previous.isValid())
                settings.setValue("terminal/backgroundOpacity", previous);
            else
                settings.remove("terminal/backgroundOpacity");
        });
        settings.setValue("terminal/backgroundOpacity", 50);
        QMainWindow window;
        TerminalAppearance::initializeWindow(&window);
        QVERIFY(window.testAttribute(Qt::WA_TranslucentBackground));
        auto* terminal = new QTermWidget(0, &window);
        window.setCentralWidget(terminal);
        terminal->startExternal();
        terminal->setBlinkingCursor(false);
        QFont testFont(QStringLiteral("monospace"), 18, QFont::Bold);
        testFont.setStyleHint(QFont::TypeWriter);
        terminal->setTerminalFont(testFont);
        window.resize(640, 320);
        window.show();
        TerminalAppearance::refreshWindow(&window);
        auto* display = terminal->findChild<Konsole::TerminalDisplay*>();
        QVERIFY(display);
        QVERIFY(!display->testAttribute(Qt::WA_OpaquePaintEvent));
        // Changing the color scheme clears the background image. It must
        // preserve the translucent backing-store configuration.
        terminal->setColorScheme("DarkPastels");
        QVERIFY(!display->testAttribute(Qt::WA_OpaquePaintEvent));
        terminal->feedData(QByteArrayLiteral("\x1b[?25l\x1b[48;2;0;0;0;38;2;255;255;255mMMMMMMMM"));
        QTest::qWait(50);
        const QImage image = window.grab().toImage();
        const QPoint origin = terminal->mapTo(&window, QPoint());
        int translucentBackground = 0, opaqueText = 0;
        for (int y = origin.y(); y < origin.y() + 40; ++y)
            for (int x = origin.x(); x < origin.x() + 180; ++x) {
                const QColor pixel = image.pixelColor(x, y);
                if (pixel.alpha() > 0 && pixel.alpha() < 255)
                    ++translucentBackground;
                if (pixel.alpha() == 255 && pixel.red() > 200)
                    ++opaqueText;
            }
        QVERIFY(translucentBackground > 100);
        QVERIFY(opaqueText > 0);
        // Full redraws from ConPTY and terminal programs use explicit cell
        // backgrounds. These must retain alpha just like the default palette.
        terminal->feedData(QByteArrayLiteral("\x1b[48;2;12;12;12m\x1b[2J"));
        window.resize(800, 450);
        QTest::qWait(50);
        const QImage redrawn = window.grab().toImage();
        const int alpha = redrawn.pixelColor(terminal->mapTo(&window, QPoint(120, 80))).alpha();
        QVERIFY(alpha > 0 && alpha < 255);
        settings.setValue("terminal/backgroundOpacity", 100);
        TerminalAppearance::refreshWindow(&window);
        QVERIFY(display->testAttribute(Qt::WA_OpaquePaintEvent));
        QTest::qWait(50);
        const QImage opaque = window.grab().toImage();
        QCOMPARE(opaque.pixelColor(origin + QPoint(150, 30)).alpha(), 255);
    }

    void testTerminalDimRendering() {
        auto checkDim = [](QWidget& widget, const std::function<void(const QByteArray&)>& feed) {
            widget.resize(640, 320);
            widget.show();
            auto render = [&](const QByteArray& rendition) {
                feed(QByteArray("\x1b[?25l\x1b[0;48;2;0;0;0;38;2;255;255;255m\x1b[2J\x1b[H") + rendition + "MMMMMMMM");
                QTest::qWait(50);
                return widget.grab().toImage();
            };
            const QImage normal = render(QByteArray());
            const QImage dim = render(QByteArrayLiteral("\x1b[2m"));
            const QImage reset = render(QByteArrayLiteral("\x1b[2;22m"));
            auto brightness = [](const QImage& image) {
                qint64 sum = 0;
                for (int y = 0; y < image.height(); ++y)
                    for (int x = 0; x < image.width(); ++x) {
                        const QColor pixel = image.pixelColor(x, y);
                        sum += pixel.red() + pixel.green() + pixel.blue();
                    }
                return sum;
            };
            QVERIFY(brightness(dim) < brightness(normal));
            QCOMPARE(reset, normal);
        };
        QTermWidget terminal(0);
        terminal.setBlinkingCursor(false);
        terminal.startExternal();
        checkDim(terminal, [&](const QByteArray& data) { terminal.feedData(data); });
        VtTerminalWidget fallback;
        checkDim(fallback, [&](const QByteArray& data) { fallback.writeData(data); });
    }

    void testTerminalRgbColors() {
        for (const QByteArray& parameters :
             {QByteArray("48;2;12;34;56"), QByteArray("48:2:12:34:56"), QByteArray("48:2::12:34:56")}) {
            QTermWidget terminal(0);
            terminal.resize(640, 320);
            terminal.setBlinkingCursor(false);
            terminal.startExternal();
            terminal.show();
            terminal.feedData(QByteArray("\x1b[") + parameters + "m          \x1b[0m");
            QTest::qWait(50);
            const QImage image = terminal.grab().toImage();
            int matchingPixels = 0;
            for (int y = 0; y < image.height(); ++y)
                for (int x = 0; x < image.width(); ++x)
                    if (image.pixelColor(x, y) == QColor(12, 34, 56))
                        ++matchingPixels;
            QVERIFY2(matchingPixels > 100, parameters.constData());
        }
    }

    void testTerminalColorQueries() {
        QTermWidget terminal(0);
        terminal.startExternal();
        QByteArray response;
        connect(&terminal, &QTermWidget::sendData, &terminal,
                [&response](const char* data, int size) { response.append(data, size); });
        terminal.feedData(QByteArrayLiteral("\x1b]10;?\x07\x1b]11;?\x1b\\"));
        QTRY_VERIFY(response.contains("\x1b]10;rgb:") && response.contains("\x1b]11;rgb:"));
        QVERIFY(QRegularExpression(QStringLiteral("rgb:[0-9a-f]{4}/[0-9a-f]{4}/[0-9a-f]{4}"))
                    .match(QString::fromLatin1(response))
                    .hasMatch());
    }

    void testShiftEnterEncoding() {
        const QByteArray shiftedEnter("\x1b[13;2u");
        VtTerminalWidget fallback;
        QSignalSpy fallbackSpy(&fallback, &VtTerminalWidget::dataReady);
        for (int key : {Qt::Key_Return, Qt::Key_Enter}) {
            QTest::keyClick(&fallback, static_cast<Qt::Key>(key), Qt::ShiftModifier);
            QCOMPARE(fallbackSpy.takeFirst().at(0).toByteArray(), shiftedEnter);
            QTest::keyClick(&fallback, static_cast<Qt::Key>(key));
            QCOMPARE(fallbackSpy.takeFirst().at(0).toByteArray(), QByteArray("\r"));
        }

        for (const QString& binding : {QStringLiteral("default"), QStringLiteral("linux")}) {
            QTermWidget terminal(0);
            terminal.setKeyBindings(binding);
            terminal.startExternal();
            QList<QByteArray> data;
            connect(&terminal, &QTermWidget::sendData, &terminal,
                    [&data](const char* bytes, int size) { data.append(QByteArray(bytes, size)); });
            for (int key : {Qt::Key_Left, Qt::Key_Right, Qt::Key_Up, Qt::Key_Down}) {
                QKeyEvent shifted(QEvent::KeyPress, key, Qt::ShiftModifier);
                terminal.sendKeyEvent(&shifted);
                const char suffix = key == Qt::Key_Left    ? 'D'
                                    : key == Qt::Key_Right ? 'C'
                                    : key == Qt::Key_Up    ? 'A'
                                                           : 'B';
                QCOMPARE(data.takeFirst(), QByteArray("\x1b[1;2") + suffix);
            }
            for (int key : {Qt::Key_Return, Qt::Key_Enter}) {
                for (auto modifiers : {Qt::KeyboardModifiers(Qt::ShiftModifier),
                                       Qt::KeyboardModifiers(Qt::ShiftModifier | Qt::KeypadModifier)}) {
                    QKeyEvent shifted(QEvent::KeyPress, key, modifiers, QStringLiteral("\r"));
                    terminal.sendKeyEvent(&shifted);
                    QCOMPARE(data.takeFirst(), shiftedEnter);
                }
                QKeyEvent plain(QEvent::KeyPress, key, Qt::NoModifier, QStringLiteral("\r"));
                terminal.sendKeyEvent(&plain);
                QCOMPARE(data.takeFirst(), QByteArray("\r"));
            }
        }
    }

    void testLinuxTerminalDeleteAndBackspaceSequences() {
        QVERIFY(QTermWidget::availableKeyBindings().contains(QStringLiteral("linux")));

        QTermWidget widget(0);
        widget.setKeyBindings(QStringLiteral("linux"));
        widget.startExternal();
        QList<QByteArray> emittedData;
        connect(&widget, &QTermWidget::sendData, &widget,
                [&emittedData](const char* data, int size) { emittedData.append(QByteArray(data, size)); });

        QKeyEvent backspaceEvent(QEvent::KeyPress, Qt::Key_Backspace, Qt::NoModifier);
        widget.sendKeyEvent(&backspaceEvent);
        QKeyEvent deleteEvent(QEvent::KeyPress, Qt::Key_Delete, Qt::NoModifier);
        widget.sendKeyEvent(&deleteEvent);

        QCOMPARE(emittedData.size(), 2);
        QCOMPARE(emittedData.at(0), QByteArray("\x7f", 1));
        QCOMPARE(emittedData.at(1), QByteArray("\x1b[3~", 4));
    }

    void testVtTerminalRendering() {
        VtTerminalWidget widget;
        widget.resize(800, 600);

        widget.writeData("AB\r\nCD\r\n");
        QCOMPARE(widget.cellChar(0, 0), QChar('A'));
        QCOMPARE(widget.cellChar(1, 0), QChar('B'));
        QCOMPARE(widget.cellChar(0, 1), QChar('C'));
        QCOMPARE(widget.cellChar(1, 1), QChar('D'));
    }

    void testVtClearScreen() {
        VtTerminalWidget widget;
        widget.resize(800, 600);

        widget.writeData("hello world");
        widget.writeData("\x1b[2J"); // clear entire screen
        QCOMPARE(widget.cellChar(0, 0), QChar(' '));
        QCOMPARE(widget.cellChar(4, 0), QChar(' '));
    }

    void testVtCursorPositioning() {
        VtTerminalWidget widget;
        widget.resize(800, 600);

        widget.writeData("abc");
        widget.writeData("\x1b[2;1H"); // cursor to row 2, column 1 (1-based)
        widget.writeData("Z");
        QCOMPARE(widget.cellChar(0, 1), QChar('Z'));
    }
};

QTEST_MAIN(TestSession)
#include "test_session.moc"
