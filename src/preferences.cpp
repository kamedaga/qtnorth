#include "preferences.h"
#include <QCoreApplication>
#include <QSettings>

namespace {
bool validInterval(int seconds) { return seconds == 0 || seconds == 15 || seconds == 30 || seconds == 60 || seconds == 120 || seconds == 300; }
}

Preferences *Preferences::instance()
{
    static auto *preferences = new Preferences(QCoreApplication::instance());
    return preferences;
}

int Preferences::notificationInterval() const
{
    bool ok = false;
    const auto seconds = QSettings().value("notifications/pollIntervalSeconds", 30).toInt(&ok);
    return ok && validInterval(seconds) ? seconds : 30;
}

void Preferences::setNotificationInterval(int seconds)
{
    if (!validInterval(seconds) || seconds == notificationInterval()) return;
    QSettings().setValue("notifications/pollIntervalSeconds", seconds);
    emit notificationIntervalChanged(seconds);
}

bool Preferences::foldLongPosts() const { return QSettings().value("display/foldLongPosts", false).toBool(); }
bool Preferences::linkPreviews() const { return QSettings().value("display/linkPreviews", true).toBool(); }

void Preferences::setFoldLongPosts(bool enabled)
{
    if (enabled == foldLongPosts()) return;
    QSettings().setValue("display/foldLongPosts", enabled);
    emit foldLongPostsChanged(enabled);
}

void Preferences::setLinkPreviews(bool enabled)
{
    if (enabled == linkPreviews()) return;
    QSettings().setValue("display/linkPreviews", enabled);
    emit linkPreviewsChanged(enabled);
}
