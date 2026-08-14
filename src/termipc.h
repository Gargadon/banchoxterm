#pragma once
#include <QtGlobal>
#include <QByteArray>
#include <QDataStream>
#include <QLocalSocket>

// IPC protocol between the MIT application (banchoxterm.exe) and the GPL
// terminal host process (banchoxterm-term.exe).
//
// The host process owns QTermWidget (GPL) and exposes its native window to be
// embedded in the app via SetParent(). All terminal bytes and commands travel
// over a local pipe (QLocalSocket), framed as:
//
//     [quint32 totalLen][quint32 type][payload bytes]
//
// totalLen is the length of everything after the length field itself.
//
// This header is intentionally dependency-free apart from Qt Core so it can be
// compiled into both the MIT app and the GPL term host.

enum TermMsg : quint32 {
    // term -> app
    TermMsg_Ready = 1,        // quint64 hwnd, quint32 cols, quint32 rows
    TermMsg_Input,            // QByteArray (user keystrokes from the emulator)
    TermMsg_SizeChanged,      // quint32 cols, quint32 rows
    TermMsg_TitleChanged,     // QString
    TermMsg_CwdChanged,       // QString
    TermMsg_Finished,         // (no payload)
    TermMsg_CopyAvailable,    // quint8 (bool)
    TermMsg_ContextMenu,      // qint32 x, qint32 y (position in host widget)
    TermMsg_SelectionText,    // QString (reply to TermMsg_SelectionRequest)
    TermMsg_CloseRequested,   // (no payload) WM_CLOSE/Alt+F4 hit the embedded
                              // window; the app must decide (show exit dialog)

    // app -> term
    TermMsg_FeedData,         // QByteArray (remote output / ConPTY output)
    TermMsg_SetFont,          // QString (QFont::toString())
    TermMsg_SetColorScheme,   // QString
    TermMsg_SetHistorySize,   // quint32
    TermMsg_Copy,
    TermMsg_Paste,
    TermMsg_Clear,
    TermMsg_ZoomIn,
    TermMsg_ZoomOut,
    TermMsg_ToggleSearchBar,
    TermMsg_SendText,         // QString
    TermMsg_SetFocus,
    TermMsg_SelectionRequest,
    TermMsg_Show,             // (no payload) the app has embedded the window
    TermMsg_Close,            // (no payload) exit the term process
};

namespace termipc {

constexpr int kStreamVersion = QDataStream::Qt_6_0;

inline void send(QLocalSocket* socket, TermMsg type, const QByteArray& payload = QByteArray())
{
    if (!socket)
        return;
    QByteArray frame;
    QDataStream ds(&frame, QIODevice::WriteOnly);
    ds.setVersion(kStreamVersion);
    ds << quint32(4 + payload.size());
    ds << quint32(type);
    frame.append(payload);
    socket->write(frame);
}

// Extracts complete frames from `incoming`, invoking handler(type, payload)
// for each. Returns the leftover (partial) bytes that must be prepended to the
// next chunk of incoming data.
template <typename Fn>
QByteArray parse(const QByteArray& incoming, Fn&& handler)
{
    const int size = incoming.size();
    int offset = 0;
    while (size - offset >= 8) {
        quint32 len = 0;
        quint32 type = 0;
        {
            QDataStream ds(incoming);
            ds.setVersion(kStreamVersion);
            ds.skipRawData(offset);
            ds >> len >> type;
        }
        if (len < 4 || size - offset < int(len) + 4)
            break;
        handler(static_cast<TermMsg>(type), incoming.mid(offset + 8, int(len) - 4));
        offset += 4 + int(len);
    }
    return incoming.mid(offset);
}

// ---- payload builders / readers (all QDataStream, Qt_6_0) ----

inline QByteArray str(const QString& s)
{
    QByteArray p;
    QDataStream ds(&p, QIODevice::WriteOnly);
    ds.setVersion(kStreamVersion);
    ds << s;
    return p;
}

inline QString readStr(const QByteArray& p)
{
    QDataStream ds(p);
    ds.setVersion(kStreamVersion);
    QString s;
    ds >> s;
    return s;
}

inline QByteArray bytes(const QByteArray& b)
{
    QByteArray p;
    QDataStream ds(&p, QIODevice::WriteOnly);
    ds.setVersion(kStreamVersion);
    ds << b;
    return p;
}

inline QByteArray readBytes(const QByteArray& p)
{
    QDataStream ds(p);
    ds.setVersion(kStreamVersion);
    QByteArray b;
    ds >> b;
    return b;
}

inline QByteArray sizeMsg(quint32 cols, quint32 rows)
{
    QByteArray p;
    QDataStream ds(&p, QIODevice::WriteOnly);
    ds.setVersion(kStreamVersion);
    ds << cols << rows;
    return p;
}

inline void readSize(const QByteArray& p, quint32& cols, quint32& rows)
{
    QDataStream ds(p);
    ds.setVersion(kStreamVersion);
    ds >> cols >> rows;
}

inline QByteArray readyMsg(quint64 hwnd, quint32 cols, quint32 rows)
{
    QByteArray p;
    QDataStream ds(&p, QIODevice::WriteOnly);
    ds.setVersion(kStreamVersion);
    ds << hwnd << cols << rows;
    return p;
}

inline void readReady(const QByteArray& p, quint64& hwnd, quint32& cols, quint32& rows)
{
    QDataStream ds(p);
    ds.setVersion(kStreamVersion);
    ds >> hwnd >> cols >> rows;
}

inline QByteArray pointMsg(qint32 x, qint32 y)
{
    QByteArray p;
    QDataStream ds(&p, QIODevice::WriteOnly);
    ds.setVersion(kStreamVersion);
    ds << x << y;
    return p;
}

inline void readPoint(const QByteArray& p, qint32& x, qint32& y)
{
    QDataStream ds(p);
    ds.setVersion(kStreamVersion);
    ds >> x >> y;
}

inline QByteArray boolMsg(bool v)
{
    QByteArray p;
    QDataStream ds(&p, QIODevice::WriteOnly);
    ds.setVersion(kStreamVersion);
    ds << quint8(v ? 1 : 0);
    return p;
}

inline bool readBool(const QByteArray& p)
{
    QDataStream ds(p);
    ds.setVersion(kStreamVersion);
    quint8 v = 0;
    ds >> v;
    return v != 0;
}

inline QByteArray u32Msg(quint32 v)
{
    QByteArray p;
    QDataStream ds(&p, QIODevice::WriteOnly);
    ds.setVersion(kStreamVersion);
    ds << v;
    return p;
}

inline quint32 readU32(const QByteArray& p)
{
    QDataStream ds(p);
    ds.setVersion(kStreamVersion);
    quint32 v = 0;
    ds >> v;
    return v;
}

} // namespace termipc