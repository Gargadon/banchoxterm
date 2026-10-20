#pragma once
#include <QFont>

class QMainWindow;
class QWidget;

namespace TerminalAppearance {
QFont defaultFont();
QFont globalFont();
int opacityPercent();
bool blurSupported();
void initializeWindow(QMainWindow* window);
void refreshWindow(QWidget* window);
} // namespace TerminalAppearance
