#pragma once
#include <QWidget>
#include <QProcess>
#include "session.h"

class QTermWidget;
class QLabel;

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
    void updateFontFromSettings();

    void closeExternalProcess();

signals:
    void tabFinished();
    void titleChanged(const QString& title);

private slots:
    void onTerminalFinished();
    void onTitleChanged();

private:
    void launchExternalClient();

    Session m_session;
    QTermWidget* m_terminal = nullptr;
    QLabel* m_statusLabel = nullptr;
    QProcess* m_externalProcess = nullptr;
    bool m_isActive = true;
};
