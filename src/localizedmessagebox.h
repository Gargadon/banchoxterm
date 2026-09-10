#pragma once

#include <QCoreApplication>
#include <QAbstractButton>
#include <QMessageBox>

// Qt's built-in QMessageBox translations are not always deployed with a
// Windows installation. Translate the standard labels through BanchoXterm's
// active application translator instead.
inline void localizeMessageBoxButtons(QMessageBox& box) {
    const auto translate = [](const char* source) {
        return QCoreApplication::translate("QMessageBox", source);
    };

    if (auto* button = box.button(QMessageBox::Yes))
        button->setText(translate("Yes"));
    if (auto* button = box.button(QMessageBox::No))
        button->setText(translate("No"));
    if (auto* button = box.button(QMessageBox::Ok))
        button->setText(translate("OK"));
    if (auto* button = box.button(QMessageBox::Cancel))
        button->setText(translate("Cancel"));
}

inline QMessageBox::StandardButton localizedQuestion(QWidget* parent, const QString& title, const QString& text,
                                                     QMessageBox::StandardButtons buttons,
                                                     QMessageBox::StandardButton defaultButton = QMessageBox::No) {
    QMessageBox box(parent);
    box.setIcon(QMessageBox::Question);
    box.setWindowTitle(title);
    box.setText(text);
    box.setStandardButtons(buttons);
    box.setDefaultButton(defaultButton);
    localizeMessageBoxButtons(box);
    return static_cast<QMessageBox::StandardButton>(box.exec());
}
