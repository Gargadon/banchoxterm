#pragma once
#include <QObject>

class QWidget;

// Checks GitHub releases for a newer BanchoXterm and offers to download it.
// Installed edition downloads and runs the NSIS installer; portable edition
// downloads the portable ZIP and swaps it in place after the app exits.
class Updater : public QObject {
    Q_OBJECT
public:
    static void checkForUpdates(QWidget* parent);
};
