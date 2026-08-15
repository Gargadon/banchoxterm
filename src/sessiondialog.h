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
class QLabel;
class QFont;

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
    QWidget* m_terminalSettingsWidget = nullptr;

    // Shared terminal settings (SSH / Local / Telnet / Serial)
    QSpinBox* m_scrollbackSpin = nullptr;
    QLabel* m_fontLabel = nullptr;
    QFont m_font;
    bool m_fontChosen = false;

    // SSH advanced
    QSpinBox* m_keepAliveSpin = nullptr;
    QLineEdit* m_cipherEdit = nullptr;
    QLineEdit* m_kexEdit = nullptr;
    QLineEdit* m_macEdit = nullptr;

    // SSH
    QLineEdit* m_hostEdit;
    QSpinBox* m_portSpin;
    QLineEdit* m_userEdit;
    QLineEdit* m_passwordEdit;
    QCheckBox* m_savePasswordCheck;
    QCheckBox* m_x11ForwardCheck = nullptr;
    QCheckBox* m_autoReconnectCheck = nullptr;
    QLineEdit* m_keyEdit;
    QLineEdit* m_jumpHostEdit = nullptr;
    QSpinBox* m_jumpPortSpin = nullptr;
    QLineEdit* m_jumpUserEdit = nullptr;
    QLineEdit* m_jumpKeyEdit = nullptr;

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
    QLineEdit* m_vncPasswordEdit = nullptr;

    // Serial
    QComboBox* m_serialPortCombo;
    QComboBox* m_serialBaudCombo;
    QComboBox* m_serialCmdCombo;

    // FTP
    QLineEdit* m_ftpHostEdit = nullptr;
    QSpinBox* m_ftpPortSpin = nullptr;
    QLineEdit* m_ftpUserEdit = nullptr;
    QLineEdit* m_ftpPasswordEdit = nullptr;
    QCheckBox* m_ftpTlsCheck = nullptr;

    // Tunnels
    QTableWidget* m_tunnelsTable;
    QPushButton* m_addTunnelBtn;
    QPushButton* m_deleteTunnelBtn;
    QList<TunnelConfig> m_tunnels;

    QString m_id;
};
