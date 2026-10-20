#include "terminalappearance.h"
#include <QMainWindow>
#include <QSettings>
#include <QPainter>
#include <QOperatingSystemVersion>
#include <QTimer>
#include <QApplication>
#include <QFontDatabase>
#include <qtermwidget.h>
#include <cmath>
#ifdef BANCHO_HAVE_KWINDOWSYSTEM
#include <KWindowEffects>
#endif
#ifdef Q_OS_WIN
#include <windows.h>
#include <dwmapi.h>
#endif

namespace {
class GlassWindowFilter : public QObject {
public:
    explicit GlassWindowFilter(QMainWindow* window) : QObject(window), m_window(window) {
    }

protected:
    bool eventFilter(QObject* watched, QEvent* event) override {
        if (watched != m_window) {
            if (event->type() == QEvent::Show || event->type() == QEvent::Hide || event->type() == QEvent::Move ||
                event->type() == QEvent::Resize) {
                auto* terminal = qobject_cast<QTermWidget*>(watched);
                if (terminal && terminal->window() == m_window)
                    QTimer::singleShot(0, m_window, [this]() { TerminalAppearance::refreshWindow(m_window); });
            }
            return false;
        }
        if (event->type() == QEvent::Show || event->type() == QEvent::Resize ||
            event->type() == QEvent::WindowActivate || event->type() == QEvent::WindowDeactivate ||
            event->type() == QEvent::ApplicationPaletteChange) {
            QTimer::singleShot(0, m_window, [this]() { TerminalAppearance::refreshWindow(m_window); });
        } else if (event->type() == QEvent::Paint) {
            QPainter painter(m_window);
            painter.setCompositionMode(QPainter::CompositionMode_Source);
            painter.fillRect(m_window->rect(), Qt::transparent);
            QRegion opaque(m_window->rect());
            if (TerminalAppearance::opacityPercent() < 100) {
                for (auto* terminal : m_window->findChildren<QTermWidget*>()) {
                    if (terminal->isVisible())
                        opaque -= QRect(terminal->mapTo(m_window, QPoint()), terminal->size());
                }
            }
            painter.setClipRegion(opaque);
            painter.fillRect(m_window->rect(), m_window->palette().color(QPalette::Window));
            return true;
        }
        return false;
    }

private:
    QMainWindow* m_window;
};
} // namespace

namespace TerminalAppearance {
QFont defaultFont() {
    QFont font = QFontDatabase::systemFont(QFontDatabase::FixedFont);
    font.setPointSize(11);
    font.setStyleHint(QFont::Monospace);
    font.setFixedPitch(true);
    return font;
}

QFont globalFont() {
    QFont font = defaultFont();
    QFont stored;
    const QString value = QSettings().value("terminal/font").toString();
    if (!value.trimmed().isEmpty() && stored.fromString(value) && !stored.family().trimmed().isEmpty())
        font = stored;
    font.setStyleHint(QFont::Monospace);
    font.setFixedPitch(true);
    return font;
}

int opacityPercent() {
    return qBound(0, QSettings().value("terminal/backgroundOpacity", 100).toInt(), 100);
}

bool blurSupported() {
#ifdef Q_OS_WIN
    const auto version = QOperatingSystemVersion::current();
    return version.majorVersion() >= 10 && version.microVersion() >= 22621;
#elif defined(BANCHO_HAVE_KWINDOWSYSTEM)
    return KWindowEffects::isEffectAvailable(KWindowEffects::BlurBehind);
#else
    return false;
#endif
}

void refreshWindow(QWidget* window) {
    const bool translucent = window->testAttribute(Qt::WA_TranslucentBackground);
    for (auto* terminal : window->findChildren<QTermWidget*>())
        terminal->setTerminalOpacity(translucent ? opacityPercent() / 100.0 : 1.0);
    if (!window->property("terminalGlass").toBool())
        return;
#ifdef BANCHO_HAVE_KWINDOWSYSTEM
    if (auto* handle = window->windowHandle()) {
        QRegion region;
        for (auto* terminal : window->findChildren<QTermWidget*>()) {
            if (terminal->isVisible())
                region += QRect(terminal->mapTo(window, QPoint()), terminal->size());
        }
        // KWindowSystem retains the request while Wayland capabilities are
        // announced asynchronously, and applies it when blur becomes available.
        const bool blur = translucent && opacityPercent() < 100 && !region.isEmpty() &&
                          QSettings().value("terminal/backgroundBlur", false).toBool();
        KWindowEffects::enableBlurBehind(handle, blur, region);
        window->setProperty("terminalBlurActive", blur);
    }
#endif
#ifdef Q_OS_WIN
    if (QApplication::platformName() == "windows") {
        const HWND hwnd = reinterpret_cast<HWND>(window->winId());
        // Let DWM composite the client area's alpha without replacing the
        // native non-client frame with a Qt layered, frameless window.
        const DWM_BLURBEHIND alpha{DWM_BB_ENABLE, TRUE, nullptr, FALSE};
        DwmEnableBlurBehindWindow(hwnd, &alpha);
        const bool blur = opacityPercent() < 100 && QSettings().value("terminal/backgroundBlur", false).toBool();
        const int backdrop = blur && blurSupported() ? 3 : 1; // DWMSBT_TRANSIENTWINDOW / NONE
        const HRESULT result = DwmSetWindowAttribute(hwnd, 38, &backdrop, sizeof(backdrop));
        window->setProperty("terminalBlurActive", blur && blurSupported() && SUCCEEDED(result));
        // A system backdrop also supplies the caption background unless it
        // has an explicit opaque color. Keep native rendering and controls,
        // using Windows' dark-mode and title-bar accent preferences.
        const QSettings personalization(
            QStringLiteral("HKEY_CURRENT_USER\\Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize"),
            QSettings::NativeFormat);
        const bool dark = personalization.value("AppsUseLightTheme", 1).toUInt() == 0;
        COLORREF caption = dark ? RGB(32, 32, 32) : RGB(255, 255, 255);
        const QSettings dwm(QStringLiteral("HKEY_CURRENT_USER\\Software\\Microsoft\\Windows\\DWM"),
                            QSettings::NativeFormat);
        const QString accentKey = window->isActiveWindow() ? "AccentColor" : "AccentColorInactive";
        if (dwm.value("ColorPrevalence", 0).toUInt() != 0 && dwm.contains(accentKey))
            caption = dwm.value(accentKey).toUInt() & 0x00ffffff;
        // Background, text and native button glyphs must agree. Setting only
        // CAPTION_COLOR leaves Windows' previous (often black) foreground.
        auto linear = [](double value) {
            value /= 255.0;
            return value <= 0.04045 ? value / 12.92 : std::pow((value + 0.055) / 1.055, 2.4);
        };
        BOOL darkCaption = 0.2126 * linear(GetRValue(caption)) + 0.7152 * linear(GetGValue(caption)) +
                               0.0722 * linear(GetBValue(caption)) <
                           0.179;
        COLORREF text = darkCaption ? RGB(255, 255, 255) : RGB(0, 0, 0);
        HIGHCONTRAST contrast{sizeof(HIGHCONTRAST)};
        if (SystemParametersInfo(SPI_GETHIGHCONTRAST, sizeof(contrast), &contrast, 0) &&
            (contrast.dwFlags & HCF_HIGHCONTRASTON)) {
            caption = GetSysColor(window->isActiveWindow() ? COLOR_ACTIVECAPTION : COLOR_INACTIVECAPTION);
            text = GetSysColor(window->isActiveWindow() ? COLOR_CAPTIONTEXT : COLOR_INACTIVECAPTIONTEXT);
            darkCaption = GetRValue(text) + GetGValue(text) + GetBValue(text) > 384;
        }
        if (!window->property("terminalCaptionDark").isValid() ||
            window->property("terminalCaptionDark").toBool() != bool(darkCaption)) {
            DwmSetWindowAttribute(hwnd, 20, &darkCaption, sizeof(darkCaption)); // native button glyphs
            window->setProperty("terminalCaptionDark", bool(darkCaption));
        }
        DwmSetWindowAttribute(hwnd, 35, &caption, sizeof(caption)); // DWMWA_CAPTION_COLOR
        DwmSetWindowAttribute(hwnd, 36, &text, sizeof(text));       // DWMWA_TEXT_COLOR
        // Extend from the bottom through the client area only. Negative
        // margins make the caption glass too; zero margins lose client alpha.
        RECT client{};
        GetClientRect(hwnd, &client);
        const MARGINS margins{0, 0, 0, client.bottom};
        DwmExtendFrameIntoClientArea(hwnd, &margins);
    }
#endif
    window->update();
}

void initializeWindow(QMainWindow* window) {
    if (opacityPercent() == 100)
        return;
    window->setProperty("terminalGlass", true);
    window->setStyleSheet(QStringLiteral("QTabWidget#terminalTabs::pane { background: transparent; }"));
    window->setAttribute(Qt::WA_TranslucentBackground);
    auto* filter = new GlassWindowFilter(window);
#ifdef BANCHO_HAVE_KWINDOWSYSTEM
    // Terminal geometry also changes when switching tabs or resizing splits.
    qApp->installEventFilter(filter);
#else
    window->installEventFilter(filter);
#endif
}
} // namespace TerminalAppearance
