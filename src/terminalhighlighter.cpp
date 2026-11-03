#include "terminalhighlighter.h"
#include <QJsonObject>
#include <QJsonDocument>
#include <QSettings>
#include <QPainter>
#include <QEvent>
#include <QInputMethodQueryEvent>
#include <QCoreApplication>
#include <TerminalDisplay.h>
#include <ScreenWindow.h>
#include <Character.h>

QRegularExpression TerminalHighlightRule::expression() const {
    // PCRE2 limits keep user-supplied backtracking patterns bounded even on a
    // malicious line. Limit subject size independently in rebuild().
    return QRegularExpression(QStringLiteral("(*LIMIT_MATCH=10000)(*LIMIT_DEPTH=100)") +
                                  (regex ? pattern : QRegularExpression::escape(pattern)),
                              caseSensitive ? QRegularExpression::NoPatternOption
                                            : QRegularExpression::CaseInsensitiveOption);
}
QJsonArray TerminalHighlighting::defaults() {
    return {QJsonObject{{"pattern", "\\b(error|failed|fatal|denied)\\b"}, {"regex", true}, {"color", "#f87171"}},
            QJsonObject{{"pattern", "\\b(warning|warn)\\b"}, {"regex", true}, {"color", "#fbbf24"}},
            QJsonObject{{"pattern", "\\b(success|passed|ok)\\b"}, {"regex", true}, {"color", "#4ade80"}}};
}
QJsonArray TerminalHighlighting::load() {
    QSettings settings;
    if (!settings.contains("terminal/highlightRules"))
        return defaults();
    return QJsonDocument::fromJson(settings.value("terminal/highlightRules").toByteArray()).array();
}
QList<TerminalHighlightRule> TerminalHighlighting::rules(const QJsonArray& json) {
    QList<TerminalHighlightRule> result;
    for (const auto& value : json) {
        const auto o = value.toObject();
        TerminalHighlightRule rule{o["pattern"].toString(), QColor(o["color"].toString()), o["regex"].toBool(),
                                   o["caseSensitive"].toBool(), o["enabled"].toBool(true)};
        if (result.size() >= 32)
            break;
        if (rule.enabled && !rule.pattern.isEmpty() && rule.pattern.size() <= 256 && rule.color.isValid() &&
            rule.expression().isValid())
            result.append(rule);
    }
    return result;
}
QString TerminalHighlighting::validate(const QJsonArray& json) {
    if (json.size() > 32)
        return QCoreApplication::translate("TerminalHighlighting", "Use at most 32 highlighting rules.");
    int row = 0;
    for (const auto& value : json) {
        ++row;
        const auto o = value.toObject();
        const QString pattern = o["pattern"].toString();
        const auto rule = TerminalHighlightRule{pattern, QColor(o["color"].toString()), o["regex"].toBool(),
                                                o["caseSensitive"].toBool()};
        QString reason;
        if (pattern.isEmpty() || pattern.size() > 256)
            reason = QCoreApplication::translate("TerminalHighlighting", "Enter a pattern of 1–256 characters.");
        else if (!rule.color.isValid())
            reason = QCoreApplication::translate("TerminalHighlighting", "Choose a valid color.");
        else if (!rule.expression().isValid())
            reason = rule.expression().errorString();
        if (!reason.isEmpty())
            return QCoreApplication::translate("TerminalHighlighting", "Rule %1: %2").arg(row).arg(reason);
    }
    return {};
}
TerminalHighlighter::TerminalHighlighter(Konsole::TerminalDisplay* display) : QWidget(display), m_display(display) {
    setObjectName("terminalHighlightOverlay");
    setAttribute(Qt::WA_TransparentForMouseEvents);
    setAttribute(Qt::WA_NoSystemBackground);
    setFocusPolicy(Qt::NoFocus);
    display->installEventFilter(this);
    if (auto* screen = display->screenWindow()) {
        auto dirty = [this] {
            m_dirty = true;
            update();
        };
        connect(screen, &Konsole::ScreenWindow::outputChanged, this, dirty);
        connect(screen, &Konsole::ScreenWindow::scrolled, this, dirty);
        connect(screen, &Konsole::ScreenWindow::selectionChanged, this, dirty);
    }
    reload();
}
void TerminalHighlighter::reload() {
    m_rules = QSettings().value("terminal/highlightingEnabled", false).toBool()
                  ? TerminalHighlighting::rules(TerminalHighlighting::load())
                  : QList<TerminalHighlightRule>{};
    m_expressions.clear();
    for (const auto& rule : m_rules)
        m_expressions.append(rule.expression());
    m_dirty = true;
    setGeometry(m_display->rect());
    setVisible(!m_rules.isEmpty());
    raise();
    update();
}
bool TerminalHighlighter::eventFilter(QObject*, QEvent* event) {
    if (event->type() == QEvent::Resize || event->type() == QEvent::FontChange || event->type() == QEvent::Show) {
        setGeometry(m_display->rect());
        m_dirty = true;
        update();
    }
    return false;
}
void TerminalHighlighter::rebuild() {
    m_markers.clear();
    m_dirty = false;
    auto* window = m_display->screenWindow();
    if (!window)
        return;
    const auto* image = window->getImage();
    const int columns = window->windowColumns();
    const int rows = window->windowLines();
    const auto properties = window->getLineProperties();
    QString text;
    QList<QPoint> positions;
    auto flush = [&] {
        for (int ruleIndex = 0; ruleIndex < m_rules.size(); ++ruleIndex) {
            const auto& rule = m_rules[ruleIndex];
            auto matches = m_expressions[ruleIndex].globalMatch(text);
            int count = 0;
            while (matches.hasNext() && ++count <= 1024) {
                const auto match = matches.next();
                const int end = int(match.capturedEnd());
                int i = int(match.capturedStart());
                while (i < end && i < positions.size()) {
                    const QPoint start = positions[i];
                    QPoint last = start;
                    while (++i < end && i < positions.size() && positions[i].y() == start.y())
                        last = positions[i];
                    int width = 1;
                    if (last.x() + 1 < columns && image[last.y() * columns + last.x() + 1].character == 0)
                        width = 2;
                    m_markers.append({QRect(start, QSize(last.x() - start.x() + width, 1)), rule.color});
                }
            }
        }
        text.clear();
        positions.clear();
    };
    for (int y = 0; y < rows; ++y) {
        // Double-size DEC lines have a different cell geometry; leave their
        // native rendering intact instead of painting misplaced markers.
        if (properties.value(y) & (Konsole::LINE_DOUBLEWIDTH | Konsole::LINE_DOUBLEHEIGHT)) {
            flush();
            continue;
        }
        for (int x = 0; x < columns; ++x) {
            const auto& cell = image[y * columns + x];
            if (!cell.character)
                continue; // trailing half of a wide character
            QString part;
            if (cell.rendition & RE_EXTENDED_CHAR) {
                ushort length = 0;
                const auto* chars = Konsole::ExtendedCharTable::instance.lookupExtendedChar(cell.character, length);
                if (chars)
                    part = QString::fromUcs4(reinterpret_cast<const char32_t*>(chars), length);
            } else {
                const char32_t c = cell.character;
                part = QString::fromUcs4(&c, 1);
            }
            if (text.size() + part.size() > 8192)
                flush();
            text += part;
            for (int i = 0; i < part.size(); ++i)
                positions.append(QPoint(x, y));
        }
        if (!(properties.value(y) & Konsole::LINE_WRAPPED))
            flush();
    }
    flush();
}
void TerminalHighlighter::paintEvent(QPaintEvent*) {
    if (m_dirty)
        rebuild();
    auto* screen = m_display->screenWindow();
    if (!screen)
        return;
    const int width = m_display->fontWidth();
    const int height = m_display->fontHeight();
    QInputMethodQueryEvent query(Qt::ImCursorRectangle);
    QCoreApplication::sendEvent(m_display, &query);
    const QPoint cursor = screen->cursorPosition();
    const QPoint origin =
        query.value(Qt::ImCursorRectangle).toRect().topLeft() - QPoint(cursor.x() * width, cursor.y() * height);
    QPainter painter(this);
    for (const auto& marker : m_markers) {
        QColor color = marker.color;
        color.setAlpha(64);
        for (int x = marker.cells.left(); x <= marker.cells.right(); ++x) {
            if (!screen->isSelected(x, marker.cells.y()) && QPoint(x, marker.cells.y()) != cursor)
                painter.fillRect(QRect(origin + QPoint(x * width, marker.cells.y() * height), QSize(width, height)),
                                 color);
        }
    }
}
