#include "linkpreview.h"
#include "northapi.h"
#include "post.h"
#include "postcard.h"
#include "posttext.h"
#include "preferences.h"
#include "remoteimage.h"
#include "theme.h"
#include "testserver.h"
#include <QDesktopServices>
#include <QBuffer>
#include <QDir>
#include <QLabel>
#include <QNetworkReply>
#include <QPushButton>
#include <QPainter>
#include <QScrollArea>
#include <QScrollBar>
#include <QSettings>
#include <QTextDocument>
#include <QTextCursor>
#include <QTemporaryDir>
#include <QVBoxLayout>
#include <QtTest>

namespace {
QJsonObject post(const QString &body) {
    return {{"id", "one"}, {"text", body}, {"author", QJsonObject{{"name", "North user"}, {"handle", "north_user"}}}};
}
Response html(const QByteArray &body, int delay = 0) {
    return {200, {}, {{"Content-Type", "text/html; charset=utf-8"}}, delay, body};
}
QString visibleTitle(LinkPreview &preview) {
    for (auto *label : preview.findChildren<QLabel *>("previewTitle")) if (!label->isHidden()) return label->text();
    return {};
}
void capture(QWidget &widget, const QString &name) {
    const auto directory = qEnvironmentVariable("QTNORTH_CAPTURE_DIR");
    if (!directory.isEmpty()) { QTest::qWait(30); QDir().mkpath(directory); widget.grab().save(directory + '/' + name + ".png"); }
}
}

class UrlCatcher : public QObject {
    Q_OBJECT
public:
    QList<QUrl> urls;
    ~UrlCatcher() override { QDesktopServices::unsetUrlHandler("https"); QDesktopServices::unsetUrlHandler("http"); }
public slots:
    void open(const QUrl &url) { urls.append(url); }
};

class PostContentTests : public QObject {
    Q_OBJECT
    QTemporaryDir m_settings;
private slots:
    void livePublicYouTubePreview() {
        const auto url = qEnvironmentVariable("QTNORTH_LIVE_YOUTUBE_URL");
        if (url.isEmpty()) QSKIP("Set QTNORTH_LIVE_YOUTUBE_URL to verify an explicitly selected public YouTube video.");
        NorthApi api; LinkPreviewService service(&api); bool done = false; LinkPreviewData result;
        auto *network = service.findChild<QNetworkAccessManager *>(); QVERIFY(network);
        connect(network, &QNetworkAccessManager::finished, this, [](QNetworkReply *reply) {
            qInfo() << "YouTube HTTP status:" << reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        });
        service.fetch(this, QUrl(url), [&](LinkPreviewData data) { result = data; done = true; });
        QTRY_VERIFY_WITH_TIMEOUT(done, 20000);
        QVERIFY2(result.error.isEmpty(), qPrintable(result.error)); QVERIFY(result.youTube);
        QVERIFY(!result.title.isEmpty()); QVERIFY(!result.author.isEmpty()); QVERIFY(!result.image.isEmpty());
        LinkPreview preview(QUrl(url), &api, nullptr, &service); preview.resize(640, 180); preview.show();
        QTRY_COMPARE(visibleTitle(preview), result.title);
        QTRY_VERIFY_WITH_TIMEOUT(preview.findChild<RemoteImage *>() && preview.findChild<RemoteImage *>()->hasImage(), 20000);
        qInfo() << "YouTube title:" << result.title << "channel:" << result.author << "thumbnail decoded.";
    }
    void livePublicXPreview() {
        const auto url = qEnvironmentVariable("QTNORTH_LIVE_X_URL");
        if (url.isEmpty()) QSKIP("Set QTNORTH_LIVE_X_URL to verify an explicitly selected public X post.");
        NorthApi api; LinkPreviewService service(&api); bool done = false; LinkPreviewData result;
        auto *network = service.findChild<QNetworkAccessManager *>(); QVERIFY(network);
        connect(network, &QNetworkAccessManager::finished, this, [](QNetworkReply *reply) {
            qInfo() << "X network result" << reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt()
                    << reply->errorString() << reply->header(QNetworkRequest::ContentTypeHeader).toString();
        });
        service.fetch(this, QUrl(url), [&](LinkPreviewData data) { result = data; done = true; });
        QTRY_VERIFY_WITH_TIMEOUT(done, 20000);
        QVERIFY2(result.error.isEmpty(), qPrintable(result.error)); QVERIFY(result.xPost);
        QVERIFY(!result.author.isEmpty()); QVERIFY(!result.description.isEmpty());
        qInfo() << "X author:" << result.author << "body characters:" << result.description.size();
        PostCard card(post(url + QString::fromUtf8("です")), false, &api); card.resize(680, 420); card.show();
        auto *preview = card.findChild<LinkPreview *>(); QVERIFY(preview);
        QTRY_COMPARE(visibleTitle(*preview), result.author);
        QVERIFY(card.findChild<PostText *>()->text().contains(QString::fromUtf8("</a>です")));
        capture(card, "x-preview-regression");
    }
    void initTestCase() {
        QCoreApplication::setOrganizationName("qtNorth-tests"); QCoreApplication::setApplicationName("post-content");
        QSettings::setDefaultFormat(QSettings::IniFormat);
        QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, m_settings.path());
        ThemeManager::instance()->setMode(ThemeManager::Light);
    }
    void init() { Preferences::instance()->setFoldLongPosts(false); Preferences::instance()->setLinkPreviews(true); }
    void cleanup() { QDesktopServices::unsetUrlHandler("https"); ThemeManager::instance()->setMode(ThemeManager::Light); }
    void urlsAreEscapedAndXMatchesOnlyActualStatusLinks() {
        const auto links = North::textLinks(QString::fromUtf8("参考 https://example.com/a?x=1&y=2。続き (https://example.org/path)."));
        QCOMPARE(links.size(), 2); QCOMPARE(links[0].url.toString(), "https://example.com/a?x=1&y=2");
        QCOMPARE(links[1].url.toString(), "https://example.org/path");
        QVERIFY(North::textLinks("javascript:alert(1) file:///etc/passwd").isEmpty());
        QVERIFY(North::isXPost(QUrl("https://twitter.com/jack/status/20?s=20")));
        QCOMPARE(North::canonicalXPost(QUrl("https://x.com/jack/status/20/photo/1")), QUrl("https://x.com/i/status/20"));
        QVERIFY(!North::isXPost(QUrl("https://x.com.evil.test/jack/status/20")));
        QVERIFY(!North::isXPost(QUrl("https://x.com/jack")));
        PostText body("<script>plain</script> https://example.com/?a=1&b=2"); body.resize(600, 100); body.show();
        QVERIFY(body.text().contains("&lt;script&gt;plain&lt;/script&gt;"));
        QVERIFY(body.text().contains("href=\"https://example.com/?a=1&amp;b=2\""));
        QVERIFY(body.text().contains("color:#1c71d8"));
        ThemeManager::instance()->setMode(ThemeManager::Dark); QVERIFY(body.text().contains("color:#70baff"));
    }
    void xPostLinkStopsBeforeAdjacentTextButJapaneseArticlePathsStillWork() {
        const auto url = QString("https://x.com/cyber_forecast/status/2114909395582910705");
        for (const auto &suffix : {QString::fromUtf8("です"), QString::fromUtf8("更新情報です。"), QString(" (update)"), QString("#topic")}) {
            const auto source = QString::fromUtf8("こちら ") + url + suffix;
            const auto links = North::textLinks(source); QCOMPARE(links.size(), 1);
            QVERIFY(North::isXPost(links[0].url));
            QCOMPARE(links[0].length, int((url + (suffix == "#topic" ? suffix : QString())).size()));
        }
        const auto tracked = North::textLinks(url + QString::fromUtf8("?s=20&t=abcです"));
        QCOMPARE(tracked.size(), 1); QCOMPARE(tracked[0].url.toString(), url + "?s=20&t=abc");
        const auto adjacent = North::textLinks(url + QString::fromUtf8("続きhttps://example.com/東京"));
        QCOMPARE(adjacent.size(), 2); QCOMPARE(adjacent[1].url.path(), QString::fromUtf8("/東京"));
        const auto article = North::textLinks(QString::fromUtf8("https://example.com/記事/東京?言語=日本語"));
        QCOMPARE(article.size(), 1); QCOMPARE(article[0].url.path(), QString::fromUtf8("/記事/東京"));
        PostText body(url + QString::fromUtf8("です")); body.show();
        QVERIFY(body.text().contains("href=\"" + url + "\"")); QVERIFY(body.text().contains(QString::fromUtf8("</a>です")));
    }
    void youtubeUrlFormsNormalizeWithoutLosingTheirOriginalLink() {
        const QUrl canonical("https://www.youtube.com/watch?v=M7lc1UVf-VE");
        const QStringList urls{
            canonical.toString(), "https://youtube.com/watch?feature=share&v=M7lc1UVf-VE&t=42",
            "https://youtu.be/M7lc1UVf-VE?si=tracking", "https://m.youtube.com/watch?v=M7lc1UVf-VE",
            "https://music.youtube.com/watch?v=M7lc1UVf-VE", "https://www.youtube.com/shorts/M7lc1UVf-VE",
            "https://www.youtube.com/live/M7lc1UVf-VE", "https://www.youtube.com/embed/M7lc1UVf-VE?start=42",
            "https://www.youtube-nocookie.com/embed/M7lc1UVf-VE"};
        for (const auto &url : urls) {
            QCOMPARE(North::canonicalYouTubeVideo(QUrl(url)), canonical);
            const auto links = North::textLinks(url + QString::fromUtf8("です"));
            QCOMPARE(links.size(), 1); QCOMPARE(links[0].url, QUrl(url));
        }
        for (const auto &url : {"https://youtube.com.evil.test/watch?v=M7lc1UVf-VE", "https://youtu.be.evil.test/M7lc1UVf-VE",
                               "https://www.youtube.com/playlist?list=PL123", "https://www.youtube.com/watch?v=bad-id",
                               "https://www.youtube.com/watch?v=M7lc1UVf-VEextra", "https://user@youtube.com/watch?v=M7lc1UVf-VE"})
            QVERIFY(North::canonicalYouTubeVideo(QUrl(url)).isEmpty());
    }
    void youtubeMetadataIsSharedAcrossUrlFormatsAndDoesNotExecuteHtml() {
        Server server; server.handler = [](Request) {
            return Response{200, {{"type", "video"}, {"title", "<b>Video & sound</b>"}, {"author_name", "Channel"},
                {"thumbnail_url", "https://i.ytimg.com/vi/M7lc1UVf-VE/hqdefault.jpg"}, {"html", "<iframe src='file:///etc/passwd'></iframe>"}}};
        };
        NorthApi api(nullptr, server.base()); api.usePersonalToken("secret"); LinkPreviewService service(&api, server.base()); int received = 0;
        for (const auto &url : {QUrl("https://youtu.be/M7lc1UVf-VE"), QUrl("https://www.youtube.com/shorts/M7lc1UVf-VE")})
            service.fetch(this, url, [&](LinkPreviewData data) {
                QVERIFY(data.youTube); QVERIFY(data.error.isEmpty()); QCOMPARE(data.title, "<b>Video & sound</b>");
                QCOMPARE(data.author, "Channel"); QCOMPARE(data.image.host(), "i.ytimg.com"); ++received;
            });
        QTRY_COMPARE(received, 2); QCOMPARE(server.requests.size(), 1);
        const auto request = server.requests.first(); QCOMPARE(request.url.path(), "/youtube/oembed");
        const QUrlQuery query(request.url); QCOMPARE(query.queryItemValue("format"), "json");
        QCOMPARE(query.queryItemValue("url"), "https://www.youtube.com/watch?v=M7lc1UVf-VE");
        QVERIFY(!request.headers.contains("authorization")); QVERIFY(!request.headers.contains("cookie"));
        QVERIFY(!LinkPreviewService::parseYouTube("<html>blocked</html>").error.isEmpty());
        QVERIFY(!LinkPreviewService::parseYouTube("{\"type\":\"video\"}").error.isEmpty());
        PostCard card(post("https://youtu.be/M7lc1UVf-VE https://www.youtube.com/watch?v=M7lc1UVf-VE"), false, &api);
        QCOMPARE(card.findChildren<LinkPreview *>().size(), 1);
    }
    void youtubeUnavailableVideosKeepTheirProviderAndCanBeRetried() {
        Server server; bool unavailable = true;
        server.handler = [&](Request) {
            if (unavailable) return Response{404, {{"error", "unavailable"}}};
            return Response{200, {{"type", "video"}, {"title", "Recovered video"}, {"author_name", "Channel"}}};
        };
        NorthApi api(nullptr, server.base()); LinkPreviewService service(&api, server.base());
        const QUrl original("https://youtu.be/M7lc1UVf-VE?t=42");
        LinkPreview preview(original, &api, nullptr, &service); preview.resize(600, 190); preview.show();
        QTRY_VERIFY(preview.findChild<QPushButton *>("previewRetry"));
        QCOMPARE(preview.findChild<QPushButton *>("previewOpen")->text(), QString::fromUtf8("YouTubeで開く"));
        unavailable = false; QTest::mouseClick(preview.findChild<QPushButton *>("previewRetry"), Qt::LeftButton);
        QTRY_COMPARE(visibleTitle(preview), "Recovered video"); QCOMPARE(server.requests.size(), 2);
        UrlCatcher catcher; QDesktopServices::setUrlHandler("https", &catcher, "open");
        QTest::mouseClick(preview.findChild<QPushButton *>("previewOpen"), Qt::LeftButton);
        QTRY_COMPARE(catcher.urls.size(), 1); QCOMPARE(catcher.urls.first(), original);
    }
    void youtubeCardLoadsItsThumbnailAndFollowsBothThemes() {
        QImage image(480, 270, QImage::Format_RGB32); image.fill(QColor("#314a6e"));
        QPainter painter(&image); painter.setPen(Qt::white); auto font = painter.font(); font.setPixelSize(32); painter.setFont(font);
        painter.drawText(image.rect(), Qt::AlignCenter, "Qt desktop apps"); painter.end();
        QByteArray bytes; QBuffer buffer(&bytes); buffer.open(QIODevice::WriteOnly); image.save(&buffer, "PNG");
        Server server; server.handler = [&](Request request) {
            if (request.url.path() == "/thumbnail.png") return Response{200, {}, {{"Content-Type", "image/png"}}, 0, bytes};
            return Response{200, {{"type", "video"}, {"title", QString::fromUtf8("Qtでデスクトップアプリを作る")},
                {"author_name", "qtNorth channel"}, {"thumbnail_url", server.base().resolved(QUrl("/thumbnail.png")).toString()}}};
        };
        NorthApi api(nullptr, server.base()); LinkPreviewService service(&api, server.base());
        LinkPreview preview(QUrl("https://www.youtube.com/watch?v=M7lc1UVf-VE"), &api, nullptr, &service); preview.resize(620, 180); preview.show();
        QTRY_COMPARE(visibleTitle(preview), QString::fromUtf8("Qtでデスクトップアプリを作る"));
        QTRY_VERIFY(preview.findChild<RemoteImage *>() && preview.findChild<RemoteImage *>()->hasImage());
        QCOMPARE(preview.findChild<QLabel *>("previewAuthor")->text(), "qtNorth channel");
        QCOMPARE(preview.findChild<QLabel *>("previewTitle")->textFormat(), Qt::PlainText);
        auto *thumbnail = preview.findChild<RemoteImage *>(); QVERIFY(thumbnail->playIndicator());
        capture(preview, "youtube-preview");
        ThemeManager::instance()->setMode(ThemeManager::Dark); capture(preview, "youtube-preview-dark");
        const auto position = thumbnail->mapTo(&preview, QPoint(thumbnail->width() / 2 - 18, thumbnail->height() / 2));
        const auto red = preview.grab().toImage().pixelColor(position);
        QVERIFY(red.red() > 200 && red.green() < 80);
    }
    void foldedPostsShowFirstThreeLinesAndExpandWithoutNavigation() {
        Preferences::instance()->setFoldLongPosts(true);
        PostCard card(post(QString::fromUtf8("1行目\n2行目\n3行目\n4行目\n5行目")), false);
        card.resize(600, 330); card.show();
        QVERIFY(QTest::qWaitForWindowExposed(&card));
        auto *body = card.findChild<PostText *>(); auto *expand = card.findChild<QPushButton *>("expandPost");
        QSignalSpy selected(&card, &PostCard::selected);
        QTRY_VERIFY(expand->isVisible());
        // Japanese fallback glyphs can have a taller line box than the Latin font metrics.
        QVERIFY(body->height() <= body->fontMetrics().lineSpacing() * 3 + 8);
        capture(card, "post-folded");
        const int collapsed = body->height();
        QTest::mouseClick(expand, Qt::LeftButton); QTRY_VERIFY(body->height() > collapsed);
        QCOMPARE(expand->text(), QString::fromUtf8("折りたたむ")); QCOMPARE(selected.count(), 0);
        QTest::mouseClick(expand, Qt::LeftButton);
        QTRY_VERIFY(body->height() <= collapsed);
        card.setExpandedBody(true); QTRY_VERIFY(expand->isHidden()); QTRY_VERIFY(body->height() > collapsed);
        Preferences::instance()->setFoldLongPosts(false); QVERIFY(expand->isHidden());
    }
    void softWrappingRecalculatesThreeLineLimitOnResize() {
        Preferences::instance()->setFoldLongPosts(true);
        PostCard card(post(QString("A long post with words ").repeated(6)), false); card.resize(290, 360); card.show();
        auto *body = card.findChild<PostText *>(); auto *expand = card.findChild<QPushButton *>("expandPost");
        QTRY_VERIFY(expand->isVisible()); QVERIFY(body->height() <= body->fontMetrics().lineSpacing() * 3 + 4);
        card.resize(1700, 360); QTRY_VERIFY(expand->isHidden());
        card.resize(290, 360); QTRY_VERIFY(expand->isVisible());
        Preferences::instance()->setFoldLongPosts(false); QTRY_VERIFY(expand->isHidden());
    }
    void clickingBlueLinkOpensItWithoutOpeningThePost() {
        UrlCatcher catcher; QDesktopServices::setUrlHandler("https", &catcher, "open");
        PostCard card(post(QString::fromUtf8("リンク https://example.com/page")), false); card.resize(700, 250); card.show();
        QVERIFY(QTest::qWaitForWindowExposed(&card));
        auto *body = card.findChild<PostText *>(); QSignalSpy selected(&card, &PostCard::selected);
        const int x = body->fontMetrics().horizontalAdvance(QString::fromUtf8("リンク ")) + 8;
        QTest::mouseClick(body, Qt::LeftButton, {}, QPoint(x, body->fontMetrics().height() / 2));
        QTRY_COMPARE(catcher.urls.size(), 1); QCOMPARE(catcher.urls.first(), QUrl("https://example.com/page"));
        QTest::qWait(20); QCOMPARE(selected.count(), 0);
    }
    void mentionsSkipEmailsAndUrlsAndPreserveJapaneseText() {
        const auto source = QString::fromUtf8("@Alice_01 と@bobさん、(@carol)\nメール alice@example.com @@no @alice@example.org\nhttps://example.com/@url_user?q=@query_user https://x.com/other/status/20。@afterです");
        const auto mentions = North::textMentions(source);
        QStringList handles;
        for (const auto &mention : mentions) {
            handles.append(mention.handle);
            QCOMPARE(source.mid(mention.start, mention.length), "@" + mention.handle);
        }
        QCOMPARE(handles, QStringList({"Alice_01", "bob", "carol", "after"}));
        PostText body(source + " <strong>&plain</strong>"); body.resize(700, 300); body.show();
        QVERIFY(body.text().contains("href=\"qtnorth-profile:Alice_01\""));
        QVERIFY(body.text().contains(QString::fromUtf8("@bob</a>さん")));
        QVERIFY(body.text().contains("&lt;strong&gt;&amp;plain&lt;/strong&gt;"));
        QVERIFY(!body.text().contains("qtnorth-profile:example"));
        QVERIFY(!body.text().contains("qtnorth-profile:url_user"));
        QVERIFY(body.text().contains("color:#1c71d8"));
        ThemeManager::instance()->setMode(ThemeManager::Dark);
        QVERIFY(body.text().contains("color:#70baff"));
        QVERIFY(body.text().contains("qtnorth-profile:Alice_01"));
    }
    void mentionClickOpensProfileWithoutOpeningPostOrBrowser() {
        UrlCatcher catcher; QDesktopServices::setUrlHandler("https", &catcher, "open");
        auto item = post(QString::fromUtf8("@Alice_01 さんへ\n2行目\n3行目\n4行目"));
        item.insert("quoted", post("@quoted_user hello"));
        Preferences::instance()->setFoldLongPosts(true);
        PostCard card(item, false); card.resize(620, 420); card.show();
        QVERIFY(QTest::qWaitForWindowExposed(&card));
        const auto texts = card.findChildren<PostText *>(); QCOMPARE(texts.size(), 2);
        QSignalSpy profile(&card, &PostCard::profileRequested), selected(&card, &PostCard::selected);
        QTest::mouseClick(texts[0], Qt::LeftButton, {}, QPoint(8, texts[0]->fontMetrics().height() / 2));
        QTRY_COMPARE(profile.count(), 1); QCOMPARE(profile[0][0].toString(), "Alice_01");
        QTest::mouseClick(texts[1], Qt::LeftButton, {}, QPoint(8, texts[1]->fontMetrics().height() / 2));
        QTRY_COMPARE(profile.count(), 2); QCOMPARE(profile[1][0].toString(), "quoted_user");
        QTest::qWait(20); QCOMPARE(selected.count(), 0); QVERIFY(catcher.urls.isEmpty());
        capture(card, "mentions");
    }
    void repostContextIconAndNameOpenReposterProfile() {
        auto wrapper = post("unused"); wrapper.insert("retweetOf", post("@north_user hello"));
        PostCard card(wrapper, false); card.resize(600, 240); card.show();
        auto *context = card.findChild<QPushButton *>("repostContext"); QVERIFY(context);
        QVERIFY(!context->icon().isNull()); QCOMPARE(context->iconSize(), QSize(14, 14));
        QSignalSpy profile(&card, &PostCard::profileRequested), selected(&card, &PostCard::selected);
        QTest::mouseClick(context, Qt::LeftButton);
        QCOMPARE(profile.count(), 1); QCOMPARE(profile[0][0].toString(), "north_user");
        QCOMPARE(selected.count(), 0);
        capture(card, "repost-context");
    }
    void northFormattingRendersNestedStylesAndRestoresOuterColors() {
        PostText body(QString::fromUtf8("前<b>太字<i>両方</i></b><色=赤>赤<color='#0f0'>緑</color>赤</色>後"));
        body.resize(650, 120); body.show();
        QCOMPARE(body.displayText(), QString::fromUtf8("前太字両方赤緑赤後"));
        QTextDocument document; document.setHtml(body.text());
        QCOMPARE(document.toPlainText(), body.displayText());
        QCOMPARE(document.find(QString::fromUtf8("太字")).charFormat().fontWeight(), int(QFont::Bold));
        QVERIFY(!document.find(QString::fromUtf8("太字")).charFormat().fontItalic());
        QVERIFY(document.find(QString::fromUtf8("両方")).charFormat().fontItalic());
        QCOMPARE(document.find(QString::fromUtf8("両方")).charFormat().fontWeight(), int(QFont::Bold));
        QCOMPARE(document.find(QString::fromUtf8("赤")).charFormat().foreground().color(), QColor("#f4212e"));
        QCOMPARE(document.find(QString::fromUtf8("緑")).charFormat().foreground().color(), QColor("#00ff00"));
        QCOMPARE(document.find(QString::fromUtf8("赤"), 7).charFormat().foreground().color(), QColor("#f4212e"));
        for (const auto &tag : {QString("b"), QString("bold"), QString::fromUtf8("太字")}) {
            PostText alias("<" + tag + ">bold</" + tag + ">"); document.setHtml(alias.text());
            QCOMPARE(document.find("bold").charFormat().fontWeight(), int(QFont::Bold));
        }
        for (const auto &tag : {QString("i"), QString("italic"), QString::fromUtf8("斜体")}) {
            PostText alias("<" + tag + ">italic</" + tag + ">"); document.setHtml(alias.text());
            QVERIFY(document.find("italic").charFormat().fontItalic());
        }
        PostText named("<COLOR=BLUE>blue</color><color=abc>hex</color>"); document.setHtml(named.text());
        QCOMPARE(document.find("blue").charFormat().foreground().color(), QColor("#1d9bf0"));
        QCOMPARE(document.find("hex").charFormat().foreground().color(), QColor("#aabbcc"));
    }
    void formattingDoesNotRenderArbitraryHtmlOrBrokenTags() {
        for (const auto &source : {QString("<b>unclosed"), QString("<color=not-a-color>x</color>"),
             QString("<b title=x>x</b>"), QString("<img src=x onerror=alert(1)>"),
             QString("<color='red; font-size:100px'>text</color>"), QString("**markdown** # heading")}) {
            PostText body(source); QCOMPARE(body.displayText(), source);
            if (body.textFormat() == Qt::PlainText) QCOMPARE(body.text(), source);
            else { QTextDocument document; document.setHtml(body.text()); QCOMPARE(document.toPlainText(), source); }
        }
        PostText mixed("<b><img src=x>safe & sound</b>"); QTextDocument document; document.setHtml(mixed.text());
        QCOMPARE(document.toPlainText(), QString("<img src=x>safe & sound"));
        QVERIFY(mixed.text().contains("&lt;img src=x&gt;"));
    }
    void hashtagsAndCashtagsIgnoreUrlsColorsAndInvalidSymbols() {
        const auto source = QString::fromUtf8("#日本語 ＃全角 #Qt6 #123 _#word &#entity /#path $AAPL $BRK.B $TOOLONG $123 $$USD foo$MSFT https://example.com/#fragment?q=$META <color=#ff8800>x</color>");
        const auto links = North::textSearchTags(source); QStringList queries;
        for (const auto &link : links) {
            QCOMPARE(link.url.scheme(), "qtnorth-search");
            queries.append(QUrlQuery(link.url).queryItemValue("q", QUrl::FullyDecoded));
        }
        QCOMPARE(queries, QStringList({QString::fromUtf8("#日本語"), QString::fromUtf8("#全角"), "#Qt6", "$AAPL", "$BRK.B"}));
        PostText color("<color=#ff8800>text #Qt6 $AAPL @north_user</color>");
        QVERIFY(!color.text().contains("q=%23ff8800"));
        QTextDocument document; document.setHtml(color.text());
        QCOMPARE(document.find("text").charFormat().foreground().color(), QColor("#ff8800"));
        QVERIFY(document.find("#Qt6").charFormat().isAnchor()); QVERIFY(document.find("$AAPL").charFormat().isAnchor());
        QVERIFY(document.find("@north_user").charFormat().isAnchor());
    }
    void styledTagClicksSearchWithoutOpeningPostAndFoldingStillWorks() {
        Preferences::instance()->setFoldLongPosts(true);
        auto item = post(QString::fromUtf8("<b>#日本語</b>\n<斜体>$AAPL</斜体>\n<色=赤>赤い文字</色>\n4行目"));
        item.insert("quoted", post("<color=green>$BRK.B</color>"));
        PostCard card(item, false); card.resize(640, 360); card.show();
        QSignalSpy search(&card, &PostCard::searchRequested), selected(&card, &PostCard::selected);
        const auto texts = card.findChildren<PostText *>(); QCOMPARE(texts.size(), 2);
        QTRY_VERIFY(card.findChild<QPushButton *>("expandPost")->isVisible());
        QTest::mouseClick(texts[0], Qt::LeftButton, {}, QPoint(8, texts[0]->fontMetrics().height() / 2));
        QTRY_COMPARE(search.count(), 1); QCOMPARE(search[0][0].toString(), QString::fromUtf8("#日本語"));
        QTest::mouseClick(texts[1], Qt::LeftButton, {}, QPoint(8, texts[1]->fontMetrics().height() / 2));
        QTRY_COMPARE(search.count(), 2); QCOMPARE(search[1][0].toString(), "$BRK.B");
        QTest::qWait(20); QCOMPARE(selected.count(), 0);
        auto *expand = card.findChild<QPushButton *>("expandPost"); const int collapsed = texts[0]->height();
        QTest::mouseClick(expand, Qt::LeftButton); QTRY_VERIFY(texts[0]->height() > collapsed);
        capture(card, "formatted-post");
        ThemeManager::instance()->setMode(ThemeManager::Dark); capture(card, "formatted-post-dark");
        QTextDocument document; document.setHtml(texts[0]->text());
        QCOMPARE(document.find(QString::fromUtf8("赤い文字")).charFormat().foreground().color(), QColor("#f4212e"));
        QCOMPARE(document.find(QString::fromUtf8("#日本語")).charFormat().foreground().color(), QColor("#70baff"));
        card.setExpandedBody(true); QTRY_VERIFY(expand->isHidden());
    }
    void metadataAndXTextAreDecodedWithoutRenderingRemoteHtml() {
        const auto data = LinkPreviewService::parseHtml(
            "<title>fallback</title><meta content='A &amp; B' property='og:title'>"
            "<meta name=description content='Description &lt;b&gt;here&lt;/b&gt;'>"
            "<meta content='/image.webp' property='og:image'>", QUrl("https://example.com/article"));
        QCOMPARE(data.title, "A & B"); QVERIFY(data.description.contains("<b>here</b>"));
        QCOMPARE(data.image, QUrl("https://example.com/image.webp"));
        const auto x = LinkPreviewService::parseX(QJsonDocument(QJsonObject{{"author_name", "Author"},
            {"html", "<blockquote><p>Hello &amp; Qt<br>second line</p>Author <a href='https://x.com/a/status/1'>2026-10-28</a></blockquote>"}}).toJson());
        QVERIFY(x.error.isEmpty()); QCOMPARE(x.author, "Author"); QCOMPARE(x.description, "Hello & Qt\nsecond line");
        QCOMPARE(x.date, "2026-10-28");
        QVERIFY(!LinkPreviewService::parseX("not json").error.isEmpty());
    }
    void repeatedPreviewsShareRequestsAndNeverSendNorthCredentials() {
        Server server; server.handler = [](Request) { return html("<title>Article</title>", 50); };
        NorthApi api(nullptr, server.base()); api.usePersonalToken("secret"); LinkPreviewService service(&api, server.base());
        int received = 0; bool destroyed = false; auto *context = new QObject;
        const auto url = server.base().resolved(QUrl("/article"));
        service.fetch(context, url, [&](LinkPreviewData) { destroyed = true; });
        for (int i = 0; i < 10; ++i) service.fetch(this, url, [&](LinkPreviewData data) { QCOMPARE(data.title, "Article"); ++received; });
        delete context; QTRY_COMPARE(received, 10); QVERIFY(!destroyed); QCOMPARE(server.requests.size(), 1);
        service.fetch(this, url, [&](LinkPreviewData) { ++received; }); QTRY_COMPARE(received, 11); QCOMPARE(server.requests.size(), 1);
        api.clearCaches(); service.fetch(this, url, [&](LinkPreviewData) { ++received; });
        QTRY_COMPARE(received, 12); QCOMPARE(server.requests.size(), 2);
        for (const auto &request : server.requests) {
            QVERIFY(!request.headers.contains("authorization")); QVERIFY(!request.headers.contains("cookie"));
        }
    }
    void xUrlsUseOembedAndShareTheSameCanonicalPost() {
        Server server; server.handler = [](Request) {
            return Response{200, {{"author_name", "Jack"}, {"html", "<blockquote><p>Hello X</p><a>Today</a></blockquote>"}}};
        };
        NorthApi api(nullptr, server.base()); LinkPreviewService service(&api, server.base()); int received = 0;
        for (const auto &url : {QUrl("https://twitter.com/jack/status/20"), QUrl("https://x.com/jack/status/20?s=20")})
            service.fetch(this, url, [&](LinkPreviewData data) { QVERIFY(data.xPost); QCOMPARE(data.author, "Jack"); QCOMPARE(data.description, "Hello X"); ++received; });
        QTRY_COMPARE(received, 2); QCOMPARE(server.requests.size(), 1);
        QCOMPARE(server.requests.first().url.path(), "/oembed"); const QUrlQuery query(server.requests.first().url);
        QCOMPARE(query.queryItemValue("omit_script"), "true"); QCOMPARE(query.queryItemValue("dnt"), "true");
        QCOMPARE(query.queryItemValue("url"), "https://x.com/i/status/20");
    }
    void disallowedTargetsRedirectsAndLargePagesFailWithoutFollowingThem() {
        Server server; server.handler = [](Request request) {
            if (request.url.path() == "/large") return Response{200, {}, {{"Content-Type", "text/html"}, {"Content-Length", "6000000"}}, 0, "too large"};
            return Response{302, {}, {{"Location", "http://localhost/private"}}};
        };
        NorthApi api(nullptr, server.base()); LinkPreviewService service(&api, server.base());
        for (const auto &url : {QUrl("file:///etc/passwd"), QUrl("http://127.0.0.2/private"), QUrl("http://192.168.1.1/"),
                               server.base().resolved(QUrl("/redirect")), server.base().resolved(QUrl("/large"))}) {
            bool done = false; service.fetch(this, url, [&](LinkPreviewData data) { QVERIFY(!data.error.isEmpty()); done = true; });
            QTRY_VERIFY(done);
        }
        QCOMPARE(server.requests.size(), 2);
        service.fetch(this, server.base().resolved(QUrl("/large")), [](LinkPreviewData) {});
        QTest::qWait(20); QCOMPARE(server.requests.size(), 2);
    }
    void largePagesCanUseMetadataWithoutDownloadingTheBody() {
        Server server; server.handler = [](Request) {
            return html(QByteArray("<html><head><title>Head only</title></head><body>") + QByteArray(1024 * 1024, 'x'));
        };
        NorthApi api(nullptr, server.base()); LinkPreviewService service(&api, server.base()); bool done = false;
        service.fetch(this, server.base(), [&](LinkPreviewData data) { QVERIFY(data.error.isEmpty()); QCOMPARE(data.title, "Head only"); done = true; });
        QTRY_VERIFY(done);
    }
    void logoutAbortsRequestsAndDropsLateAndQueuedResults() {
        Server server; server.handler = [](Request) { return html("<title>Private URL</title>", 150); };
        NorthApi api(nullptr, server.base()); api.usePersonalToken("one"); LinkPreviewService service(&api, server.base()); bool called = false;
        for (int i = 0; i < 12; ++i)
            service.fetch(this, server.base().resolved(QUrl("/" + QString::number(i))), [&](LinkPreviewData) { called = true; });
        QTRY_COMPARE(server.requests.size(), 4); api.logout(); QTest::qWait(200);
        QVERIFY(!called); QCOMPARE(server.requests.size(), 4);
        api.usePersonalToken("two"); server.handler = [](Request) { return html("<title>New</title>"); };
        service.fetch(this, server.base().resolved(QUrl("/0")), [&](LinkPreviewData data) { QCOMPARE(data.title, "New"); called = true; });
        QTRY_VERIFY(called); QCOMPARE(server.requests.size(), 5);
    }
    void rateLimitsDelayOtherPreviewsWithoutRetryingTheFailedUrl() {
        Server server; server.handler = [](Request request) {
            if (request.url.path() == "/limited") return Response{429, {}, {{"Retry-After", "1"}}};
            return html("<title>Ready</title>");
        };
        NorthApi api(nullptr, server.base()); LinkPreviewService service(&api, server.base()); bool failed = false, done = false;
        const auto limited = server.base().resolved(QUrl("/limited"));
        service.fetch(this, limited, [&](LinkPreviewData data) { QVERIFY(!data.error.isEmpty()); failed = true; });
        QTRY_VERIFY(failed);
        service.fetch(this, server.base().resolved(QUrl("/next")), [&](LinkPreviewData data) { QCOMPARE(data.title, "Ready"); done = true; });
        QTest::qWait(100); QCOMPARE(server.requests.size(), 1);
        QTRY_VERIFY(done); QCOMPARE(server.requests.size(), 2);
        service.fetch(this, limited, [](LinkPreviewData data) { QVERIFY(!data.error.isEmpty()); });
        QTest::qWait(20); QCOMPARE(server.requests.size(), 2);
    }
    void failedPreviewsCanBeRetriedWithoutClearingOtherCachedUrls() {
        Server server; bool failed = true; server.handler = [&](Request request) {
            if (failed && request.url.path() == "/article") return Response{500, {{"error", "temporary"}}};
            return html("<title>Recovered</title>");
        };
        NorthApi api(nullptr, server.base()); LinkPreviewService service(&api, server.base());
        bool primed = false;
        service.fetch(this, server.base().resolved(QUrl("/other")), [&](LinkPreviewData) { primed = true; }); QTRY_VERIFY(primed);
        LinkPreview preview(server.base().resolved(QUrl("/article")), &api, nullptr, &service); preview.resize(500, 220); preview.show();
        QTRY_VERIFY(preview.findChild<QPushButton *>("previewRetry"));
        QCOMPARE(server.requests.size(), 2); failed = false;
        QTest::mouseClick(preview.findChild<QPushButton *>("previewRetry"), Qt::LeftButton);
        QTRY_COMPARE(visibleTitle(preview), "Recovered"); QCOMPARE(server.requests.size(), 3);
        primed = false; service.fetch(this, server.base().resolved(QUrl("/other")), [&](LinkPreviewData) { primed = true; });
        QTRY_VERIFY(primed); QCOMPARE(server.requests.size(), 3);
    }
    void cacheClearingDoesNotBypassRetryAfterOrStrandWaitingPreviews() {
        Server server; server.handler = [](Request request) {
            if (request.url.path() == "/limited") return Response{429, {}, {{"Retry-After", "1"}}};
            return html("<title>Recovered</title>");
        };
        NorthApi api(nullptr, server.base()); LinkPreviewService service(&api, server.base()); bool failed = false, done = false;
        service.fetch(this, server.base().resolved(QUrl("/limited")), [&](LinkPreviewData) { failed = true; });
        QTRY_VERIFY(failed); api.clearCaches();
        service.fetch(this, server.base().resolved(QUrl("/next")), [&](LinkPreviewData data) { QCOMPARE(data.title, "Recovered"); done = true; });
        QTest::qWait(100); QCOMPARE(server.requests.size(), 1);
        QTRY_VERIFY(done); QCOMPARE(server.requests.size(), 2);
    }
    void hiddenPostsAndDisabledPreviewsNeverRequestUrls() {
        Server server; NorthApi api(nullptr, server.base()); LinkPreviewService service(&api, server.base());
        auto hidden = post(server.base().resolved(QUrl("/private")).toString()); hidden.insert("hiddenReason", "muted");
        PostCard card(hidden, false, &api); card.show(); QVERIFY(card.findChildren<LinkPreview *>().isEmpty());
        Preferences::instance()->setLinkPreviews(false);
        PostCard visible(post(server.base().resolved(QUrl("/article")).toString()), false, &api); visible.show();
        QTest::qWait(50); QCOMPARE(server.requests.size(), 0);
    }
    void previewsWaitUntilVisibleAndRespectTheDisplayOption() {
        Server server; server.handler = [](Request) { return html("<title>Lazy preview</title>"); };
        NorthApi api(nullptr, server.base()); LinkPreviewService service(&api, server.base());
        QScrollArea scroll; scroll.setWidgetResizable(true); scroll.resize(600, 360);
        auto *canvas = new QWidget; auto *layout = new QVBoxLayout(canvas);
        auto *spacer = new QWidget; spacer->setFixedHeight(900); layout->addWidget(spacer);
        auto *preview = new LinkPreview(server.base().resolved(QUrl("/article")), &api, canvas, &service); layout->addWidget(preview);
        scroll.setWidget(canvas); scroll.show(); QTest::qWait(60); QCOMPARE(server.requests.size(), 0);
        auto *bar = scroll.verticalScrollBar(); QTRY_VERIFY(bar->maximum() > 0); bar->setValue(bar->maximum());
        QTRY_COMPARE(visibleTitle(*preview), "Lazy preview"); QCOMPARE(server.requests.size(), 1);
        Preferences::instance()->setLinkPreviews(false); QVERIFY(preview->isHidden());
        api.clearCaches(); Preferences::instance()->setLinkPreviews(true);
        bar->setValue(bar->maximum()); QTRY_COMPARE(server.requests.size(), 2);
        UrlCatcher catcher; QDesktopServices::setUrlHandler("http", &catcher, "open");
        QTest::mouseClick(preview->findChild<QPushButton *>("previewOpen"), Qt::LeftButton);
        QTRY_COMPARE(catcher.urls.size(), 1); QDesktopServices::unsetUrlHandler("http");
    }
    void presentationOfLinksAndXCards() {
        Server server; server.handler = [](Request request) {
            if (request.url.path() == "/oembed") return Response{200, {{"author_name", "North user"},
                {"html", QString::fromUtf8("<blockquote><p>Qtでつくる、日々のためのアプリ。<br>リンクから投稿をそのまま読めます。</p><a>2026年10月28日</a></blockquote>")}}};
            return html("<meta property='og:title' content='Qt Documentation'><meta name='description' content='Build desktop applications with Qt.'>");
        };
        NorthApi api(nullptr, server.base()); LinkPreviewService service(&api, server.base());
        QWidget window; auto *layout = new QVBoxLayout(&window);
        auto *text = new PostText(QString::fromUtf8("リンクをアプリの中でも読みやすく。\nhttps://doc.qt.io/qt-6/\n長い投稿は設定から3行表示にもできます。")); layout->addWidget(text);
        auto *article = new LinkPreview(server.base().resolved(QUrl("/article")), &api, &window, &service); layout->addWidget(article);
        auto *x = new LinkPreview(QUrl("https://x.com/north_user/status/20"), &api, &window, &service); layout->addWidget(x);
        window.resize(620, 520); window.show(); QTRY_COMPARE(visibleTitle(*x), "North user");
        QTRY_COMPARE(visibleTitle(*article), "Qt Documentation"); capture(window, "link-previews");
        ThemeManager::instance()->setMode(ThemeManager::Dark); capture(window, "link-previews-dark");
    }
};
QTEST_MAIN(PostContentTests)
#include "tst_postcontent.moc"
