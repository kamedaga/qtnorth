#include "post.h"
#include <QUrl>
#include <QRegularExpression>
#include <QSet>
#include <QUrlQuery>
#include <algorithm>

QString North::relativePostTime(const QDateTime &timestamp, const QDateTime &now)
{
    if (!timestamp.isValid() || !now.isValid() || timestamp > now) return {};
    const qint64 seconds = timestamp.secsTo(now);
    if (seconds < 60) return QStringLiteral("%1秒前").arg(seconds);
    if (seconds < 3600) return QStringLiteral("%1分前").arg(seconds / 60);
    if (seconds < 86400) return QStringLiteral("%1時間前").arg(seconds / 3600);
    if (seconds < 7 * 86400) return QStringLiteral("%1日前").arg(seconds / 86400);
    return {};
}

int North::textWeight(const QString &text)
{
    int weight = 0;
    for (auto cp : text.toUcs4()) {
        // ASCII and half-width forms. The API remains the final validator.
        weight += (cp <= 0x7f || (cp >= 0xff61 && cp <= 0xff9f)) ? 1 : 2;
    }
    return weight;
}

QJsonObject North::displayedPost(const QJsonObject &post)
{
    const auto original = post.value("retweetOf").toObject();
    return original.isEmpty() ? post : original;
}

QString North::postUrl(const QJsonObject &post)
{
    const auto p = displayedPost(post);
    return QStringLiteral("https://north.rip/%1/status/%2")
        .arg(QString::fromLatin1(QUrl::toPercentEncoding(p.value("author").toObject().value("handle").toString())),
             QString::fromLatin1(QUrl::toPercentEncoding(p.value("id").toString())));
}

QString North::plainPostText(const QJsonObject &post)
{
    if (post.value("deleted").toBool() || post.value("unavailable").toBool())
        return QStringLiteral("このポストは表示できません。");
    if (!post.value("hiddenReason").toString().isEmpty())
        return QStringLiteral("ミュートまたはブロックしているアカウントのポストです。");
    return post.value("text").toString();
}

QList<North::TextLink> North::textLinks(const QString &text)
{
    static const QRegularExpression pattern(QStringLiteral(R"(https?://[^\s<>"'「」『』【】（）。、！？]+)"), QRegularExpression::CaseInsensitiveOption);
    static const QRegularExpression xPost(QStringLiteral(
        R"(^https?://(?:(?:www|mobile)\.)?(?:x\.com|twitter\.com)/(?:[A-Za-z0-9_]+/status|i/(?:web/)?status)/[0-9]+(?:/(?:photo|video)/[0-9]+)?(?:\?[A-Za-z0-9._~%!$&()*+,;=:@/?-]*)?(?:#[A-Za-z0-9._~%!$&()*+,;=:@/?-]*)?)"),
        QRegularExpression::CaseInsensitiveOption);
    static const QRegularExpression youtubeHost(QStringLiteral(
        R"(^https?://(?:(?:www|m|music)\.)?(?:youtube\.com|youtu\.be|youtube-nocookie\.com)/)"), QRegularExpression::CaseInsensitiveOption);
    QList<TextLink> links;
    qsizetype position = 0;
    while (true) {
        const auto match = pattern.match(text, position);
        if (!match.hasMatch()) break;
        auto value = match.captured();
        while (!value.isEmpty()) {
            const auto last = value.back();
            if (QStringLiteral(".,!?:;").contains(last) || (last == ')' && value.count(')') > value.count('('))
                || (last == ']' && value.count(']') > value.count('['))) value.chop(1);
            else break;
        }
        // A post ID is numeric: adjacent prose is not part of an X status URL.
        // Keep this specific to X so other sites can still use Japanese paths.
        const auto x = xPost.match(value);
        if (x.hasMatch()) value = x.captured();
        if (youtubeHost.match(value).hasMatch()) {
            qsizetype end = 0;
            while (end < value.size() && value.at(end).unicode() < 128) ++end;
            const auto candidate = value.left(end);
            if (!canonicalYouTubeVideo(QUrl(candidate)).isEmpty()) value = candidate;
        }
        position = match.capturedStart() + std::max<qsizetype>(1, value.size());
        const QUrl url(value);
        if (url.isValid() && !url.host().isEmpty() && url.userInfo().isEmpty())
            links.append({int(match.capturedStart()), int(value.size()), url});
    }
    return links;
}

QList<North::TextMention> North::textMentions(const QString &text)
{
    static const QRegularExpression pattern(QStringLiteral(R"((?<![A-Za-z0-9_@])@([A-Za-z0-9_]+)(?![A-Za-z0-9_@]))"));
    const auto links = textLinks(text);
    QList<TextMention> mentions;
    auto matches = pattern.globalMatch(text);
    while (matches.hasNext()) {
        const auto match = matches.next();
        const int start = int(match.capturedStart());
        const bool insideLink = std::any_of(links.begin(), links.end(), [start](const TextLink &link) {
            return start >= link.start && start < link.start + link.length;
        });
        if (!insideLink) mentions.append({start, int(match.capturedLength()), match.captured(1)});
    }
    return mentions;
}

QList<North::TextLink> North::textSearchTags(const QString &text)
{
    static const QRegularExpression hashtag(QStringLiteral(R"((?<![A-Za-z0-9_&/])[#＃]([\p{L}\p{M}\p{Nd}_゠-ヿ぀-ゟ一-鿿]*[\p{L}\p{M}_゠-ヿ぀-ゟ一-鿿][\p{L}\p{M}\p{Nd}_゠-ヿ぀-ゟ一-鿿]*))"));
    static const QRegularExpression cashtag(QStringLiteral(R"((?<![A-Za-z0-9_$])\$([A-Za-z]{1,6}(?:\.[A-Za-z]{1,2})?)(?![A-Za-z0-9]))"));
    static const QRegularExpression formatting(QStringLiteral(R"(</?\p{Cf}*(?:色|color|太字|bold|b|斜体|italic|i)\p{Cf}*[^<>]{0,48}>)"), QRegularExpression::CaseInsensitiveOption);
    QList<QPair<int, int>> protectedRanges;
    for (const auto &url : textLinks(text)) protectedRanges.append({url.start, url.start + url.length});
    auto tags = formatting.globalMatch(text);
    while (tags.hasNext()) { const auto tag = tags.next(); protectedRanges.append({int(tag.capturedStart()), int(tag.capturedEnd())}); }
    QList<TextLink> links;
    auto scan = [&](const QRegularExpression &pattern, const QString &prefix) {
        auto matches = pattern.globalMatch(text);
        while (matches.hasNext()) {
            const auto match = matches.next(); const int start = int(match.capturedStart());
            if (std::any_of(protectedRanges.begin(), protectedRanges.end(), [start](const auto &range) {
                return start >= range.first && start < range.second;
            })) continue;
            QUrl target("qtnorth-search:"); QUrlQuery query; query.addQueryItem("q", prefix + match.captured(1)); target.setQuery(query);
            links.append({start, int(match.capturedLength()), target});
        }
    };
    scan(hashtag, "#"); scan(cashtag, "$");
    std::sort(links.begin(), links.end(), [](const TextLink &a, const TextLink &b) { return a.start < b.start; });
    return links;
}

QUrl North::canonicalXPost(const QUrl &url)
{
    static const QSet<QString> hosts{"x.com", "www.x.com", "mobile.x.com", "twitter.com", "www.twitter.com", "mobile.twitter.com"};
    static const QRegularExpression path(QStringLiteral(R"(^/(?:[A-Za-z0-9_]+/status|i/(?:web/)?status)/(\d+)(?:/.*)?$)"));
    const auto match = path.match(url.path());
    if (!hosts.contains(url.host().toLower()) || !match.hasMatch() || !url.userInfo().isEmpty()
        || (url.scheme() != "http" && url.scheme() != "https") || (url.port(-1) != -1 && url.port() != 443)) return {};
    return QUrl("https://x.com/i/status/" + match.captured(1));
}

bool North::isXPost(const QUrl &url) { return !canonicalXPost(url).isEmpty(); }

QUrl North::canonicalYouTubeVideo(const QUrl &url)
{
    if (!url.isValid() || !url.userInfo().isEmpty() || (url.scheme() != "http" && url.scheme() != "https")
        || (url.port(-1) != -1 && url.port() != 443 && !(url.scheme() == "http" && url.port() == 80))) return {};
    static const QSet<QString> hosts{"youtube.com", "www.youtube.com", "m.youtube.com", "music.youtube.com"};
    static const QSet<QString> shortHosts{"youtu.be", "www.youtu.be"};
    static const QSet<QString> embedHosts{"youtube-nocookie.com", "www.youtube-nocookie.com"};
    static const QRegularExpression videoId("^[A-Za-z0-9_-]{11}$");
    static const QRegularExpression videoPath("^/(shorts|live|embed)/([A-Za-z0-9_-]{11})/?$");
    const auto host = url.host().toLower(); QString id;
    if (shortHosts.contains(host)) {
        id = url.path().mid(1);
        if (id.endsWith('/')) id.chop(1);
    } else if (hosts.contains(host)) {
        if (url.path() == "/watch" || url.path() == "/watch/") id = QUrlQuery(url).queryItemValue("v");
        else id = videoPath.match(url.path()).captured(2);
    } else if (embedHosts.contains(host) && url.path().startsWith("/embed/")) id = videoPath.match(url.path()).captured(2);
    if (!videoId.match(id).hasMatch()) return {};
    QUrl canonical("https://www.youtube.com/watch"); QUrlQuery query; query.addQueryItem("v", id); canonical.setQuery(query);
    return canonical;
}
