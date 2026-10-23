#pragma once

#include <QApplication>
#include <QDrag>
#include <QHash>
#include <QMimeData>
#include <QMouseEvent>
#include <QPointer>
#include <QTabBar>
#include <QTabWidget>
#include <QUuid>

// An in-process payload keeps the actual widget alive through normal Qt ownership;
// foreign MIME data cannot supply an arbitrary widget pointer.
class PaneTabMimeData : public QMimeData {
public:
    PaneTabMimeData(QTabWidget* source, QWidget* widget) : pane(source), tab(widget) {
        m_token = QUuid::createUuid().toRfc4122();
        live().insert(m_token, this);
        setData("application/x-banchoxterm-pane-tab", m_token);
    }
    ~PaneTabMimeData() override {
        live().remove(m_token);
    }
    static const PaneTabMimeData* fromMimeData(const QMimeData* data) {
        // Native drag backends may wrap QMimeData in another class. Resolve
        // only a live, unguessable token instead of depending on its RTTI.
        return live().value(data->data("application/x-banchoxterm-pane-tab"), nullptr);
    }
    QPointer<QTabWidget> pane;
    QPointer<QWidget> tab;

private:
    QByteArray m_token;
    static QHash<QByteArray, PaneTabMimeData*>& live() {
        static QHash<QByteArray, PaneTabMimeData*> drags;
        return drags;
    }
};

class PaneTabBar : public QTabBar {
public:
    explicit PaneTabBar(QTabWidget* pane) : QTabBar(pane), m_pane(pane) {
        setAcceptDrops(true);
    }

protected:
    void mousePressEvent(QMouseEvent* event) override {
        if (event->button() == Qt::LeftButton) {
            m_origin = event->position().toPoint();
            m_tab = m_pane->widget(tabAt(m_origin));
        }
        QTabBar::mousePressEvent(event);
    }

    void mouseMoveEvent(QMouseEvent* event) override {
        const QPoint position = event->position().toPoint();
        if (m_tab && (event->buttons() & Qt::LeftButton) && !rect().contains(position) &&
            (position - m_origin).manhattanLength() >= QApplication::startDragDistance()) {
            QPointer<QWidget> tab = m_tab;
            m_tab.clear();
            const int index = m_pane->indexOf(tab);
            if (index < 0)
                return;
            const QPixmap preview = grab(tabRect(index));
            // Finish QTabBar's internal reorder gesture before starting a cross-pane drag.
            QMouseEvent release(QEvent::MouseButtonRelease, event->position(), event->globalPosition(), Qt::LeftButton,
                                Qt::NoButton, event->modifiers());
            QTabBar::mouseReleaseEvent(&release);
            QPointer<QDrag> drag = new QDrag(this);
            drag->setMimeData(new PaneTabMimeData(m_pane, tab));
            drag->setPixmap(preview);
            drag->setHotSpot(QPoint(preview.width() / 2, preview.height() / 2));
            drag->exec(Qt::MoveAction);
            if (drag)
                drag->deleteLater();
            return;
        }
        QTabBar::mouseMoveEvent(event);
    }

    void mouseReleaseEvent(QMouseEvent* event) override {
        m_tab.clear();
        QTabBar::mouseReleaseEvent(event);
    }

private:
    QTabWidget* m_pane;
    QPointer<QWidget> m_tab;
    QPoint m_origin;
};

class TerminalPane : public QTabWidget {
public:
    explicit TerminalPane(QWidget* parent) : QTabWidget(parent) {
        setTabBar(new PaneTabBar(this));
        setAcceptDrops(true);
    }
};
