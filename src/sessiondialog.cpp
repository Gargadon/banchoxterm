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
#include <QCheckBox>
#include <QUuid>
#include "keyring.h"

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

    // Session Type
    auto* typeLayout = new QHBoxLayout();
    auto* typeLabel = new QLabel(tr("Session Type:"), this);
    m_typeCombo = new QComboBox(this);
    m_typeCombo->addItem(tr("SSH Session"), static_cast<int>(SessionType::SSH));
    m_typeCombo->addItem(tr("Local Terminal"), static_cast<int>(SessionType::Local));
    m_typeCombo->addItem(tr("Telnet Session"), static_cast<int>(SessionType::Telnet));
    m_typeCombo->addItem(tr("Serial Connection"), static_cast<int>(SessionType::Serial));
    typeLayout->addWidget(typeLabel);
    typeLayout->addWidget(m_typeCombo);
    mainLayout->addLayout(typeLayout);

    // Session Name
    auto* nameLayout = new QHBoxLayout();
    auto* nameLabel = new QLabel(tr("Session Name:"), this);
    m_nameEdit = new QLineEdit(this);
    m_nameEdit->setPlaceholderText(tr("My Server"));
    nameLayout->addWidget(nameLabel);
    nameLayout->addWidget(m_nameEdit);
    mainLayout->addLayout(nameLayout);

    // Stacked Widget for SSH vs Local settings
    m_stackedWidget = new QStackedWidget(this);

    // SSH Widget
    auto* sshWidget = new QWidget(this);
    auto* sshForm = new QFormLayout(sshWidget);
    sshForm->setContentsMargins(0, 10, 0, 10);
    sshForm->setSpacing(10);

    m_hostEdit = new QLineEdit(sshWidget);
    m_hostEdit->setPlaceholderText("192.168.1.100 or example.com");
    sshForm->addRow(tr("Host / IP:"), m_hostEdit);

    m_portSpin = new QSpinBox(sshWidget);
    m_portSpin->setRange(1, 65535);
    m_portSpin->setValue(22);
    sshForm->addRow(tr("Port:"), m_portSpin);

    m_userEdit = new QLineEdit(sshWidget);
    m_userEdit->setPlaceholderText("root / username");
    sshForm->addRow(tr("Username:"), m_userEdit);

    m_passwordEdit = new QLineEdit(sshWidget);
    m_passwordEdit->setEchoMode(QLineEdit::Password);
    m_passwordEdit->setPlaceholderText(tr("Optional (stored in Keyring)"));
    sshForm->addRow(tr("Password:"), m_passwordEdit);

    m_savePasswordCheck = new QCheckBox(tr("Save securely in system keyring"), sshWidget);
    sshForm->addRow("", m_savePasswordCheck);

    m_x11ForwardCheck = new QCheckBox(tr("Enable X11 Forwarding (-Y)"), sshWidget);
    sshForm->addRow("", m_x11ForwardCheck);

    // Key file SSH option
    auto* keyLayout = new QHBoxLayout();
    m_keyEdit = new QLineEdit(sshWidget);
    m_keyEdit->setPlaceholderText(tr("Optional (uses agent if empty)"));
    auto* keyBrowseBtn = new QPushButton(tr("Browse..."), sshWidget);
    keyLayout->addWidget(m_keyEdit);
    keyLayout->addWidget(keyBrowseBtn);
    sshForm->addRow(tr("Private Key:"), keyLayout);

    m_stackedWidget->addWidget(sshWidget);

    // Local Widget
    auto* localWidget = new QWidget(this);
    auto* localForm = new QFormLayout(localWidget);
    localForm->setContentsMargins(0, 10, 0, 10);
    localForm->setSpacing(10);

    m_shellEdit = new QLineEdit(localWidget);
    m_shellEdit->setText("/bin/bash");
    localForm->addRow(tr("Shell Path:"), m_shellEdit);

    m_stackedWidget->addWidget(localWidget);

    // Telnet Widget
    auto* telnetWidget = new QWidget(this);
    auto* telnetForm = new QFormLayout(telnetWidget);
    telnetForm->setContentsMargins(0, 10, 0, 10);
    telnetForm->setSpacing(10);

    m_telnetHostEdit = new QLineEdit(telnetWidget);
    m_telnetHostEdit->setPlaceholderText("192.168.1.100 or example.com");
    telnetForm->addRow(tr("Host / IP:"), m_telnetHostEdit);

    m_telnetPortSpin = new QSpinBox(telnetWidget);
    m_telnetPortSpin->setRange(1, 65535);
    m_telnetPortSpin->setValue(23);
    telnetForm->addRow(tr("Port:"), m_telnetPortSpin);

    m_stackedWidget->addWidget(telnetWidget);

    // Serial Widget
    auto* serialWidget = new QWidget(this);
    auto* serialForm = new QFormLayout(serialWidget);
    serialForm->setContentsMargins(0, 10, 0, 10);
    serialForm->setSpacing(10);

    m_serialPortEdit = new QLineEdit(serialWidget);
    m_serialPortEdit->setText("/dev/ttyUSB0");
    serialForm->addRow(tr("Serial Port:"), m_serialPortEdit);

    m_serialBaudCombo = new QComboBox(serialWidget);
    m_serialBaudCombo->addItems({"9600", "19200", "38400", "57600", "115200"});
    m_serialBaudCombo->setCurrentText("115200");
    serialForm->addRow(tr("Baud Rate:"), m_serialBaudCombo);

    m_serialCmdCombo = new QComboBox(serialWidget);
    m_serialCmdCombo->addItems({"picocom", "screen", "minicom"});
    serialForm->addRow(tr("Serial Tool:"), m_serialCmdCombo);

    m_stackedWidget->addWidget(serialWidget);

    mainLayout->addWidget(m_stackedWidget);

    // Connect combobox switch
    connect(m_typeCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
            [this](int index) { m_stackedWidget->setCurrentIndex(index); });

    // Connect browse button
    connect(keyBrowseBtn, &QPushButton::clicked, this, [this]() {
        QString path =
            QFileDialog::getOpenFileName(this, tr("Select Private Key"), QDir::homePath(), tr("All Files (*)"));
        if (!path.isEmpty()) {
            m_keyEdit->setText(path);
        }
    });

    // Action buttons
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
    if (session.type == SessionType::SSH) {
        m_typeCombo->setCurrentIndex(0);
        m_hostEdit->setText(session.host);
        m_portSpin->setValue(session.port);
        m_userEdit->setText(session.user);
        m_keyEdit->setText(session.keyPath);
        m_x11ForwardCheck->setChecked(session.x11Forwarding);

        QString password = Keyring::lookupPassword(session.id);
        if (!password.isEmpty()) {
            m_passwordEdit->setText(password);
            m_savePasswordCheck->setChecked(true);
        }
    } else if (session.type == SessionType::Local) {
        m_typeCombo->setCurrentIndex(1);
        m_shellEdit->setText(session.shellPath);
    } else if (session.type == SessionType::Telnet) {
        m_typeCombo->setCurrentIndex(2);
        m_telnetHostEdit->setText(session.host);
        m_telnetPortSpin->setValue(session.port);
    } else if (session.type == SessionType::Serial) {
        m_typeCombo->setCurrentIndex(3);
        m_serialPortEdit->setText(session.serialPort);
        m_serialBaudCombo->setCurrentText(QString::number(session.baudRate));
        m_serialCmdCombo->setCurrentText(session.serialCmd);
    }
}

Session SessionDialog::getSession() const {
    Session s;
    s.id = m_id;
    s.name = m_nameEdit->text().trimmed();

    int index = m_typeCombo->currentIndex();
    if (s.name.isEmpty()) {
        if (index == 0)
            s.name = QString("SSH: %1").arg(m_hostEdit->text());
        else if (index == 1)
            s.name = tr("Local Shell");
        else if (index == 2)
            s.name = QString("Telnet: %1").arg(m_telnetHostEdit->text());
        else if (index == 3)
            s.name = QString("Serial: %1").arg(m_serialPortEdit->text());
    }

    if (index == 0) {
        s.type = SessionType::SSH;
        s.host = m_hostEdit->text().trimmed();
        s.port = m_portSpin->value();
        s.user = m_userEdit->text().trimmed();
        s.keyPath = m_keyEdit->text().trimmed();
        s.x11Forwarding = m_x11ForwardCheck->isChecked();
    } else if (index == 1) {
        s.type = SessionType::Local;
        s.shellPath = m_shellEdit->text().trimmed();
    } else if (index == 2) {
        s.type = SessionType::Telnet;
        s.host = m_telnetHostEdit->text().trimmed();
        s.port = m_telnetPortSpin->value();
    } else if (index == 3) {
        s.type = SessionType::Serial;
        s.serialPort = m_serialPortEdit->text().trimmed();
        s.baudRate = m_serialBaudCombo->currentText().toInt();
        s.serialCmd = m_serialCmdCombo->currentText();
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
    }
    QDialog::accept();
}
