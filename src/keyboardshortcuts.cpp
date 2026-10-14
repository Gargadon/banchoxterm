#include "keyboardshortcuts.h"

#include <QAction>
#include <QCoreApplication>
#include <QSettings>
#include <QWidget>

KeyboardShortcuts& KeyboardShortcuts::instance() {
    static auto* shortcuts = new KeyboardShortcuts(QCoreApplication::instance());
    return *shortcuts;
}

QList<ShortcutDefinition> KeyboardShortcuts::definitions() {
    const auto keys = [](const char* value) {
        return QKeySequence::fromString(QString::fromLatin1(value), QKeySequence::PortableText);
    };
    return {
        {"closeTab", tr("Close session"), keys("Ctrl+Shift+W")},
        {"quit", tr("Quit application"), keys("Ctrl+Shift+Q")},
        {"findTerminal", tr("Find in current terminal"), keys("Ctrl+Shift+F")},
        {"findAll", tr("Find in all sessions"), keys("Ctrl+Alt+Shift+F")},
        {"copy", tr("Copy selection"), keys("Ctrl+Shift+C")},
        {"paste", tr("Paste"), keys("Ctrl+Shift+V")},
        {"zoomIn", tr("Zoom in"), keys("Ctrl+Shift++")},
        {"zoomOut", tr("Zoom out"), keys("Ctrl+Shift+-")},
        {"commandPalette", tr("Command palette"), keys("Ctrl+Shift+P")},
        {"lockApplication", tr("Lock application"), keys("Ctrl+Alt+Shift+L")},
        {"toggleSplit", tr("Toggle split view"), keys("Ctrl+Shift+O")},
        {"toggleGrid", tr("Toggle grid view"), keys("Ctrl+Alt+Shift+G")},
        {"moveTab", tr("Move tab to other pane"), keys("Ctrl+Alt+Shift+M")},
        {"detachTab", tr("Detach tab"), keys("Ctrl+Alt+Shift+D")},
    };
}

QKeySequence KeyboardShortcuts::sequence(const QString& id) {
    QSettings settings;
    const QString key = "shortcuts/" + id;
    // An explicit empty value disables a shortcut; absence means the default.
    if (settings.contains(key))
        return QKeySequence::fromString(settings.value(key).toString(), QKeySequence::PortableText);
    for (const auto& definition : definitions())
        if (definition.id == id)
            return definition.defaultSequence;
    return {};
}

void KeyboardShortcuts::bindAction(QAction* action, const QString& id, Qt::ShortcutContext context) {
    action->setObjectName("shortcut_" + id);
    action->setShortcutContext(context);
    if (context == Qt::WindowShortcut) {
        // Menus/ribbon pages may be hidden. Associate window actions with the
        // owning window too, so their shortcuts do not depend on page visibility.
        QWidget* window = nullptr;
        for (QObject* owner = action->parent(); owner; owner = owner->parent())
            if (auto* widget = qobject_cast<QWidget*>(owner); widget && widget->isWindow())
                window = widget;
        if (window)
            window->addAction(action);
    }
    const QString tooltip = action->toolTip().remove('&');
    const auto update = [action, id, tooltip]() {
        const QKeySequence keys = sequence(id);
        action->setShortcut(keys);
        action->setToolTip(keys.isEmpty() ? tooltip : tooltip + " (" + keys.toString(QKeySequence::NativeText) + ")");
    };
    connect(&instance(), &KeyboardShortcuts::changed, action, update);
    update();
}

void KeyboardShortcuts::save(const QMap<QString, QKeySequence>& sequences) {
    QSettings settings;
    for (const auto& definition : definitions()) {
        if (!sequences.contains(definition.id))
            continue;
        const QString key = "shortcuts/" + definition.id;
        const QKeySequence keys = sequences.value(definition.id);
        if (keys == definition.defaultSequence)
            settings.remove(key);
        else
            settings.setValue(key, keys.toString(QKeySequence::PortableText));
    }
    emit instance().changed();
}
