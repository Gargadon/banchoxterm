#pragma once
#include <QObject>
#include <QByteArray>
#include <QString>
#include <QPair>
#include <QList>

class QWidget;
class QLocalServer;
class QLocalSocket;
class QProcess;
class QFont;
class QPoint;

// App-side (MIT) client for the GPL terminal host process (banchoxterm-term).
//
// Spawns the term host, embeds its native window into a container widget via
// SetParent(), and relays terminal bytes/commands over a local pipe. This is
// what keeps QTermWidget (GPL) out of banchoxterm.exe entirely.
class TerminalHostClient : public QObject {
    Q_OBJECT
public:
    explicit TerminalHostClient(QWidget* parent = nullptr);
    ~TerminalHostClient() override;

    QWidget* widget() const {
        return m_container;
    }
    bool isEmbedded() const {
        return m_hwnd != nullptr;
    }
    int columns() const {
        return m_cols;
    }
    int rows() const {
        return m_rows;
    }
    bool hasSelection() const {
        return m_hasSelection;
    }

    void setHistorySize(int lines);
    void setFont(const QFont& font);
    void setColorScheme(const QString& name);
    void feedData(const QByteArray& data);
    void sendText(const QString& text);
    void copy();
    void paste();
    void clear();
    void zoomIn();
    void zoomOut();
    void toggleSearchBar();
    void requestFocus();
    void requestSelection();
    void repositionChild();

signals:
    void ready();
    void inputReceived(const QByteArray& data);
    void sizeChanged(int rows, int cols);
    void titleChanged(const QString& title);
    void cwdChanged(const QString& dir);
    void finished();
    void copyAvailable(bool available);
    void contextMenuRequested(const QPoint& pos);
    void selectionText(const QString& text);
    void closeRequested();

protected:
    bool eventFilter(QObject* obj, QEvent* event) override;

private:
    void startHost();
    void onNewConnection();
    void onReadyRead();
    void onDisconnected();
    void handleMessage(quint32 type, const QByteArray& payload);
    void embedWindow(quint64 hwnd);
    void cleanup();
    void sendOrQueue(quint32 type, const QByteArray& payload = QByteArray());

    QWidget* m_container = nullptr;
    QLocalServer* m_server = nullptr;
    QLocalSocket* m_socket = nullptr;
    QProcess* m_process = nullptr;
    void* m_hwnd = nullptr; // HWND of the embedded term host window
    int m_cols = 0;
    int m_rows = 0;
    bool m_hasSelection = false;
    bool m_closing = false;
    QByteArray m_rxBuffer;
    QString m_pipeName;
    QList<QPair<quint32, QByteArray>> m_pending;
};