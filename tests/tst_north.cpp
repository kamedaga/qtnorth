#include "northapi.h"
#include "post.h"
#include <QJsonArray>
#include <QJsonDocument>
#include <QSignalSpy>
#include <QTcpServer>
#include <QTcpSocket>
#include <QtTest>
#include <memory>

#include "testserver.h"

class NorthTests : public QObject {
    Q_OBJECT
private slots:
    void cacheClearDropsEntriesAndOutstandingResultsCannotRefillThem() {
        Server server; int reads = 0, images = 0;
        server.handler = [&](Request request) {
            if (request.url.path().startsWith("/media/")) {
                ++images; return Response{200, {}, {}, images == 3 ? 100 : 0, "image bytes"};
            }
            ++reads; return Response{200, {{"data", QJsonObject{{"value", 1}}}}, {}, reads == 3 ? 100 : 0};
        };
        NorthApi api(nullptr, server.base()); api.usePersonalToken("test-token");
        int received = 0;
        auto fetch = [&] {
            api.request(this, "GET", "/2/timelines/home", {}, {}, [&](ApiResult r) { QVERIFY(r.ok()); ++received; }, {}, NorthApi::ReadPolicy::Cached);
            api.fetchMedia(this, QUrl("/media/avatar"), [&](MediaResult r) { QVERIFY(r.ok()); ++received; });
        };
        fetch(); QTRY_COMPARE(received, 2); QCOMPARE(reads, 1); QCOMPARE(images, 1);
        fetch(); QTRY_COMPARE(received, 4); QCOMPARE(reads, 1); QCOMPARE(images, 1);
        api.clearCaches(); fetch(); QTRY_COMPARE(received, 6); QCOMPARE(reads, 2); QCOMPARE(images, 2);
        api.clearCaches(); fetch(); QTRY_COMPARE(reads, 3); QTRY_COMPARE(images, 3);
        api.clearCaches(); QTRY_COMPARE(received, 8);
        fetch(); QTRY_COMPARE(received, 10); QCOMPARE(reads, 4); QCOMPARE(images, 4);
    }
    void cachedReadsShareTransportAndKeepFreshReadsExplicit() {
        Server server;
        server.handler = [](Request) { return Response{200, {{"data", QJsonObject{{"value", 1}}}}, {}, 40}; };
        NorthApi api(nullptr, server.base()); api.usePersonalToken("test-token");
        int received = 0; bool destroyedCalled = false;
        auto *discarded = new QObject;
        api.request(discarded, "GET", "/2/timelines/home", {}, {}, [&](ApiResult) { destroyedCalled = true; }, {}, NorthApi::ReadPolicy::Cached);
        for (int i = 0; i < 10; ++i)
            api.request(this, "GET", "/2/timelines/home", {}, {}, [&](ApiResult r) { QVERIFY(r.ok()); ++received; }, {}, NorthApi::ReadPolicy::Cached);
        delete discarded;
        QTRY_COMPARE(received, 10); QCOMPARE(server.requests.size(), 1); QVERIFY(!destroyedCalled);
        auto read = [&](const QString &path, const QUrlQuery &query, NorthApi::ReadPolicy policy) {
            bool done = false;
            api.request(this, "GET", path, query, {}, [&](ApiResult r) { QVERIFY(r.ok()); done = true; }, {}, policy);
            QTRY_VERIFY(done);
        };
        read("/2/timelines/home", {}, NorthApi::ReadPolicy::Cached); QCOMPARE(server.requests.size(), 1);
        read("/2/timelines/home", QUrlQuery("cursor=next"), NorthApi::ReadPolicy::Cached); QCOMPARE(server.requests.size(), 2);
        read("/2/timelines/home", {}, NorthApi::ReadPolicy::Fresh); QCOMPARE(server.requests.size(), 3);
        api.invalidateReads();
        read("/2/timelines/home", {}, NorthApi::ReadPolicy::Cached); QCOMPARE(server.requests.size(), 4);
        // Login validation must never be served from a navigation cache.
        read("/2/users/me", {}, NorthApi::ReadPolicy::Cached);
        read("/2/users/me", {}, NorthApi::ReadPolicy::Cached); QCOMPARE(server.requests.size(), 6);
        bool queued = false;
        api.request(this, "GET", "/2/timelines/home", {}, {}, [&](ApiResult) { queued = true; }, {}, NorthApi::ReadPolicy::Cached);
        api.usePersonalToken("another-account"); QTest::qWait(20); QVERIFY(!queued);
        read("/2/timelines/home", {}, NorthApi::ReadPolicy::Cached); QCOMPARE(server.requests.size(), 7);
        QCOMPARE(server.requests.last().headers.value("authorization"), "Bearer another-account");
    }
    void writesInvalidateReadsAcrossOutstandingRequests() {
        Server server; int value = 1, reads = 0;
        server.handler = [&](Request request) {
            if (request.method == "POST") { value = 2; return Response{200, {{"data", QJsonObject{}}}, {}, 80}; }
            ++reads;
            return Response{200, {{"data", QJsonObject{{"value", value}}}}, {}, reads == 1 ? 150 : 0};
        };
        NorthApi api(nullptr, server.base()); api.usePersonalToken("test-token");
        bool oldDone = false;
        api.request(this, "GET", "/2/timelines/home", {}, {}, [&](ApiResult) { oldDone = true; }, {}, NorthApi::ReadPolicy::Cached);
        QTRY_COMPARE(reads, 1);
        auto *writeContext = new QObject;
        api.request(writeContext, "POST", "/2/tweets", {}, {{"text", "test"}}, [](ApiResult) {});
        QTRY_COMPARE(value, 2); delete writeContext;
        auto read = [&] {
            bool done = false;
            api.request(this, "GET", "/2/timelines/home", {}, {}, [&](ApiResult r) {
                QVERIFY(r.ok()); QCOMPARE(r.data().value("value").toInt(), 2); done = true;
            }, {}, NorthApi::ReadPolicy::Cached);
            QTRY_VERIFY(done);
        };
        read(); QCOMPARE(reads, 2);
        QTRY_VERIFY(oldDone); // Neither this old response nor the completed write may leave an old cache.
        read(); QCOMPARE(reads, 3);
        read(); QCOMPARE(reads, 3);
    }
    void mediaSharesDownloadsAndClearsOnAccountChange() {
        Server server;
        server.handler = [](Request request) {
            return Response{200, {}, {}, 40, request.headers.value("authorization")};
        };
        NorthApi api(nullptr, server.base()); api.usePersonalToken("one");
        int received = 0; bool destroyedCalled = false;
        auto *discarded = new QObject;
        api.fetchMedia(discarded, QUrl("/media/avatar"), [&](MediaResult) { destroyedCalled = true; });
        for (int i = 0; i < 10; ++i) api.fetchMedia(this, QUrl("/media/avatar"), [&](MediaResult result) {
            QVERIFY(result.ok()); QCOMPARE(result.bytes, "Bearer one"); ++received;
        });
        delete discarded;
        QTRY_COMPARE(received, 10); QCOMPARE(server.requests.size(), 1); QVERIFY(!destroyedCalled);
        api.fetchMedia(this, QUrl("/media/avatar"), [&](MediaResult r) { QVERIFY(r.ok()); ++received; });
        QTRY_COMPARE(received, 11); QCOMPARE(server.requests.size(), 1);
        bool stale = false;
        api.fetchMedia(this, QUrl("/media/avatar"), [&](MediaResult) { stale = true; });
        api.usePersonalToken("two"); QTest::qWait(20); QVERIFY(!stale);
        bool done = false;
        api.fetchMedia(this, QUrl("/media/avatar"), [&](MediaResult r) { QCOMPARE(r.bytes, "Bearer two"); done = true; });
        QTRY_VERIFY(done); QCOMPARE(server.requests.size(), 2);
        api.logout(); done = false;
        api.fetchMedia(this, QUrl("/media/avatar"), [&](MediaResult r) { QCOMPARE(r.status, 401); done = true; });
        QTRY_VERIFY(done); QCOMPARE(server.requests.size(), 2);
    }
    void failedReadsAndMediaAreNotCached() {
        Server server;
        server.handler = [](Request) { return Response{500, {{"error", "temporary"}}}; };
        NorthApi api(nullptr, server.base()); api.usePersonalToken("test-token");
        for (int i = 0; i < 2; ++i) {
            bool done = false;
            api.request(this, "GET", "/2/timelines/home", {}, {}, [&](ApiResult r) { QVERIFY(!r.ok()); done = true; }, {}, NorthApi::ReadPolicy::Cached);
            QTRY_VERIFY(done);
            done = false;
            api.fetchMedia(this, QUrl("/media/avatar"), [&](MediaResult r) { QVERIFY(!r.ok()); done = true; });
            QTRY_VERIFY(done);
        }
        QCOMPARE(server.requests.size(), 4);
    }
    void textAndVisibility() {
        QCOMPARE(North::textWeight("hello"), 5);
        QCOMPARE(North::textWeight(QString::fromUtf8("日本語")), 6);
        QCOMPARE(North::textWeight(QString::fromUtf8("ｶﾀｶﾅ")), 4);
        QCOMPARE(North::textWeight(QString::fromUtf8("A🌱")), 3);
        QJsonObject p{{"id", "2115083392503513450"}, {"text", "hidden text"}, {"hiddenReason", "muted"}};
        QVERIFY(!North::plainPostText(p).contains("hidden text"));
        QJsonObject original{{"id", "2115083392503513450"}, {"text", "original"}, {"author", QJsonObject{{"handle", "test"}}}};
        QJsonObject wrapper{{"id", "other"}, {"retweetOf", original}};
        QCOMPARE(North::displayedPost(wrapper), original);
        QCOMPARE(North::postUrl(wrapper), "https://north.rip/test/status/2115083392503513450");
    }
    void headersCursorAndStringIds() {
        Server server;
        server.handler = [](Request) { return Response{200, {{"data", QJsonObject{{"items", QJsonArray{QJsonObject{{"id", "2115083392503513450"}}}}, {"nextCursor", "cursor+/=second"}}}}}; };
        NorthApi api(nullptr, server.base());
        api.usePersonalToken("test-token");
        QUrlQuery query;
        query.addQueryItem("q", QString::fromUtf8("#日本語 & Qt"));
        query.addQueryItem("cursor", "signed+/=cursor");
        ApiResult result;
        bool done = false;
        api.request(this, "GET", "/2/tweets/search", query, {}, [&](ApiResult r) { result = r; done = true; });
        QTRY_VERIFY(done);
        QVERIFY(result.ok());
        QCOMPARE(result.data().value("items").toArray()[0].toObject().value("id").toString(), "2115083392503513450");
        QCOMPARE(server.requests.size(), 1);
        QCOMPARE(server.requests[0].headers.value("authorization"), "Bearer test-token");
        QVERIFY(!server.requests[0].headers.contains("cookie"));
        QUrlQuery got(server.requests[0].url);
        QCOMPARE(got.queryItemValue("q", QUrl::FullyDecoded), QString::fromUtf8("#日本語 & Qt"));
        QCOMPARE(got.queryItemValue("cursor", QUrl::FullyDecoded), "signed+/=cursor");
    }
    void mutationsKeepKeyAndDoNotAutoRetry() {
        Server server;
        server.handler = [](Request) { return Response{500, {{"errors", QJsonArray{QJsonObject{{"code", 131}, {"message", "uncertain write"}}}}}}; };
        NorthApi api(nullptr, server.base());
        api.usePersonalToken("test-token");
        const auto key = NorthApi::newIdempotencyKey();
        for (int i = 0; i < 2; ++i) {
            bool done = false;
            api.request(this, "POST", "/2/tweets", {}, {{"text", QString::fromUtf8("投稿")}}, [&](ApiResult r) {
                QVERIFY(!r.ok()); QCOMPARE(r.error, "uncertain write"); done = true;
            }, key);
            QTRY_VERIFY(done);
        }
        QCOMPARE(server.requests.size(), 2);
        QCOMPARE(server.requests[0].headers.value("idempotency-key"), key);
        QCOMPARE(server.requests[1].headers.value("idempotency-key"), key);
        QCOMPARE(server.requests[0].body.value("text").toString(), QString::fromUtf8("投稿"));
        QVERIFY(api.authenticated());
    }
    void rateLimitAndAuthentication() {
        Server server;
        server.handler = [](Request) { return Response{429, {{"errors", QJsonArray{QJsonObject{{"message", "rate limit"}}}}}, {{"Retry-After", "1"}}}; };
        NorthApi api(nullptr, server.base());
        api.usePersonalToken("test-token");
        QSignalSpy limited(&api, &NorthApi::rateLimited);
        bool done = false;
        api.request(this, "GET", "/2/timelines/home", {}, {}, [&](ApiResult r) { QCOMPARE(r.retryAfter, 1); done = true; });
        QTRY_VERIFY(done);
        QCOMPARE(limited.count(), 1);
        done = false;
        api.request(this, "GET", "/2/timelines/home", {}, {}, [&](ApiResult r) { QCOMPARE(r.status, 429); done = true; });
        QTRY_VERIFY(done);
        QCOMPARE(server.requests.size(), 1);
        QTest::qWait(1100);
        server.handler = [](Request) { return Response{401, {{"errors", QJsonArray{QJsonObject{{"code", 32}, {"message", "expired"}}}}}}; };
        QSignalSpy expired(&api, &NorthApi::sessionExpired);
        done = false;
        api.request(this, "GET", "/2/users/me", {}, {}, [&](ApiResult r) { QCOMPARE(r.status, 401); done = true; });
        QTRY_VERIFY(done);
        QCOMPARE(expired.count(), 1);
        QVERIFY(!api.authenticated());
    }
    void logoutAndContextLifetime() {
        Server server;
        server.handler = [](Request) { return Response{200, {{"data", QJsonObject{{"ok", true}}}}, {}, 150}; };
        NorthApi api(nullptr, server.base());
        api.usePersonalToken("old-token");
        bool called = false;
        api.request(this, "GET", "/2/timelines/home", {}, {}, [&](ApiResult) { called = true; });
        QTRY_COMPARE(server.requests.size(), 1);
        api.logout();
        api.usePersonalToken("new-token");
        QTest::qWait(200);
        QVERIFY(!called);
        auto *context = new QObject;
        api.request(context, "GET", "/2/timelines/home", {}, {}, [&](ApiResult) { called = true; });
        delete context;
        QTest::qWait(200);
        QVERIFY(!called);
    }
    void redirectsAndMalformedResponses() {
        Server server;
        NorthApi api(nullptr, server.base());
        api.usePersonalToken("test-token");
        server.handler = [port = server.serverPort()](Request) { return Response{302, {}, {{"Location", "http://127.0.0.1:" + QByteArray::number(port) + "/capture"}}}; };
        bool done = false;
        api.request(this, "GET", "/2/users/me", {}, {}, [&](ApiResult r) { QVERIFY(!r.ok()); QCOMPARE(r.status, 302); done = true; });
        QTRY_VERIFY(done);
        QCOMPARE(server.requests.size(), 1);
        server.handler = [](Request) { return Response{200, {}, {}, 0, "<html>not JSON</html>"}; };
        done = false;
        api.request(this, "GET", "/2/users/me", {}, {}, [&](ApiResult r) { QVERIFY(!r.ok()); done = true; });
        QTRY_VERIFY(done);
        done = false;
        api.request(this, "GET", "/api/internal", {}, {}, [&](ApiResult r) { QCOMPARE(r.status, 400); done = true; });
        QTRY_VERIFY(done);
        QCOMPARE(server.requests.size(), 2);
    }
    void deviceAndRotatingRefresh() {
        Server server;
        int polls = 0, refreshes = 0;
        server.handler = [&](Request req) {
            if (req.url.path() == "/oauth/device/code") {
                return Response{200, {{"device_code", "device-secret"}, {"user_code", "ABCD-EFGH"},
                    {"verification_uri", "https://north.rip/device"}, {"interval", 5}, {"expires_in", 600}}};
            }
            if (req.body.value("grant_type") == "refresh_token") {
                ++refreshes;
                return Response{200, {{"access_token", "rotated-access"}, {"refresh_token", "rotated-refresh"},
                    {"token_type", "Bearer"}, {"expires_in", 3600}, {"scope", "users.read posts.read"}}, {}, 150};
            }
            if (req.url.path() == "/oauth/token") {
                if (++polls == 1) return Response{400, {{"error", "authorization_pending"}}};
                return Response{200, {{"access_token", "first-access"}, {"refresh_token", "first-refresh"},
                    {"token_type", "Bearer"}, {"expires_in", 60}, {"scope", "users.read posts.read"}}};
            }
            return Response{200, {{"data", QJsonObject{{"handle", "test"}}}}};
        };
        NorthApi api(nullptr, server.base());
        QSignalSpy authorization(&api, &NorthApi::deviceAuthorization);
        QSignalSpy loggedIn(&api, &NorthApi::loginSucceeded);
        api.startDeviceLogin("registered-client", false);
        QTRY_COMPARE(authorization.count(), 1);
        QCOMPARE(authorization[0][0].toString(), "ABCD-EFGH");
        QCOMPARE(server.requests[0].body.value("scope").toString(), "users.read posts.read notifications.read");
        QCOMPARE(server.requests[0].body.value("client_id").toString(), "registered-client");
        QTRY_COMPARE_WITH_TIMEOUT(loggedIn.count(), 1, 12000);
        QVERIFY(api.authenticated());
        QVERIFY(api.allowsScope("posts.read"));
        QVERIFY(!api.allowsScope("posts.write"));
        QTRY_COMPARE_WITH_TIMEOUT(refreshes, 1, 2500);
        bool done = false;
        api.request(this, "GET", "/2/users/me", {}, {}, [&](ApiResult r) { QVERIFY(r.ok()); done = true; });
        QTRY_VERIFY(done);
        QCOMPARE(server.requests.last().headers.value("authorization"), "Bearer rotated-access");
        const auto refreshRequest = server.requests[server.requests.size() - 2];
        QCOMPARE(refreshRequest.body.value("refresh_token").toString(), "first-refresh");
        QVERIFY(!refreshRequest.headers.contains("authorization"));
        QCOMPARE(refreshes, 1);
    }
    void privateMediaAndRedirects() {
        Server server, other;
        const QByteArray bytes("\0binary-image", 13);
        server.handler = [&](Request req) {
            if (req.url.path() == "/media/redirect") return Response{302, {}, {{"Location", other.base().toEncoded() + "/capture"}}};
            return Response{200, {}, {{"Content-Type", "image/png"}}, 0, bytes};
        };
        NorthApi api(nullptr, server.base());
        api.usePersonalToken("private-token");
        bool done = false;
        api.fetchMedia(this, QUrl("/media/photo.png"), [&](MediaResult r) { QVERIFY(r.ok()); QCOMPARE(r.bytes, bytes); done = true; });
        QTRY_VERIFY(done);
        QCOMPARE(server.requests.last().headers.value("authorization"), "Bearer private-token");
        QVERIFY(!server.requests.last().headers.contains("cookie"));
        done = false;
        api.fetchMedia(this, QUrl("/assets/public.png"), [&](MediaResult r) { QVERIFY(r.ok()); done = true; });
        QTRY_VERIFY(done);
        QVERIFY(!server.requests.last().headers.contains("authorization"));
        done = false;
        api.fetchMedia(this, QUrl("/media/redirect"), [&](MediaResult r) { QVERIFY(!r.ok()); done = true; });
        QTRY_VERIFY(done);
        QCOMPARE(other.requests.size(), 0);
        done = false;
        api.fetchMedia(this, other.base().resolved(QUrl("/plain-http")), [&](MediaResult r) { QVERIFY(!r.ok()); done = true; });
        QTRY_VERIFY(done);
        QCOMPARE(other.requests.size(), 0);
    }
    void mediaLimitsAndLogout() {
        Server server;
        server.handler = [](Request) { return Response{200, {}, {{"Content-Length", "33554433"}}, 0, "x"}; };
        NorthApi api(nullptr, server.base());
        api.usePersonalToken("test-token");
        bool done = false;
        api.fetchMedia(this, QUrl("/media/huge"), [&](MediaResult r) { QVERIFY(!r.ok()); QVERIFY(r.bytes.isEmpty()); done = true; });
        QTRY_VERIFY(done);
        server.handler = [](Request) { return Response{200, {}, {}, 100, "bytes"}; };
        bool called = false;
        api.fetchMedia(this, QUrl("/media/slow"), [&](MediaResult) { called = true; });
        QTRY_COMPARE(server.requests.size(), 2);
        api.logout();
        QTest::qWait(150);
        QVERIFY(!called);
    }
    void messagePermissionsAreOptIn() {
        Server server;
        server.handler = [](Request) { return Response{200, {{"device_code", "code"}, {"user_code", "TEST"},
            {"verification_uri", "https://north.rip/device"}, {"interval", 5}, {"expires_in", 600}}}; };
        NorthApi api(nullptr, server.base());
        QSignalSpy authorization(&api, &NorthApi::deviceAuthorization);
        api.startDeviceLogin("client", false, true);
        QTRY_COMPARE(authorization.count(), 1);
        const auto scope = server.requests.first().body.value("scope").toString();
        QVERIFY(scope.contains("notifications.read")); QVERIFY(!scope.contains("notifications.write"));
        QVERIFY(scope.contains("dm.read")); QVERIFY(scope.contains("dm.write")); QVERIFY(scope.contains("dm.conversations.write"));
        QVERIFY(!scope.contains("dm.receipts.write"));
    }
    void notificationWriteScopeRequiresWriteConsent() {
        Server server;
        server.handler = [](Request) { return Response{200, {{"device_code", "code"}, {"user_code", "TEST"},
            {"verification_uri", "https://north.rip/device"}, {"interval", 5}, {"expires_in", 600}}}; };
        NorthApi api(nullptr, server.base());
        QSignalSpy authorization(&api, &NorthApi::deviceAuthorization);
        api.startDeviceLogin("client", true);
        QTRY_COMPARE(authorization.count(), 1);
        const auto scope = server.requests.first().body.value("scope").toString();
        QVERIFY(scope.contains("notifications.read")); QVERIFY(scope.contains("notifications.write"));
        QVERIFY(!scope.contains("dm.read"));
        api.cancelDeviceLogin();
    }
    void sessionRoundTripAndOriginBinding() {
        Server server;
        NorthApi source(nullptr, server.base()); source.usePersonalToken("saved-token");
        const auto saved = source.sessionData();
        NorthApi restored(nullptr, server.base());
        QVERIFY(restored.restoreSession(saved));
        QCOMPARE(restored.sessionData(), saved);
        auto wrongOrigin = saved; wrongOrigin.insert("origin", "https://other.invalid");
        QVERIFY(!restored.restoreSession(wrongOrigin));
        auto marker = saved; marker.insert("kind", "refresh_pending");
        QVERIFY(!restored.restoreSession(marker));
        restored.logout();
        QVERIFY(restored.sessionData().isEmpty());
    }
    void restoredOAuthPersistsMarkerBeforeRotating() {
        Server server;
        int refreshes = 0;
        server.handler = [&](Request request) {
            if (request.url.path() == "/oauth/token") {
                ++refreshes;
                return Response{200, {{"access_token", "new-access"}, {"refresh_token", "new-refresh"},
                    {"token_type", "Bearer"}, {"expires_in", 3600}, {"scope", "users.read posts.read"}}};
            }
            return Response{200, {{"data", QJsonObject{{"handle", "me"}}}}};
        };
        NorthApi api(nullptr, server.base());
        const QJsonObject saved{{"version", 1}, {"origin", server.base().toString()}, {"kind", "oauth"},
            {"client_id", "client"}, {"access_token", "old-access"}, {"refresh_token", "old-refresh"},
            {"expires_at", QDateTime::currentDateTimeUtc().addSecs(-120).toString(Qt::ISODateWithMs)},
            {"scope", "users.read posts.read"}};
        std::function<void(bool)> ready;
        QJsonObject persisted;
        api.setRefreshGuard([&](const QJsonObject &marker, std::function<void(bool)> next) { persisted = marker; ready = next; });
        connect(&api, &NorthApi::sessionChanged, &api, [&] { persisted = api.sessionData(); });
        QVERIFY(api.restoreSession(saved));
        int completed = 0;
        for (int i = 0; i < 2; ++i) api.request(this, "GET", "/2/users/me", {}, {}, [&](ApiResult result) { QVERIFY(result.ok()); ++completed; });
        QVERIFY(bool(ready));
        QCOMPARE(persisted.value("kind").toString(), "refresh_pending");
        QVERIFY(!persisted.contains("refresh_token"));
        QTest::qWait(30);
        QCOMPARE(server.requests.size(), 0);
        ready(true);
        QTRY_COMPARE(completed, 2);
        QCOMPARE(refreshes, 1);
        QCOMPARE(server.requests.first().body.value("refresh_token").toString(), "old-refresh");
        QCOMPARE(persisted.value("refresh_token").toString(), "new-refresh");
        QCOMPARE(server.requests.last().headers.value("authorization"), "Bearer new-access");
        api.logout();
        QVERIFY(api.restoreSession(saved));
        bool failed = false;
        api.request(this, "GET", "/2/users/me", {}, {}, [&](ApiResult r) { QVERIFY(!r.ok()); failed = true; });
        ready(false);
        QTRY_VERIFY(failed);
        QVERIFY(!api.authenticated());
        QCOMPARE(refreshes, 1);
    }
};
QTEST_GUILESS_MAIN(NorthTests)
#include "tst_north.moc"
