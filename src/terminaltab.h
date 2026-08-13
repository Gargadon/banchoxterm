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
class QFrame;
class QLineEdit;
class QPushButton;
class QCheckBox;
class ConPty;
class QFile;
class QAxWidget;
class VncClientWidget;

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
    void sendInputText(const QString& text);
    void sendRaw(const QString& text);
    bool searchText(const QString& str, bool next, bool caseSensitive);
    void copySelection();
    void pasteSelection();
    void clearTerminal();

    void closeExternalProcess();

protected:
    void resizeEvent(QResizeEvent* event) override;

signals:
    void tabFinished();
    void titleChanged(const QString& title);
    void remoteDirChanged(const QString& dir);
    void reconnectRequested(const Session& session);

private slots:
    void onTerminalFinished();
    void onRemoteDirChanged(const QString& dir);
#ifndef Q_OS_WIN
    void onTitleChanged();
    void showTerminalContextMenu(const QPoint& pos);
    void onShellDataReceived(const QByteArray& data);
    void onShellClosed();
    void onSendData(const char* data, int size);
    void flushWriteBuffer();
#endif
    void showSearchFrame();
    void hideSearchFrame();
    void onSearchNext();
    void onSearchPrev();

private:
    void launchExternalClient();
    void setupSshTerminal();
    void setupWindowsTerminal();
    void setupWindowsRdpActiveX();
    void setupEmbeddedVnc();
    void startConPtyPolling();
    void pollConPtyOutput();
    void startLogging();
    void logData(const QByteArray& data);
    void maybeScheduleReconnect();

    Session m_session;
    QTermWidget* m_terminal = nullptr;
    VtTerminalWidget* m_vtTerminal = nullptr;
    QLabel* m_statusLabel = nullptr;
    QWidget* m_embeddedContainer = nullptr;
    QProcess* m_externalProcess = nullptr;
    SshConnection* m_connection = nullptr;
    QThread* m_connectionThread = nullptr;
    QByteArray m_writeBuffer;
    QTimer* m_flushTimer = nullptr;
    QFile* m_logFile = nullptr;
    QTimer* m_reconnectTimer = nullptr;
    QAxWidget* m_rdpWidget = nullptr;
    QTimer* m_rdpPollTimer = nullptr;
    bool m_rdpWasConnected = false;
    VncClientWidget* m_vncWidget = nullptr;
#ifdef Q_OS_WIN
    ConPty* m_conpty = nullptr;
    QTimer* m_conptyPollTimer = nullptr;
#endif
    bool m_isActive = true;

    QFrame* m_searchFrame = nullptr;
    QLineEdit* m_searchEdit = nullptr;
    QPushButton* m_btnPrev = nullptr;
    QPushButton* m_btnNext = nullptr;
    QCheckBox* m_caseSensitiveCheck = nullptr;
};
