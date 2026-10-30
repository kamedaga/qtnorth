#include "posttime.h"
#include "post.h"
#include <QApplication>
#include <QLabel>
#include <QPointer>
#include <QTimer>

QLabel *postTimeLabel(const QString &createdAt, const QString &format)
{
    const auto timestamp = QDateTime::fromString(createdAt, Qt::ISODate);
    auto *label = new QLabel;
    label->setObjectName("muted");
    label->setProperty("postTimestamp", true);
    label->setTextFormat(Qt::PlainText);
    label->setToolTip(timestamp.toLocalTime().toString("yyyy/MM/dd HH:mm:ss t"));
    auto update = [label, timestamp, format] {
        const auto relative = North::relativePostTime(timestamp);
        const auto absolute = timestamp.toLocalTime().toString(format);
        label->setText(relative.isEmpty() ? absolute : relative + QStringLiteral(" · ") + absolute);
    };
    update();
    // One clock for every post. Recalculate only labels currently on screen.
    static QPointer<QTimer> clock;
    if (!clock) {
        clock = new QTimer(qApp);
        clock->start(1000);
    }
    QObject::connect(clock, &QTimer::timeout, label, [label, update] {
        if (label->isVisible() && !label->visibleRegion().isEmpty()) update();
    });
    return label;
}
