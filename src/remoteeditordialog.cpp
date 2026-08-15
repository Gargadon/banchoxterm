#include "remoteeditordialog.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QLabel>
#include <QFile>
#include <QTextStream>
#include <QMessageBox>

RemoteEditorDialog::RemoteEditorDialog(const QString& fileName, const QString& localPath, QWidget* parent)
    : QDialog(parent), m_localPath(localPath) {
    setupUi(fileName);
    loadFile();
}

RemoteEditorDialog::~RemoteEditorDialog() {
}

void RemoteEditorDialog::setupUi(const QString& fileName) {
    resize(700, 500);
    setWindowTitle(tr("Edit Remote File: %1").arg(fileName));

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(15, 15, 15, 15);
    layout->setSpacing(12);

    auto* header = new QLabel(tr("Editing: %1").arg(fileName), this);
    layout->addWidget(header);

    m_textEdit = new QPlainTextEdit(this);
    layout->addWidget(m_textEdit);

    m_statusLabel = new QLabel(this);
    layout->addWidget(m_statusLabel);

    auto* btns = new QHBoxLayout();
    btns->addStretch();

    auto* cancelBtn = new QPushButton(tr("Cancel"), this);
    auto* saveBtn = new QPushButton(tr("Save"), this);
    saveBtn->setObjectName("primaryButton");

    btns->addWidget(cancelBtn);
    btns->addWidget(saveBtn);
    layout->addLayout(btns);

    connect(cancelBtn, &QPushButton::clicked, this, &QDialog::reject);
    connect(saveBtn, &QPushButton::clicked, this, &RemoteEditorDialog::onSave);
}

void RemoteEditorDialog::loadFile() {
    QFile file(m_localPath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        QMessageBox::critical(this, tr("Error"), tr("Could not open local temp file for reading."));
        reject();
        return;
    }

    QTextStream in(&file);
    m_textEdit->setPlainText(in.readAll());
    file.close();
    m_statusLabel->setText(tr("File loaded successfully."));
}

void RemoteEditorDialog::onSave() {
    QFile file(m_localPath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        QMessageBox::critical(this, tr("Error"), tr("Could not open local temp file for writing."));
        return;
    }

    QTextStream out(&file);
    out << m_textEdit->toPlainText();
    file.close();

    m_saved = true;
    accept();
}
