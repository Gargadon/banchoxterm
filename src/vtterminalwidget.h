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

    QSize sizeHint() const override;

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
    void mouseMoveEvent(QMouseEvent* e) override;
    void mouseReleaseEvent(QMouseEvent* e) override;
    void wheelEvent(QWheelEvent* e) override;

private:
    struct Cell {
        ushort ch = u' ';
        QColor fg;
        QColor bg;
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

    void parse(const QByteArray& data);
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
    bool m_darkBackground = true;

    QFont m_font;
    int m_charWidth = 8;
    int m_charHeight = 16;

    // scrollback
    QVector<QVector<Cell>> m_scrollback;
    int m_scrollbackMax = 5000;
    int m_scrollOffset = 0;

    // selection (in full-content line coordinates)
    bool m_selecting = false;
    int m_selStartX = 0;
    int m_selStartY = 0;
    int m_selEndX = 0;
    int m_selEndY = 0;
};
