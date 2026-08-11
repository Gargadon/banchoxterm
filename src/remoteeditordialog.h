#pragma once
#include <QDialog>

class QPlainTextEdit;
class QLabel;

class RemoteEditorDialog : public QDialog {
    Q_OBJECT
public:
    explicit RemoteEditorDialog(const QString& fileName, const QString& localPath, QWidget* parent = nullptr);
    ~RemoteEditorDialog() override;

    bool isSaved() const {
        return m_saved;
    }

private slots:
    void onSave();

private:
    void setupUi(const QString& fileName);
    void loadFile();

    QString m_localPath;
    QPlainTextEdit* m_textEdit;
    QLabel* m_statusLabel;
    bool m_saved = false;
};
