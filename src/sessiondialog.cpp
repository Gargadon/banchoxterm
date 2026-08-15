#include "sessiondialog.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFormLayout>
#include <QComboBox>
#include <QLineEdit>
#include <QSpinBox>
#include <QPushButton>
#include <QLabel>
#include <QStackedWidget>
#include <QFileDialog>
#include <QFontDialog>
#include <QCheckBox>
#include <QUuid>
#include "keyring.h"
#include <QTableWidget>
#include <QHeaderView>
#include <QTabWidget>
#include <QDialogButtonBox>
#include <QGroupBox>
#include <QSerialPortInfo>

class TunnelEditDialog : public QDialog {
public:
    explicit TunnelEditDialog(QWidget* parent = nullptr) : QDialog(parent) {
        setWindowTitle(tr("Add SSH Tunnel"));
        setMinimumWidth(320);

        auto* layout = new QFormLayout(this);

        m_typeCombo = new QComboBox(this);
        m_typeCombo->addItem(tr("Local (Forward local port to remote)"), static_cast<int>(TunnelConfig::Type::Local));
        m_typeCombo->addItem(tr("Remote (Forward remote port to local)"), static_cast<int>(TunnelConfig::Type::Remote));
        m_typeCombo->addItem(tr("Dynamic (SOCKS5 proxy)"), static_cast<int>(TunnelConfig::Type::Dynamic));

        m_localPortSpin = new QSpinBox(this);
        m_localPortSpin->setRange(1, 65535);
        m_localPortSpin->setValue(8080);

        m_remoteHostEdit = new QLineEdit(this);
        m_remoteHostEdit->setPlaceholderText("e.g. localhost or 192.168.1.50");

        m_remotePortSpin = new QSpinBox(this);
        m_remotePortSpin->setRange(1, 65535);
        m_remotePortSpin->setValue(80);

        layout->addRow(tr("Tunnel Type:"), m_typeCombo);
        layout->addRow(tr("Local Port:"), m_localPortSpin);
        layout->addRow(tr("Remote Host:"), m_remoteHostEdit);
        layout->addRow(tr("Remote Port:"), m_remotePortSpin);

        auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
        connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
        connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
        layout->addRow(buttons);

        connect(m_typeCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int) {
            bool isDynamic = (m_typeCombo->currentData().toInt() == static_cast<int>(TunnelConfig::Type::Dynamic));
            m_remoteHostEdit->setDisabled(isDynamic);
            m_remotePortSpin->setDisabled(isDynamic);
        });
    }

    TunnelConfig getTunnelConfig() const {
        TunnelConfig c;
        c.type = static_cast<TunnelConfig::Type>(m_typeCombo->currentData().toInt());
        c.localPort = m_localPortSpin->value();
        c.remoteHost = m_remoteHostEdit->text().trimmed();
        c.remotePort = m_remotePortSpin->value();
        return c;
    }

private:
    QComboBox* m_typeCombo;
    QSpinBox* m_localPortSpin;
    QLineEdit* m_remoteHostEdit;
    QSpinBox* m_remotePortSpin;
};

SessionDialog::SessionDialog(QWidget* parent) : QDialog(parent) {
    m_id = QUuid::createUuid().toString();
    setupUi();
    setWindowTitle(tr("New Session"));
}

SessionDialog::SessionDialog(const Session& session, QWidget* parent) : QDialog(parent) {
    m_id = session.id;
    setupUi();
    loadSession(session);
    setWindowTitle(tr("Edit Session"));
}

void SessionDialog::setupUi() {
    resize(450, 350);

    auto* mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(20, 20, 20, 20);
    mainLayout->setSpacing(15);

    auto* typeLayout = new QHBoxLayout();
    auto* typeLabel = new QLabel(tr("Session Type:"), this);
    m_typeCombo = new QComboBox(this);
    m_typeCombo->addItem(tr("SSH Session"), static_cast<int>(SessionType::SSH));
    m_typeCombo->addItem(tr("Local Terminal"), static_cast<int>(SessionType::Local));
    m_typeCombo->addItem(tr("Telnet Session"), static_cast<int>(SessionType::Telnet));
    m_typeCombo->addItem(tr("RDP Session"), static_cast<int>(SessionType::RDP));
    m_typeCombo->addItem(tr("VNC Session"), static_cast<int>(SessionType::VNC));
    m_typeCombo->addItem(tr("Serial Connection"), static_cast<int>(SessionType::Serial));
    m_typeCombo->addItem(tr("FTP Connection"), static_cast<int>(SessionType::FTP));
    typeLayout->addWidget(typeLabel);
    typeLayout->addWidget(m_typeCombo);
    mainLayout->addLayout(typeLayout);

    auto* nameLayout = new QHBoxLayout();
    auto* nameLabel = new QLabel(tr("Session Name:"), this);
    m_nameEdit = new QLineEdit(this);
    m_nameEdit->setPlaceholderText(tr("My Server"));
    nameLayout->addWidget(nameLabel);
    nameLayout->addWidget(m_nameEdit);
    mainLayout->addLayout(nameLayout);

    auto* groupLayout = new QHBoxLayout();
    auto* groupLabel = new QLabel(tr("Group:"), this);
    m_groupEdit = new QLineEdit(this);
    m_groupEdit->setPlaceholderText(tr("Optional (e.g. Production)"));
    groupLayout->addWidget(groupLabel);
    groupLayout->addWidget(m_groupEdit);
    mainLayout->addLayout(groupLayout);

    m_stackedWidget = new QStackedWidget(this);

    // ── SSH page ──
    auto* sshWidget = new QWidget(this);
    auto* sshLayout = new QVBoxLayout(sshWidget);
    sshLayout->setContentsMargins(0, 5, 0, 5);
    sshLayout->setSpacing(10);

    auto* sshTabs = new QTabWidget(sshWidget);
    sshLayout->addWidget(sshTabs);

    // Tab 1: Connection Info
    auto* connTab = new QWidget(sshTabs);
    auto* sshForm = new QFormLayout(connTab);
    connTab->setLayout(sshForm);
    sshForm->setContentsMargins(10, 10, 10, 10);
    sshForm->setSpacing(10);

    m_hostEdit = new QLineEdit(connTab);
    m_hostEdit->setPlaceholderText(tr("192.168.1.100 or example.com"));
    sshForm->addRow(tr("Host / IP:"), m_hostEdit);

    m_portSpin = new QSpinBox(connTab);
    m_portSpin->setRange(1, 65535);
    m_portSpin->setValue(22);
    sshForm->addRow(tr("Port:"), m_portSpin);

    m_userEdit = new QLineEdit(connTab);
    m_userEdit->setPlaceholderText(tr("root / username"));
    sshForm->addRow(tr("Username:"), m_userEdit);

    m_passwordEdit = new QLineEdit(connTab);
    m_passwordEdit->setEchoMode(QLineEdit::Password);
    m_passwordEdit->setPlaceholderText(tr("Optional (stored in Keyring)"));
    sshForm->addRow(tr("Password:"), m_passwordEdit);

    m_savePasswordCheck = new QCheckBox(tr("Save securely in system keyring"), connTab);
    sshForm->addRow("", m_savePasswordCheck);

    m_x11ForwardCheck = new QCheckBox(tr("Enable X11 Forwarding (-Y)"), connTab);
    sshForm->addRow("", m_x11ForwardCheck);

    m_autoReconnectCheck = new QCheckBox(tr("Auto-reconnect on disconnect"), connTab);
    sshForm->addRow("", m_autoReconnectCheck);

    auto* keyLayout = new QHBoxLayout();
    m_keyEdit = new QLineEdit(connTab);
    m_keyEdit->setPlaceholderText(tr("Optional (uses agent if empty)"));
    auto* keyBrowseBtn = new QPushButton(tr("Browse..."), connTab);
    keyLayout->addWidget(m_keyEdit);
    keyLayout->addWidget(keyBrowseBtn);
    sshForm->addRow(tr("Private Key:"), keyLayout);

    connect(keyBrowseBtn, &QPushButton::clicked, this, [this]() {
        QString path = QFileDialog::getOpenFileName(this, tr("Select Private Key"), QDir::homePath(), tr("All Files (*)"));
        if (!path.isEmpty()) {
            m_keyEdit->setText(path);
        }
    });

    auto* jumpLabel = new QLabel(tr("Optional SSH bastion (ProxyJump)"), connTab);
    sshForm->addRow(QString(), jumpLabel);

    m_jumpHostEdit = new QLineEdit(connTab);
    m_jumpHostEdit->setPlaceholderText(tr("bastion.example.com (empty = direct)"));
    sshForm->addRow(tr("Jump host:"), m_jumpHostEdit);

    m_jumpPortSpin = new QSpinBox(connTab);
    m_jumpPortSpin->setRange(1, 65535);
    m_jumpPortSpin->setValue(22);
    sshForm->addRow(tr("Jump port:"), m_jumpPortSpin);

    m_jumpUserEdit = new QLineEdit(connTab);
    m_jumpUserEdit->setPlaceholderText(tr("Optional (uses target user)"));
    sshForm->addRow(tr("Jump user:"), m_jumpUserEdit);

    auto* jumpKeyLayout = new QHBoxLayout();
    m_jumpKeyEdit = new QLineEdit(connTab);
    m_jumpKeyEdit->setPlaceholderText(tr("Optional (uses target key or agent)"));
    auto* jumpKeyBrowseBtn = new QPushButton(tr("Browse..."), connTab);
    jumpKeyLayout->addWidget(m_jumpKeyEdit);
    jumpKeyLayout->addWidget(jumpKeyBrowseBtn);
    sshForm->addRow(tr("Jump private key:"), jumpKeyLayout);

    connect(jumpKeyBrowseBtn, &QPushButton::clicked, this, [this]() {
        const QString path = QFileDialog::getOpenFileName(this, tr("Select Jump Private Key"), QDir::homePath(), tr("All Files (*)"));
        if (!path.isEmpty())
            m_jumpKeyEdit->setText(path);
    });

    sshTabs->addTab(connTab, tr("SSH Connection"));

    // Tab 2: Tunnels Config
    auto* tunnelsTab = new QWidget(sshTabs);
    auto* tunnelsLayout = new QVBoxLayout(tunnelsTab);
    tunnelsTab->setLayout(tunnelsLayout);
    tunnelsLayout->setContentsMargins(10, 10, 10, 10);
    tunnelsLayout->setSpacing(10);

    m_tunnelsTable = new QTableWidget(tunnelsTab);
    m_tunnelsTable->setColumnCount(4);
    m_tunnelsTable->setHorizontalHeaderLabels({tr("Type"), tr("Local Port"), tr("Remote Host"), tr("Remote Port")});
    m_tunnelsTable->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    m_tunnelsTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_tunnelsTable->setSelectionMode(QAbstractItemView::SingleSelection);
    tunnelsLayout->addWidget(m_tunnelsTable);

    auto* tbtnLayout = new QHBoxLayout();
    m_addTunnelBtn = new QPushButton(tr("Add Tunnel"), tunnelsTab);
    m_deleteTunnelBtn = new QPushButton(tr("Delete Tunnel"), tunnelsTab);
    tbtnLayout->addWidget(m_addTunnelBtn);
    tbtnLayout->addWidget(m_deleteTunnelBtn);
    tunnelsLayout->addLayout(tbtnLayout);

    connect(m_addTunnelBtn, &QPushButton::clicked, this, [this]() {
        TunnelEditDialog dialog(this);
        if (dialog.exec() == QDialog::Accepted) {
            TunnelConfig config = dialog.getTunnelConfig();
            m_tunnels.append(config);
            
            int row = m_tunnelsTable->rowCount();
            m_tunnelsTable->insertRow(row);
            
            QString typeStr = (config.type == TunnelConfig::Type::Local) ? tr("Local (L)") :
                              (config.type == TunnelConfig::Type::Remote) ? tr("Remote (R)") : tr("Dynamic (D)");
            m_tunnelsTable->setItem(row, 0, new QTableWidgetItem(typeStr));
            m_tunnelsTable->setItem(row, 1, new QTableWidgetItem(QString::number(config.localPort)));
            m_tunnelsTable->setItem(row, 2, new QTableWidgetItem(config.remoteHost));
            m_tunnelsTable->setItem(row, 3, new QTableWidgetItem(config.type == TunnelConfig::Type::Dynamic ? "-" : QString::number(config.remotePort)));
        }
    });

    connect(m_deleteTunnelBtn, &QPushButton::clicked, this, [this]() {
        int row = m_tunnelsTable->currentRow();
        if (row >= 0 && row < m_tunnels.size()) {
            m_tunnels.removeAt(row);
            m_tunnelsTable->removeRow(row);
        }
    });

    sshTabs->addTab(tunnelsTab, tr("Port Forwarding"));

    // Tab 3: Advanced (keep-alive, algorithms)
    auto* advancedTab = new QWidget(sshTabs);
    auto* advancedForm = new QFormLayout(advancedTab);
    advancedTab->setLayout(advancedForm);
    advancedForm->setContentsMargins(10, 10, 10, 10);
    advancedForm->setSpacing(10);

    m_keepAliveSpin = new QSpinBox(advancedTab);
    m_keepAliveSpin->setRange(0, 3600);
    m_keepAliveSpin->setValue(0);
    m_keepAliveSpin->setToolTip(tr("Seconds between SSH keep-alive probes. 0 disables it."));
    advancedForm->addRow(tr("Keep-alive (sec, 0=off):"), m_keepAliveSpin);

    m_cipherEdit = new QLineEdit(advancedTab);
    m_cipherEdit->setPlaceholderText(tr("Default"));
    m_cipherEdit->setToolTip(tr("Comma-separated, e.g. aes128-ctr,aes256-ctr"));
    advancedForm->addRow(tr("Encryption ciphers:"), m_cipherEdit);

    m_kexEdit = new QLineEdit(advancedTab);
    m_kexEdit->setPlaceholderText(tr("Default"));
    m_kexEdit->setToolTip(tr("Comma-separated, e.g. curve25519-sha256,diffie-hellman-group14-sha256"));
    advancedForm->addRow(tr("Key exchange:"), m_kexEdit);

    m_macEdit = new QLineEdit(advancedTab);
    m_macEdit->setPlaceholderText(tr("Default"));
    m_macEdit->setToolTip(tr("Comma-separated, e.g. hmac-sha2-256,hmac-sha2-512"));
    advancedForm->addRow(tr("MAC algorithms:"), m_macEdit);

    advancedForm->addRow(new QLabel(tr("Leave a field empty to use libssh2 defaults."), advancedTab));

    sshTabs->addTab(advancedTab, tr("Advanced"));

    m_stackedWidget->addWidget(sshWidget); // index 0

    // ── Local page ──
    auto* localWidget = new QWidget(this);
    auto* localForm = new QFormLayout(localWidget);
    localForm->setContentsMargins(0, 10, 0, 10);
    localForm->setSpacing(10);

    m_shellEdit = new QLineEdit(localWidget);
#ifdef Q_OS_WIN
    m_shellEdit->setText("cmd.exe");
#else
    m_shellEdit->setText("/bin/bash");
#endif
    localForm->addRow(tr("Shell Path:"), m_shellEdit);

    m_stackedWidget->addWidget(localWidget); // index 1

    // ── Telnet page ──
    auto* telnetWidget = new QWidget(this);
    auto* telnetForm = new QFormLayout(telnetWidget);
    telnetForm->setContentsMargins(0, 10, 0, 10);
    telnetForm->setSpacing(10);

    m_telnetHostEdit = new QLineEdit(telnetWidget);
    m_telnetHostEdit->setPlaceholderText(tr("192.168.1.100 or example.com"));
    telnetForm->addRow(tr("Host / IP:"), m_telnetHostEdit);

    m_telnetPortSpin = new QSpinBox(telnetWidget);
    m_telnetPortSpin->setRange(1, 65535);
    m_telnetPortSpin->setValue(23);
    telnetForm->addRow(tr("Port:"), m_telnetPortSpin);

    m_stackedWidget->addWidget(telnetWidget); // index 2

    // ── RDP page ──
    auto* rdpWidget = new QWidget(this);
    auto* rdpForm = new QFormLayout(rdpWidget);
    rdpForm->setContentsMargins(0, 10, 0, 10);
    rdpForm->setSpacing(10);

    m_rdpHostEdit = new QLineEdit(rdpWidget);
    m_rdpHostEdit->setPlaceholderText(tr("192.168.1.100 or example.com"));
    rdpForm->addRow(tr("Host / IP:"), m_rdpHostEdit);

    m_rdpPortSpin = new QSpinBox(rdpWidget);
    m_rdpPortSpin->setRange(1, 65535);
    m_rdpPortSpin->setValue(3389);
    rdpForm->addRow(tr("Port:"), m_rdpPortSpin);

    m_rdpUserEdit = new QLineEdit(rdpWidget);
    m_rdpUserEdit->setPlaceholderText(tr("Administrator / username"));
    rdpForm->addRow(tr("Username:"), m_rdpUserEdit);

    m_stackedWidget->addWidget(rdpWidget); // index 3

    // ── VNC page ──
    auto* vncWidget = new QWidget(this);
    auto* vncForm = new QFormLayout(vncWidget);
    vncForm->setContentsMargins(0, 10, 0, 10);
    vncForm->setSpacing(10);

    m_vncHostEdit = new QLineEdit(vncWidget);
    m_vncHostEdit->setPlaceholderText(tr("192.168.1.100 or example.com"));
    vncForm->addRow(tr("Host / IP:"), m_vncHostEdit);

    m_vncPortSpin = new QSpinBox(vncWidget);
    m_vncPortSpin->setRange(1, 65535);
    m_vncPortSpin->setValue(5900);
    vncForm->addRow(tr("Port:"), m_vncPortSpin);

    m_vncPasswordEdit = new QLineEdit(vncWidget);
    m_vncPasswordEdit->setEchoMode(QLineEdit::Password);
    m_vncPasswordEdit->setPlaceholderText(tr("Optional (stored in Keyring)"));
    vncForm->addRow(tr("Password:"), m_vncPasswordEdit);

    m_stackedWidget->addWidget(vncWidget); // index 4

    // ── Serial page ──
    auto* serialWidget = new QWidget(this);
    auto* serialForm = new QFormLayout(serialWidget);
    serialForm->setContentsMargins(0, 10, 0, 10);
    serialForm->setSpacing(10);

    m_serialPortCombo = new QComboBox(serialWidget);
    m_serialPortCombo->setEditable(true);
    m_serialPortCombo->setInsertPolicy(QComboBox::NoInsert);
    m_serialPortCombo->setPlaceholderText(tr("Select or enter a port (e.g. COM3 or /dev/ttyUSB0)"));
    for (const QSerialPortInfo& info : QSerialPortInfo::availablePorts()) {
        const QString name = info.portName();
        QString label = name;
        if (!info.description().isEmpty())
            label += QStringLiteral(" — ") + info.description();
        m_serialPortCombo->addItem(label, name);
    }
    if (m_serialPortCombo->count() == 0)
        m_serialPortCombo->setCurrentText(QStringLiteral("COM3"));
    serialForm->addRow(tr("Serial Port:"), m_serialPortCombo);

    m_serialBaudCombo = new QComboBox(serialWidget);
    m_serialBaudCombo->addItems({"9600", "19200", "38400", "57600", "115200"});
    m_serialBaudCombo->setCurrentText("115200");
    serialForm->addRow(tr("Baud Rate:"), m_serialBaudCombo);

    m_serialCmdCombo = new QComboBox(serialWidget);
    m_serialCmdCombo->addItems({"picocom", "screen", "minicom"});
    serialForm->addRow(tr("Serial Tool:"), m_serialCmdCombo);

    m_stackedWidget->addWidget(serialWidget); // index 5

    // ── FTP page ──
    auto* ftpWidget = new QWidget(this);
    auto* ftpForm = new QFormLayout(ftpWidget);
    ftpForm->setContentsMargins(0, 10, 0, 10);
    ftpForm->setSpacing(10);

    m_ftpHostEdit = new QLineEdit(ftpWidget);
    m_ftpHostEdit->setPlaceholderText(tr("192.168.1.100 or example.com"));
    ftpForm->addRow(tr("Host / IP:"), m_ftpHostEdit);

    m_ftpPortSpin = new QSpinBox(ftpWidget);
    m_ftpPortSpin->setRange(1, 65535);
    m_ftpPortSpin->setValue(21);
    ftpForm->addRow(tr("Port:"), m_ftpPortSpin);

    m_ftpUserEdit = new QLineEdit(ftpWidget);
    m_ftpUserEdit->setPlaceholderText(tr("anonymous / username"));
    ftpForm->addRow(tr("Username:"), m_ftpUserEdit);

    m_ftpPasswordEdit = new QLineEdit(ftpWidget);
    m_ftpPasswordEdit->setEchoMode(QLineEdit::Password);
    m_ftpPasswordEdit->setPlaceholderText(tr("Optional (stored in Keyring)"));
    ftpForm->addRow(tr("Password:"), m_ftpPasswordEdit);

    m_ftpTlsCheck = new QCheckBox(tr("Use explicit FTPS (TLS certificate validation)"), ftpWidget);
    m_ftpTlsCheck->setChecked(true);
    ftpForm->addRow(QString(), m_ftpTlsCheck);

    m_stackedWidget->addWidget(ftpWidget); // index 6

    mainLayout->addWidget(m_stackedWidget);

    // ── Shared terminal settings (scrollback, font) ──
    // Applies to SSH / Local / Telnet / Serial; hidden for RDP / VNC / FTP.
    m_terminalSettingsWidget = new QWidget(this);
    auto* termForm = new QFormLayout(m_terminalSettingsWidget);
    termForm->setContentsMargins(0, 0, 0, 0);
    termForm->setSpacing(8);

    m_scrollbackSpin = new QSpinBox(m_terminalSettingsWidget);
    m_scrollbackSpin->setRange(0, 1000000);
    m_scrollbackSpin->setSingleStep(1000);
    m_scrollbackSpin->setValue(5000);
    m_scrollbackSpin->setToolTip(tr("Lines of scrollback history. 0 disables scrollback."));
    termForm->addRow(tr("Scrollback (lines):"), m_scrollbackSpin);

    auto* fontRow = new QHBoxLayout();
    m_fontLabel = new QLabel(m_terminalSettingsWidget);
    m_font = QFont("Monospace", 11);
    m_font.setStyleHint(QFont::Monospace);
    m_font.setFixedPitch(true);
    m_fontLabel->setText(tr("Global default"));
    fontRow->addWidget(m_fontLabel, 1);
    auto* chooseFontBtn = new QPushButton(tr("Choose Font..."), m_terminalSettingsWidget);
    fontRow->addWidget(chooseFontBtn);
    termForm->addRow(tr("Font:"), fontRow);

    connect(chooseFontBtn, &QPushButton::clicked, this, [this]() {
        QFontDialog dialog(m_font, this);
        dialog.setWindowTitle(tr("Select Terminal Font"));
        dialog.setOption(QFontDialog::MonospacedFonts, true);
        if (dialog.exec() == QDialog::Accepted) {
            m_font = dialog.selectedFont();
            m_font.setFixedPitch(true);
            m_fontLabel->setText(QString("%1, %2pt").arg(m_font.family()).arg(m_font.pointSize()));
            m_fontChosen = true;
        }
    });

    mainLayout->addWidget(m_terminalSettingsWidget);

    auto updateTerminalSettingsVisibility = [this](int index) {
        // SSH(0), Local(1), Telnet(2), Serial(5) use a terminal emulator.
        const bool visible = (index == 0 || index == 1 || index == 2 || index == 5);
        m_terminalSettingsWidget->setVisible(visible);
        adjustSize();
    };
    updateTerminalSettingsVisibility(0);
    connect(m_typeCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, updateTerminalSettingsVisibility);

    connect(m_typeCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
            [this](int index) { m_stackedWidget->setCurrentIndex(index); });

    auto* btnLayout = new QHBoxLayout();
    btnLayout->addStretch();
    auto* cancelBtn = new QPushButton(tr("Cancel"), this);
    auto* saveBtn = new QPushButton(tr("Save"), this);
    saveBtn->setObjectName("primaryButton");

    btnLayout->addWidget(cancelBtn);
    btnLayout->addWidget(saveBtn);
    mainLayout->addLayout(btnLayout);

    connect(cancelBtn, &QPushButton::clicked, this, &QDialog::reject);
    connect(saveBtn, &QPushButton::clicked, this, &QDialog::accept);
}

void SessionDialog::loadSession(const Session& session) {
    m_nameEdit->setText(session.name);
    m_groupEdit->setText(session.group);

    switch (session.type) {
    case SessionType::SSH:
        m_typeCombo->setCurrentIndex(0);
        m_hostEdit->setText(session.host);
        m_portSpin->setValue(session.port);
        m_userEdit->setText(session.user);
        m_keyEdit->setText(session.keyPath);
        if (m_jumpHostEdit)
            m_jumpHostEdit->setText(session.jumpHost);
        if (m_jumpPortSpin)
            m_jumpPortSpin->setValue(session.jumpPort > 0 ? session.jumpPort : 22);
        if (m_jumpUserEdit)
            m_jumpUserEdit->setText(session.jumpUser);
        if (m_jumpKeyEdit)
            m_jumpKeyEdit->setText(session.jumpKeyPath);
        if (m_x11ForwardCheck)
            m_x11ForwardCheck->setChecked(session.x11Forwarding);
        if (m_autoReconnectCheck)
            m_autoReconnectCheck->setChecked(session.autoReconnect);

        m_tunnels = session.tunnels;
        m_tunnelsTable->setRowCount(0);
        for (const TunnelConfig& config : m_tunnels) {
            int row = m_tunnelsTable->rowCount();
            m_tunnelsTable->insertRow(row);
            
            QString typeStr = (config.type == TunnelConfig::Type::Local) ? tr("Local (L)") :
                              (config.type == TunnelConfig::Type::Remote) ? tr("Remote (R)") : tr("Dynamic (D)");
            m_tunnelsTable->setItem(row, 0, new QTableWidgetItem(typeStr));
            m_tunnelsTable->setItem(row, 1, new QTableWidgetItem(QString::number(config.localPort)));
            m_tunnelsTable->setItem(row, 2, new QTableWidgetItem(config.remoteHost));
            m_tunnelsTable->setItem(row, 3, new QTableWidgetItem(config.type == TunnelConfig::Type::Dynamic ? "-" : QString::number(config.remotePort)));
        }

        {
            QString password = Keyring::lookupPassword(session.id);
            if (!password.isEmpty()) {
                m_passwordEdit->setText(password);
                m_savePasswordCheck->setChecked(true);
            }
        }
        break;
    case SessionType::Local:
        m_typeCombo->setCurrentIndex(1);
        m_shellEdit->setText(session.shellPath);
        break;
    case SessionType::Telnet:
        m_typeCombo->setCurrentIndex(2);
        m_telnetHostEdit->setText(session.host);
        m_telnetPortSpin->setValue(session.port);
        break;
    case SessionType::RDP:
        m_typeCombo->setCurrentIndex(3);
        m_rdpHostEdit->setText(session.host);
        m_rdpPortSpin->setValue(session.port > 0 ? session.port : 3389);
        m_rdpUserEdit->setText(session.user);
        break;
    case SessionType::VNC:
        m_typeCombo->setCurrentIndex(4);
        m_vncHostEdit->setText(session.host);
        m_vncPortSpin->setValue(session.port > 0 ? session.port : 5900);
        {
            QString password = Keyring::lookupPassword(session.id);
            if (m_vncPasswordEdit && !password.isEmpty())
                m_vncPasswordEdit->setText(password);
        }
        break;
    case SessionType::Serial:
    {
        m_typeCombo->setCurrentIndex(5);
        const int portIndex = m_serialPortCombo->findData(session.serialPort);
        if (portIndex >= 0)
            m_serialPortCombo->setCurrentIndex(portIndex);
        else
            m_serialPortCombo->setCurrentText(session.serialPort);
        m_serialBaudCombo->setCurrentText(QString::number(session.baudRate));
        m_serialCmdCombo->setCurrentText(session.serialCmd);
        break;
    }
    case SessionType::FTP:
        m_typeCombo->setCurrentIndex(6);
        m_ftpHostEdit->setText(session.host);
        m_ftpPortSpin->setValue(session.port > 0 ? session.port : 21);
        m_ftpUserEdit->setText(session.user);
        m_ftpTlsCheck->setChecked(session.ftpTls);
        {
            QString password = Keyring::lookupPassword(session.id);
            if (m_ftpPasswordEdit && !password.isEmpty())
                m_ftpPasswordEdit->setText(password);
        }
        break;
    }

    // Shared terminal settings
    m_scrollbackSpin->setValue(session.scrollback > 0 ? session.scrollback : 5000);
    m_fontChosen = !session.fontFamily.isEmpty();
    if (m_fontChosen) {
        m_font = QFont(session.fontFamily, session.fontSize > 0 ? session.fontSize : 11);
        m_font.setFixedPitch(true);
        m_font.setStyleHint(QFont::Monospace);
        m_fontLabel->setText(QString("%1, %2pt").arg(m_font.family()).arg(m_font.pointSize()));
    } else {
        m_fontLabel->setText(tr("Global default"));
    }

    // SSH advanced options
    if (m_keepAliveSpin) {
        m_keepAliveSpin->setValue(qMax(0, session.keepAliveSeconds));
        m_cipherEdit->setText(session.cryptCipher);
        m_kexEdit->setText(session.kexAlgo);
        m_macEdit->setText(session.macAlgo);
    }
}

Session SessionDialog::getSession() const {
    Session s;
    s.id = m_id;
    s.name = m_nameEdit->text().trimmed();
    s.group = m_groupEdit->text().trimmed();

    int index = m_typeCombo->currentIndex();
    if (s.name.isEmpty()) {
        switch (index) {
        case 0:
            s.name = QString("SSH: %1").arg(m_hostEdit->text());
            break;
        case 1:
            s.name = tr("Local Shell");
            break;
        case 2:
            s.name = QString("Telnet: %1").arg(m_telnetHostEdit->text());
            break;
        case 3:
            s.name = QString("RDP: %1").arg(m_rdpHostEdit->text());
            break;
        case 4:
            s.name = QString("VNC: %1").arg(m_vncHostEdit->text());
            break;
        case 5:
            s.name = QString("Serial: %1").arg(m_serialPortCombo->currentText());
            break;
        case 6:
            s.name = QString("FTP: %1").arg(m_ftpHostEdit->text());
            break;
        }
    }

    switch (index) {
    case 0:
        s.type = SessionType::SSH;
        s.host = m_hostEdit->text().trimmed();
        s.port = m_portSpin->value();
        s.user = m_userEdit->text().trimmed();
        s.keyPath = m_keyEdit->text().trimmed();
        s.jumpHost = m_jumpHostEdit ? m_jumpHostEdit->text().trimmed() : QString();
        s.jumpPort = m_jumpPortSpin ? m_jumpPortSpin->value() : 22;
        s.jumpUser = m_jumpUserEdit ? m_jumpUserEdit->text().trimmed() : QString();
        s.jumpKeyPath = m_jumpKeyEdit ? m_jumpKeyEdit->text().trimmed() : QString();
        s.x11Forwarding = m_x11ForwardCheck ? m_x11ForwardCheck->isChecked() : false;
        s.autoReconnect = m_autoReconnectCheck ? m_autoReconnectCheck->isChecked() : false;
        s.tunnels = m_tunnels;
        break;
    case 1:
        s.type = SessionType::Local;
        s.shellPath = m_shellEdit->text().trimmed();
        break;
    case 2:
        s.type = SessionType::Telnet;
        s.host = m_telnetHostEdit->text().trimmed();
        s.port = m_telnetPortSpin->value();
        break;
    case 3:
        s.type = SessionType::RDP;
        s.host = m_rdpHostEdit->text().trimmed();
        s.port = m_rdpPortSpin->value();
        s.user = m_rdpUserEdit->text().trimmed();
        break;
    case 4:
        s.type = SessionType::VNC;
        s.host = m_vncHostEdit->text().trimmed();
        s.port = m_vncPortSpin->value();
        break;
    case 5:
    {
        s.type = SessionType::Serial;
        const QString visiblePort = m_serialPortCombo->currentText().trimmed();
        const int selectedPort = m_serialPortCombo->currentIndex();
        if (selectedPort >= 0 && m_serialPortCombo->itemText(selectedPort) == visiblePort)
            s.serialPort = m_serialPortCombo->itemData(selectedPort).toString().trimmed();
        if (s.serialPort.isEmpty())
            s.serialPort = visiblePort;
        s.baudRate = m_serialBaudCombo->currentText().toInt();
        s.serialCmd = m_serialCmdCombo->currentText();
        break;
    }
    case 6:
        s.type = SessionType::FTP;
        s.host = m_ftpHostEdit->text().trimmed();
        s.port = m_ftpPortSpin->value();
        s.user = m_ftpUserEdit->text().trimmed();
        s.ftpTls = m_ftpTlsCheck && m_ftpTlsCheck->isChecked();
        break;
    }

    // Shared terminal settings
    s.scrollback = m_scrollbackSpin->value();
    if (m_fontChosen) {
        s.fontFamily = m_font.family();
        s.fontSize = m_font.pointSize();
    } else {
        s.fontFamily.clear();
        s.fontSize = 0;
    }

    // SSH advanced options
    if (m_keepAliveSpin) {
        s.keepAliveSeconds = m_keepAliveSpin->value();
        s.cryptCipher = m_cipherEdit->text().trimmed();
        s.kexAlgo = m_kexEdit->text().trimmed();
        s.macAlgo = m_macEdit->text().trimmed();
    }
    return s;
}

void SessionDialog::accept() {
    if (m_typeCombo->currentIndex() == 0) {
        if (m_savePasswordCheck->isChecked()) {
            QString pwd = m_passwordEdit->text();
            if (!pwd.isEmpty()) {
                Keyring::storePassword(m_id, pwd);
            }
        } else {
            Keyring::deletePassword(m_id);
        }
    } else if (m_typeCombo->currentIndex() == 4) {
        QString pwd = m_vncPasswordEdit ? m_vncPasswordEdit->text() : QString();
        if (!pwd.isEmpty()) {
            Keyring::storePassword(m_id, pwd);
        } else {
            Keyring::deletePassword(m_id);
        }
    } else if (m_typeCombo->currentIndex() == 6) {
        QString pwd = m_ftpPasswordEdit ? m_ftpPasswordEdit->text() : QString();
        if (!pwd.isEmpty()) {
            Keyring::storePassword(m_id, pwd);
        } else {
            Keyring::deletePassword(m_id);
        }
    }
    QDialog::accept();
}
