#pragma once
#include <QDialog>
#include "session.h"

class QComboBox;
class QLineEdit;
class QSpinBox;
class QStackedWidget;
class QCheckBox;
class QTableWidget;
class QPushButton;

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
    QLineEdit* m_groupEdit;
    QComboBox* m_typeCombo;
    QStackedWidget* m_stackedWidget;

    // SSH
    QLineEdit* m_hostEdit;
    QSpinBox* m_portSpin;
    QLineEdit* m_userEdit;
    QLineEdit* m_passwordEdit;
    QCheckBox* m_savePasswordCheck;
    QCheckBox* m_x11ForwardCheck = nullptr;
    QCheckBox* m_autoReconnectCheck = nullptr;
    QLineEdit* m_keyEdit;

    // Local
    QLineEdit* m_shellEdit;

    // Telnet
    QLineEdit* m_telnetHostEdit;
    QSpinBox* m_telnetPortSpin;

    // RDP
    QLineEdit* m_rdpHostEdit;
    QSpinBox* m_rdpPortSpin;
    QLineEdit* m_rdpUserEdit;

    // VNC
    QLineEdit* m_vncHostEdit;
    QSpinBox* m_vncPortSpin;

    // Serial
    QLineEdit* m_serialPortEdit;
    QComboBox* m_serialBaudCombo;
    QComboBox* m_serialCmdCombo;

    // Tunnels
    QTableWidget* m_tunnelsTable;
    QPushButton* m_addTunnelBtn;
    QPushButton* m_deleteTunnelBtn;
    QList<TunnelConfig> m_tunnels;

    QString m_id;
};
