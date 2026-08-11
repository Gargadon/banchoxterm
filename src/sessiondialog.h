#pragma once
#include <QDialog>
#include "session.h"

class QComboBox;
class QLineEdit;
class QSpinBox;
class QStackedWidget;
class QCheckBox;

class SessionDialog : public QDialog {
    Q_OBJECT
public:
    explicit SessionDialog(QWidget* parent = nullptr);
    explicit SessionDialog(const Session& session, QWidget* parent = nullptr);

    Session getSession() const;
    void accept() override;

private:
    void setupUi();
    void loadSession(const Session& session);

    QLineEdit* m_nameEdit;
    QComboBox* m_typeCombo;
    QStackedWidget* m_stackedWidget;

    // SSH Widgets
    QLineEdit* m_hostEdit;
    QSpinBox* m_portSpin;
    QLineEdit* m_userEdit;
    QLineEdit* m_passwordEdit;
    QCheckBox* m_savePasswordCheck;
    QCheckBox* m_x11ForwardCheck;
    QLineEdit* m_keyEdit;

    // Local Widgets
    QLineEdit* m_shellEdit;

    // Telnet Widgets
    QLineEdit* m_telnetHostEdit;
    QSpinBox* m_telnetPortSpin;

    // Serial Widgets
    QLineEdit* m_serialPortEdit;
    QComboBox* m_serialBaudCombo;
    QComboBox* m_serialCmdCombo;

    QString m_id;
};
