#pragma once
#include <QWidget>
#include <QProcess>
#include "session.h"

class QTermWidget;
class QLabel;
class QThread;
class QTimer;
class SshConnection;
class QFrame;
class QLineEdit;
class QPushButton;
class QCheckBox;
class ConPty;
class QFile;
class QAxWidget;
class VncClientWidget;
class QSerialPort;

class TerminalTab : public QWidget {
    Q_OBJECT
public:
    explicit TerminalTab(const Session& session, QWidget* parent = nullptr);
    ~TerminalTab() override;

    const Session& getSession() const {
        return m_session;
    }
    void setRemoteDirectory(const QString& path) {
        m_session.remoteDirectory = path;
    }
    QTermWidget* getTerminalWidget() const {
        return m_terminal;
    }
    bool isSsh() const {
        return m_session.type == SessionType::SSH;
    }
    const Session& session() const {
        return m_session;
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
    void syncTerminalSize();
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
    void closeRequested();

private slots:
    void onTerminalFinished();
    void onRemoteDirChanged(const QString& dir);
    void onTitleChanged();
    void showTerminalContextMenu(const QPoint& pos);
    void onSendData(const char* data, int size);
    void showSearchFrame();
    void hideSearchFrame();
    void onSearchNext();
    void onSearchPrev();

private:
    void launchExternalClient();
    void setupSshTerminal();
    void setupLocalTerminal();
    void setupWindowsConPty(const QString& program, const QStringList& args);
    void applySshOptions();
    void setupWindowsRdpActiveX();
    void setupEmbeddedVnc();
    void startConPtyPolling();
    void pollConPtyOutput();
    void startLogging();
    void logData(const QByteArray& data);
    void maybeScheduleReconnect();
    void applyTerminalSize(int rows, int cols);
    void feedTerminalData(const QByteArray& data);
    QWidget* terminalView() const;
    bool hasSelection() const;
    void doSendText(const QString& text);
    void doToggleSearchBar();
    void doFocusTerminal();
    void doCopy();
    void doPaste();
    void doClear();
    void doZoomIn();
    void doZoomOut();

    Session m_session;
    QTermWidget* m_terminal = nullptr;
    QLabel* m_statusLabel = nullptr;
    QWidget* m_embeddedContainer = nullptr;
    QProcess* m_externalProcess = nullptr;
    SshConnection* m_connection = nullptr;
    QThread* m_connectionThread = nullptr;
    QFile* m_logFile = nullptr;
    QTimer* m_reconnectTimer = nullptr;
    QAxWidget* m_rdpWidget = nullptr;
    QTimer* m_rdpPollTimer = nullptr;
    bool m_rdpWasConnected = false;
    VncClientWidget* m_vncWidget = nullptr;
#ifdef Q_OS_WIN
    QSerialPort* m_serialPort = nullptr;
    ConPty* m_conpty = nullptr;
    QTimer* m_conptyPollTimer = nullptr;
    bool m_conptyStarted = false;
    QString m_pendingShell;
#endif
    bool m_isActive = true;

    QFrame* m_searchFrame = nullptr;
    QLineEdit* m_searchEdit = nullptr;
    QPushButton* m_btnPrev = nullptr;
    QPushButton* m_btnNext = nullptr;
    QCheckBox* m_caseSensitiveCheck = nullptr;
};
