#pragma once

#include <QObject>
#include <QKeySequence>
#include <QMap>
#include <QList>

class QAction;

struct ShortcutDefinition {
    QString id;
    QString label;
    QKeySequence defaultSequence;
};

class KeyboardShortcuts : public QObject {
    Q_OBJECT
public:
    static KeyboardShortcuts& instance();
    static QList<ShortcutDefinition> definitions();
    static QKeySequence sequence(const QString& id);
    static void bindAction(QAction* action, const QString& id, Qt::ShortcutContext context = Qt::WindowShortcut);
    static void save(const QMap<QString, QKeySequence>& sequences);

signals:
    void changed();

private:
    explicit KeyboardShortcuts(QObject* parent) : QObject(parent) {
    }
};
