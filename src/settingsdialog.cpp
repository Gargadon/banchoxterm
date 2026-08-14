#include "settingsdialog.h"
#include "apppaths.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QTabWidget>
#include <QComboBox>
#include <QRadioButton>
#include <QCheckBox>
#include <QLineEdit>
#include <QPushButton>
#include <QLabel>
#include <QFileDialog>
#include <QFontDialog>
#include <QSettings>
#include <QDialogButtonBox>
#include <QMessageBox>
#include <QDir>
#ifndef Q_OS_WIN
#include <qtermwidget.h>
#endif
#include <QInputDialog>
#include <QListWidget>
#include <QStandardPaths>
#include <QFile>
#include <QRegularExpression>
#include "masterpasswordmanager.h"

SettingsDialog::SettingsDialog(QWidget* parent) : QDialog(parent) {
    setWindowTitle(tr("Configuration"));
    setMinimumSize(420, 320);

    loadSettings();

    auto* mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(15, 15, 15, 15);
    mainLayout->setSpacing(12);

    auto* tabWidget = new QTabWidget(this);

    // 1. Appearance Tab
    auto* appearanceTab = new QWidget(tabWidget);
    auto* appLayout = new QVBoxLayout(appearanceTab);
    appLayout->setSpacing(15);
    appLayout->setContentsMargins(15, 15, 15, 15);

    // Theme selector
    auto* themeLayout = new QHBoxLayout();
    auto* themeLabel = new QLabel(tr("Interface Theme:"), appearanceTab);
    m_themeCombo = new QComboBox(appearanceTab);
    m_themeCombo->addItem(tr("System Native (Auto)"), "system");
    m_themeCombo->addItem(tr("Tokyo Night (Dark)"), "dark");
    m_themeCombo->addItem(tr("Classic (Light)"), "light");
    int themeIdx = m_themeCombo->findData(m_themeMode);
    if (themeIdx != -1)
        m_themeCombo->setCurrentIndex(themeIdx);
    else
        m_themeCombo->setCurrentIndex(0);

    themeLayout->addWidget(themeLabel);
    themeLayout->addWidget(m_themeCombo);
    appLayout->addLayout(themeLayout);

    // Language selector
    auto* langLayout = new QHBoxLayout();
    auto* langLabel = new QLabel(tr("Language (Requires Restart):"), appearanceTab);
    m_langCombo = new QComboBox(appearanceTab);
    m_langCombo->addItem(tr("English"), "en");
    m_langCombo->addItem(tr("Spanish"), "es");
    m_langCombo->addItem(tr("Portuguese"), "pt");
    if (m_lang == "es")
        m_langCombo->setCurrentIndex(1);
    else if (m_lang == "pt")
        m_langCombo->setCurrentIndex(2);
    else
        m_langCombo->setCurrentIndex(0);
    langLayout->addWidget(langLabel);
    langLayout->addWidget(m_langCombo);
    appLayout->addLayout(langLayout);

    // Terminal Color Scheme selector
    auto* colorSchemeLayout = new QHBoxLayout();
    auto* colorSchemeLabel = new QLabel(tr("Terminal Color Scheme:"), appearanceTab);
    m_colorSchemeCombo = new QComboBox(appearanceTab);
    QStringList schemes;
#ifndef Q_OS_WIN
    schemes = QTermWidget::availableColorSchemes();
#else
    schemes = {"BlackOnLightYellow", "BlackOnRandomLight", "BlackOnWhite", "Breeze", "BreezeModified",
               "DarkPastels", "Falcon", "GreenOnBlack", "Linux", "Nord", "Solarized", "SolarizedLight",
               "Tango", "Ubuntu", "WhiteOnBlack"};
#endif
    m_colorSchemeCombo->addItems(schemes);
    int schemeIdx = schemes.indexOf(m_colorScheme);
    if (schemeIdx != -1)
        m_colorSchemeCombo->setCurrentIndex(schemeIdx);
    colorSchemeLayout->addWidget(colorSchemeLabel);
    colorSchemeLayout->addWidget(m_colorSchemeCombo);
    appLayout->addLayout(colorSchemeLayout);

    // Font selector
    auto* fontGroupBox = new QWidget(appearanceTab);
    auto* fontLayout = new QVBoxLayout(fontGroupBox);
    fontLayout->setContentsMargins(0, 0, 0, 0);
    fontLayout->setSpacing(8);

    auto* fontTitle = new QLabel(tr("Terminal Typography:"), appearanceTab);
    fontTitle->setStyleSheet("font-weight: bold;");
    fontLayout->addWidget(fontTitle);

    auto* fontBtnLayout = new QHBoxLayout();
    m_fontPreviewLabel = new QLabel(appearanceTab);
    updateFontLabel();
    auto* changeFontBtn = new QPushButton(tr("Choose Font..."), appearanceTab);
    fontBtnLayout->addWidget(m_fontPreviewLabel, 1);
    fontBtnLayout->addWidget(changeFontBtn);
    fontLayout->addLayout(fontBtnLayout);

    appLayout->addWidget(fontGroupBox);

    m_shellIntegrationCheck = new QCheckBox(tr("Enable remote shell integration (directory tracking)"), appearanceTab);
    m_shellIntegrationCheck->setChecked(m_enableShellIntegration);
    appLayout->addWidget(m_shellIntegrationCheck);

    m_loggingCheck = new QCheckBox(tr("Log session output to file"), appearanceTab);
    m_loggingCheck->setChecked(m_loggingEnabled);
    appLayout->addWidget(m_loggingCheck);

    auto* logDirWidget = new QWidget(appearanceTab);
    auto* logDirLayout = new QHBoxLayout(logDirWidget);
    logDirLayout->setContentsMargins(20, 0, 0, 0);
    logDirLayout->setSpacing(8);

    auto* logDirLabel = new QLabel(tr("Log directory:"), logDirWidget);
    m_logDirEdit = new QLineEdit(m_logDir, logDirWidget);
    m_logDirEdit->setPlaceholderText(tr("Default: Documents"));
    m_logDirEdit->setEnabled(m_loggingEnabled);
    auto* logDirBrowseBtn = new QPushButton(tr("Browse..."), logDirWidget);
    logDirBrowseBtn->setEnabled(m_loggingEnabled);

    logDirLayout->addWidget(logDirLabel);
    logDirLayout->addWidget(m_logDirEdit, 1);
    logDirLayout->addWidget(logDirBrowseBtn);
    appLayout->addWidget(logDirWidget);

    connect(logDirBrowseBtn, &QPushButton::clicked, this, [this]() {
        QString dir = QFileDialog::getExistingDirectory(this, tr("Select Log Directory"),
                                                        m_logDirEdit->text().isEmpty() ? QDir::homePath()
                                                                                       : m_logDirEdit->text());
        if (!dir.isEmpty())
            m_logDirEdit->setText(dir);
    });
    connect(m_loggingCheck, &QCheckBox::toggled, this, [this, logDirBrowseBtn](bool checked) {
        m_logDirEdit->setEnabled(checked);
        logDirBrowseBtn->setEnabled(checked);
    });

    appLayout->addStretch();
    tabWidget->addTab(appearanceTab, tr("Appearance"));

    // 2. Editor Tab
    auto* editorTab = new QWidget(tabWidget);
    auto* editorLayout = new QVBoxLayout(editorTab);
    editorLayout->setSpacing(12);
    editorLayout->setContentsMargins(15, 15, 15, 15);

    auto* editorTitle = new QLabel(tr("External Text Editor:"), editorTab);
    editorTitle->setStyleSheet("font-weight: bold;");
    editorLayout->addWidget(editorTitle);

    m_sysDefaultRadio = new QRadioButton(tr("Use System Default Text Editor"), editorTab);
    m_customRadio = new QRadioButton(tr("Use Custom Text Editor Command:"), editorTab);

    if (m_useCustomEditor) {
        m_customRadio->setChecked(true);
    } else {
        m_sysDefaultRadio->setChecked(true);
    }

    editorLayout->addWidget(m_sysDefaultRadio);
    editorLayout->addWidget(m_customRadio);

    auto* customEditorWidget = new QWidget(editorTab);
    auto* customEditLayout = new QHBoxLayout(customEditorWidget);
    customEditLayout->setContentsMargins(20, 0, 0, 0);
    customEditLayout->setSpacing(8);

    m_editorPathEdit = new QLineEdit(m_customEditorPath, customEditorWidget);
    m_editorPathEdit->setPlaceholderText(tr("e.g. /usr/bin/nano, kate, code..."));
    m_editorPathEdit->setEnabled(m_useCustomEditor);

    auto* browseBtn = new QPushButton(tr("Browse..."), customEditorWidget);
    browseBtn->setEnabled(m_useCustomEditor);

    customEditLayout->addWidget(m_editorPathEdit, 1);
    customEditLayout->addWidget(browseBtn);
    editorLayout->addWidget(customEditorWidget);

    editorLayout->addStretch();
    tabWidget->addTab(editorTab, tr("Text Editor"));

    // 3. Security Tab
    auto* securityTab = new QWidget(tabWidget);
    auto* securityLayout = new QVBoxLayout(securityTab);
    securityLayout->setSpacing(12);
    securityLayout->setContentsMargins(15, 15, 15, 15);

    auto* securityTitle = new QLabel(tr("Master Password:"), securityTab);
    securityTitle->setStyleSheet("font-weight: bold;");
    securityLayout->addWidget(securityTitle);

    m_masterPasswordCheck = new QCheckBox(tr("Use Master Password to protect session credentials"), securityTab);
    m_masterPasswordCheck->setChecked(MasterPasswordManager::instance().isEnabled());
    securityLayout->addWidget(m_masterPasswordCheck);

    auto* knownHostsTitle = new QLabel(tr("SSH Host Keys:"), securityTab);
    knownHostsTitle->setStyleSheet("font-weight: bold;");
    securityLayout->addWidget(knownHostsTitle);

    auto* knownHostsBtn = new QPushButton(tr("Manage Known Hosts..."), securityTab);
    securityLayout->addWidget(knownHostsBtn);
    connect(knownHostsBtn, &QPushButton::clicked, this, &SettingsDialog::onManageKnownHosts);

    securityLayout->addStretch();
    tabWidget->addTab(securityTab, tr("Security"));

    mainLayout->addWidget(tabWidget);

    // Dialog buttons
    auto* buttonBox = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    mainLayout->addWidget(buttonBox);

    // Event Connections
    connect(changeFontBtn, &QPushButton::clicked, this, &SettingsDialog::onChangeFontClicked);
    connect(browseBtn, &QPushButton::clicked, this, &SettingsDialog::onBrowseEditorClicked);
    connect(buttonBox, &QDialogButtonBox::accepted, this, &SettingsDialog::onAccept);
    connect(buttonBox, &QDialogButtonBox::rejected, this, &QDialog::reject);

    connect(m_customRadio, &QRadioButton::toggled, this, [this, browseBtn](bool checked) {
        m_editorPathEdit->setEnabled(checked);
        browseBtn->setEnabled(checked);
    });

    connect(m_masterPasswordCheck, &QCheckBox::clicked, this, [this](bool checked) {
        if (checked) {
            bool ok;
            QString pwd1 = QInputDialog::getText(this, tr("Set Master Password"),
                                                 tr("Enter new Master Password:"), QLineEdit::Password, "", &ok);
            if (!ok || pwd1.isEmpty()) {
                m_masterPasswordCheck->setChecked(false);
                return;
            }
            QString pwd2 = QInputDialog::getText(this, tr("Confirm Master Password"),
                                                 tr("Confirm new Master Password:"), QLineEdit::Password, "", &ok);
            if (!ok || pwd2.isEmpty() || pwd1 != pwd2) {
                QMessageBox::warning(this, tr("Passwords Mismatch"), tr("Passwords do not match or are empty."));
                m_masterPasswordCheck->setChecked(false);
                return;
            }

            MasterPasswordManager::instance().setMasterPassword(pwd1);
            QMessageBox::information(this, tr("Master Password Set"), tr("All future session passwords will be encrypted with your Master Password."));
        } else {
            bool ok;
            QString currentPwd = QInputDialog::getText(this, tr("Disable Master Password"),
                                                       tr("Enter current Master Password:"), QLineEdit::Password, "", &ok);
            if (!ok || currentPwd.isEmpty()) {
                m_masterPasswordCheck->setChecked(true);
                return;
            }
            if (MasterPasswordManager::instance().disableMasterPassword(currentPwd)) {
                QMessageBox::information(this, tr("Master Password Disabled"), tr("Master Password protection is now disabled. Saved passwords will be stored in plain text."));
            } else {
                QMessageBox::warning(this, tr("Incorrect Password"), tr("The Master Password you entered is incorrect. Protection remains enabled."));
                m_masterPasswordCheck->setChecked(true);
            }
        }
    });
}

void SettingsDialog::loadSettings() {
    QSettings settings;
    // Theme
    if (settings.contains("theme/mode")) {
        m_themeMode = settings.value("theme/mode", "system").toString();
    } else if (settings.contains("theme/dark")) {
        m_themeMode = settings.value("theme/dark", true).toBool() ? "dark" : "light";
    } else {
        m_themeMode = "system";
    }

    // Font
    if (settings.contains("terminal/font")) {
        m_font.fromString(settings.value("terminal/font").toString());
    } else {
        m_font = QFont("Monospace", 11);
        m_font.setStyleHint(QFont::Monospace);
    }
    m_font.setFixedPitch(true);

    // Editor
    m_useCustomEditor = settings.value("editor/useCustom", false).toBool();
    m_customEditorPath = settings.value("editor/customPath", "").toString();

    // Color Scheme
    m_colorScheme = settings.value("terminal/colorScheme", "DarkPastels").toString();

    // Language
    m_lang = settings.value("locale/lang", "en").toString();

    // Shell Integration
    m_enableShellIntegration = settings.value("terminal/shellIntegration", true).toBool();

    // Logging
    m_loggingEnabled = settings.value("terminal/loggingEnabled", false).toBool();
    m_logDir = settings.value("terminal/logDirectory", "").toString();
}

void SettingsDialog::saveSettings() {
    QSettings settings;
    m_themeMode = m_themeCombo->currentData().toString();
    m_useCustomEditor = m_customRadio->isChecked();
    m_customEditorPath = m_editorPathEdit->text().trimmed();

    QString newLang = m_langCombo->currentData().toString();
    m_colorScheme = m_colorSchemeCombo->currentText();

    m_enableShellIntegration = m_shellIntegrationCheck->isChecked();
    m_loggingEnabled = m_loggingCheck->isChecked();
    m_logDir = m_logDirEdit->text().trimmed();

    settings.setValue("theme/mode", m_themeMode);
    settings.setValue("theme/dark", (m_themeMode == "dark"));
    settings.setValue("terminal/font", m_font.toString());
    settings.setValue("editor/useCustom", m_useCustomEditor);
    settings.setValue("editor/customPath", m_customEditorPath);
    settings.setValue("terminal/colorScheme", m_colorScheme);
    settings.setValue("terminal/shellIntegration", m_enableShellIntegration);
    settings.setValue("terminal/loggingEnabled", m_loggingEnabled);
    settings.setValue("terminal/logDirectory", m_logDir);

    if (newLang != m_lang) {
        settings.setValue("locale/lang", newLang);
        QMessageBox::information(this, tr("Restart Required"),
                                 tr("Please restart BanchoXterm to apply the language change."));
    }
}

void SettingsDialog::updateFontLabel() {
    m_fontPreviewLabel->setText(QString("%1, %2pt").arg(m_font.family()).arg(m_font.pointSize()));
    m_fontPreviewLabel->setFont(m_font);
}

void SettingsDialog::onChangeFontClicked() {
    QFontDialog dialog(m_font, this);
    dialog.setWindowTitle(tr("Select Terminal Font"));
    dialog.setOption(QFontDialog::MonospacedFonts, true);

    if (dialog.exec() == QDialog::Accepted) {
        m_font = dialog.selectedFont();
        m_font.setFixedPitch(true);
        updateFontLabel();
    }
}

void SettingsDialog::onBrowseEditorClicked() {
    QString path = QFileDialog::getOpenFileName(this, tr("Select Text Editor Executable"), "/usr/bin");
    if (!path.isEmpty()) {
        m_editorPathEdit->setText(path);
    }
}

void SettingsDialog::onAccept() {
    saveSettings();
    accept();
}

void SettingsDialog::onManageKnownHosts() {
    const QString path = AppPaths::configDir() + "/known_hosts";
    QFile file(path);
    QStringList lines;
    if (file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        lines = QString::fromUtf8(file.readAll()).split('\n');
        file.close();
    }

    QDialog dlg(this);
    dlg.setWindowTitle(tr("Known Hosts"));
    dlg.setMinimumWidth(480);
    auto* lay = new QVBoxLayout(&dlg);

    auto* list = new QListWidget(&dlg);
    lay->addWidget(list);

    QList<int> lineIndexes;
    for (int i = 0; i < lines.size(); ++i) {
        const QString t = lines[i].trimmed();
        if (t.isEmpty() || t.startsWith('#'))
            continue;
        const QStringList parts = t.split(QRegularExpression("\\s+"), Qt::SkipEmptyParts);
        QString label = parts.size() >= 2 ? QString("%1  (%2)").arg(parts[0], parts[1]) : t;
        list->addItem(label);
        lineIndexes.append(i);
    }

    auto* btnRow = new QHBoxLayout();
    auto* delBtn = new QPushButton(tr("Remove Selected"), &dlg);
    auto* closeBtn = new QPushButton(tr("Close"), &dlg);
    btnRow->addWidget(delBtn);
    btnRow->addStretch();
    btnRow->addWidget(closeBtn);
    lay->addLayout(btnRow);

    bool changed = false;
    connect(closeBtn, &QPushButton::clicked, &dlg, &QDialog::accept);
    connect(delBtn, &QPushButton::clicked, &dlg, [&]() {
        const int row = list->currentRow();
        if (row < 0 || row >= lineIndexes.size())
            return;
        lines[lineIndexes[row]].clear();
        list->takeItem(row);
        lineIndexes.removeAt(row);
        changed = true;
    });

    dlg.exec();

    if (changed) {
        QFile out(path);
        if (out.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text)) {
            QStringList remaining;
            for (const QString& l : lines)
                if (!l.trimmed().isEmpty())
                    remaining.append(l);
            if (!remaining.isEmpty())
                out.write(remaining.join('\n').toUtf8() + '\n');
            out.close();
        }
    }
}
