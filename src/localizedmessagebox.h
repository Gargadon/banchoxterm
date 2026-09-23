#pragma once

#include <QCoreApplication>
#include <QAbstractButton>
#include <QIcon>
#include <QMessageBox>

// Keep these labels in BanchoXterm's catalogs instead of relying on the
// platform Qt translations, which may not be installed on Windows.
[[maybe_unused]] inline constexpr const char* kMessageBoxTranslationSources[] = {
    QT_TRANSLATE_NOOP("QMessageBox", "Yes"),
    QT_TRANSLATE_NOOP("QMessageBox", "No"),
    QT_TRANSLATE_NOOP("QMessageBox", "OK"),
    QT_TRANSLATE_NOOP("QMessageBox", "Cancel"),
};

// Qt's built-in QMessageBox translations are not always deployed with a
// Windows installation. Translate the standard labels through BanchoXterm's
// active application translator instead.
inline void localizeMessageBoxButtons(QMessageBox& box) {
    const auto translate = [](const char* source) { return QCoreApplication::translate("QMessageBox", source); };

    if (auto* button = box.button(QMessageBox::Yes)) {
        button->setText(translate("Yes"));
        button->setIcon(QIcon(QStringLiteral(":/icons/check.svg")));
    }
    if (auto* button = box.button(QMessageBox::No)) {
        button->setText(translate("No"));
        button->setIcon(QIcon(QStringLiteral(":/icons/close.svg")));
    }
    if (auto* button = box.button(QMessageBox::Ok)) {
        button->setText(translate("OK"));
        button->setIcon(QIcon(QStringLiteral(":/icons/check.svg")));
    }
    if (auto* button = box.button(QMessageBox::Cancel)) {
        button->setText(translate("Cancel"));
        button->setIcon(QIcon(QStringLiteral(":/icons/close.svg")));
    }
}

inline QMessageBox::StandardButton localizedQuestion(QWidget* parent, const QString& title, const QString& text,
                                                     QMessageBox::StandardButtons buttons,
                                                     QMessageBox::StandardButton defaultButton = QMessageBox::No,
                                                     QMessageBox::Icon icon = QMessageBox::Question) {
    QMessageBox box(parent);
    box.setIcon(icon);
    box.setWindowTitle(title);
    box.setText(text);
    box.setStandardButtons(buttons);
    box.setDefaultButton(defaultButton);
    localizeMessageBoxButtons(box);
    return static_cast<QMessageBox::StandardButton>(box.exec());
}
