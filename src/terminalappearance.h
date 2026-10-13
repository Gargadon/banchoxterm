#pragma once

class QMainWindow;
class QWidget;

namespace TerminalAppearance {
int opacityPercent();
bool blurSupported();
void initializeWindow(QMainWindow* window);
void refreshWindow(QWidget* window);
} // namespace TerminalAppearance
