#include "windowchrome.h"
#include "icons.h"
#include <QEvent>
#include <QHBoxLayout>
#include <QLabel>
#include <QMouseEvent>
#include <QPushButton>
#include <QScopedValueRollback>
#include <QStyle>
#include <QWindow>
#include <array>

namespace {
constexpr int edgeWidth = 5;

class ResizeEdge : public QWidget {
public:
    ResizeEdge(QWidget *window, Qt::Edges edges) : QWidget(window), m_window(window), m_edges(edges) {
        setObjectName("windowResizeEdge");
        setAccessibleName(tr("ウィンドウのサイズ変更"));
        const bool horizontal = edges.testFlag(Qt::LeftEdge) || edges.testFlag(Qt::RightEdge);
        const bool vertical = edges.testFlag(Qt::TopEdge) || edges.testFlag(Qt::BottomEdge);
        if (horizontal && vertical) setCursor((edges == (Qt::TopEdge | Qt::LeftEdge) || edges == (Qt::BottomEdge | Qt::RightEdge)) ? Qt::SizeFDiagCursor : Qt::SizeBDiagCursor);
        else setCursor(horizontal ? Qt::SizeHorCursor : Qt::SizeVerCursor);
    }
protected:
    void mousePressEvent(QMouseEvent *event) override {
        if (event->button() == Qt::LeftButton && m_window->windowHandle()) {
            m_window->windowHandle()->startSystemResize(m_edges);
            event->accept(); return;
        }
        QWidget::mousePressEvent(event);
    }
private:
    QWidget *m_window;
    Qt::Edges m_edges;
};

class WindowFrame : public QObject {
public:
    explicit WindowFrame(QWidget *window) : QObject(window), m_window(window) {
        window->setWindowFlag(Qt::FramelessWindowHint);
        window->setProperty("customFrame", true);
        const std::array<Qt::Edges, 8> edges{Qt::TopEdge, Qt::RightEdge, Qt::BottomEdge, Qt::LeftEdge,
            Qt::TopEdge | Qt::LeftEdge, Qt::TopEdge | Qt::RightEdge, Qt::BottomEdge | Qt::RightEdge, Qt::BottomEdge | Qt::LeftEdge};
        for (size_t i = 0; i < edges.size(); ++i) m_edges[i] = new ResizeEdge(window, edges[i]);
        window->installEventFilter(this);
        layout();
    }
protected:
    bool eventFilter(QObject *object, QEvent *event) override {
        if (object == m_window && (event->type() == QEvent::Resize || event->type() == QEvent::Show || event->type() == QEvent::WindowStateChange)) layout();
        return QObject::eventFilter(object, event);
    }
private:
    void layout() {
        if (m_layouting) return;
        QScopedValueRollback<bool> guard(m_layouting, true);
        const bool full = m_window->isMaximized() || m_window->isFullScreen();
        const int e = full ? 0 : edgeWidth;
        const QMargins margins(e, e, e, e);
        if (m_window->contentsMargins() != margins) m_window->setContentsMargins(margins);
        if (m_window->property("maximized").toBool() != full) {
            m_window->setProperty("maximized", full);
            m_window->style()->unpolish(m_window); m_window->style()->polish(m_window);
        }
        const int w = m_window->width(), h = m_window->height();
        const std::array<QRect, 8> rects{QRect(e, 0, w - 2 * e, e), QRect(w - e, e, e, h - 2 * e),
            QRect(e, h - e, w - 2 * e, e), QRect(0, e, e, h - 2 * e), QRect(0, 0, e, e),
            QRect(w - e, 0, e, e), QRect(w - e, h - e, e, e), QRect(0, h - e, e, e)};
        for (size_t i = 0; i < m_edges.size(); ++i) {
            m_edges[i]->setGeometry(rects[i]); m_edges[i]->setVisible(!full); m_edges[i]->raise();
        }
    }
    QWidget *m_window;
    std::array<ResizeEdge *, 8> m_edges{};
    bool m_layouting = false;
};
}

WindowTitleBar::WindowTitleBar(QWidget *window, bool fullControls, bool showCaption)
    : QMenuBar(window), m_window(window)
{
    if (!window->property("customFrame").toBool()) new WindowFrame(window);
    setObjectName("windowTitleBar"); setNativeMenuBar(false); setFixedHeight(42);
    if (showCaption) {
        auto *caption = new QWidget;
        caption->setAttribute(Qt::WA_TransparentForMouseEvents);
        auto *layout = new QHBoxLayout(caption); layout->setContentsMargins(10, 0, 14, 0); layout->setSpacing(8);
        auto *icon = new QLabel; icon->setPixmap(window->windowIcon().isNull() ? QIcon(":/assets/qtNorth-icon.svg").pixmap(20, 20) : window->windowIcon().pixmap(20, 20));
        auto *title = new QLabel(window->windowTitle()); title->setObjectName("windowCaption"); title->setTextFormat(Qt::PlainText);
        connect(window, &QWidget::windowTitleChanged, title, &QLabel::setText);
        layout->addWidget(icon); layout->addWidget(title);
        setCornerWidget(caption, Qt::TopLeftCorner);
    }
    auto *controls = new QWidget;
    auto *layout = new QHBoxLayout(controls); layout->setContentsMargins(4, 0, 4, 0); layout->setSpacing(2);
    auto control = [layout](const QString &icon, const QString &name, const QString &text) {
        auto *button = new QPushButton(appIcon(icon), {}); button->setObjectName(name);
        button->setProperty("windowControl", true); button->setFixedSize(34, 30); button->setIconSize(QSize(16, 16));
        button->setToolTip(text); button->setAccessibleName(text); button->setAutoDefault(false);
        button->setFocusPolicy(Qt::NoFocus);
        layout->addWidget(button); return button;
    };
    if (fullControls) {
        auto *minimize = control("minimize", "windowMinimize", tr("最小化"));
        connect(minimize, &QPushButton::clicked, window, &QWidget::showMinimized);
        m_maximize = control("maximize", "windowMaximize", tr("最大化"));
        connect(m_maximize, &QPushButton::clicked, this, &WindowTitleBar::toggleMaximized);
    }
    m_close = control("close", "windowClose", tr("閉じる")); m_close->setProperty("closeControl", true);
    connect(m_close, &QPushButton::clicked, window, &QWidget::close);
    setCornerWidget(controls, Qt::TopRightCorner);
    window->installEventFilter(this);
    updateState();
}

void WindowTitleBar::setMenusVisible(bool visible) { for (auto *action : actions()) action->setVisible(visible); }

void WindowTitleBar::toggleMaximized()
{
    if (m_window->isMaximized()) m_window->showNormal(); else m_window->showMaximized();
}

void WindowTitleBar::updateState()
{
    if (!m_maximize) return;
    const bool maximized = m_window->isMaximized();
    m_maximize->setIcon(appIcon(maximized ? "restore" : "maximize"));
    m_maximize->setToolTip(maximized ? tr("元に戻す") : tr("最大化"));
    m_maximize->setAccessibleName(m_maximize->toolTip());
}

bool WindowTitleBar::eventFilter(QObject *object, QEvent *event)
{
    if (object == m_window && event->type() == QEvent::WindowStateChange) updateState();
    return QMenuBar::eventFilter(object, event);
}

void WindowTitleBar::mousePressEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton && !actionAt(event->pos())) {
        if (m_window->windowHandle()) m_window->windowHandle()->startSystemMove();
        event->accept(); return;
    }
    QMenuBar::mousePressEvent(event);
}

void WindowTitleBar::mouseDoubleClickEvent(QMouseEvent *event)
{
    if (m_maximize && event->button() == Qt::LeftButton && !actionAt(event->pos())) {
        toggleMaximized(); event->accept(); return;
    }
    QMenuBar::mouseDoubleClickEvent(event);
}
