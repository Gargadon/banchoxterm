#pragma once
#include <QWidget>
#include <QJsonArray>
#include <QRegularExpression>
#include <QColor>

namespace Konsole {
class TerminalDisplay;
}

struct TerminalHighlightRule {
    QString pattern;
    QColor color;
    bool regex = false;
    bool caseSensitive = false;
    bool enabled = true;
    QRegularExpression expression() const;
};

namespace TerminalHighlighting {
QJsonArray defaults();
QJsonArray load();
QList<TerminalHighlightRule> rules(const QJsonArray&);
QString validate(const QJsonArray&);
} // namespace TerminalHighlighting

// An application-owned, mouse-transparent marker layer. The emulation, selected
// text, ANSI colors and saved output are never changed by highlighting.
class TerminalHighlighter : public QWidget {
public:
    explicit TerminalHighlighter(Konsole::TerminalDisplay* display);
    void reload();

protected:
    void paintEvent(QPaintEvent*) override;
    bool eventFilter(QObject*, QEvent*) override;

private:
    struct Marker {
        QRect cells;
        QColor color;
    };
    void rebuild();
    Konsole::TerminalDisplay* m_display;
    QList<TerminalHighlightRule> m_rules;
    QList<QRegularExpression> m_expressions;
    QList<Marker> m_markers;
    bool m_dirty = true;
};
