#include "vtterminalwidget.h"
#include <QKeyEvent>
#include <QMouseEvent>
#include <QWheelEvent>
#include <QPainter>
#include <QFontMetrics>
#include <QApplication>
#include <QClipboard>
#include <QScrollBar>
#include <QDebug>
#include <utility>

namespace {

struct Palette {
    QColor fg;
    QColor bg;
    QColor basic[8];
    QColor bright[8];
};

const Palette& darkPalette() {
    static const Palette p = {
        QColor("#c0caf5"),
        QColor("#16161e"),
        {QColor("#15161e"), QColor("#f7768e"), QColor("#9ece6a"), QColor("#e0af68"), QColor("#7aa2f7"),
         QColor("#bb9af7"), QColor("#7dcfff"), QColor("#a9b1d6")},
        {QColor("#414868"), QColor("#ff8998"), QColor("#b9f27c"), QColor("#ff9e64"), QColor("#89b4fa"),
         QColor("#cba6f7"), QColor("#89ddff"), QColor("#c0caf5")},
    };
    return p;
}

QColor color256(int idx) {
    if (idx < 16)
        return darkPalette().basic[idx % 8];
    if (idx < 232) {
        int n = idx - 16;
        int r = n / 36;
        int g = (n % 36) / 6;
        int b = n % 6;
        const int vals[6] = {0, 95, 135, 175, 215, 255};
        return QColor(vals[r], vals[g], vals[b]);
    }
    int v = 8 + (idx - 232) * 10;
    return QColor(v, v, v);
}

ushort decodeUtf8(const QByteArray& data, int& i) {
    unsigned char c = static_cast<unsigned char>(data[i]);
    if (c < 0x80) {
        i += 1;
        return c;
    }
    if (c < 0xE0) {
        if (i + 1 < data.size()) {
            unsigned char c2 = static_cast<unsigned char>(data[i + 1]);
            i += 2;
            return ((c & 0x1F) << 6) | (c2 & 0x3F);
        }
    } else if (c < 0xF0) {
        if (i + 2 < data.size()) {
            unsigned char c2 = static_cast<unsigned char>(data[i + 1]);
            unsigned char c3 = static_cast<unsigned char>(data[i + 2]);
            i += 3;
            return ((c & 0x0F) << 12) | ((c2 & 0x3F) << 6) | (c3 & 0x3F);
        }
    }
    i += 1;
    return c;
}

} // namespace

VtTerminalWidget::VtTerminalWidget(QWidget* parent) : QWidget(parent) {
    m_font = QFont("Monospace", 11);
    m_font.setStyleHint(QFont::Monospace);
    m_font.setFixedPitch(true);

    setFocusPolicy(Qt::StrongFocus);
    setAttribute(Qt::WA_OpaquePaintEvent);

    m_attrs.fg = darkPalette().fg;
    m_attrs.bg = darkPalette().bg;

    recomputeMetrics();
    ensureSize();
}

VtTerminalWidget::~VtTerminalWidget() {
}

QSize VtTerminalWidget::sizeHint() const {
    return QSize(m_charWidth * m_cols, m_charHeight * m_rows);
}

void VtTerminalWidget::recomputeMetrics() {
    QFontMetrics fm(m_font);
    m_charWidth = fm.horizontalAdvance('M');
    m_charHeight = fm.height();
}

void VtTerminalWidget::resizeEvent(QResizeEvent* e) {
    QWidget::resizeEvent(e);
    int newCols = qMax(1, width() / m_charWidth);
    int newRows = qMax(1, height() / m_charHeight);
    if (newCols != m_cols || newRows != m_rows) {
        // Preserve the visible screen content across the resize.
        QVector<Cell> old = m_cells;
        int oldCols = m_cols;
        int oldRows = m_rows;
        m_cols = newCols;
        m_rows = newRows;
        m_cells.fill(Cell{}, m_cols * m_rows);
        for (int y = 0; y < qMin(oldRows, m_rows); y++)
            for (int x = 0; x < qMin(oldCols, m_cols); x++)
                m_cells[y * m_cols + x] = old[y * oldCols + x];
        m_cursorX = qMin(m_cursorX, m_cols - 1);
        m_cursorY = qMin(m_cursorY, m_rows - 1);
        m_scrollback.clear();
        m_scrollOffset = 0;
        m_selecting = false;
        emit resized(m_cols, m_rows);
    }
}

void VtTerminalWidget::ensureSize() {
    m_cells.fill({}, m_cols * m_rows);
    if (m_cursorX >= m_cols)
        m_cursorX = m_cols - 1;
    if (m_cursorY >= m_rows)
        m_cursorY = m_rows - 1;
}

void VtTerminalWidget::resetAttributes() {
    m_attrs = Cell{};
    m_attrs.fg = darkPalette().fg;
    m_attrs.bg = darkPalette().bg;
}

void VtTerminalWidget::scrollUp() {
    m_scrollback.append(m_cells.mid(0, m_cols));
    while (m_scrollback.size() > m_scrollbackMax)
        m_scrollback.removeFirst();
    m_cells.remove(0, m_cols);
    m_cells.append(QVector<Cell>(m_cols, Cell{}));
    // Keep the scrolled-up view anchored to the same content.
    if (m_scrollOffset > 0)
        m_scrollOffset = qMin(m_scrollOffset + 1, m_scrollback.size());
}

void VtTerminalWidget::newLine() {
    m_cursorX = 0;
    if (m_cursorY == m_rows - 1) {
        scrollUp();
    } else {
        m_cursorY++;
    }
}

void VtTerminalWidget::setChar(int x, int y, ushort ch) {
    if (x < 0 || x >= m_cols || y < 0 || y >= m_rows)
        return;
    Cell& cell = m_cells[y * m_cols + x];
    cell.ch = ch;
    cell.fg = m_attrs.fg;
    cell.bg = m_attrs.bg;
    cell.bold = m_attrs.bold;
    cell.dim = m_attrs.dim;
    cell.italic = m_attrs.italic;
    cell.underline = m_attrs.underline;
    cell.inverse = m_attrs.inverse;
    cell.fgSet = m_attrs.fgSet;
    cell.bgSet = m_attrs.bgSet;
}

void VtTerminalWidget::writeData(const QByteArray& data) {
    parse(data);
    update();
}

void VtTerminalWidget::parse(const QByteArray& data) {
    int i = 0;
    while (i < data.size()) {
        unsigned char c = static_cast<unsigned char>(data[i]);

        if (c == 0x1B) {
            i++;
            if (i < data.size() && data[i] == '[') {
                i++;
                parseCsi(data, i);
            } else if (i < data.size() && data[i] == ']') {
                i++;
                parseOsc(data, i);
            } else if (i < data.size() && data[i] == '(') {
                i++;
                if (i < data.size())
                    i++;
            } else if (i < data.size() && data[i] == ')') {
                i++;
                if (i < data.size())
                    i++;
            } else if (i < data.size() && data[i] == '7') {
                i++;
            } else if (i < data.size() && data[i] == '8') {
                i++;
            } else if (i < data.size() && data[i] == 'M') {
                i++;
                m_cursorY = qMax(0, m_cursorY - 1);
            }
            continue;
        }

        if (c == '\r') {
            m_cursorX = 0;
            i++;
        } else if (c == '\n') {
            newLine();
            i++;
        } else if (c == '\b') {
            m_cursorX = qMax(0, m_cursorX - 1);
            i++;
        } else if (c == '\t') {
            m_cursorX = qMin(m_cols - 1, ((m_cursorX / 8) + 1) * 8);
            i++;
        } else if (c == 0x07) {
            i++; // bell, ignore
        } else if (c < 0x20) {
            i++; // other control chars ignored
        } else {
            ushort ch = decodeUtf8(data, i);
            if (m_cursorX >= m_cols) {
                m_cursorX = 0;
                newLine();
            }
            setChar(m_cursorX, m_cursorY, ch);
            m_cursorX++;
        }
    }
}

void VtTerminalWidget::parseCsi(const QByteArray& data, int& i) {
    QString params;
    while (i < data.size()) {
        char c = data[i];
        if (c >= 0x40 && c <= 0x7E) {
            // final byte
            QVector<int> nums;
            if (!params.isEmpty()) {
                const QStringList parts = params.split(';');
                for (const QString& p : parts) {
                    bool ok = false;
                    int v = p.toInt(&ok);
                    nums.append(ok ? v : 0);
                }
            }
            switch (c) {
            case 'A':
                m_cursorY = qMax(0, m_cursorY - (nums.isEmpty() ? 1 : qMax(1, nums[0])));
                break;
            case 'B':
                m_cursorY = qMin(m_rows - 1, m_cursorY + (nums.isEmpty() ? 1 : qMax(1, nums[0])));
                break;
            case 'C':
                m_cursorX = qMin(m_cols - 1, m_cursorX + (nums.isEmpty() ? 1 : qMax(1, nums[0])));
                break;
            case 'D':
                m_cursorX = qMax(0, m_cursorX - (nums.isEmpty() ? 1 : qMax(1, nums[0])));
                break;
            case 'E':
                m_cursorX = 0;
                m_cursorY = qMin(m_rows - 1, m_cursorY + (nums.isEmpty() ? 1 : qMax(1, nums[0])));
                break;
            case 'F':
                m_cursorX = 0;
                m_cursorY = qMax(0, m_cursorY - (nums.isEmpty() ? 1 : qMax(1, nums[0])));
                break;
            case 'G':
                m_cursorX = qBound(0, (nums.isEmpty() ? 1 : nums[0]) - 1, m_cols - 1);
                break;
            case 'd':
                m_cursorY = qBound(0, (nums.isEmpty() ? 1 : nums[0]) - 1, m_rows - 1);
                break;
            case 'H':
            case 'f': {
                int row = nums.size() > 0 ? nums[0] : 1;
                int col = nums.size() > 1 ? nums[1] : 1;
                m_cursorY = qBound(0, row - 1, m_rows - 1);
                m_cursorX = qBound(0, col - 1, m_cols - 1);
                break;
            }
            case 'J': {
                int mode = nums.isEmpty() ? 0 : nums[0];
                if (mode == 2 || mode == 3) {
                    for (int y = 0; y < m_rows; y++)
                        for (int x = 0; x < m_cols; x++)
                            m_cells[y * m_cols + x] = Cell{};
                    m_scrollback.clear();
                } else if (mode == 0) {
                    for (int x = m_cursorX; x < m_cols; x++)
                        m_cells[m_cursorY * m_cols + x] = Cell{};
                    for (int y = m_cursorY + 1; y < m_rows; y++)
                        for (int x = 0; x < m_cols; x++)
                            m_cells[y * m_cols + x] = Cell{};
                } else if (mode == 1) {
                    for (int y = 0; y < m_cursorY; y++)
                        for (int x = 0; x < m_cols; x++)
                            m_cells[y * m_cols + x] = Cell{};
                    for (int x = 0; x <= m_cursorX; x++)
                        m_cells[m_cursorY * m_cols + x] = Cell{};
                }
                break;
            }
            case 'K': {
                int mode = nums.isEmpty() ? 0 : nums[0];
                if (mode == 0) {
                    for (int x = m_cursorX; x < m_cols; x++)
                        m_cells[m_cursorY * m_cols + x] = Cell{};
                } else if (mode == 1) {
                    for (int x = 0; x <= m_cursorX; x++)
                        m_cells[m_cursorY * m_cols + x] = Cell{};
                } else if (mode == 2) {
                    for (int x = 0; x < m_cols; x++)
                        m_cells[m_cursorY * m_cols + x] = Cell{};
                }
                break;
            }
            case 'm':
                applySgr(nums);
                break;
            case 'L': {
                int n = nums.isEmpty() ? 1 : qMax(1, nums[0]);
                for (int k = 0; k < n; k++) {
                    m_cells.insert(m_cursorY * m_cols, m_cols, Cell{});
                    m_cells.remove(m_rows * m_cols, m_cols);
                }
                break;
            }
            case 'M': {
                int n = nums.isEmpty() ? 1 : qMax(1, nums[0]);
                for (int k = 0; k < n; k++) {
                    m_cells.remove(m_cursorY * m_cols, m_cols);
                    m_cells.append(QVector<Cell>(m_cols, Cell{}));
                }
                break;
            }
            case 'P': {
                int n = nums.isEmpty() ? 1 : qMax(1, nums[0]);
                for (int x = m_cursorX; x < m_cols - n; x++)
                    m_cells[m_cursorY * m_cols + x] = m_cells[m_cursorY * m_cols + x + n];
                for (int x = m_cols - n; x < m_cols; x++)
                    m_cells[m_cursorY * m_cols + x] = Cell{};
                break;
            }
            case '@': {
                int n = nums.isEmpty() ? 1 : qMax(1, nums[0]);
                for (int x = m_cols - 1; x >= m_cursorX + n; x--)
                    m_cells[m_cursorY * m_cols + x] = m_cells[m_cursorY * m_cols + x - n];
                for (int x = m_cursorX; x < m_cursorX + n && x < m_cols; x++)
                    m_cells[m_cursorY * m_cols + x] = Cell{};
                break;
            }
            case 'h':
            case 'l':
            case 'r':
            case 'n':
            case 'X':
                break; // ignored / not needed for minimal terminal
            default:
                break;
            }
            i++;
            return;
        }
        params.append(c);
        i++;
    }
}

void VtTerminalWidget::parseOsc(const QByteArray& data, int& i) {
    QString str;
    while (i < data.size()) {
        char c = data[i];
        if (c == 0x07) {
            i++;
            break;
        }
        if (c == 0x1B && i + 1 < data.size() && data[i + 1] == '\\') {
            i += 2;
            break;
        }
        str.append(c);
        i++;
    }
    // OSC 0/2 set title; OSC 7 sets working directory.
    if (str.startsWith("0;")) {
        emit titleChanged(str.mid(2));
    } else if (str.startsWith("2;")) {
        emit titleChanged(str.mid(2));
    } else if (str.startsWith("7;")) {
        QString uri = str.mid(2);
        const QString prefix = "file://";
        if (uri.startsWith(prefix)) {
            QString path = uri.mid(prefix.size());
            int slash = path.indexOf('/');
            if (slash >= 0)
                path = path.mid(slash);
            emit workingDirectoryChanged(path);
        }
    }
}

void VtTerminalWidget::applySgr(const QVector<int>& params) {
    if (params.isEmpty()) {
        resetAttributes();
        return;
    }
    for (int idx = 0; idx < params.size(); idx++) {
        int p = params[idx];
        if (p == 0) {
            resetAttributes();
        } else if (p == 1) {
            m_attrs.bold = true;
        } else if (p == 2) {
            m_attrs.dim = true;
        } else if (p == 3) {
            m_attrs.italic = true;
        } else if (p == 4) {
            m_attrs.underline = true;
        } else if (p == 7) {
            m_attrs.inverse = true;
        } else if (p == 22) {
            m_attrs.bold = false;
            m_attrs.dim = false;
        } else if (p == 23) {
            m_attrs.italic = false;
        } else if (p == 24) {
            m_attrs.underline = false;
        } else if (p == 27) {
            m_attrs.inverse = false;
        } else if (p >= 30 && p <= 37) {
            m_attrs.fg = darkPalette().basic[p - 30];
            m_attrs.fgSet = true;
        } else if (p == 38 && idx + 1 < params.size() && params[idx + 1] == 5 && idx + 2 < params.size()) {
            m_attrs.fg = color256(params[idx + 2]);
            m_attrs.fgSet = true;
            idx += 2;
        } else if (p == 38 && idx + 1 < params.size() && params[idx + 1] == 2 && idx + 4 < params.size()) {
            m_attrs.fg = QColor(params[idx + 2], params[idx + 3], params[idx + 4]);
            m_attrs.fgSet = true;
            idx += 4;
        } else if (p == 39) {
            m_attrs.fg = darkPalette().fg;
            m_attrs.fgSet = false;
        } else if (p >= 40 && p <= 47) {
            m_attrs.bg = darkPalette().basic[p - 40];
            m_attrs.bgSet = true;
        } else if (p == 48 && idx + 1 < params.size() && params[idx + 1] == 5 && idx + 2 < params.size()) {
            m_attrs.bg = color256(params[idx + 2]);
            m_attrs.bgSet = true;
            idx += 2;
        } else if (p == 48 && idx + 1 < params.size() && params[idx + 1] == 2 && idx + 4 < params.size()) {
            m_attrs.bg = QColor(params[idx + 2], params[idx + 3], params[idx + 4]);
            m_attrs.bgSet = true;
            idx += 4;
        } else if (p == 49) {
            m_attrs.bg = darkPalette().bg;
            m_attrs.bgSet = false;
        } else if (p >= 90 && p <= 97) {
            m_attrs.fg = darkPalette().bright[p - 90];
            m_attrs.fgSet = true;
        } else if (p >= 100 && p <= 107) {
            m_attrs.bg = darkPalette().bright[p - 100];
            m_attrs.bgSet = true;
        }
    }
}

void VtTerminalWidget::keyPressEvent(QKeyEvent* e) {
    QByteArray out;

    switch (e->key()) {
    case Qt::Key_Return:
    case Qt::Key_Enter:
        out = "\r";
        break;
    case Qt::Key_Backspace:
        out = "\x7f";
        break;
    case Qt::Key_Tab:
        out = "\t";
        break;
    case Qt::Key_Up:
        out = "\x1b[A";
        break;
    case Qt::Key_Down:
        out = "\x1b[B";
        break;
    case Qt::Key_Right:
        out = "\x1b[C";
        break;
    case Qt::Key_Left:
        out = "\x1b[D";
        break;
    case Qt::Key_Home:
        out = "\x1b[H";
        break;
    case Qt::Key_End:
        out = "\x1b[F";
        break;
    case Qt::Key_Delete:
        out = "\x1b[3~";
        break;
    case Qt::Key_PageUp:
        out = "\x1b[5~";
        break;
    case Qt::Key_PageDown:
        out = "\x1b[6~";
        break;
    default: {
        if (e->modifiers() & Qt::ControlModifier) {
            int k = e->key();
            if (k >= Qt::Key_A && k <= Qt::Key_Z) {
                out.append(static_cast<char>(k - Qt::Key_A + 1));
            } else if (k == Qt::Key_Space) {
                out.append('\0');
            }
        } else {
            QString text = e->text();
            if (!text.isEmpty()) {
                out = text.toUtf8();
            }
        }
        break;
    }
    }

    if (!out.isEmpty()) {
        emit dataReady(out);
        e->accept();
        return;
    }
    QWidget::keyPressEvent(e);
}

void VtTerminalWidget::paintEvent(QPaintEvent*) {
    QPainter painter(this);
    painter.setFont(m_font);

    painter.fillRect(rect(), darkPalette().bg);

    int firstLine = firstVisibleLine();
    for (int y = 0; y < m_rows; y++) {
        int fullLine = firstLine + y;
        for (int x = 0; x < m_cols; x++) {
            Cell c = cellAt(x, fullLine);
            if (inSelection(x, fullLine)) {
                c.bg = QColor("#264f78");
                c.bgSet = true;
            }
            if (c.ch != u' ' || c.bgSet || c.inverse || c.underline)
                drawCell(painter, x, y, c);
        }
    }

    if (m_scrollOffset == 0) {
        painter.setPen(darkPalette().fg);
        int cx = m_cursorX * m_charWidth;
        int cy = m_cursorY * m_charHeight;
        painter.drawRect(cx, cy, m_charWidth, m_charHeight);
    }
}

void VtTerminalWidget::drawCell(QPainter& painter, int x, int y, const Cell& c) {
    QColor fg = c.fg;
    QColor bg = c.bg;
    if (c.inverse)
        std::swap(fg, bg);
    if (c.bold)
        fg = fg.lighter(130);

    if (bg != darkPalette().bg) {
        painter.fillRect(x * m_charWidth, y * m_charHeight, m_charWidth, m_charHeight, bg);
    }

    painter.setPen(fg);
    if (c.bold) {
        QFont f = painter.font();
        f.setBold(true);
        painter.setFont(f);
    } else if (c.italic) {
        QFont f = painter.font();
        f.setItalic(true);
        painter.setFont(f);
    }

    if (c.ch != u' ') {
        painter.drawText(x * m_charWidth, y * m_charHeight, m_charWidth, m_charHeight, Qt::AlignLeft | Qt::AlignTop,
                         QString(QChar(c.ch)));
    }

    if (c.underline) {
        painter.drawLine(x * m_charWidth, (y + 1) * m_charHeight - 1, (x + 1) * m_charWidth,
                         (y + 1) * m_charHeight - 1);
    }

    if (c.bold || c.italic) {
        painter.setFont(m_font);
    }
}

int VtTerminalWidget::totalLines() const {
    return m_scrollback.size() + m_rows;
}

int VtTerminalWidget::firstVisibleLine() const {
    return m_scrollback.size() - m_scrollOffset;
}

VtTerminalWidget::Cell VtTerminalWidget::cellAt(int x, int fullLine) const {
    if (x < 0 || x >= m_cols)
        return Cell{};
    if (fullLine < m_scrollback.size()) {
        if (fullLine < 0)
            return Cell{};
        return m_scrollback[fullLine][x];
    }
    int screenLine = fullLine - m_scrollback.size();
    if (screenLine >= 0 && screenLine < m_rows) {
        return m_cells[screenLine * m_cols + x];
    }
    return Cell{};
}

QPoint VtTerminalWidget::posToGrid(const QPoint& pos) const {
    int x = qBound(0, pos.x() / m_charWidth, m_cols - 1);
    int row = qBound(0, pos.y() / m_charHeight, m_rows - 1);
    int fullLine = firstVisibleLine() + row;
    return QPoint(x, fullLine);
}

bool VtTerminalWidget::inSelection(int x, int fullLine) const {
    int sx = m_selStartX;
    int sy = m_selStartY;
    int ex = m_selEndX;
    int ey = m_selEndY;
    if (sy > ey || (sy == ey && sx > ex)) {
        std::swap(sx, ex);
        std::swap(sy, ey);
    }
    if (sx == ex && sy == ey)
        return false;

    if (fullLine < sy || fullLine > ey)
        return false;
    if (fullLine == sy && fullLine == ey)
        return x >= sx && x <= ex;
    if (fullLine == sy)
        return x >= sx;
    if (fullLine == ey)
        return x <= ex;
    return true;
}

QString VtTerminalWidget::selectedText() const {
    int sx = m_selStartX;
    int sy = m_selStartY;
    int ex = m_selEndX;
    int ey = m_selEndY;
    if (sy > ey || (sy == ey && sx > ex)) {
        std::swap(sx, ex);
        std::swap(sy, ey);
    }
    if (sx == ex && sy == ey)
        return QString();

    QString result;
    for (int line = sy; line <= ey; line++) {
        if (line != sy)
            result += '\n';
        int startX = (line == sy) ? sx : 0;
        int endX = (line == ey) ? ex : m_cols - 1;
        QString lineText;
        for (int x = startX; x <= endX; x++) {
            Cell c = cellAt(x, line);
            lineText += QChar(c.ch);
        }
        while (lineText.endsWith(u' '))
            lineText.chop(1);
        result += lineText;
    }
    return result;
}

void VtTerminalWidget::mousePressEvent(QMouseEvent* e) {
    if (e->button() == Qt::LeftButton) {
        QPoint g = posToGrid(e->pos());
        m_selecting = true;
        m_selStartX = m_selEndX = g.x();
        m_selStartY = m_selEndY = g.y();
        update();
    } else if (e->button() == Qt::MiddleButton) {
        pasteClipboard();
    }
    QWidget::mousePressEvent(e);
}

void VtTerminalWidget::mouseMoveEvent(QMouseEvent* e) {
    if (m_selecting) {
        QPoint g = posToGrid(e->pos());
        m_selEndX = g.x();
        m_selEndY = g.y();
        update();
    }
    QWidget::mouseMoveEvent(e);
}

void VtTerminalWidget::mouseReleaseEvent(QMouseEvent* e) {
    if (e->button() == Qt::LeftButton && m_selecting) {
        m_selecting = false;
        update();
    }
    QWidget::mouseReleaseEvent(e);
}

void VtTerminalWidget::wheelEvent(QWheelEvent* e) {
    int delta = e->angleDelta().y();
    int lines = qMax(1, qAbs(delta) / 120);
    if (delta > 0) {
        m_scrollOffset = qMin(m_scrollback.size(), m_scrollOffset + lines);
    } else {
        m_scrollOffset = qMax(0, m_scrollOffset - lines);
    }
    update();
    e->accept();
}

void VtTerminalWidget::copyClipboard() {
    QString text = selectedText();
    if (!text.isEmpty()) {
        QApplication::clipboard()->setText(text);
    }
}

void VtTerminalWidget::pasteClipboard() {
    QString text = QApplication::clipboard()->text();
    if (!text.isEmpty()) {
        emit dataReady(text.toUtf8());
    }
}
