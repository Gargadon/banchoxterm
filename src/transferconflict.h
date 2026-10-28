#pragma once
#include <QApplication>
#include <QCheckBox>
#include <QMessageBox>
#include <QLayout>
#include <QGridLayout>
#include <QDialogButtonBox>
#include <QPushButton>
#include <QSettings>
#include <QThread>
#include <memory>
#include <mutex>

// One state shared by every file and connection in a transfer queue.
struct TransferConflict {
    enum Decision { Ask = 0, Overwrite = 1, Skip = 2, Cancel = 3 };
    int decision = Ask;
    bool cancelled = false;
    std::mutex mutex;
    int resolve(const QString& path) {
        std::unique_lock<std::mutex> lock(mutex);
        int result = Cancel;
        auto prompt = [&] {
            if (cancelled) return;
            result = decision == Ask ? QSettings().value("sftp/existingFilePolicy", Ask).toInt() : decision;
            if (result != Overwrite && result != Skip) {
                QMessageBox box(QMessageBox::Question, QApplication::translate("TransferConflict", "File already exists"),
                    QApplication::translate("TransferConflict", "The destination already exists:\n%1").arg(path), QMessageBox::NoButton, QApplication::activeWindow());
                auto* overwrite = box.addButton(QApplication::translate("TransferConflict", "Overwrite"), QMessageBox::AcceptRole);
                auto* skip = box.addButton(QApplication::translate("TransferConflict", "Skip this file"), QMessageBox::ActionRole);
                auto* cancel = box.addButton(QApplication::translate("TransferConflict", "Cancel transfer"), QMessageBox::RejectRole);
                box.setTextFormat(Qt::PlainText);
                box.setDefaultButton(cancel);
                auto* batch = new QCheckBox(QApplication::translate("TransferConflict", "Apply to all conflicts in this transfer"), &box);
                box.setCheckBox(batch);
                // A second checkbox belongs to the dialog layout, independently of the batch choice.
                auto* always = new QCheckBox(QApplication::translate("TransferConflict", "Remember for future transfers"), &box);
                auto* grid = qobject_cast<QGridLayout*>(box.layout());
                auto* buttons = box.findChild<QDialogButtonBox*>();
                int row, column, rows, columns;
                grid->getItemPosition(grid->indexOf(buttons), &row, &column, &rows, &columns);
                grid->removeWidget(buttons);
                grid->addWidget(always, row, 1, 1, grid->columnCount() - 1);
                grid->addWidget(buttons, row + 1, column, rows, columns);
                box.exec();
                result = box.clickedButton() == overwrite ? Overwrite : box.clickedButton() == skip ? Skip : Cancel;
                if (result != Cancel) {
                    if (batch->isChecked()) decision = result;
                    if (always->isChecked()) QSettings().setValue("sftp/existingFilePolicy", result);
                }
            }
            if (result == Cancel) cancelled = true;
        };
        if (QThread::currentThread() == qApp->thread()) prompt();
        else QMetaObject::invokeMethod(qApp, prompt, Qt::BlockingQueuedConnection);
        return result;
    }
};
