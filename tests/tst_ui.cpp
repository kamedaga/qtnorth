#include "mainwindow.h"
#include "memorystore.h"
#include "messagespage.h"
#include "postcard.h"
#include "remoteimage.h"
#include "profilepage.h"
#include "theme.h"
#include "preferences.h"
#include "icons.h"
#include "windowchrome.h"
#include "testserver.h"
#include <QApplication>
#include <QAction>
#include <QBuffer>
#include <QComboBox>
#include <QCheckBox>
#include <QDialog>
#include <QDir>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMenuBar>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QScrollArea>
#include <QScrollBar>
#include <QScopeGuard>
#include <QSettings>
#include <QStackedWidget>
#include <QTabWidget>
#include <QTabBar>
#include <QToolButton>
#include <QTemporaryDir>
#include <QVBoxLayout>
#include <QtTest>
#include <algorithm>

namespace {
QByteArray png() {
    QImage image(160, 100, QImage::Format_RGB32);
    image.fill(QColor("#94b5c8"));
    QByteArray bytes; QBuffer buffer(&bytes); buffer.open(QIODevice::WriteOnly); image.save(&buffer, "PNG");
    return bytes;
}
QJsonObject post(const QString &id = "2115083392503513450") {
    return {{"id", id}, {"text", "A post with an image."}, {"createdAt", "2026-10-27T12:00:00Z"},
        {"author", QJsonObject{{"id", "author"}, {"handle", "north_user"}, {"name", "North user"}}},
        {"media", QJsonArray{QJsonObject{{"id", "photo"}, {"kind", "PHOTO"}, {"url", "/media/photo.png"}, {"width", 160}, {"height", 100}}}}};
}
QPushButton *findButton(QWidget &widget, const QString &text) {
    for (auto *button : widget.findChildren<QPushButton *>()) if (button->text() == text) return button;
    return nullptr;
}
bool containsText(QWidget &widget, const QString &text) {
    for (auto *label : widget.findChildren<QLabel *>()) if (label->text() == text && label->isVisible()) return true;
    return false;
}
void submitToken(QWidget &window) {
    window.findChild<QTabWidget *>("loginMethods")->setCurrentIndex(1);
    window.findChild<QLineEdit *>("personalToken")->setText("test-token");
    QTest::mouseClick(window.findChild<QPushButton *>("tokenLogin"), Qt::LeftButton);
}
void capture(QWidget &widget, const QString &name) {
    const auto dir = qEnvironmentVariable("QTNORTH_CAPTURE_DIR");
    if (!dir.isEmpty()) { QTest::qWait(50); QDir().mkpath(dir); widget.grab().save(dir + "/" + name + ".png"); }
}
}

class UiTests : public QObject {
    Q_OBJECT
    QTemporaryDir m_settings;
private slots:
    void initTestCase() {
        QCoreApplication::setOrganizationName("qtNorth-tests");
        QCoreApplication::setApplicationName("ui");
        QSettings::setDefaultFormat(QSettings::IniFormat);
        QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, m_settings.path());
    }
    void imageDecodeViewerAndLogout() {
        Server server;
        server.handler = [](Request) { return Response{200, {}, {{"Content-Type", "image/png"}}, 0, png()}; };
        NorthApi api(nullptr, server.base()); api.usePersonalToken("token");
        RemoteImage image(&api, QUrl("/media/photo.png")); image.resize(400, 240); image.show();
        QTRY_VERIFY(image.hasImage());
        QTest::mouseClick(&image, Qt::LeftButton);
        QTRY_VERIFY(image.findChild<QDialog *>("imageViewer"));
        auto *viewer = image.findChild<QDialog *>("imageViewer");
        QVERIFY(viewer->isVisible());
        api.logout();
        QVERIFY(!image.hasImage());
        QTRY_VERIFY(!image.findChild<QDialog *>("imageViewer"));
        QCOMPARE(server.requests.first().headers.value("authorization"), "Bearer token");
    }
    void sensitiveMediaDoesNotLoadBeforeReveal() {
        Server server;
        server.handler = [](Request) { return Response{200, {}, {{"Content-Type", "image/png"}}, 0, png()}; };
        NorthApi api(nullptr, server.base()); api.usePersonalToken("token");
        auto p = post(); auto media = p.value("media").toArray();
        auto item = media[0].toObject(); item.insert("sensitive", true); media[0] = item; p.insert("media", media);
        PostCard card(p, true, &api); card.resize(500, 400); card.show();
        QVERIFY(QTest::qWaitForWindowExposed(&card));
        QCOMPARE(server.requests.size(), 0);
        QVERIFY(card.findChildren<RemoteImage *>().isEmpty());
        auto *reveal = card.findChild<QPushButton *>("sensitiveMedia"); QVERIFY(reveal);
        QTest::mouseClick(reveal, Qt::LeftButton);
        QTRY_COMPARE(server.requests.size(), 1);
        QTRY_VERIFY(card.findChild<RemoteImage *>()->hasImage());
    }
    void startsWithFullLoginAndNoProtectedRequests() {
        Server server;
        MemoryStore store;
        MainWindow window(nullptr, server.base(), &store); window.show();
        auto *shell = window.findChild<QStackedWidget *>("appPages");
        QCOMPARE(shell->currentWidget()->objectName(), "loginPage");
        QVERIFY(window.menuBar()->isVisible());
        for (auto *action : window.menuBar()->actions()) QVERIFY(!action->isVisible());
        QTRY_VERIFY(window.findChild<QTabWidget *>("loginMethods")->isEnabled());
        QTest::qWait(30);
        QCOMPARE(server.requests.size(), 0);
        capture(window, "login");
    }
    void oauthStartsWithoutClientSetup_data() {
        QTest::addColumn<QString>("previousClientId");
        QTest::newRow("fresh-install") << QString{};
        QTest::newRow("previous-client-setting") << QString{"previous-client"};
    }
    void oauthStartsWithoutClientSetup() {
        QFETCH(QString, previousClientId);
        const auto original = QSettings().value("oauth/clientId");
        const auto restore = qScopeGuard([original] {
            if (original.isValid()) QSettings().setValue("oauth/clientId", original);
            else QSettings().remove("oauth/clientId");
        });
        QSettings().setValue("oauth/clientId", previousClientId);
        Server server;
        // Stop before browser authorization so this regression test stays local.
        server.handler = [](Request) { return Response{400, {{"error", "access_denied"}}}; };
        MemoryStore store;
        MainWindow window(nullptr, server.base(), &store); window.show();
        auto *methods = window.findChild<QTabWidget *>("loginMethods");
        QTRY_VERIFY(methods->isEnabled());
        auto *authorize = findButton(window, "ブラウザーでログイン"); QVERIFY(authorize);
        QTest::mouseClick(authorize, Qt::LeftButton);
        QTRY_COMPARE(server.requests.size(), 1);
        QCOMPARE(server.requests.first().url.path(), "/oauth/device/code");
        QCOMPARE(server.requests.first().body.value("client_id").toString(), "cmvv27dcd00dlgkrdx8q6vnxi");
        QVERIFY(server.requests.first().body.value("scope").toString().contains("polls.vote"));
        QVERIFY(server.requests.first().body.value("scope").toString().contains("follows.write"));
        QVERIFY(!server.requests.first().headers.contains("authorization"));
        QTRY_VERIFY(methods->isEnabled());
        QCOMPARE(window.findChild<QStackedWidget *>("appPages")->currentWidget()->objectName(), "loginPage");
    }
    void pollComposerValidatesChoicesAndKeysTheWholePayload() {
        Server server; QList<Request> posts;
        server.handler = [&](Request r) {
            if (r.url.path() == "/2/users/me") return Response{200, {{"data", QJsonObject{{"id", "me"}, {"handle", "me"}}}}};
            if (r.url.path() == "/2/notifications/unread_count") return Response{200, {{"data", QJsonObject{{"count", 0}}}}};
            if (r.method == "POST" && r.url.path() == "/2/tweets") {
                posts.append(r); return posts.size() < 3 ? Response{500, {{"error", "temporary"}}, {}, 40} : Response{201, {{"data", QJsonObject{{"id", "created"}}}}};
            }
            return Response{200, {{"data", QJsonObject{{"items", QJsonArray{}}}}}};
        };
        MemoryStore store; MainWindow window(nullptr, server.base(), &store); window.show();
        QTRY_VERIFY(window.findChild<QTabWidget *>("loginMethods")->isEnabled()); submitToken(window);
        QTRY_VERIFY(window.findChild<QStackedWidget *>("appPages")->currentWidget()->objectName() != "loginPage");
        QAction *compose = nullptr; for (auto *a : window.findChildren<QAction *>()) if (a->text() == "新規ポスト") compose = a;
        QVERIFY(compose); compose->trigger();
        auto *dialog = window.findChild<QDialog *>("composeDialog"); QVERIFY(dialog);
        auto *editor = dialog->findChild<QPlainTextEdit *>("composeText"); auto *enabled = dialog->findChild<QCheckBox *>("composePoll");
        auto *first = dialog->findChild<QLineEdit *>("pollOption1Input"); auto *second = dialog->findChild<QLineEdit *>("pollOption2Input");
        auto *duration = dialog->findChild<QComboBox *>("pollDuration"); auto *send = dialog->findChild<QPushButton *>("composeSend");
        enabled->setChecked(true); first->setText("A"); second->setText("a"); editor->setPlainText("どちらが好き？"); QVERIFY(!send->isEnabled());
        first->setText("寿司"); second->setText("天ぷら"); duration->setCurrentIndex(duration->findData(5)); QVERIFY(send->isEnabled());
        QTest::mouseClick(send, Qt::LeftButton); QVERIFY(!enabled->isEnabled());
        QTRY_VERIFY(posts.size() == 1 && send->isEnabled());
        QCOMPARE(posts[0].body.value("poll").toObject().value("durationMinutes").toInt(), 5);
        QCOMPARE(posts[0].body.value("poll").toObject().value("options").toArray(), QJsonArray({"寿司", "天ぷら"}));
        QTest::mouseClick(send, Qt::LeftButton); QTRY_VERIFY(posts.size() == 2 && send->isEnabled());
        QCOMPARE(posts[0].headers.value("idempotency-key"), posts[1].headers.value("idempotency-key"));
        duration->setCurrentIndex(duration->findData(30)); QTest::mouseClick(send, Qt::LeftButton);
        QTRY_COMPARE(posts.size(), 3);
        QVERIFY(posts[2].headers.value("idempotency-key") != posts[1].headers.value("idempotency-key"));
        QTRY_VERIFY(!window.findChild<QDialog *>("composeDialog"));
    }
    void disclaimerIsReadableBeforeLogin() {
        Server server; MemoryStore store;
        MainWindow window(nullptr, server.base(), &store); window.show();
        QTest::mouseClick(window.findChild<QPushButton *>("loginAbout"), Qt::LeftButton);
        auto *dialog = window.findChild<QDialog *>("aboutDialog"); QVERIFY(dialog);
        auto *button = dialog->findChild<QPushButton *>("aboutDisclaimer"); QVERIFY(button); QVERIFY(button->isEnabled());
        QTest::mouseClick(button, Qt::LeftButton);
        auto *page = dialog->findChild<QStackedWidget *>("aboutPages")->currentWidget();
        QVERIFY(containsText(*page, "免責事項"));
        bool found = false; for (auto *text : page->findChildren<QLabel *>()) if (text->text().contains("ご自身の判断と責任")) found = true;
        QVERIFY(found); QCOMPARE(server.requests.size(), 0);
        QTest::mouseClick(dialog->findChild<QPushButton *>("aboutClose"), Qt::LeftButton);
    }
    void storedLoginAutomaticallyResumesAndLogoutErases() {
        Server server;
        server.handler = [](Request request) {
            if (request.url.path() == "/2/users/me") return Response{200, {{"data", QJsonObject{{"id", "me"}, {"handle", "me"}}}}};
            if (request.url.path() == "/2/notifications/unread_count") return Response{200, {{"data", QJsonObject{{"count", 0}}}}};
            return Response{200, {{"data", QJsonObject{{"items", QJsonArray{}}}}}};
        };
        MemoryStore store;
        {
            MainWindow first(nullptr, server.base(), &store); first.show();
            QTRY_VERIFY(first.findChild<QTabWidget *>("loginMethods")->isEnabled());
            submitToken(first);
            QTRY_VERIFY(first.findChild<QStackedWidget *>("appPages")->currentWidget()->objectName() != "loginPage");
            QTRY_VERIFY(!store.busy());
            // Complete this window's initial reads before reusing the server log.
            QTRY_VERIFY(std::any_of(server.requests.begin(), server.requests.end(), [](const Request &r) { return r.url.path() == "/2/timelines/home"; }));
            QTRY_VERIFY(std::any_of(server.requests.begin(), server.requests.end(), [](const Request &r) { return r.url.path() == "/2/notifications/unread_count"; }));
            QTRY_VERIFY(server.findChildren<QTcpSocket *>().isEmpty());
            QVERIFY(!store.data.isEmpty());
            first.close();
            QVERIFY(!store.data.isEmpty());
        }
        server.requests.clear();
        MainWindow second(nullptr, server.base(), &store); second.show();
        auto *shell = second.findChild<QStackedWidget *>("appPages");
        QCOMPARE(shell->currentWidget()->objectName(), "loginPage");
        QTRY_VERIFY(shell->currentWidget()->objectName() != "loginPage");
        QCOMPARE(server.requests.first().url.path(), "/2/users/me");
        QCOMPARE(server.requests.first().headers.value("authorization"), "Bearer test-token");
        auto *logout = findButton(second, QString::fromUtf8("ログアウト")); QVERIFY(logout);
        QTest::mouseClick(logout, Qt::LeftButton);
        QCOMPARE(shell->currentWidget()->objectName(), "loginPage");
        QTRY_VERIFY(!store.busy());
        QVERIFY(store.data.isEmpty());
        QVERIFY(second.findChild<QLineEdit *>("personalToken")->text().isEmpty());
    }
    void savedLoginErrorsPreserveOnlyRetryableCredentials() {
        for (const int status : {500, 401}) {
            Server server;
            server.handler = [status](Request) { return Response{status, {{"errors", QJsonArray{QJsonObject{{"message", "login failed"}}}}}}; };
            NorthApi api(nullptr, server.base()); api.usePersonalToken("test-token");
            MemoryStore store; store.data = QJsonDocument(api.sessionData()).toJson(QJsonDocument::Compact);
            MainWindow window(nullptr, server.base(), &store); window.show();
            QTRY_VERIFY(!server.requests.isEmpty());
            QTRY_VERIFY(window.findChild<QTabWidget *>("loginMethods")->isEnabled());
            QTRY_VERIFY(!store.busy());
            QCOMPARE(window.findChild<QStackedWidget *>("appPages")->currentWidget()->objectName(), "loginPage");
            QCOMPARE(store.data.isEmpty(), status == 401);
            QCOMPARE(window.findChild<QPushButton *>("resumeLogin")->isVisible(), status != 401);
            QCOMPARE(server.requests.size(), 1);
        }
    }
    void failedSaveDoesNotEnterMainWindow() {
        Server server;
        server.handler = [](Request) { return Response{200, {{"data", QJsonObject{{"id", "me"}, {"handle", "me"}}}}}; };
        MemoryStore store; store.failWrites = true;
        MainWindow window(nullptr, server.base(), &store); window.show();
        QTRY_VERIFY(window.findChild<QTabWidget *>("loginMethods")->isEnabled());
        submitToken(window);
        QTRY_VERIFY(!server.requests.isEmpty());
        QTRY_VERIFY(window.findChild<QTabWidget *>("loginMethods")->isEnabled());
        QCOMPARE(window.findChild<QStackedWidget *>("appPages")->currentWidget()->objectName(), "loginPage");
        QVERIFY(!window.findChild<NorthApi *>()->authenticated());
        QVERIFY(store.data.isEmpty());
        QCOMPARE(server.requests.size(), 1);
    }
    void expiredStoredOAuthRotatesAndKeepsNewTokensAfterNetworkFailure() {
        Server server;
        int refreshes = 0;
        bool failMe = true;
        server.handler = [&](Request request) {
            if (request.url.path() == "/oauth/token") {
                ++refreshes;
                return Response{200, {{"access_token", "rotated-access"}, {"refresh_token", "rotated-refresh"},
                    {"token_type", "Bearer"}, {"expires_in", 3600}, {"scope", "users.read posts.read"}}};
            }
            if (request.url.path() == "/2/users/me") {
                if (failMe) return Response{500, {{"errors", QJsonArray{QJsonObject{{"message", "temporary error"}}}}}};
                return Response{200, {{"data", QJsonObject{{"id", "me"}, {"handle", "me"}}}}};
            }
            return Response{200, {{"data", QJsonObject{{"items", QJsonArray{}}}}}};
        };
        MemoryStore store;
        store.data = QJsonDocument(QJsonObject{{"version", 1}, {"origin", server.base().toString()}, {"kind", "oauth"},
            {"client_id", "client"}, {"access_token", "old-access"}, {"refresh_token", "old-refresh"},
            {"expires_at", QDateTime::currentDateTimeUtc().addSecs(-120).toString(Qt::ISODateWithMs)},
            {"scope", "users.read posts.read"}}).toJson(QJsonDocument::Compact);
        MainWindow window(nullptr, server.base(), &store); window.show();
        QTRY_COMPARE(refreshes, 1);
        QTRY_VERIFY(window.findChild<QTabWidget *>("loginMethods")->isEnabled());
        QTRY_VERIFY(!store.busy());
        QCOMPARE(store.writes.size(), 2);
        QCOMPARE(QJsonDocument::fromJson(store.writes[0]).object().value("kind").toString(), "refresh_pending");
        QCOMPARE(QJsonDocument::fromJson(store.data).object().value("refresh_token").toString(), "rotated-refresh");
        failMe = false;
        QTest::mouseClick(window.findChild<QPushButton *>("resumeLogin"), Qt::LeftButton);
        QTRY_VERIFY(window.findChild<QStackedWidget *>("appPages")->currentWidget()->objectName() != "loginPage");
        QCOMPARE(refreshes, 1);
        QVERIFY(std::any_of(server.requests.begin(), server.requests.end(), [](const Request &request) {
            return request.url.path() == "/2/users/me" && request.headers.value("authorization") == "Bearer rotated-access";
        }));
    }
    void draggingTextDoesNotNavigate() {
        auto p = post(); p.remove("media");
        PostCard card(p, false); card.resize(500, 220); card.show();
        QVERIFY(QTest::qWaitForWindowExposed(&card));
        auto *body = card.findChild<QLabel *>("postText");
        QSignalSpy selected(&card, &PostCard::selected);
        QTest::mousePress(body, Qt::LeftButton, {}, QPoint(2, body->height() / 2));
        QTest::mouseMove(body, QPoint(150, body->height() / 2));
        QTest::mouseRelease(body, Qt::LeftButton, {}, QPoint(150, body->height() / 2));
        QTest::qWait(10);
        QCOMPARE(selected.count(), 0);
    }
    void profileNavigationImagesPaginationAndRelationships() {
        Server server;
        int pagesLoaded = 0;
        const QJsonObject me{{"id", "me"}, {"handle", "me"}, {"name", "Me"}};
        auto profilePost = [](const QString &id) {
            auto item = post(id); item.remove("media"); return item;
        };
        server.handler = [&](Request request) {
            const auto path = request.url.path();
            if (path.startsWith("/media/")) return Response{200, {}, {{"Content-Type", "image/png"}}, 0, png()};
            if (path == "/2/users/me") return Response{200, {{"data", me}}};
            if (path == "/2/timelines/home") {
                auto item = post(); auto author = item.value("author").toObject(); author.insert("avatarUrl", "/media/avatar.png"); item.insert("author", author);
                return Response{200, {{"data", QJsonObject{{"items", QJsonArray{item}}}}}};
            }
            if (path == "/2/users/north_user" || path == "/2/users/other") return Response{200, {{"data", QJsonObject{
                {"id", path.endsWith("other") ? "other" : "author"}, {"handle", path.endsWith("other") ? "other" : "north_user"},
                {"name", path.endsWith("other") ? "Haru" : "North user"}, {"avatarUrl", "/media/avatar.png"}, {"headerUrl", "/media/header.png"},
                {"bio", QString::fromUtf8("日々のことと、つくったもの。\nQtでデスクトップアプリを開発しています。")}, {"location", "Tokyo"},
                {"createdAt", "2025-05-01T00:00:00Z"}, {"followingCount", 128}, {"followerCount", 256}, {"tweetCount", 31},
                {"following", true}, {"followedBy", true}}}}};
            if (path.endsWith("/tweets")) {
                ++pagesLoaded; QJsonArray items;
                const bool more = QUrlQuery(request.url).hasQueryItem("cursor");
                if (more) items.append(profilePost("profile-30"));
                else for (int i = 0; i < 30; ++i) items.append(profilePost("profile-" + QString::number(i)));
                return Response{200, {{"data", QJsonObject{{"items", items}, {"nextCursor", more ? QJsonValue(QJsonValue::Null) : QJsonValue("profile+/=")}}}}};
            }
            if (path.endsWith("/followers") || path.endsWith("/following")) return Response{200, {{"data", QJsonObject{{"items", QJsonArray{QJsonObject{
                {"id", "other"}, {"handle", "other"}, {"name", "Haru"}, {"bio", "Hello"}, {"avatarUrl", "/media/avatar.png"}}}}}}}};
            if (path.endsWith("/conversation")) return Response{200, {{"data", QJsonObject{{"tweet", profilePost("profile-0")}, {"ancestors", QJsonArray{}}, {"replies", QJsonArray{}}}}}};
            return Response{404, {}};
        };
        MemoryStore store; MainWindow window(nullptr, server.base(), &store); window.show();
        QTRY_VERIFY(window.findChild<QTabWidget *>("loginMethods")->isEnabled()); submitToken(window);
        auto *pages = window.findChild<QStackedWidget *>("workspacePages");
        QTRY_VERIFY(pages->currentWidget()->findChild<PostCard *>());
        auto *timeline = pages->currentWidget();
        auto *card = timeline->findChild<PostCard *>();
        auto *avatar = card->findChild<RemoteImage *>(); QVERIFY(avatar);
        QSignalSpy postClicks(card, &PostCard::selected);
        QTest::mouseClick(avatar, Qt::LeftButton);
        auto *profile = window.findChild<ProfilePage *>();
        QTRY_COMPARE(pages->currentWidget(), profile);
        QTRY_VERIFY(profile->findChild<QLabel *>("profileName"));
        QTRY_COMPARE(profile->findChildren<PostCard *>().size(), 30);
        QCOMPARE(postClicks.count(), 0);
        QTRY_VERIFY(profile->findChild<RemoteImage *>("profileAvatarImage")->hasImage());
        QTRY_VERIFY(profile->findChild<RemoteImage *>("profileCoverImage")->hasImage());
        auto *relation = profile->findChild<QLabel *>("profileRelation"); QVERIFY(relation);
        QVERIFY(relation->text().contains(QString::fromUtf8("フォロー中")));
        QVERIFY(relation->text().contains(QString::fromUtf8("フォローされています")));
        capture(window, "profile");
        auto *profileScroll = profile->findChild<QScrollArea *>("profileScroll");
        auto *bar = profileScroll->verticalScrollBar();
        QTRY_VERIFY(bar->maximum() > profileScroll->viewport()->height());
        bar->setValue(bar->maximum());
        QTRY_COMPARE(profile->findChildren<PostCard *>().size(), 31);
        QCOMPARE(pagesLoaded, 2);
        bar->setValue(0);
        ThemeManager::instance()->setMode(ThemeManager::Dark); capture(window, "profile-dark");
        ThemeManager::instance()->setMode(ThemeManager::Light);
        auto *body = profile->findChild<PostCard *>()->findChild<QLabel *>("postText");
        QTest::mouseClick(body, Qt::LeftButton, {}, QPoint(12, 8));
        QTRY_COMPARE(pages->currentWidget()->objectName(), "postPage");
        auto *back = window.findChild<QPushButton *>("backFromPost");
        QTest::mouseClick(back, Qt::LeftButton); QCOMPARE(pages->currentWidget(), profile);
        QPushButton *followers = nullptr;
        for (auto *button : profile->findChildren<QPushButton *>("profileCount")) if (button->property("section") == "followers") followers = button;
        QVERIFY(followers); QTest::mouseClick(followers, Qt::LeftButton);
        QTRY_COMPARE(profile->findChildren<PostCard *>().size(), 0);
        QTRY_VERIFY(profile->findChild<QPushButton *>("authorName"));
        QTest::mouseClick(profile->findChild<QPushButton *>("authorName"), Qt::LeftButton);
        QTRY_COMPARE(profile->handle(), "other");
        QTRY_VERIFY(containsText(*profile, "Haru"));
        QTest::mouseClick(back, Qt::LeftButton);
        QTRY_COMPARE(profile->handle(), "north_user");
        QTRY_COMPARE(profile->section(), 2);
        QTest::mouseClick(back, Qt::LeftButton); QCOMPARE(pages->currentWidget(), timeline);
        QTest::mouseClick(card->findChild<QPushButton *>("authorName"), Qt::LeftButton);
        QTRY_COMPARE(pages->currentWidget(), profile);
        QTest::mouseClick(findButton(window, QString::fromUtf8("ログアウト")), Qt::LeftButton);
        QTRY_VERIFY(profile->findChildren<PostCard *>().isEmpty());
        QVERIFY(profile->handle().isEmpty());
    }
    void hashtagAndCashtagOpenSearchWithTheCorrectQuery() {
        Server server; QStringList queries;
        server.handler = [&](Request request) {
            const auto path = request.url.path();
            if (path == "/2/users/me") return Response{200, {{"data", QJsonObject{{"id", "me"}, {"handle", "me"}}}}};
            if (path == "/2/notifications/unread_count") return Response{200, {{"data", QJsonObject{{"count", 0}}}}};
            if (path == "/2/tweets/search") queries.append(QUrlQuery(request.url).queryItemValue("q", QUrl::FullyDecoded));
            auto item = post(); item.remove("media");
            item.insert("text", path == "/2/tweets/search" ? QString("<i>$BRK.B</i>") : QString::fromUtf8("<太字>#日本語</太字>"));
            return Response{200, {{"data", QJsonObject{{"items", QJsonArray{item}}}}}};
        };
        MemoryStore store; MainWindow window(nullptr, server.base(), &store); window.show();
        QTRY_VERIFY(window.findChild<QTabWidget *>("loginMethods")->isEnabled()); submitToken(window);
        auto *pages = window.findChild<QStackedWidget *>("workspacePages");
        QTRY_VERIFY(pages->currentWidget()->findChild<PostCard *>());
        auto *body = pages->currentWidget()->findChild<PostCard *>()->findChild<QLabel *>("postText");
        QTest::mouseClick(body, Qt::LeftButton, {}, QPoint(8, body->fontMetrics().height() / 2));
        QTRY_COMPARE(queries, QStringList({QString::fromUtf8("#日本語")}));
        auto *input = window.findChild<QLineEdit *>("postSearch");
        QVERIFY(input->isVisible()); QCOMPARE(input->text(), QString::fromUtf8("#日本語"));
        QTRY_VERIFY(pages->currentWidget()->findChild<PostCard *>());
        QTRY_VERIFY(pages->currentWidget()->findChild<PostCard *>()->findChild<QLabel *>("postText")->text().contains("$BRK.B"));
        body = pages->currentWidget()->findChild<PostCard *>()->findChild<QLabel *>("postText");
        QTest::mouseClick(body, Qt::LeftButton, {}, QPoint(8, body->fontMetrics().height() / 2));
        QTRY_COMPARE(queries, QStringList({QString::fromUtf8("#日本語"), "$BRK.B"}));
        QCOMPARE(input->text(), "$BRK.B");
        QVERIFY(window.findChild<QWidget *>("inspector")->isHidden());
        QVERIFY(std::none_of(server.requests.begin(), server.requests.end(), [](const Request &r) { return r.url.path().endsWith("/conversation"); }));
    }
    void sidebarShowsOwnAvatarAndNavigatesToOwnProfile() {
        Server server;
        server.handler = [](Request request) {
            const auto path = request.url.path();
            if (path == "/media/me.png") return Response{200, {}, {{"Content-Type", "image/png"}}, 0, png()};
            if (path == "/2/users/me") return Response{200, {{"data", QJsonObject{{"id", "me"}, {"handle", "me"}, {"name", "My account"}, {"avatarUrl", "/media/me.png"}}}}};
            if (path == "/2/notifications/unread_count") return Response{200, {{"data", QJsonObject{{"count", 0}}}}};
            return Response{200, {{"data", QJsonObject{{"items", QJsonArray{}}}}}};
        };
        MemoryStore store; MainWindow window(nullptr, server.base(), &store); window.show();
        QVERIFY(!window.findChild<RemoteImage *>("sessionAvatar"));
        QTRY_VERIFY(window.findChild<QTabWidget *>("loginMethods")->isEnabled()); submitToken(window);
        QTRY_VERIFY(window.findChild<RemoteImage *>("sessionAvatar"));
        auto *avatar = window.findChild<RemoteImage *>("sessionAvatar");
        QTRY_VERIFY(avatar->hasImage()); QCOMPARE(avatar->size(), QSize(40, 40));
        capture(window, "account-avatar");
        QTest::mouseClick(avatar, Qt::LeftButton);
        auto *profile = window.findChild<ProfilePage *>();
        QTRY_COMPARE(profile->handle(), "me");
        QCOMPARE(window.findChild<QStackedWidget *>("workspacePages")->currentWidget(), profile);
        QVERIFY(!avatar->findChild<QDialog *>("imageViewer"));
        QTest::mouseClick(findButton(window, QString::fromUtf8("ログアウト")), Qt::LeftButton);
        QTRY_VERIFY(!window.findChild<RemoteImage *>("sessionAvatar"));
    }
    void appearanceAndAboutWorkBeforeLogin() {
        MemoryStore store;
        MainWindow window(nullptr, QUrl("https://unused.test.invalid"), &store); window.show();
        auto *manager = ThemeManager::instance();
        const auto icon = appIcon("user");
        manager->setMode(ThemeManager::Light);
        const QImage light = icon.pixmap(24, 24).toImage();
        auto *darkAction = window.findChild<QAction *>("themeDark"); QVERIFY(darkAction); darkAction->trigger();
        QVERIFY(manager->isDark());
        QCOMPARE(QSettings().value("appearance/theme").toString(), "dark");
        QVERIFY(QApplication::palette().color(QPalette::Base).lightness() < 128);
        QVERIFY(QApplication::palette().color(QPalette::Text).lightness() > 200);
        QVERIFY(light != icon.pixmap(24, 24).toImage());
        QTRY_VERIFY(window.findChild<QTabWidget *>("loginMethods")->isEnabled()); capture(window, "login-dark");
        QTest::mouseClick(window.findChild<QPushButton *>("loginAbout"), Qt::LeftButton);
        auto *dialog = window.findChild<QDialog *>("aboutDialog"); QVERIFY(dialog);
        QTRY_VERIFY(dialog->isVisible());
        capture(*dialog, "about-dark");
        QTest::mouseClick(findButton(*dialog, QString::fromUtf8("使用ライブラリー")), Qt::LeftButton);
        auto *pages = dialog->findChild<QStackedWidget *>("aboutPages");
        QCOMPARE(pages->currentIndex(), 1);
        for (auto *label : dialog->findChildren<QLabel *>()) {
            QVERIFY(!label->text().contains(QString::fromUtf8("デザインの参考")));
            QVERIFY(!label->text().contains("libadwaita"));
        }
        QTest::mouseClick(dialog->findChild<QPushButton *>("aboutBack"), Qt::LeftButton); QCOMPARE(pages->currentIndex(), 0);
        manager->setMode(ThemeManager::Light); capture(*dialog, "about");
        QCOMPARE(QApplication::palette().color(QPalette::Base), QColor("#ffffff"));
        QTest::mouseClick(dialog->findChild<QPushButton *>("aboutClose"), Qt::LeftButton);
        QTRY_VERIFY(!window.findChild<QDialog *>("aboutDialog"));
        manager->setMode(ThemeManager::System);
    }
    void settingsApplyPersistAndOpenFromBothScreens() {
        auto *preferences = Preferences::instance(); auto *manager = ThemeManager::instance();
        const int previousInterval = preferences->notificationInterval(); const auto previousTheme = manager->mode();
        const bool previousFold = preferences->foldLongPosts(), previousPreviews = preferences->linkPreviews();
        const auto restore = qScopeGuard([=] {
            preferences->setNotificationInterval(previousInterval); manager->setMode(previousTheme);
            preferences->setFoldLongPosts(previousFold); preferences->setLinkPreviews(previousPreviews);
        });
        Server server;
        server.handler = [](Request request) {
            if (request.url.path() == "/2/users/me") return Response{200, {{"data", QJsonObject{{"id", "me"}, {"handle", "me"}}}}};
            if (request.url.path().endsWith("unread_count")) return Response{200, {{"data", QJsonObject{{"count", 0}}}}};
            return Response{200, {{"data", QJsonObject{{"items", QJsonArray{}}}}}};
        };
        MemoryStore store; MainWindow window(nullptr, server.base(), &store); window.show();
        QTRY_VERIFY(window.findChild<QTabWidget *>("loginMethods")->isEnabled());
        QTest::mouseClick(window.findChild<QPushButton *>("loginSettings"), Qt::LeftButton);
        auto *dialog = window.findChild<QDialog *>("settingsDialog"); QVERIFY(dialog); QTRY_VERIFY(dialog->isVisible());
        QVERIFY(dialog->windowFlags().testFlag(Qt::FramelessWindowHint));
        QTest::mouseClick(dialog->findChild<QToolButton *>("settingsThemeDark"), Qt::LeftButton);
        QVERIFY(manager->isDark()); QCOMPARE(QSettings().value("appearance/theme").toString(), "dark"); capture(*dialog, "settings-dark");
        manager->setMode(ThemeManager::Light);
        QVERIFY(dialog->findChild<QToolButton *>("settingsThemeLight")->isChecked()); capture(*dialog, "settings");
        auto *interval = dialog->findChild<QComboBox *>("notificationInterval"); QVERIFY(interval);
        interval->setCurrentIndex(interval->findData(0)); QCOMPARE(preferences->notificationInterval(), 0);
        QCOMPARE(QSettings().value("notifications/pollIntervalSeconds").toInt(), 0);
        dialog->findChild<QCheckBox *>("foldLongPosts")->setChecked(true);
        dialog->findChild<QCheckBox *>("linkPreviews")->setChecked(false);
        QVERIFY(preferences->foldLongPosts()); QVERIFY(!preferences->linkPreviews());
        QTest::mouseClick(dialog->findChild<QPushButton *>("clearCache"), Qt::LeftButton);
        QVERIFY(dialog->findChild<QLabel *>("settingsCacheStatus")->isVisible());
        QCOMPARE(server.requests.size(), 0);
        QTest::mouseClick(dialog->findChild<QPushButton *>("settingsClose"), Qt::LeftButton);
        QTRY_VERIFY(!window.findChild<QDialog *>("settingsDialog"));
        auto *action = window.findChild<QAction *>("showSettings"); QVERIFY(action); action->trigger();
        dialog = window.findChild<QDialog *>("settingsDialog"); QVERIFY(dialog);
        QCOMPARE(dialog->findChild<QComboBox *>("notificationInterval")->currentData().toInt(), 0);
        QVERIFY(dialog->findChild<QCheckBox *>("foldLongPosts")->isChecked());
        QVERIFY(!dialog->findChild<QCheckBox *>("linkPreviews")->isChecked());
        action->trigger(); QCOMPARE(window.findChildren<QDialog *>("settingsDialog").size(), 1);
        dialog->close(); QTRY_VERIFY(!window.findChild<QDialog *>("settingsDialog"));
        submitToken(window);
        auto *sidebarSettings = window.findChild<QPushButton *>("sidebarSettings"); QTRY_VERIFY(sidebarSettings->isVisible());
        QTest::mouseClick(sidebarSettings, Qt::LeftButton);
        QTRY_VERIFY(window.findChild<QDialog *>("settingsDialog"));
        window.findChild<QDialog *>("settingsDialog")->close();
        QTRY_VERIFY(!window.findChild<QDialog *>("settingsDialog"));
    }
    void profileDiscardsLateResponsesAndClearsPrivateLists() {
        Server server;
        bool firstPosts = true;
        server.handler = [&](Request request) {
            const auto path = request.url.path();
            if (path == "/2/users/old" || path == "/2/users/current") {
                const bool old = path.endsWith("old");
                return Response{200, {{"data", QJsonObject{{"id", old ? "old" : "current"}, {"handle", old ? "old" : "current"},
                    {"name", old ? "Old profile" : "Current profile"}, {"protected", !old}, {"following", false}}}}, {}, old ? 150 : 0};
            }
            if (path.endsWith("/following")) return Response{200, {{"data", QJsonObject{{"items", QJsonArray{QJsonObject{{"id", "friend"}, {"name", "Friend"}, {"handle", "friend"}}}}}}}};
            if (firstPosts) {
                firstPosts = false;
                return Response{200, {{"data", QJsonObject{{"items", QJsonArray{post("late-private-post")}}}}}, {}, 150};
            }
            return Response{200, {{"data", QJsonObject{{"items", QJsonArray{}}}}}};
        };
        NorthApi api(nullptr, server.base()); api.usePersonalToken("test-token");
        ProfilePage page(&api, [&api](const QJsonObject &item) { return new PostCard(item, false, &api); });
        page.resize(600, 700); page.show();
        page.openUser("old", "me"); QTRY_COMPARE(server.requests.size(), 1);
        page.openUser("current", "me");
        QTRY_VERIFY(containsText(page, "Current profile"));
        QTRY_COMPARE(server.requests.size(), 3);
        auto *tabs = page.findChild<QTabBar *>("profileTabs"); tabs->setCurrentIndex(1);
        QTRY_VERIFY(page.findChild<QPushButton *>("authorName"));
        QTest::qWait(180);
        QVERIFY(!containsText(page, "Old profile"));
        QVERIFY(page.findChildren<PostCard *>().isEmpty());
        tabs->setCurrentIndex(0);
        QTRY_VERIFY(containsText(page, QString::fromUtf8("このアカウントの一覧は非公開です")));
        QTRY_VERIFY(page.findChildren<QPushButton *>("authorName").isEmpty());
        api.logout();
        QVERIFY(page.handle().isEmpty());
        QVERIFY(!containsText(page, "Current profile"));
    }
    void systemPaletteChangesRespectManualTheme() {
        auto *manager = ThemeManager::instance(); manager->setMode(ThemeManager::Light);
        auto external = QApplication::palette(); external.setColor(QPalette::Window, QColor("#111111"));
        QApplication::setPalette(external);
        QVERIFY(!manager->isDark());
        QCOMPARE(QApplication::palette().color(QPalette::Window), QColor("#ffffff"));
#if QT_VERSION < QT_VERSION_CHECK(6, 5, 0)
        manager->setMode(ThemeManager::System);
        QVERIFY(manager->isDark());
        external = QApplication::palette(); external.setColor(QPalette::Window, Qt::white);
        QApplication::setPalette(external);
        QVERIFY(!manager->isDark());
#endif
        manager->setMode(ThemeManager::System);
    }
    void customFrameWindowControlsAndContentInput() {
        QWidget window; window.setWindowTitle("qtNorth"); window.resize(800, 600);
        auto *layout = new QVBoxLayout(&window); layout->setContentsMargins(0, 0, 0, 0);
        auto *bar = new WindowTitleBar(&window); layout->addWidget(bar);
        auto *content = new QPushButton("Content"); layout->addWidget(content); layout->addStretch();
        int clicks = 0; connect(content, &QPushButton::clicked, &window, [&] { ++clicks; });
        window.show(); QVERIFY(QTest::qWaitForWindowExposed(&window));
        QVERIFY(window.windowFlags().testFlag(Qt::FramelessWindowHint));
        QCOMPARE(window.findChildren<QWidget *>("windowResizeEdge").size(), 8);
        QTest::mouseClick(content, Qt::LeftButton); QCOMPARE(clicks, 1);
        auto *maximize = bar->findChild<QPushButton *>("windowMaximize"); QVERIFY(maximize);
        QTest::mouseClick(maximize, Qt::LeftButton); QTRY_VERIFY(window.isMaximized());
        QCOMPARE(window.contentsMargins(), QMargins());
        QTest::mouseClick(maximize, Qt::LeftButton); QTRY_VERIFY(!window.isMaximized());
        QVERIFY(window.contentsMargins().left() > 0);
        QTest::mouseClick(bar->findChild<QPushButton *>("windowMinimize"), Qt::LeftButton);
        QTRY_VERIFY(window.isMinimized());
        window.showNormal(); QVERIFY(QTest::qWaitForWindowExposed(&window));
        QTest::mouseClick(content, Qt::LeftButton); QCOMPARE(clicks, 2);
        QTest::mouseDClick(bar, Qt::LeftButton, {}, QPoint(bar->width() / 2, bar->height() / 2));
        QTRY_VERIFY(window.isMaximized());
        QTest::mouseDClick(bar, Qt::LeftButton, {}, QPoint(bar->width() / 2, bar->height() / 2));
        QTRY_VERIFY(!window.isMaximized());
        QTest::mouseClick(bar->closeButton(), Qt::LeftButton); QVERIFY(!window.isVisible());
    }
    void changingConversationDiscardsLateMessages() {
        Server server;
        const QJsonObject me{{"id", "me"}, {"name", "Me"}, {"handle", "me"}};
        server.handler = [&](Request request) {
            if (request.url.path() == "/2/dm/conversations") {
                const QJsonArray conversations{
                    QJsonObject{{"id", "A"}, {"name", "A"}, {"participants", QJsonArray{me}}},
                    QJsonObject{{"id", "B"}, {"name", "B"}, {"participants", QJsonArray{me}}}
                };
                Response response;
                response.json.insert("data", QJsonObject{{"items", conversations}});
                return response;
            }
            const bool old = request.url.path().contains("/A/");
            const QJsonObject message{{"id", old ? "old" : "new"},
                {"text", old ? "Late private A" : "Current private B"}, {"createdAt", "2026-10-27T12:00:00Z"},
                {"sender", QJsonObject{{"id", "other"}}}};
            Response response;
            response.json.insert("data", QJsonObject{{"items", QJsonArray{message}}});
            response.delay = old ? 120 : 0;
            return response;
        };
        NorthApi api(nullptr, server.base()); api.usePersonalToken("token");
        MessagesPage page(&api); page.resize(850, 600); page.setSession(me); page.show(); page.reload();
        auto *list = page.findChild<QListWidget *>("conversationList");
        QTRY_COMPARE(list->count(), 2);
        list->setCurrentRow(0);
        QTRY_COMPARE(server.requests.size(), 2);
        list->setCurrentRow(1);
        QTRY_VERIFY(containsText(page, "Current private B"));
        QTest::qWait(150);
        QVERIFY(!containsText(page, "Late private A"));
        api.logout();
        QVERIFY(!containsText(page, "Current private B"));
    }
    void navigationReusesRecentReadsAndManualRefreshFetchesAgain() {
        Server server; int homes = 0, conversations = 0;
        auto item = post(); item.remove("media");
        auto detail = item; detail.insert("text", "Conversation refreshed");
        server.handler = [&](Request request) {
            const auto path = request.url.path();
            if (path == "/2/users/me") return Response{200, {{"data", QJsonObject{{"id", "me"}, {"handle", "me"}}}}};
            if (path.endsWith("unread_count")) return Response{200, {{"data", QJsonObject{{"count", 0}}}}};
            if (path.endsWith("/conversation")) {
                ++conversations;
                return Response{200, {{"data", QJsonObject{{"tweet", detail}, {"ancestors", QJsonArray{}}, {"replies", QJsonArray{}}}}}};
            }
            if (QUrlQuery(request.url).queryItemValue("ranked") == "0") ++homes;
            return Response{200, {{"data", QJsonObject{{"items", QJsonArray{item}}}}}};
        };
        MemoryStore store; MainWindow window(nullptr, server.base(), &store); window.show();
        QTRY_VERIFY(window.findChild<QTabWidget *>("loginMethods")->isEnabled()); submitToken(window);
        auto *timeline = window.findChild<QScrollArea *>("timelineScroll");
        QTRY_COMPARE(timeline->findChildren<PostCard *>().size(), 1); QCOMPARE(homes, 1);
        QPushButton *home = nullptr, *ranked = nullptr;
        for (auto *button : window.findChildren<QPushButton *>()) {
            if (button->property("view") == "home") home = button;
            if (button->property("view") == "ranked") ranked = button;
        }
        QVERIFY(home); QVERIFY(ranked);
        QTest::mouseClick(ranked, Qt::LeftButton);
        QTRY_COMPARE(timeline->findChildren<PostCard *>().size(), 1);
        QTest::mouseClick(home, Qt::LeftButton);
        QTRY_COMPARE(timeline->findChildren<PostCard *>().size(), 1); QCOMPARE(homes, 1);
        auto *pages = window.findChild<QStackedWidget *>("workspacePages");
        for (int i = 0; i < 2; ++i) {
            QTest::mouseClick(timeline->findChild<PostCard *>()->findChild<QLabel *>("postText"), Qt::LeftButton, {}, QPoint(12, 8));
            QTRY_COMPARE(pages->currentWidget()->objectName(), "postPage");
            QTRY_COMPARE(conversations, 1);
            // Let the first response finish so the second open uses the cached result.
            QTRY_VERIFY(containsText(*pages->currentWidget(), "Conversation refreshed"));
            QTest::mouseClick(window.findChild<QPushButton *>("backFromPost"), Qt::LeftButton);
            QTRY_VERIFY(timeline->isVisible());
        }
        QCOMPARE(conversations, 1);
        QTest::mouseClick(home, Qt::LeftButton); QTRY_COMPARE(homes, 2);
        QTRY_COMPARE(timeline->findChildren<PostCard *>().size(), 1);
    }
    void automaticTimelinePaginationAndViewChanges() {
        Server server;
        int cursorRequests = 0;
        bool delayed = false;
        server.handler = [&](Request request) {
            if (request.url.path() == "/2/users/me") return Response{200, {{"data", QJsonObject{{"id", "me"}, {"handle", "me"}}}}};
            const QUrlQuery query(request.url);
            QJsonArray items;
            QJsonValue next(QJsonValue::Null);
            int delay = 0;
            if (query.queryItemValue("ranked") == "1") {
                auto item = post("ranked"); item.remove("media"); items.append(item);
            } else if (query.hasQueryItem("cursor")) {
                ++cursorRequests;
                auto duplicate = post("home-29"); duplicate.remove("media"); items.append(duplicate);
                auto item = post(delayed ? "stale" : "home-30"); item.remove("media"); items.append(item);
                delay = delayed ? 200 : 80;
            } else {
                for (int i = 0; i < 30; ++i) {
                    auto item = post("home-" + QString::number(i)); item.remove("media"); items.append(item);
                }
                next = "next+/=";
            }
            return Response{200, {{"data", QJsonObject{{"items", items}, {"nextCursor", next}}}}, {}, delay};
        };
        MemoryStore store;
        MainWindow window(nullptr, server.base(), &store); window.show();
        QTRY_VERIFY(window.findChild<QTabWidget *>("loginMethods")->isEnabled());
        submitToken(window);
        QPushButton *home = nullptr, *ranked = nullptr;
        for (auto *button : window.findChildren<QPushButton *>()) {
            if (button->property("view") == "home") home = button;
            if (button->property("view") == "ranked") ranked = button;
        }
        QVERIFY(home); QVERIFY(ranked);
        QTest::mouseClick(home, Qt::LeftButton);
        auto *timeline = window.findChild<QScrollArea *>("timelineScroll");
        QTRY_COMPARE(timeline->findChildren<PostCard *>().size(), 30);
        auto *bar = timeline->verticalScrollBar();
        QTRY_VERIFY(bar->maximum() > timeline->viewport()->height());
        QCOMPARE(cursorRequests, 0);
        const int oldMaximum = bar->maximum();
        bar->setValue(oldMaximum);
        QTRY_COMPARE(cursorRequests, 1);
        bar->setValue(oldMaximum - 2); bar->setValue(oldMaximum);
        PostCard *visibleCard = nullptr;
        int visibleY = timeline->viewport()->height();
        for (auto *card : timeline->findChildren<PostCard *>()) {
            const int y = card->mapTo(timeline->viewport(), QPoint()).y();
            if (y + card->height() > 0 && y < visibleY) { visibleCard = card; visibleY = y; }
        }
        QVERIFY(visibleCard);
        QTRY_COMPARE(timeline->findChildren<PostCard *>().size(), 31);
        QCOMPARE(cursorRequests, 1);
        QTest::qWait(30); // Allow the queued layout and anchor correction to settle.
        QCOMPARE(visibleCard->mapTo(timeline->viewport(), QPoint()).y(), visibleY);
        QVERIFY(timeline->viewport()->updatesEnabled());
        QCOMPARE(QUrlQuery(server.requests.last().url).queryItemValue("cursor", QUrl::FullyDecoded), "next+/=");
        bar->setValue(bar->maximum()); QTest::qWait(50);
        QCOMPARE(cursorRequests, 1);
        delayed = true;
        QTest::mouseClick(home, Qt::LeftButton);
        QTRY_COMPARE(timeline->findChildren<PostCard *>().size(), 30);
        QTRY_VERIFY(bar->maximum() > timeline->viewport()->height());
        bar->setValue(bar->maximum());
        QTRY_COMPARE(cursorRequests, 2);
        QTest::mouseClick(ranked, Qt::LeftButton);
        QTRY_COMPARE(timeline->findChildren<PostCard *>().size(), 1);
        QTest::qWait(240);
        QCOMPARE(timeline->findChildren<PostCard *>().size(), 1);
        QCOMPARE(timeline->findChild<PostCard *>()->post().value("id").toString(), "ranked");
    }
    void timelineHasNoScrollableBlankTailAfterContentResizes() {
        Server server; int moreRequests = 0;
        server.handler = [&](Request request) {
            if (request.url.path() == "/2/users/me") return Response{200, {{"data", QJsonObject{{"id", "me"}, {"handle", "me"}}}}};
            if (request.url.path() == "/media/photo.png") return Response{200, {}, {{"Content-Type", "image/png"}}, 0, png()};
            QJsonArray items;
            const bool more = QUrlQuery(request.url).hasQueryItem("cursor");
            if (more) ++moreRequests;
            for (int i = 0; i < (more ? 1 : 30); ++i) {
                auto item = post(QString::number(more ? 30 : i));
                item.insert("text", QString("A long line that wraps across the width of the post. ").repeated(15));
                if (i % 3) item.remove("media");
                items.append(item);
            }
            return Response{200, {{"data", QJsonObject{{"items", items}, {"nextCursor", more ? QJsonValue(QJsonValue::Null) : QJsonValue("next")}}}}};
        };
        auto *preferences = Preferences::instance(); const bool folded = preferences->foldLongPosts();
        const auto restore = qScopeGuard([&] { preferences->setFoldLongPosts(folded); });
        preferences->setFoldLongPosts(false);
        MemoryStore store; MainWindow window(nullptr, server.base(), &store); window.show();
        QTRY_VERIFY(window.findChild<QTabWidget *>("loginMethods")->isEnabled()); submitToken(window);
        auto *timeline = window.findChild<QScrollArea *>("timelineScroll");
        QTRY_COMPARE(timeline->findChildren<PostCard *>().size(), 30);
        QTest::qWait(100);
        auto *bar = timeline->verticalScrollBar();
        for (const bool fold : {false, true}) {
            preferences->setFoldLongPosts(fold);
            for (int width : {940, 1550, 1100}) {
                window.resize(width, 700); QTest::qWait(100);
                auto cards = timeline->findChildren<PostCard *>();
                int bottom = 0;
                for (auto *card : cards) bottom = std::max(bottom, card->mapTo(timeline->widget(), QPoint(0, card->height())).y());
                const int blank = timeline->widget()->height() - bottom;
                QVERIFY2(blank <= 2, qPrintable(QString("Scrollable blank tail: %1px; canvas=%2, last post bottom=%3, width=%4, folded=%5")
                    .arg(blank).arg(timeline->widget()->height()).arg(bottom).arg(width).arg(fold)));
            }
        }
        auto cards = timeline->findChildren<PostCard *>(); int bottom = 0;
        for (auto *card : cards) bottom = std::max(bottom, card->mapTo(timeline->widget(), QPoint(0, card->height())).y());
        bar->setValue(bottom - timeline->viewport()->height());
        QTRY_COMPARE(moreRequests, 1);
        QTRY_COMPARE(timeline->findChildren<PostCard *>().size(), 31);
        QTest::qWait(100);
        bottom = 0;
        for (auto *card : timeline->findChildren<PostCard *>()) {
            bottom = std::max(bottom, card->mapTo(timeline->widget(), QPoint(0, card->height())).y());
            QVERIFY(card->height() >= card->heightForWidth(card->width()));
        }
        QVERIFY(timeline->widget()->height() - bottom <= 2);
    }
    void authenticatedPostNavigationAndMessageRetry() {
        Server server;
        int sends = 0;
        const QJsonObject me{{"id", "me"}, {"name", "My account"}, {"handle", "me"}};
        const QJsonObject conversation{{"id", "chat-1"}, {"participants", QJsonArray{me,
            QJsonObject{{"id", "other"}, {"name", "Haru"}, {"handle", "haru"}}}}, {"group", false}};
        server.handler = [&](Request request) {
            const auto path = request.url.path();
            if (path == "/media/photo.png") return Response{200, {}, {{"Content-Type", "image/png"}}, 0, png()};
            if (path == "/2/users/me") return Response{200, {{"data", me}}};
            if (path == "/2/timelines/home") return Response{200, {{"data", QJsonObject{{"items", QJsonArray{post()}}, {"nextCursor", QJsonValue::Null}}}}};
            if (path.endsWith("/conversation")) return Response{200, {{"data", QJsonObject{{"tweet", post()}, {"ancestors", QJsonArray{}},
                {"replies", QJsonArray{}}, {"nextCursor", QJsonValue::Null}}}}};
            if (path == "/2/dm/conversations") return Response{200, {{"data", QJsonObject{{"items", QJsonArray{conversation}}}}}};
            if (path.endsWith("/messages") && request.method == "GET") return Response{200, {{"data", QJsonObject{{"conversation", conversation},
                {"items", QJsonArray{QJsonObject{{"id", "message-1"}, {"text", "Private hello"}, {"sender", QJsonObject{{"id", "other"}}}, {"createdAt", "2026-10-27T12:00:00Z"}}}}}}}};
            if (path.endsWith("/messages") && request.method == "POST") {
                if (++sends == 1) return Response{500, {{"errors", QJsonArray{QJsonObject{{"message", "Try again"}}}}}};
                return Response{201, {{"data", QJsonObject{{"id", "message-2"}, {"text", request.body.value("text")}, {"sender", me}, {"createdAt", "2026-10-27T12:01:00Z"}}}}};
            }
            return Response{404, {}};
        };
        MemoryStore store;
        MainWindow window(nullptr, server.base(), &store); window.show();
        QTRY_VERIFY(window.findChild<QTabWidget *>("loginMethods")->isEnabled());
        submitToken(window);
        auto *pages = window.findChild<QStackedWidget *>("workspacePages");
        QTRY_VERIFY(pages->currentWidget()->findChild<PostCard *>());
        auto *timeline = pages->currentWidget();
        auto *card = timeline->findChild<PostCard *>();
        QTRY_VERIFY(card->findChild<RemoteImage *>()->hasImage());
        auto *inspector = window.findChild<QWidget *>("inspector"); QVERIFY(inspector);
        auto *toggleInspector = window.findChild<QAction *>("toggleInspector"); QVERIFY(toggleInspector);
        QVERIFY(inspector->isHidden()); QVERIFY(!toggleInspector->isEnabled());
        capture(window, "preview");
        QTest::mouseClick(card->findChild<QLabel *>("postText"), Qt::LeftButton, {}, QPoint(20, 8));
        QTRY_COMPARE(pages->currentWidget()->objectName(), "postPage");
        QTRY_VERIFY(inspector->isVisible()); QVERIFY(toggleInspector->isEnabled());
        toggleInspector->trigger(); QVERIFY(inspector->isHidden());
        toggleInspector->trigger(); QVERIFY(inspector->isVisible());
        QTRY_VERIFY(std::any_of(server.requests.begin(), server.requests.end(), [](const Request &r) { return r.url.path().endsWith("/conversation"); }));
        QTRY_VERIFY(pages->currentWidget()->findChild<RemoteImage *>()->hasImage());
        capture(window, "post");
        QTest::mouseClick(window.findChild<QPushButton *>("backFromPost"), Qt::LeftButton);
        QCOMPARE(pages->currentWidget(), timeline);
        QVERIFY(inspector->isHidden()); QVERIFY(!toggleInspector->isEnabled());
        QPushButton *messages = nullptr;
        for (auto *button : window.findChildren<QPushButton *>()) if (button->property("view") == "messages") messages = button;
        QVERIFY(messages); QTest::mouseClick(messages, Qt::LeftButton);
        auto *inbox = pages->currentWidget();
        auto *list = inbox->findChild<QListWidget *>("conversationList"); QVERIFY(list);
        QTRY_COMPARE(list->count(), 1);
        list->setCurrentRow(0);
        QTRY_VERIFY(containsText(*inbox, "Private hello"));
        capture(window, "messages");
        QPushButton *newConversation = nullptr;
        for (auto *button : inbox->findChildren<QPushButton *>())
            if (button->accessibleName() == QString::fromUtf8("新しいメッセージ")) newConversation = button;
        QVERIFY(newConversation); QTest::mouseClick(newConversation, Qt::LeftButton);
        auto *newDialog = inbox->findChild<QDialog *>("newConversationDialog"); QVERIFY(newDialog);
        QTRY_VERIFY(newDialog->isVisible());
        QVERIFY(newDialog->windowFlags().testFlag(Qt::FramelessWindowHint));
        auto *dialogBar = newDialog->findChild<WindowTitleBar *>(); QVERIFY(dialogBar);
        QTest::mouseClick(dialogBar->closeButton(), Qt::LeftButton);
        QTRY_VERIFY(!inbox->findChild<QDialog *>("newConversationDialog"));
        QVERIFY(newConversation->isEnabled());
        auto *editor = inbox->findChild<QPlainTextEdit *>("messageEditor");
        QTRY_VERIFY(editor->isEnabled()); editor->setPlainText("A message");
        auto *send = inbox->findChild<QPushButton *>("sendMessage"); QVERIFY(send);
        QTest::mouseClick(send, Qt::LeftButton);
        QTRY_COMPARE(sends, 1);
        QTRY_VERIFY(send->isEnabled());
        QCOMPARE(editor->toPlainText(), "A message");
        QTest::mouseClick(send, Qt::LeftButton);
        QTRY_COMPARE(sends, 2);
        QTRY_VERIFY(editor->toPlainText().isEmpty());
        QList<Request> sent;
        for (const auto &r : server.requests) {
            if (r.method == "POST" && r.url.path().endsWith("/messages")) sent << r;
            QVERIFY(!r.url.path().endsWith("/read"));
        }
        QCOMPARE(sent.size(), 2);
        QVERIFY(!sent[0].headers.value("idempotency-key").isEmpty());
        QCOMPARE(sent[0].headers.value("idempotency-key"), sent[1].headers.value("idempotency-key"));
        QCOMPARE(sent[0].body.value("text").toString(), "A message");
        window.findChild<NorthApi *>()->logout();
        QTRY_VERIFY(inbox->findChildren<QLabel *>("messageText").isEmpty());
    }
};
QTEST_MAIN(UiTests)
#include "tst_ui.moc"
