#pragma once
#include <QWidget>
#include <QProcess>
#include "session.h"

class QTermWidget;
class QLabel;
class QThread;
class QTimer;
class SshConnection;
class VtTerminalWidget;
#ifdef Q_OS_WIN
#include "conpty.h"
#endif

class TerminalTab : public QWidget {
    Q_OBJECT
public:
    explicit TerminalTab(const Session& session, QWidget* parent = nullptr);
    ~TerminalTab() override;

    const Session& getSession() const {
        return m_session;
    }
    QTermWidget* getTerminalWidget() const {
        return m_terminal;
    }
    bool isSsh() const {
        return m_session.type == SessionType::SSH;
    }
    bool isRdpOrVnc() const {
        return m_session.type == SessionType::RDP || m_session.type == SessionType::VNC;
    }
    bool isSessionActive() const {
        return m_isActive;
    }
    SshConnection* connection() const {
        return m_connection;
    }
    void updateFontFromSettings();

    void closeExternalProcess();

protected:
    void resizeEvent(QResizeEvent* event) override;

signals:
    void tabFinished();
    void titleChanged(const QString& title);
    void remoteDirChanged(const QString& dir);

private slots:
    void onTerminalFinished();
    void onTitleChanged();
    void onRemoteDirChanged(const QString& dir);
    void showTerminalContextMenu(const QPoint& pos);
    void onShellDataReceived(const QByteArray& data);
    void onShellClosed();
    void onSendData(const char* data, int size);
    void flushWriteBuffer();

private:
    void launchExternalClient();
    void setupSshTerminal();
    void setupWindowsTerminal();
    void startConPtyPolling();
    void pollConPtyOutput();

    Session m_session;
    QTermWidget* m_terminal = nullptr;
    VtTerminalWidget* m_vtTerminal = nullptr;
    QLabel* m_statusLabel = nullptr;
    QProcess* m_externalProcess = nullptr;
    SshConnection* m_connection = nullptr;
    QThread* m_connectionThread = nullptr;
    QByteArray m_writeBuffer;
    QTimer* m_flushTimer = nullptr;
#ifdef Q_OS_WIN
    ConPty* m_conpty = nullptr;
    QTimer* m_conptyPollTimer = nullptr;
#endif
    bool m_isActive = true;
};
