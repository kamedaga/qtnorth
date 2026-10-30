#pragma once
#include <QJsonObject>
#include <QDateTime>
#include <QString>
#include <QUrl>
#include <QList>

namespace North {
// north counts narrow characters as one and wide characters as two.
int textWeight(const QString &text);
QJsonObject displayedPost(const QJsonObject &post);
QString postUrl(const QJsonObject &post);
QString plainPostText(const QJsonObject &post);
struct TextLink { int start, length; QUrl url; };
QList<TextLink> textLinks(const QString &text);
struct TextMention { int start, length; QString handle; };
QList<TextMention> textMentions(const QString &text);
QList<TextLink> textSearchTags(const QString &text);
bool isXPost(const QUrl &url);
QUrl canonicalXPost(const QUrl &url);
QUrl canonicalYouTubeVideo(const QUrl &url);
QString relativePostTime(const QDateTime &timestamp, const QDateTime &now = QDateTime::currentDateTimeUtc());
}
