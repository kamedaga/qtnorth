#include "scrollbehavior.h"
#include <QAbstractScrollArea>
#include <QApplication>
#include <QEvent>
#include <QScrollBar>
#include <QScrollArea>
#include <QWidget>
#include <QWheelEvent>
#include <algorithm>
#include <cmath>
#include <limits>

namespace {
class AnchorKeeper : public QObject {
public:
    explicit AnchorKeeper(QScrollArea *area) : QObject(area), m_area(area) {
        setObjectName("scrollAnchorKeeper");
        m_timer.setSingleShot(true);
        connect(&m_timer, &QTimer::timeout, this, [this] {
            if (!m_anchor || !m_area->isVisible() || !m_anchor->isVisible()) { release(); return; }
            const int y = m_anchor->mapTo(m_area->widget(), QPoint()).y();
            SmoothScroll::shiftFor(m_area, y - m_y); m_y = y;
            if (--m_passes > 0) m_timer.start(0); else release();
        });
    }
    bool capture() {
        if (m_anchor) return true;
        if (!m_area->widget() || !m_area->isVisible()) return false;
        const int top = m_area->verticalScrollBar()->value();
        int first = std::numeric_limits<int>::max();
        for (auto *row : m_area->widget()->findChildren<QWidget *>()) {
            if (!row->property("scrollAnchor").toBool() || !row->isVisible()) continue;
            const int y = row->mapTo(m_area->widget(), QPoint()).y();
            if (y + row->height() <= top || y >= top + m_area->viewport()->height() || y >= first) continue;
            first = y; m_anchor = row; m_y = y;
        }
        if (m_anchor) {
            m_restoreUpdates = m_area->viewport()->updatesEnabled();
            if (m_restoreUpdates) m_area->viewport()->setUpdatesEnabled(false);
        }
        return !m_anchor.isNull();
    }
    void restoreSoon() { m_passes = 3; m_timer.start(0); }
private:
    void release() {
        m_anchor.clear();
        if (m_restoreUpdates) m_area->viewport()->setUpdatesEnabled(true);
        m_restoreUpdates = false;
    }
    QScrollArea *m_area;
    QPointer<QWidget> m_anchor;
    QTimer m_timer;
    int m_y = 0, m_passes = 0;
    bool m_restoreUpdates = false;
};
}

ScrollAnchorGuard::ScrollAnchorGuard(QScrollArea *area, bool enabled)
{
    if (!enabled || !area) return;
    auto *object = area->findChild<QObject *>("scrollAnchorKeeper", Qt::FindDirectChildrenOnly);
    auto *keeper = object ? static_cast<AnchorKeeper *>(object) : new AnchorKeeper(area);
    if (keeper->capture()) m_keeper = keeper;
}

ScrollAnchorGuard::~ScrollAnchorGuard()
{
    if (m_keeper) static_cast<AnchorKeeper *>(m_keeper.data())->restoreSoon();
}

void HorizontalWheelTabBar::wheelEvent(QWheelEvent *event)
{
    if (m_forwarding) { event->ignore(); return; }
    if (!m_wheelTime.isValid() || m_wheelTime.elapsed() > 400 || event->phase() == Qt::ScrollBegin || event->phase() == Qt::ScrollEnd)
        m_angle = m_pixels = 0;
    m_wheelTime.start();
    const auto pixels = event->pixelDelta(), angle = event->angleDelta();
    const bool pixelInput = !pixels.isNull() && std::abs(angle.x()) < 120;
    const auto movement = pixelInput ? pixels : angle;
    if (movement.x() == 0 || std::abs(movement.y()) > std::abs(movement.x())) {
        // Leave vertical scrolling to the containing page; never call QTabBar's
        // default wheel handler, which changes tabs for a vertical wheel.
        m_angle = m_pixels = 0;
        for (auto *parent = parentWidget(); parent; parent = parent->parentWidget()) {
            if (auto *area = qobject_cast<QAbstractScrollArea *>(parent)) {
                const QPointF position = area->viewport()->mapFromGlobal(event->globalPosition().toPoint());
                QWheelEvent forwarded(position, event->globalPosition(), event->pixelDelta(), event->angleDelta(),
                    event->buttons(), event->modifiers(), event->phase(), event->inverted(), event->source(), event->pointingDevice());
                m_forwarding = true; QApplication::sendEvent(area->viewport(), &forwarded); m_forwarding = false;
                event->accept(); return;
            }
        }
        event->ignore(); return;
    }
    event->accept();
    if (event->phase() == Qt::ScrollMomentum || count() < 2) return;
    const int delta = pixelInput ? pixels.x() : angle.x(), threshold = pixelInput ? 60 : 120;
    auto &remainder = pixelInput ? m_pixels : m_angle;
    if ((remainder < 0 && delta > 0) || (remainder > 0 && delta < 0)) remainder = 0;
    remainder += delta;
    const int steps = remainder / threshold; remainder %= threshold;
    if (steps) setCurrentIndex(std::clamp(currentIndex() - steps, 0, count() - 1));
}

SmoothScroll::SmoothScroll(QAbstractScrollArea *area)
    : QObject(area), m_area(area), m_bar(area->verticalScrollBar()), m_animation(m_bar, "value")
{
    setObjectName("smoothScroll");
    m_bar->setSingleStep(20);
    m_animation.setDuration(170);
    m_animation.setEasingCurve(QEasingCurve::OutCubic);
    area->installEventFilter(this);
    area->viewport()->installEventFilter(this);
    m_bar->installEventFilter(this);
    connect(m_bar, &QScrollBar::sliderPressed, this, &SmoothScroll::stop);
    connect(m_bar, &QScrollBar::actionTriggered, this, &SmoothScroll::stop);
}

void SmoothScroll::stop()
{
    m_animation.stop();
    m_target = m_bar->value();
}

void SmoothScroll::stopFor(QAbstractScrollArea *area)
{
    if (auto *scroll = area->findChild<SmoothScroll *>("smoothScroll", Qt::FindDirectChildrenOnly)) scroll->stop();
}

void SmoothScroll::shiftFor(QAbstractScrollArea *area, int delta)
{
    if (!delta) return;
    auto *bar = area->verticalScrollBar();
    auto *scroll = area->findChild<SmoothScroll *>("smoothScroll", Qt::FindDirectChildrenOnly);
    if (!scroll) { bar->setValue(bar->value() + delta); return; }
    const bool moving = scroll->m_animation.state() == QAbstractAnimation::Running;
    const int remaining = std::max(1, scroll->m_animation.duration() - scroll->m_animation.currentTime());
    const qreal target = scroll->m_target + delta;
    scroll->m_animation.stop();
    bar->setValue(bar->value() + delta);
    scroll->m_target = moving ? std::clamp(target, qreal(bar->minimum()), qreal(bar->maximum())) : bar->value();
    if (moving) {
        scroll->m_animation.setDuration(remaining);
        scroll->m_animation.setStartValue(bar->value());
        scroll->m_animation.setEndValue(int(std::round(scroll->m_target)));
        scroll->m_animation.start();
    }
}

bool SmoothScroll::eventFilter(QObject *, QEvent *event)
{
    if (event->type() == QEvent::Hide || event->type() == QEvent::KeyPress) { stop(); return false; }
    if (event->type() != QEvent::Wheel) return false;
    auto *wheel = static_cast<QWheelEvent *>(event);
    if (wheel->modifiers().testFlag(Qt::ControlModifier) || wheel->modifiers().testFlag(Qt::ShiftModifier)) return false;
    if (std::abs(wheel->pixelDelta().x()) > std::abs(wheel->pixelDelta().y())
        || std::abs(wheel->angleDelta().x()) > std::abs(wheel->angleDelta().y())) return false;
    // Touchpads already supply a pixel stream, including native momentum.
    if (!wheel->pixelDelta().isNull()) {
        stop();
        m_bar->setValue(m_bar->value() - wheel->pixelDelta().y());
        wheel->accept();
        return true;
    }
    if (wheel->angleDelta().y() == 0) return false;
    const qreal delta = -wheel->angleDelta().y() / 120.0 * std::max(1, QApplication::wheelScrollLines()) * 20;
    const bool moving = m_animation.state() == QAbstractAnimation::Running;
    if (!moving || (m_target - m_bar->value()) * delta < 0) m_target = m_bar->value();
    m_target = std::clamp(m_target + delta, qreal(m_bar->minimum()), qreal(m_bar->maximum()));
    m_animation.stop();
    m_animation.setStartValue(m_bar->value());
    m_animation.setEndValue(int(std::round(m_target)));
    m_animation.setDuration(170);
    m_animation.start();
    wheel->accept();
    return true;
}

EdgeLoader::EdgeLoader(QAbstractScrollArea *area, Edge edge)
    : QObject(area), m_area(area), m_bar(area->verticalScrollBar()), m_edge(edge)
{
    m_check.setSingleShot(true);
    m_check.setInterval(16); // Let layout/scroll ranges settle before testing the edge.
    connect(&m_check, &QTimer::timeout, this, &EdgeLoader::check);
    connect(m_bar, &QScrollBar::valueChanged, this, &EdgeLoader::schedule);
    connect(m_bar, &QScrollBar::rangeChanged, this, &EdgeLoader::schedule);
    connect(m_bar, &QScrollBar::actionTriggered, this, &EdgeLoader::userScrolled);
    connect(m_bar, &QScrollBar::sliderMoved, this, &EdgeLoader::userScrolled);
    area->installEventFilter(this);
    area->viewport()->installEventFilter(this);
    m_bar->installEventFilter(this);
}

void EdgeLoader::reset()
{
    m_check.stop(); m_cursor.clear(); m_requested.clear();
    m_loading = false; m_failed = false; m_paused = false; m_autoPages = 0; m_retryAt = {};
}

void EdgeLoader::setLoading() { m_loading = true; }

void EdgeLoader::setPaused(bool paused)
{
    m_paused = paused;
    if (!paused) schedule();
}

void EdgeLoader::setCursor(const QString &cursor)
{
    m_cursor = m_requested.contains(cursor) ? QString() : cursor;
    m_loading = false; m_failed = false;
    schedule();
}

void EdgeLoader::fail(int retryAfter)
{
    m_loading = false; m_failed = true;
    m_retryAt = QDateTime::currentDateTimeUtc().addSecs(std::max(0, retryAfter));
}

void EdgeLoader::schedule()
{
    if (!m_check.isActive()) m_check.start();
}

void EdgeLoader::userScrolled()
{
    m_autoPages = 0;
    if (m_failed && QDateTime::currentDateTimeUtc() >= m_retryAt) {
        m_requested.remove(m_cursor);
        m_failed = false;
    }
    schedule();
}

bool EdgeLoader::eventFilter(QObject *, QEvent *event)
{
    if (event->type() == QEvent::Show || event->type() == QEvent::Resize) schedule();
    if (event->type() == QEvent::Wheel) {
        auto *wheel = static_cast<QWheelEvent *>(event);
        const int delta = wheel->pixelDelta().isNull() ? wheel->angleDelta().y() : wheel->pixelDelta().y();
        if ((m_edge == Bottom && delta < 0) || (m_edge == Top && delta > 0)) userScrolled();
    }
    return false;
}

void EdgeLoader::check()
{
    if (m_loading || m_failed || m_paused || m_cursor.isEmpty() || !m_area->isVisible() || m_autoPages >= 5) return;
    const int distance = m_edge == Bottom ? m_bar->maximum() - m_bar->value() : m_bar->value() - m_bar->minimum();
    const int threshold = m_edge == Bottom ? std::max(120, m_area->viewport()->height() / 2) : 80;
    if (distance > threshold || m_requested.contains(m_cursor)) return;
    m_requested.insert(m_cursor);
    m_loading = true;
    ++m_autoPages;
    emit loadMoreRequested();
}
