#pragma once
#include <QWidget>
#include "session.h"

class QTermWidget;

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
    bool isSessionActive() const {
        return m_isActive;
    }
    void updateFontFromSettings();

signals:
    void tabFinished();
    void titleChanged(const QString& title);

private slots:
    void onTerminalFinished();
    void onTitleChanged();

private:
    Session m_session;
    QTermWidget* m_terminal;
    bool m_isActive = true;
};
