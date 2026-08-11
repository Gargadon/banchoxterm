#include "settingsdialog.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QTabWidget>
#include <QComboBox>
#include <QRadioButton>
#include <QLineEdit>
#include <QPushButton>
#include <QLabel>
#include <QFileDialog>
#include <QFontDialog>
#include <QSettings>
#include <QDialogButtonBox>
#include <QMessageBox>
#include <qtermwidget.h>

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
    m_themeCombo->addItem(tr("Tokyo Night (Dark)"), true);
    m_themeCombo->addItem(tr("Classic (Light)"), false);
    m_themeCombo->setCurrentIndex(m_darkTheme ? 0 : 1);
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
    QStringList schemes = QTermWidget::availableColorSchemes();
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
}

void SettingsDialog::loadSettings() {
    QSettings settings;
    // Theme
    m_darkTheme = settings.value("theme/dark", true).toBool();

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
}

void SettingsDialog::saveSettings() {
    QSettings settings;
    m_darkTheme = m_themeCombo->currentData().toBool();
    m_useCustomEditor = m_customRadio->isChecked();
    m_customEditorPath = m_editorPathEdit->text().trimmed();

    QString newLang = m_langCombo->currentData().toString();
    m_colorScheme = m_colorSchemeCombo->currentText();

    settings.setValue("theme/dark", m_darkTheme);
    settings.setValue("terminal/font", m_font.toString());
    settings.setValue("editor/useCustom", m_useCustomEditor);
    settings.setValue("editor/customPath", m_customEditorPath);
    settings.setValue("terminal/colorScheme", m_colorScheme);

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
