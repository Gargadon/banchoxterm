#pragma once
#include <QDialog>
#include <QFont>

class QComboBox;
class QLineEdit;
class QLabel;
class QRadioButton;
class QCheckBox;

class SettingsDialog : public QDialog {
    Q_OBJECT
public:
    explicit SettingsDialog(QWidget* parent = nullptr);

    QFont selectedFont() const {
        return m_font;
    }
    QString themeMode() const {
        return m_themeMode;
    }
    bool useCustomEditor() const {
        return m_useCustomEditor;
    }
    QString customEditorPath() const {
        return m_customEditorPath;
    }
    QString selectedColorScheme() const {
        return m_colorScheme;
    }
    QString selectedLang() const {
        return m_lang;
    }
    bool enableShellIntegration() const {
        return m_enableShellIntegration;
    }

private slots:
    void onChangeFontClicked();
    void onBrowseEditorClicked();
    void onAccept();

private:
    void loadSettings();
    void saveSettings();
    void updateFontLabel();

    QFont m_font;
    QString m_themeMode = "system";

    bool m_useCustomEditor = false;
    QString m_customEditorPath;
    QString m_colorScheme;
    QString m_lang;
    bool m_enableShellIntegration = true;
    bool m_loggingEnabled = false;
    QString m_logDir;

    QLabel* m_fontPreviewLabel = nullptr;
    QComboBox* m_themeCombo = nullptr;
    QComboBox* m_colorSchemeCombo = nullptr;
    QComboBox* m_langCombo = nullptr;
    QRadioButton* m_sysDefaultRadio = nullptr;
    QRadioButton* m_customRadio = nullptr;
    QLineEdit* m_editorPathEdit = nullptr;
    QCheckBox* m_shellIntegrationCheck = nullptr;
    QCheckBox* m_loggingCheck = nullptr;
    QLineEdit* m_logDirEdit = nullptr;
    QCheckBox* m_masterPasswordCheck = nullptr;
};
