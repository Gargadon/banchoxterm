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
#include <QRegularExpression>

namespace {

VtPalette getPalette(const QString& schemeName) {
    static const QMap<QString, VtPalette> palettes = {
        {"DarkPastels", {
            QColor("#c0caf5"), QColor("#16161e"),
            {QColor("#15161e"), QColor("#f7768e"), QColor("#9ece6a"), QColor("#e0af68"), QColor("#7aa2f7"), QColor("#bb9af7"), QColor("#7dcfff"), QColor("#a9b1d6")},
            {QColor("#414868"), QColor("#ff8998"), QColor("#b9f27c"), QColor("#ff9e64"), QColor("#89b4fa"), QColor("#cba6f7"), QColor("#89ddff"), QColor("#c0caf5")}
        }},
        {"Tango", {
            QColor("#d3d7cf"), QColor("#2e3436"),
            {QColor("#2e3436"), QColor("#cc0000"), QColor("#4e9a06"), QColor("#c4a000"), QColor("#3465a4"), QColor("#75507b"), QColor("#06989a"), QColor("#d3d7cf")},
            {QColor("#555753"), QColor("#ef2929"), QColor("#8ae234"), QColor("#fce94f"), QColor("#729fcf"), QColor("#ad7fa8"), QColor("#34e2e2"), QColor("#eeeeec")}
        }},
        {"Breeze", {
            QColor("#fcfcfc"), QColor("#232629"),
            {QColor("#232629"), QColor("#ed1515"), QColor("#11d116"), QColor("#f67400"), QColor("#1d99f3"), QColor("#9b59b6"), QColor("#1abc9c"), QColor("#fcfcfc")},
            {QColor("#7f8c8d"), QColor("#c0392b"), QColor("#27ae60"), QColor("#f39c12"), QColor("#2980b9"), QColor("#8e44ad"), QColor("#16a085"), QColor("#ffffff")}
        }},
        {"Linux", {
            QColor("#b2b2b2"), QColor("#000000"),
            {QColor("#000000"), QColor("#aa0000"), QColor("#00aa00"), QColor("#aa5500"), QColor("#00aa00"), QColor("#aa00aa"), QColor("#00aaaa"), QColor("#aaaaaa")},
            {QColor("#555555"), QColor("#ff5555"), QColor("#55ff55"), QColor("#ffff55"), QColor("#5555ff"), QColor("#ff55ff"), QColor("#55ffff"), QColor("#ffffff")}
        }},
        {"Solarized", {
            QColor("#839496"), QColor("#002b36"),
            {QColor("#073642"), QColor("#dc322f"), QColor("#859900"), QColor("#b58900"), QColor("#268bd2"), QColor("#d33682"), QColor("#2aa198"), QColor("#eee8d5")},
            {QColor("#002b36"), QColor("#cb4b16"), QColor("#586e75"), QColor("#657b83"), QColor("#839496"), QColor("#6c71c4"), QColor("#93a1a1"), QColor("#fdf6e3")}
        }},
        {"Ubuntu", {
            QColor("#eeeeec"), QColor("#300a24"),
            {QColor("#2e3436"), QColor("#cc0000"), QColor("#4e9a06"), QColor("#c4a000"), QColor("#3465a4"), QColor("#75507b"), QColor("#06989a"), QColor("#d3d7cf")},
            {QColor("#555753"), QColor("#ef2929"), QColor("#8ae234"), QColor("#fce94f"), QColor("#729fcf"), QColor("#ad7fa8"), QColor("#34e2e2"), QColor("#eeeeec")}
        }},
        {"WhiteOnBlack", {
            QColor("#ffffff"), QColor("#000000"),
            {QColor("#000000"), QColor("#b21818"), QColor("#18b218"), QColor("#b26818"), QColor("#1818b2"), QColor("#b218b2"), QColor("#18b2b2"), QColor("#b2b2b2")},
            {QColor("#686868"), QColor("#ff5454"), QColor("#54ff54"), QColor("#ffff54"), QColor("#5454ff"), QColor("#ff54ff"), QColor("#54ffff"), QColor("#ffffff")}
        }},
        {"BlackOnWhite", {
            QColor("#000000"), QColor("#ffffff"),
            {QColor("#000000"), QColor("#b21818"), QColor("#18b218"), QColor("#b26818"), QColor("#1818b2"), QColor("#b218b2"), QColor("#18b2b2"), QColor("#b2b2b2")},
            {QColor("#686868"), QColor("#ff5454"), QColor("#54ff54"), QColor("#ffff54"), QColor("#5454ff"), QColor("#ff54ff"), QColor("#54ffff"), QColor("#ffffff")}
        }},
        {"GreenOnBlack", {
            QColor("#18f018"), QColor("#000000"),
            {QColor("#000000"), QColor("#fa4b4b"), QColor("#18b218"), QColor("#b26818"), QColor("#5ca7fb"), QColor("#e11ee1"), QColor("#18b2b2"), QColor("#b2b2b2")},
            {QColor("#686868"), QColor("#ff5454"), QColor("#54ff54"), QColor("#ffff54"), QColor("#5454ff"), QColor("#ff54ff"), QColor("#54ffff"), QColor("#ffffff")}
        }},
        {"Nord", {
            QColor("#d8dee9"), QColor("#2e3440"),
            {QColor("#3b4252"), QColor("#bf616a"), QColor("#a3be8c"), QColor("#ebcb8b"), QColor("#81a1c1"), QColor("#b48ead"), QColor("#88c0d0"), QColor("#e5e9f0")},
            {QColor("#4c566a"), QColor("#bf616a"), QColor("#a3be8c"), QColor("#ebcb8b"), QColor("#81a1c1"), QColor("#b48ead"), QColor("#8fbcbb"), QColor("#eceff4")}
        }},
        {"SolarizedLight", {
            QColor("#657b83"), QColor("#fdf6e3"),
            {QColor("#073642"), QColor("#dc322f"), QColor("#859900"), QColor("#b58900"), QColor("#268bd2"), QColor("#d33682"), QColor("#2aa198"), QColor("#eee8d5")},
            {QColor("#002b36"), QColor("#cb4b16"), QColor("#586e75"), QColor("#657b83"), QColor("#839496"), QColor("#6c71c4"), QColor("#93a1a1"), QColor("#fdf6e3")}
        }},
        {"Falcon", {
            QColor("#c2c2c2"), QColor("#223333"),
            {QColor("#959595"), QColor("#ff6565"), QColor("#84c24e"), QColor("#cfbf29"), QColor("#6ed7ff"), QColor("#fcaf3e"), QColor("#b7b0e8"), QColor("#ffffff")},
            {QColor("#959595"), QColor("#ff6565"), QColor("#84c24e"), QColor("#cfbf29"), QColor("#6ed7ff"), QColor("#fcaf3e"), QColor("#b7b0e8"), QColor("#ffffff")}
        }},
        {"BlackOnLightYellow", {
            QColor("#000000"), QColor("#ffffdd"),
            {QColor("#000000"), QColor("#b21818"), QColor("#18b218"), QColor("#b26818"), QColor("#1818b2"), QColor("#b218b2"), QColor("#18b2b2"), QColor("#b2b2b2")},
            {QColor("#686868"), QColor("#ff5454"), QColor("#54ff54"), QColor("#ffff54"), QColor("#5454ff"), QColor("#ff54ff"), QColor("#54ffff"), QColor("#ffffff")}
        }},
        {"BlackOnRandomLight", {
            QColor("#000000"), QColor("#f7f7d6"),
            {QColor("#000000"), QColor("#b21818"), QColor("#18b218"), QColor("#b26818"), QColor("#1818b2"), QColor("#b218b2"), QColor("#18b2b2"), QColor("#b2b2b2")},
            {QColor("#686868"), QColor("#ff5454"), QColor("#54ff54"), QColor("#ffff54"), QColor("#5454ff"), QColor("#ff54ff"), QColor("#54ffff"), QColor("#ffffff")}
        }},
        {"BreezeModified", {
            QColor("#eff0f1"), QColor("#31363b"),
            {QColor("#073642"), QColor("#ed1515"), QColor("#11d116"), QColor("#f67400"), QColor("#1d99f3"), QColor("#9b59b6"), QColor("#1abc9c"), QColor("#eff0f1")},
            {QColor("#ff5500"), QColor("#c0392b"), QColor("#1cdc9a"), QColor("#fdbc4b"), QColor("#3daee9"), QColor("#8e44ad"), QColor("#16a085"), QColor("#fcfcfc")}
        }}
    };

    return palettes.value(schemeName, palettes.value("DarkPastels"));
}

QColor color256(int idx, const VtPalette& pal) {
    if (idx < 16)
        return pal.basic[idx % 8];
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

#include <QTimer>

VtPalette VtTerminalWidget::currentPalette() const {
    return getPalette(m_colorScheme);
}


void VtTerminalWidget::setColorScheme(const QString& name) {
    m_colorScheme = name.isEmpty() ? "DarkPastels" : name;
    update();
}

VtTerminalWidget::VtTerminalWidget(QWidget* parent) : QWidget(parent) {
    m_font = QFont("Monospace", 11);
    m_font.setStyleHint(QFont::Monospace);
    m_font.setFixedPitch(true);

    setFocusPolicy(Qt::StrongFocus);
    setAttribute(Qt::WA_OpaquePaintEvent);

    resetAttributes();

    m_cursorBlinkTimer = new QTimer(this);
    m_cursorBlinkTimer->setInterval(500);
    connect(m_cursorBlinkTimer, &QTimer::timeout, this, [this]() {
        m_cursorBlinkState = !m_cursorBlinkState;
        update();
    });
    m_cursorBlinkTimer->start();

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

void VtTerminalWidget::setTerminalFont(const QFont& font) {
    m_font = font;
    m_font.setFixedPitch(true);
    recomputeMetrics();

    if (width() > 0 && height() > 0) {
        int newCols = qMax(1, width() / m_charWidth);
        int newRows = qMax(1, height() / m_charHeight);
        if (newCols != m_cols || newRows != m_rows) {
            QVector<Cell> old = m_cells;
            int oldCols = m_cols;
            int oldRows = m_rows;

            m_cols = newCols;
            m_rows = newRows;
            m_cells.fill(Cell{}, m_cols * m_rows);

            int copyRows = qMin(oldRows, m_rows);
            int copyCols = qMin(oldCols, m_cols);
            for (int r = 0; r < copyRows; ++r) {
                for (int c = 0; c < copyCols; ++c) {
                    m_cells[r * m_cols + c] = old[r * oldCols + c];
                }
            }
            m_cursorX = qBound(0, m_cursorX, m_cols - 1);
            m_cursorY = qBound(0, m_cursorY, m_rows - 1);

            emit resized(m_cols, m_rows);
        }
    }
    update();
}

void VtTerminalWidget::resizeEvent(QResizeEvent* e) {
    QWidget::resizeEvent(e);
    int newCols = qMax(1, width() / m_charWidth);
    int newRows = qMax(1, height() / m_charHeight);
    if (newCols != m_cols || newRows != m_rows) {
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
    m_attrs.fgIndex = -1;
    m_attrs.bgIndex = -1;
    m_attrs.fgSet = false;
    m_attrs.bgSet = false;
}

void VtTerminalWidget::scrollUp() {
    m_scrollback.append(m_cells.mid(0, m_cols));
    while (m_scrollback.size() > m_scrollbackMax)
        m_scrollback.removeFirst();
    m_cells.remove(0, m_cols);
    m_cells.append(QVector<Cell>(m_cols, Cell{}));
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
    cell.fgIndex = m_attrs.fgIndex;
    cell.bgIndex = m_attrs.bgIndex;
    cell.bold = m_attrs.bold;
    cell.dim = m_attrs.dim;
    cell.italic = m_attrs.italic;
    cell.underline = m_attrs.underline;
    cell.inverse = m_attrs.inverse;
    cell.fgSet = m_attrs.fgSet;
    cell.bgSet = m_attrs.bgSet;
}


void VtTerminalWidget::writeData(const QByteArray& data) {
    m_parseBuffer.append(data);
    parseBuffer();
    update();
}

void VtTerminalWidget::parse(const QByteArray& data) {
    m_parseBuffer.append(data);
    parseBuffer();
}

void VtTerminalWidget::parseBuffer() {
    int i = 0;
    int lastValidPos = 0;

    while (i < m_parseBuffer.size()) {
        lastValidPos = i;
        unsigned char c = static_cast<unsigned char>(m_parseBuffer[i]);

        if (c == 0x1B) { // ESC sequence
            if (i + 1 >= m_parseBuffer.size()) {
                // Partial ESC at end of buffer, wait for next chunk
                break;
            }
            char nextChar = m_parseBuffer[i + 1];
            if (nextChar == '[') {
                // CSI sequence
                int csiStart = i;
                i += 2;
                bool complete = false;
                while (i < m_parseBuffer.size()) {
                    char csiByte = m_parseBuffer[i];
                    if (csiByte >= 0x40 && csiByte <= 0x7E) {
                        complete = true;
                        break;
                    }
                    i++;
                }
                if (!complete) {
                    // Partial CSI sequence, wait for next chunk
                    i = csiStart;
                    break;
                }
                int csiEnd = i;
                i = csiStart + 2;
                parseCsi(m_parseBuffer, i);
                i = csiEnd + 1;
                continue;
            } else if (nextChar == ']') {
                // OSC sequence
                int oscStart = i;
                i += 2;
                bool complete = false;
                while (i < m_parseBuffer.size()) {
                    char b = m_parseBuffer[i];
                    if (b == 0x07) {
                        complete = true;
                        break;
                    }
                    if (b == 0x1B && i + 1 < m_parseBuffer.size() && m_parseBuffer[i + 1] == '\\') {
                        complete = true;
                        break;
                    }
                    i++;
                }
                if (!complete) {
                    // Partial OSC sequence, wait for next chunk
                    i = oscStart;
                    break;
                }
                int oscEnd = i;
                i = oscStart + 2;
                parseOsc(m_parseBuffer, i);
                i = oscEnd + 1;
                continue;
            } else if (nextChar == '(' || nextChar == ')') {
                if (i + 2 >= m_parseBuffer.size()) {
                    i = lastValidPos;
                    break;
                }
                i += 3;
                continue;
            } else if (nextChar == '7' || nextChar == '8') {
                i += 2;
                continue;
            } else if (nextChar == 'M') {
                m_cursorY = qMax(0, m_cursorY - 1);
                i += 2;
                continue;
            } else {
                i += 2;
                continue;
            }
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
            i++;
        } else if (c < 0x20) {
            i++;
        } else {
            // Check UTF-8 character length completeness
            int neededBytes = 1;
            if (c >= 0xC0 && c < 0xE0) neededBytes = 2;
            else if (c >= 0xE0 && c < 0xF0) neededBytes = 3;
            else if (c >= 0xF0) neededBytes = 4;

            if (i + neededBytes > m_parseBuffer.size()) {
                // Partial UTF-8 byte sequence at end of buffer, wait for next chunk
                i = lastValidPos;
                break;
            }

            ushort ch = decodeUtf8(m_parseBuffer, i);
            if (m_cursorX >= m_cols) {
                m_cursorX = 0;
                newLine();
            }
            setChar(m_cursorX, m_cursorY, ch);
            m_cursorX++;
        }

        lastValidPos = i;
    }

    if (i > 0) {
        m_parseBuffer.remove(0, i);
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
                QString cleanParams = params;
                while (!cleanParams.isEmpty() && !cleanParams[0].isDigit()) {
                    cleanParams.remove(0, 1);
                }
                const QStringList parts = cleanParams.split(';');
                for (const QString& p : parts) {
                    bool ok = false;
                    int v = p.toInt(&ok);
                    if (ok)
                        nums.append(v);
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
            case 'l': {
                bool enable = (c == 'h');
                if (params.startsWith('?')) {
                    for (int n : nums) {
                        if (n == 1000)
                            m_mouseTrackingNormal = enable;
                        else if (n == 1002)
                            m_mouseTrackingButton = enable;
                        else if (n == 1003)
                            m_mouseTrackingAny = enable;
                        else if (n == 1006)
                            m_mouseTrackingSgr = enable;
                        else if (n == 1015)
                            m_mouseTrackingUrxvt = enable;
                        else if (n == 25)
                            m_cursorVisible = enable;
                        else if (n == 2004)
                            m_bracketedPaste = enable;
                    }
                    setMouseTracking(m_mouseTrackingAny || m_mouseTrackingButton || m_mouseTrackingNormal);
                }
                break;
            }
            case 'r':
            case 'n':
            case 'X':
                break;
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
            m_attrs.fgIndex = p - 30;
            m_attrs.fgSet = true;
        } else if (p == 38 && idx + 1 < params.size() && params[idx + 1] == 5 && idx + 2 < params.size()) {
            int cIdx = params[idx + 2];
            if (cIdx < 16) {
                m_attrs.fgIndex = cIdx;
            } else {
                m_attrs.fgIndex = -1;
                m_attrs.fg = color256(cIdx, currentPalette());
            }
            m_attrs.fgSet = true;
            idx += 2;
        } else if (p == 38 && idx + 1 < params.size() && params[idx + 1] == 2 && idx + 4 < params.size()) {
            m_attrs.fgIndex = -1;
            m_attrs.fg = QColor(params[idx + 2], params[idx + 3], params[idx + 4]);
            m_attrs.fgSet = true;
            idx += 4;
        } else if (p == 39) {
            m_attrs.fgIndex = -1;
            m_attrs.fgSet = false;
        } else if (p >= 40 && p <= 47) {
            m_attrs.bgIndex = p - 40;
            m_attrs.bgSet = true;
        } else if (p == 48 && idx + 1 < params.size() && params[idx + 1] == 5 && idx + 2 < params.size()) {
            int cIdx = params[idx + 2];
            if (cIdx < 16) {
                m_attrs.bgIndex = cIdx;
            } else {
                m_attrs.bgIndex = -1;
                m_attrs.bg = color256(cIdx, currentPalette());
            }
            m_attrs.bgSet = true;
            idx += 2;
        } else if (p == 48 && idx + 1 < params.size() && params[idx + 1] == 2 && idx + 4 < params.size()) {
            m_attrs.bgIndex = -1;
            m_attrs.bg = QColor(params[idx + 2], params[idx + 3], params[idx + 4]);
            m_attrs.bgSet = true;
            idx += 4;
        } else if (p == 49) {
            m_attrs.bgIndex = -1;
            m_attrs.bgSet = false;
        } else if (p >= 90 && p <= 97) {
            m_attrs.fgIndex = (p - 90) + 8;
            m_attrs.fgSet = true;
        } else if (p >= 100 && p <= 107) {
            m_attrs.bgIndex = (p - 100) + 8;
            m_attrs.bgSet = true;
        }
    }
}

void VtTerminalWidget::focusInEvent(QFocusEvent* e) {
    QWidget::focusInEvent(e);
    m_cursorBlinkState = true;
    if (m_cursorBlinkTimer)
        m_cursorBlinkTimer->start(500);
    update();
}

void VtTerminalWidget::focusOutEvent(QFocusEvent* e) {
    QWidget::focusOutEvent(e);
    if (m_cursorBlinkTimer)
        m_cursorBlinkTimer->stop();
    m_cursorBlinkState = false;
    update();
}

void VtTerminalWidget::keyPressEvent(QKeyEvent* e) {
    m_cursorBlinkState = true;
    if (m_cursorBlinkTimer)
        m_cursorBlinkTimer->start(500);

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

struct HighlightRule {
    QRegularExpression regex;
    QColor fgColor;
    bool bold = false;
};

static QVector<HighlightRule> getHighlightRules() {
    static QVector<HighlightRule> rules = {
        {QRegularExpression(R"(\b\d{1,3}\.\d{1,3}\.\d{1,3}\.\d{1,3}\b)"), QColor("#9ece6a"), true},
        {QRegularExpression(R"(\b\d+\b)"), QColor("#ff9e64"), false},
        {QRegularExpression(R"(\b(error|failed|failure|critical|fatal|exception)\b)", QRegularExpression::CaseInsensitiveOption), QColor("#f7768e"), true},
        {QRegularExpression(R"(\b(success|ok|connected|accepted|established|online)\b)", QRegularExpression::CaseInsensitiveOption), QColor("#9ece6a"), true},
        {QRegularExpression(R"(\b(warning|warn|attention)\b)", QRegularExpression::CaseInsensitiveOption), QColor("#e0af68"), true},
        {QRegularExpression(R"(\b(https?://\S+|/[a-zA-Z0-9_\-\./]+)\b)"), QColor("#7dcfff"), false}
    };
    return rules;
}

void VtTerminalWidget::paintEvent(QPaintEvent*) {
    QPainter painter(this);
    painter.setFont(m_font);

    painter.fillRect(rect(), currentPalette().bg);

    int firstLine = firstVisibleLine();
    QVector<HighlightRule> rules = getHighlightRules();

    QVector<QVector<QColor>> overrideFg(m_rows, QVector<QColor>(m_cols, QColor()));
    QVector<QVector<bool>> overrideBold(m_rows, QVector<bool>(m_cols, false));

    for (int y = 0; y < m_rows; ++y) {
        int fullLine = firstLine + y;
        QString lineText;
        lineText.reserve(m_cols);
        for (int x = 0; x < m_cols; ++x) {
            lineText.append(QChar(cellAt(x, fullLine).ch));
        }

        for (const auto& rule : rules) {
            auto matchIterator = rule.regex.globalMatch(lineText);
            while (matchIterator.hasNext()) {
                auto match = matchIterator.next();
                int start = match.capturedStart();
                int end = match.capturedEnd();
                for (int x = start; x < end && x < m_cols; ++x) {
                    overrideFg[y][x] = rule.fgColor;
                    if (rule.bold) {
                        overrideBold[y][x] = true;
                    }
                }
            }
        }
    }

    for (int y = 0; y < m_rows; y++) {
        int fullLine = firstLine + y;
        for (int x = 0; x < m_cols; x++) {
            Cell c = cellAt(x, fullLine);
            if (overrideFg[y][x].isValid()) {
                c.fg = overrideFg[y][x];
                c.fgSet = true;
                if (overrideBold[y][x]) {
                    c.bold = true;
                }
            }
            if (inSelection(x, fullLine)) {
                c.bg = QColor("#264f78");
                c.bgSet = true;
            }
            if (c.ch != u' ' || c.bgSet || c.inverse || c.underline || c.fgSet)
                drawCell(painter, x, y, c);
        }
    }

    if (m_scrollOffset == 0 && m_cursorVisible) {
        int cx = m_cursorX * m_charWidth;
        int cy = m_cursorY * m_charHeight;

        if (hasFocus()) {
            if (m_cursorBlinkState) {
                // Solid filled block cursor
                QColor cursorColor = currentPalette().fg;
                painter.fillRect(cx, cy, m_charWidth, m_charHeight, cursorColor);

                // Render character under cursor inverted in dark background color
                Cell c = cellAt(m_cursorX, firstLine + m_cursorY);
                if (c.ch != u' ') {
                    painter.setPen(currentPalette().bg);
                    if (c.bold) {
                        QFont f = painter.font();
                        f.setBold(true);
                        painter.setFont(f);
                    }
                    painter.drawText(cx, cy, m_charWidth, m_charHeight, Qt::AlignLeft | Qt::AlignTop,
                                     QString(QChar(c.ch)));
                    if (c.bold) {
                        painter.setFont(m_font);
                    }
                }
            }
        } else {
            // Unfocused hollow outline block cursor
            painter.setPen(currentPalette().fg);
            painter.drawRect(cx, cy, m_charWidth - 1, m_charHeight - 1);
        }
    }

}

void VtTerminalWidget::drawCell(QPainter& painter, int x, int y, const Cell& c) {
    QColor fg;
    if (!c.fgSet) {
        fg = currentPalette().fg;
    } else if (c.fgIndex >= 0 && c.fgIndex < 8) {
        fg = currentPalette().basic[c.fgIndex];
    } else if (c.fgIndex >= 8 && c.fgIndex < 16) {
        fg = currentPalette().bright[c.fgIndex - 8];
    } else {
        fg = c.fg;
    }

    QColor bg;
    if (!c.bgSet) {
        bg = currentPalette().bg;
    } else if (c.bgIndex >= 0 && c.bgIndex < 8) {
        bg = currentPalette().basic[c.bgIndex];
    } else if (c.bgIndex >= 8 && c.bgIndex < 16) {
        bg = currentPalette().bright[c.bgIndex - 8];
    } else {
        bg = c.bg;
    }

    if (c.inverse)
        std::swap(fg, bg);
    if (c.bold && c.fgSet)
        fg = fg.lighter(130);

    if (bg != currentPalette().bg) {
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
    int col = qBound(0, e->pos().x() / m_charWidth, m_cols - 1);
    int row = qBound(0, e->pos().y() / m_charHeight, m_rows - 1);

    // If mouse tracking is enabled by remote app (e.g. htop/mc/vim), send VT mouse report
    if (m_mouseTrackingNormal || m_mouseTrackingButton) {
        int btn = 0;
        if (e->button() == Qt::RightButton)
            btn = 2;
        else if (e->button() == Qt::MiddleButton)
            btn = 1;
        if (m_mouseTrackingSgr) {
            // SGR format: ESC [ < btn ; col ; row M
            QByteArray report = QString("\x1b[<%1;%2;%3M").arg(btn).arg(col + 1).arg(row + 1).toUtf8();
            emit dataReady(report);
        } else {
            // Normal format: ESC [ M btn col row (X10 32-offset)
            QByteArray report;
            report.append("\x1b[M");
            report.append(static_cast<char>(32 + btn));
            report.append(static_cast<char>(32 + col + 1));
            report.append(static_cast<char>(32 + row + 1));
            emit dataReady(report);
        }
    }

    if (e->button() == Qt::LeftButton) {
        QPoint g = posToGrid(e->pos());
        m_selecting = true;
        m_selStartX = m_selEndX = g.x();
        m_selStartY = m_selEndY = g.y();
        update();
    } else if (e->button() == Qt::MiddleButton && !m_mouseTrackingNormal) {
        pasteClipboard();
    }
    QWidget::mousePressEvent(e);
}

void VtTerminalWidget::mouseDoubleClickEvent(QMouseEvent* e) {
    if (e->button() == Qt::LeftButton) {
        QPoint g = posToGrid(e->pos());
        int line = g.y();
        int col = g.x();

        // Select the word under cursor
        int startX = col;
        while (startX > 0) {
            QChar ch = cellAt(startX - 1, line).ch;
            if (ch.isSpace() || ch.isPunct())
                break;
            startX--;
        }
        int endX = col;
        while (endX < m_cols - 1) {
            QChar ch = cellAt(endX + 1, line).ch;
            if (ch.isSpace() || ch.isPunct())
                break;
            endX++;
        }

        m_selecting = false;
        m_selStartX = startX;
        m_selStartY = line;
        m_selEndX = endX;
        m_selEndY = line;
        copyClipboard();
        update();
    }
    QWidget::mouseDoubleClickEvent(e);
}

void VtTerminalWidget::mouseMoveEvent(QMouseEvent* e) {
    int col = qBound(0, e->pos().x() / m_charWidth, m_cols - 1);
    int row = qBound(0, e->pos().y() / m_charHeight, m_rows - 1);

    if (m_mouseTrackingAny || (m_mouseTrackingButton && (e->buttons() != Qt::NoButton))) {
        int btn = 32; // motion flag
        if (e->buttons() & Qt::LeftButton)
            btn += 0;
        else if (e->buttons() & Qt::RightButton)
            btn += 2;
        else if (e->buttons() & Qt::MiddleButton)
            btn += 1;
        else
            btn = 35; // motion without button

        if (m_mouseTrackingSgr) {
            QByteArray report = QString("\x1b[<%1;%2;%3M").arg(btn).arg(col + 1).arg(row + 1).toUtf8();
            emit dataReady(report);
        } else if (m_mouseTrackingUrxvt) {
            QByteArray report = QString("\x1b[%1;%2;%3M").arg(btn + 32).arg(col + 1).arg(row + 1).toUtf8();
            emit dataReady(report);
        }
    }

    if (m_selecting) {
        QPoint g = posToGrid(e->pos());
        m_selEndX = g.x();
        m_selEndY = g.y();
        update();
    }
    QWidget::mouseMoveEvent(e);
}

void VtTerminalWidget::mouseReleaseEvent(QMouseEvent* e) {
    int col = qBound(0, e->pos().x() / m_charWidth, m_cols - 1);
    int row = qBound(0, e->pos().y() / m_charHeight, m_rows - 1);

    if (m_mouseTrackingNormal || m_mouseTrackingButton || m_mouseTrackingAny) {
        if (m_mouseTrackingSgr) {
            // SGR release format uses 'm' instead of 'M'
            int btn = 0;
            if (e->button() == Qt::RightButton)
                btn = 2;
            else if (e->button() == Qt::MiddleButton)
                btn = 1;
            QByteArray report = QString("\x1b[<%1;%2;%3m").arg(btn).arg(col + 1).arg(row + 1).toUtf8();
            emit dataReady(report);
        } else {
            QByteArray report;
            report.append("\x1b[M");
            report.append(static_cast<char>(32 + 3)); // 3 = release
            report.append(static_cast<char>(32 + col + 1));
            report.append(static_cast<char>(32 + row + 1));
            emit dataReady(report);
        }
    }

    if (e->button() == Qt::LeftButton && m_selecting) {
        m_selecting = false;
        copyClipboard();
        update();
    }
    QWidget::mouseReleaseEvent(e);
}

void VtTerminalWidget::wheelEvent(QWheelEvent* e) {
    if (m_mouseTrackingNormal || m_mouseTrackingButton || m_mouseTrackingAny) {
        QPoint pos = e->position().toPoint();
        int col = qBound(0, pos.x() / m_charWidth, m_cols - 1);
        int row = qBound(0, pos.y() / m_charHeight, m_rows - 1);
        int btn = (e->angleDelta().y() > 0) ? 64 : 65; // 64 = wheel up, 65 = wheel down
        if (m_mouseTrackingSgr) {
            QByteArray report = QString("\x1b[<%1;%2;%3M").arg(btn).arg(col + 1).arg(row + 1).toUtf8();
            emit dataReady(report);
        } else {
            QByteArray report;
            report.append("\x1b[M");
            report.append(static_cast<char>(32 + btn));
            report.append(static_cast<char>(32 + col + 1));
            report.append(static_cast<char>(32 + row + 1));
            emit dataReady(report);
        }
        e->accept();
        return;
    }

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
        if (m_bracketedPaste)
            emit dataReady("\x1b[200~" + text.toUtf8() + "\x1b[201~");
        else
            emit dataReady(text.toUtf8());
    }
}

bool VtTerminalWidget::findText(const QString& str, bool next, bool caseSensitive) {
    if (str.isEmpty())
        return false;

    int total = totalLines();
    int startLine = 0;
    int startCol = 0;

    if (m_selStartX != m_selEndX || m_selStartY != m_selEndY) {
        if (next) {
            int ey = m_selEndY;
            int ex = m_selEndX;
            int sy = m_selStartY;
            int sx = m_selStartX;
            if (sy > ey || (sy == ey && sx > ex)) {
                std::swap(sx, ex);
                std::swap(sy, ey);
            }
            startLine = ey;
            startCol = ex + 1;
        } else {
            int ey = m_selEndY;
            int ex = m_selEndX;
            int sy = m_selStartY;
            int sx = m_selStartX;
            if (sy > ey || (sy == ey && sx > ex)) {
                std::swap(sx, ex);
                std::swap(sy, ey);
            }
            startLine = sy;
            startCol = sx - 1;
        }
    } else {
        startLine = firstVisibleLine();
        startCol = 0;
    }

    auto getLineText = [this](int line) {
        QString txt;
        txt.reserve(m_cols);
        for (int x = 0; x < m_cols; ++x) {
            txt.append(QChar(cellAt(x, line).ch));
        }
        return txt;
    };

    Qt::CaseSensitivity cs = caseSensitive ? Qt::CaseSensitive : Qt::CaseInsensitive;

    if (next) {
        for (int line = startLine; line < total; ++line) {
            QString lineText = getLineText(line);
            int col = (line == startLine) ? startCol : 0;
            if (col >= lineText.size())
                continue;

            int idx = lineText.indexOf(str, col, cs);
            if (idx != -1) {
                m_selStartX = idx;
                m_selStartY = line;
                m_selEndX = idx + str.length() - 1;
                m_selEndY = line;
                ensureLineVisible(line);
                update();
                return true;
            }
        }

        // Bucle desde el inicio
        for (int line = 0; line <= startLine; ++line) {
            QString lineText = getLineText(line);
            int idx = lineText.indexOf(str, 0, cs);
            if (line == startLine && idx >= startCol)
                continue;
            if (idx != -1) {
                m_selStartX = idx;
                m_selStartY = line;
                m_selEndX = idx + str.length() - 1;
                m_selEndY = line;
                ensureLineVisible(line);
                update();
                return true;
            }
        }
    } else {
        for (int line = startLine; line >= 0; --line) {
            QString lineText = getLineText(line);
            int col = (line == startLine) ? startCol : lineText.size();

            int idx = lineText.lastIndexOf(str, col, cs);
            if (idx != -1) {
                m_selStartX = idx;
                m_selStartY = line;
                m_selEndX = idx + str.length() - 1;
                m_selEndY = line;
                ensureLineVisible(line);
                update();
                return true;
            }
        }

        // Bucle desde el final
        for (int line = total - 1; line >= startLine; --line) {
            QString lineText = getLineText(line);
            int col = (line == startLine) ? startCol : lineText.size();
            int idx = lineText.lastIndexOf(str, col, cs);
            if (idx != -1) {
                m_selStartX = idx;
                m_selStartY = line;
                m_selEndX = idx + str.length() - 1;
                m_selEndY = line;
                ensureLineVisible(line);
                update();
                return true;
            }
        }
    }

    return false;
}

void VtTerminalWidget::ensureLineVisible(int line) {
    int firstVis = firstVisibleLine();
    int lastVis = firstVis + m_rows - 1;
    if (line < firstVis) {
        m_scrollOffset = m_scrollback.size() - line;
    } else if (line > lastVis) {
        m_scrollOffset = m_scrollback.size() - (line - m_rows + 1);
    }
    m_scrollOffset = qBound(0, m_scrollOffset, m_scrollback.size());
}
