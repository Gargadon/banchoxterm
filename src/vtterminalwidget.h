#pragma once
#include <QWidget>
#include <QColor>
#include <QVector>
#include <QFont>
#include <QPoint>

// A self-contained VT/xterm terminal emulator widget.
//
// Unlike QTermWidget it does not spawn a process; instead it renders bytes
// passed to writeData() and emits keystrokes via dataReady(). This makes it
// usable on platforms where QTermWidget is unavailable (e.g. Windows), both
// for libssh2 shell channels and for ConPTY-backed local shells.
struct VtPalette {
    QColor fg;
    QColor bg;
    QColor basic[8];
    QColor bright[8];
};

class VtTerminalWidget : public QWidget {
    Q_OBJECT

public:
    explicit VtTerminalWidget(QWidget* parent = nullptr);
    ~VtTerminalWidget() override;

    void writeData(const QByteArray& data);
    void copyClipboard();
    void pasteClipboard();
    bool findText(const QString& str, bool next, bool caseSensitive);
    void ensureLineVisible(int line);
    void setTerminalFont(const QFont& font);
    void setColorScheme(const QString& name);



    QSize sizeHint() const override;

    // Debug accessors
    int rows() const { return m_rows; }
    int cols() const { return m_cols; }
    QChar cellChar(int col, int row) const {
        if (col < 0 || col >= m_cols || row < 0 || row >= m_rows) return QChar(' ');
        return QChar(m_cells[row * m_cols + col].ch);
    }

signals:
    void dataReady(const QByteArray& data);
    void finished();
    void titleChanged(const QString& title);
    void workingDirectoryChanged(const QString& dir);
    void resized(int cols, int rows);

protected:
    void keyPressEvent(QKeyEvent* e) override;
    void paintEvent(QPaintEvent* e) override;
    void resizeEvent(QResizeEvent* e) override;
    void mousePressEvent(QMouseEvent* e) override;
    void mouseDoubleClickEvent(QMouseEvent* e) override;
    void mouseMoveEvent(QMouseEvent* e) override;
    void mouseReleaseEvent(QMouseEvent* e) override;
    void wheelEvent(QWheelEvent* e) override;
    void focusInEvent(QFocusEvent* e) override;
    void focusOutEvent(QFocusEvent* e) override;


private:
    struct Cell {
        ushort ch = u' ';
        QColor fg;
        QColor bg;
        int fgIndex = -1;
        int bgIndex = -1;
        bool bold = false;
        bool dim = false;
        bool italic = false;
        bool underline = false;
        bool inverse = false;
        bool fgSet = false;
        bool bgSet = false;
    };


    void ensureSize();
    void recomputeMetrics();
    void scrollUp();
    void newLine();
    void setChar(int x, int y, ushort ch);
    void resetAttributes();
    void drawCell(QPainter& painter, int x, int y, const Cell& c);
    VtPalette currentPalette() const;
    void rebuildHighlightCache(int firstLine);




    void parse(const QByteArray& data);
    void parseBuffer();
    void parseCsi(const QByteArray& data, int& i);
    void parseOsc(const QByteArray& data, int& i);
    void applySgr(const QVector<int>& params);


    int totalLines() const;
    int firstVisibleLine() const;
    Cell cellAt(int x, int fullLine) const;
    QPoint posToGrid(const QPoint& pos) const;
    QString selectedText() const;
    bool inSelection(int x, int fullLine) const;

    int m_cols = 80;
    int m_rows = 24;
    int m_cursorX = 0;
    int m_cursorY = 0;

    QVector<Cell> m_cells;
    Cell m_attrs;
    QString m_colorScheme = "DarkPastels";
    bool m_darkBackground = true;


    QFont m_font;
    int m_charWidth = 8;
    int m_charHeight = 16;

    // scrollback
    QVector<QVector<Cell>> m_scrollback;
    int m_scrollbackMax = 5000;
    int m_scrollOffset = 0;

    // mouse tracking (xterm 1000 / 1002 / 1003 / 1006 SGR / 1015 URXVT)
    bool m_mouseTrackingNormal = false; // DECSET 1000
    bool m_mouseTrackingButton = false; // DECSET 1002
    bool m_mouseTrackingAny = false;    // DECSET 1003
    bool m_mouseTrackingSgr = false;    // DECSET 1006
    bool m_mouseTrackingUrxvt = false;  // DECSET 1015

    // bracketed paste (DECSET 2004)
    bool m_bracketedPaste = false;

    // selection (in full-content line coordinates)
    bool m_selecting = false;
    int m_selStartX = 0;
    int m_selStartY = 0;
    int m_selEndX = 0;
    int m_selEndY = 0;

    // stream buffer for partial sequence & UTF-8 parsing across chunk boundaries
    QByteArray m_parseBuffer;

    // highlight cache (regex results), rebuilt when content/scroll changes
    QVector<QVector<QColor>> m_overrideFg;
    QVector<QVector<bool>> m_overrideBold;
    bool m_highlightDirty = true;
    int m_highlightFirstLine = -1;

    // cursor state & blinking
    bool m_cursorVisible = true;
    bool m_cursorBlinkState = true;
    QTimer* m_cursorBlinkTimer = nullptr;
};


