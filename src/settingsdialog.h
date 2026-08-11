#pragma once
#include <QDialog>
#include <QFont>

class QComboBox;
class QLineEdit;
class QLabel;
class QRadioButton;

class SettingsDialog : public QDialog {
    Q_OBJECT
public:
    explicit SettingsDialog(QWidget* parent = nullptr);

    QFont selectedFont() const {
        return m_font;
    }
    bool isDarkTheme() const {
        return m_darkTheme;
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

private slots:
    void onChangeFontClicked();
    void onBrowseEditorClicked();
    void onAccept();

private:
    void loadSettings();
    void saveSettings();
    void updateFontLabel();

    QFont m_font;
    bool m_darkTheme = true;
    bool m_useCustomEditor = false;
    QString m_customEditorPath;
    QString m_colorScheme;
    QString m_lang;

    QLabel* m_fontPreviewLabel = nullptr;
    QComboBox* m_themeCombo = nullptr;
    QComboBox* m_colorSchemeCombo = nullptr;
    QComboBox* m_langCombo = nullptr;
    QRadioButton* m_sysDefaultRadio = nullptr;
    QRadioButton* m_customRadio = nullptr;
    QLineEdit* m_editorPathEdit = nullptr;
};
