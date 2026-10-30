#pragma once
#include <QDateTime>
#include <QElapsedTimer>
#include <QObject>
#include <QPropertyAnimation>
#include <QPointer>
#include <QSet>
#include <QTimer>
#include <QTabBar>
class QAbstractScrollArea;
class QScrollBar;
class QScrollArea;

// Keeps the visible row in place while a batch of rows changes the layout.
class ScrollAnchorGuard {
public:
    explicit ScrollAnchorGuard(QScrollArea *area, bool enabled = true);
    ~ScrollAnchorGuard();
    Q_DISABLE_COPY(ScrollAnchorGuard)
private:
    QPointer<QObject> m_keeper;
};

class HorizontalWheelTabBar : public QTabBar {
public:
    using QTabBar::QTabBar;
protected:
    void wheelEvent(QWheelEvent *event) override;
private:
    QElapsedTimer m_wheelTime;
    int m_angle = 0, m_pixels = 0;
    bool m_forwarding = false;
};

class SmoothScroll : public QObject {
    Q_OBJECT
public:
    explicit SmoothScroll(QAbstractScrollArea *area);
    void stop();
    static void stopFor(QAbstractScrollArea *area);
    static void shiftFor(QAbstractScrollArea *area, int delta);
protected:
    bool eventFilter(QObject *object, QEvent *event) override;
private:
    QAbstractScrollArea *m_area;
    QScrollBar *m_bar;
    QPropertyAnimation m_animation;
    qreal m_target = 0;
};

class EdgeLoader : public QObject {
    Q_OBJECT
public:
    enum Edge { Bottom, Top };
    explicit EdgeLoader(QAbstractScrollArea *area, Edge edge = Bottom);
    void reset();
    void setLoading();
    void setCursor(const QString &cursor);
    void fail(int retryAfter = 0);
    void setPaused(bool paused);
signals:
    void loadMoreRequested();
protected:
    bool eventFilter(QObject *object, QEvent *event) override;
private:
    void schedule();
    void check();
    void userScrolled();
    QAbstractScrollArea *m_area;
    QScrollBar *m_bar;
    Edge m_edge;
    QTimer m_check;
    QString m_cursor;
    QSet<QString> m_requested;
    QDateTime m_retryAt;
    bool m_loading = false, m_failed = false, m_paused = false;
    int m_autoPages = 0;
};
