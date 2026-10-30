#include "reactionusers.h"
#include "mainwindow.h"
#include "memorystore.h"
#include "northapi.h"
#include "postcard.h"
#include "profilepage.h"
#include "remoteimage.h"
#include "theme.h"
#include "testserver.h"
#include <QBuffer>
#include <QDialog>
#include <QDir>
#include <QJsonArray>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QScrollArea>
#include <QScrollBar>
#include <QSettings>
#include <QSignalSpy>
#include <QStackedWidget>
#include <QTabWidget>
#include <QTemporaryDir>
#include <QtTest>
#include <algorithm>

namespace {
QJsonObject user(const QString &id, bool image = false) {
    QJsonObject value{{"id", id}, {"handle", id}, {"name", "User " + id}};
    if (image) value.insert("avatarUrl", "/media/avatar.png");
    return value;
}
QJsonObject post(const QString &id = "100") {
    return {{"id", id}, {"text", "A post"}, {"author", user("author")}, {"retweetCount", 2}, {"likeCount", 3}};
}
Response users(const QJsonArray &items, const QString &cursor = {}) {
    return {200, {{"data", QJsonObject{{"items", items}, {"nextCursor", cursor}}}}};
}
int requests(const Server &server, const QString &suffix) {
    return std::count_if(server.requests.begin(), server.requests.end(), [&](const Request &request) { return request.url.path().endsWith(suffix); });
}
QPushButton *identity(QWidget *root, const QString &handle) {
    for (auto *button : root->findChildren<QPushButton *>("authorName"))
        if (button->property("profileHandle").toString() == handle) return button;
    return nullptr;
}
void capture(QWidget &widget, const QString &name) {
    const auto dir = qEnvironmentVariable("QTNORTH_CAPTURE_DIR");
    if (!dir.isEmpty()) { QTest::qWait(50); QDir().mkpath(dir); widget.grab().save(dir + "/" + name + ".png"); }
}
}

class ReactionTests : public QObject {
    Q_OBJECT
    QTemporaryDir m_settings;
private slots:
    void initTestCase() {
        QCoreApplication::setOrganizationName("qtNorth-tests"); QCoreApplication::setApplicationName("reactions");
        QSettings::setDefaultFormat(QSettings::IniFormat);
        QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, m_settings.path());
        ThemeManager::instance()->setMode(ThemeManager::Light);
    }
    void detailRepostProfilesAndLazyCachedLikesPopup() {
        Server server;
        server.handler = [](Request request) {
            const auto path = request.url.path();
            if (path == "/media/avatar.png") {
                QImage image(64, 64, QImage::Format_RGB32); image.fill(QColor("#5aa7c3"));
                QByteArray bytes; QBuffer buffer(&bytes); buffer.open(QIODevice::WriteOnly); image.save(&buffer, "PNG");
                return Response{200, {}, {{"Content-Type", "image/png"}}, 0, bytes};
            }
            if (path == "/2/users/me") return Response{200, {{"data", user("me")}}};
            if (path == "/2/notifications/unread_count") return Response{200, {{"data", QJsonObject{{"count", 0}}}}};
            if (path == "/2/timelines/home") {
                auto wrapper = post("999"); wrapper.insert("retweetOf", post());
                return users({wrapper});
            }
            if (path.endsWith("/conversation")) return Response{200, {{"data", QJsonObject{{"tweet", post()}, {"replies", QJsonArray{}}}}}, {}, 80};
            if (path == "/2/tweets/100/retweets") return users({user("reposter", true), user("second")});
            if (path == "/2/tweets/100/likes") return users({user("liker", true), user("another")});
            if (path == "/2/users/reposter") return Response{200, {{"data", user("reposter")}}};
            if (path == "/2/users/liker") return Response{200, {{"data", user("liker")}}};
            if (path.endsWith("/tweets")) return users({});
            return Response{404, {}};
        };
        MemoryStore store; MainWindow window(nullptr, server.base(), &store); window.show();
        QTRY_VERIFY(window.findChild<QTabWidget *>("loginMethods")->isEnabled());
        window.findChild<QTabWidget *>("loginMethods")->setCurrentIndex(1);
        window.findChild<QLineEdit *>("personalToken")->setText("test-token");
        QTest::mouseClick(window.findChild<QPushButton *>("tokenLogin"), Qt::LeftButton);
        auto *pages = window.findChild<QStackedWidget *>("workspacePages");
        QTRY_VERIFY(pages->currentWidget()->findChild<PostCard *>());
        QCOMPARE(requests(server, "/retweets"), 0); QCOMPARE(requests(server, "/likes"), 0);
        auto *body = pages->currentWidget()->findChild<PostCard *>()->findChild<QLabel *>("postText");
        QTest::mouseClick(body, Qt::LeftButton, {}, QPoint(10, 6));
        auto *reactions = window.findChild<PostReactions *>();
        QTRY_VERIFY(reactions->isVisible());
        QTRY_VERIFY(identity(reactions, "reposter"));
        QTest::qWait(150); // Conversation refresh must preserve the reaction list and its request.
        QCOMPARE(requests(server, "/retweets"), 1); QCOMPARE(requests(server, "/likes"), 0);
        auto *avatar = reactions->findChild<RemoteImage *>(); QVERIFY(avatar);
        QTRY_VERIFY(avatar->hasImage());
        capture(window, "post-reactions");
        QTest::mouseClick(identity(reactions, "reposter"), Qt::LeftButton);
        auto *profile = window.findChild<ProfilePage *>();
        QTRY_COMPARE(profile->handle(), "reposter"); QVERIFY(reactions->isHidden());
        QTest::mouseClick(window.findChild<QPushButton *>("backFromPost"), Qt::LeftButton);
        QTRY_VERIFY(identity(reactions, "reposter"));
        QCOMPARE(requests(server, "/retweets"), 1);
        auto *likes = reactions->findChild<QPushButton *>("showLikeUsers");
        QTest::mouseClick(likes, Qt::LeftButton);
        QTRY_VERIFY(window.findChild<QDialog *>("likeUsersDialog"));
        auto *dialog = window.findChild<QDialog *>("likeUsersDialog");
        QTRY_VERIFY(identity(dialog, "liker"));
        QCOMPARE(requests(server, "/likes"), 1);
        QTRY_VERIFY(dialog->findChild<RemoteImage *>()->hasImage());
        capture(*dialog, "like-users");
        ThemeManager::instance()->setMode(ThemeManager::Dark); capture(*dialog, "like-users-dark");
        ThemeManager::instance()->setMode(ThemeManager::Light);
        dialog->close(); QTRY_VERIFY(!window.findChild<QDialog *>("likeUsersDialog"));
        QTest::mouseClick(likes, Qt::LeftButton);
        QTRY_VERIFY(window.findChild<QDialog *>("likeUsersDialog"));
        dialog = window.findChild<QDialog *>("likeUsersDialog");
        QTRY_VERIFY(identity(dialog, "liker")); QCOMPARE(requests(server, "/likes"), 1);
        QTest::mouseClick(dialog->findChild<RemoteImage *>(), Qt::LeftButton);
        QTRY_COMPARE(profile->handle(), "liker");
        QTRY_VERIFY(!window.findChild<QDialog *>("likeUsersDialog"));
        for (const auto &request : server.requests) if (request.url.path().endsWith("/likes") || request.url.path().endsWith("/retweets"))
            QCOMPARE(request.headers.value("authorization"), "Bearer test-token");
    }
    void paginationDeduplicatesUsersAndRepeatedCursors() {
        Server server;
        server.handler = [](Request request) {
            const auto cursor = QUrlQuery(request.url).queryItemValue("cursor");
            QJsonArray items;
            if (cursor.isEmpty()) for (int i = 0; i < 20; ++i) items.append(user("person" + QString::number(i)));
            else { items.append(user("person0")); items.append(user("last")); }
            return users(items, "next+/=");
        };
        NorthApi api(nullptr, server.base()); api.usePersonalToken("token");
        ReactionUsersList list(&api, ReactionUsersList::Reposts); list.resize(270, 230); list.setPost("100");
        QCOMPARE(server.requests.size(), 0); list.show();
        QTRY_VERIFY(identity(&list, "person19"));
        auto *scroll = list.findChild<QScrollArea *>();
        QTRY_VERIFY(scroll->verticalScrollBar()->maximum() > 200);
        scroll->verticalScrollBar()->setValue(scroll->verticalScrollBar()->maximum());
        QTRY_VERIFY(identity(&list, "last"));
        QCOMPARE(list.findChildren<QFrame *>("reactionUserRow").size(), 21);
        QCOMPARE(server.requests.size(), 2);
        QCOMPARE(QUrlQuery(server.requests.last().url).queryItemValue("cursor"), "next+/=");
        scroll->verticalScrollBar()->setValue(scroll->verticalScrollBar()->maximum()); QTest::qWait(60);
        QCOMPARE(server.requests.size(), 2);
        QSignalSpy navigation(&list, &ReactionUsersList::profileRequested);
        QTest::mouseClick(identity(&list, "last"), Qt::LeftButton);
        QCOMPARE(navigation.count(), 1); QCOMPARE(navigation.first().first().toString(), "last");
    }
    void discardsLateResponsesAndClearsOnLogout() {
        Server server;
        server.handler = [](Request request) {
            const bool old = request.url.path().contains("/100/");
            auto response = users({user(old ? "stale" : "current")}); response.delay = old ? 150 : 0;
            return response;
        };
        NorthApi api(nullptr, server.base()); api.usePersonalToken("token");
        ReactionUsersList list(&api, ReactionUsersList::Reposts); list.setPost("100"); list.show();
        QTRY_COMPARE(server.requests.size(), 1);
        list.setPost("200"); QTRY_VERIFY(identity(&list, "current"));
        QTest::qWait(200); QVERIFY(!identity(&list, "stale"));
        api.logout();
        QTRY_VERIFY(list.findChildren<QFrame *>("reactionUserRow").isEmpty());
        list.hide(); list.show(); QTest::qWait(30); QCOMPARE(server.requests.size(), 2);
    }
    void emptyMalformedErrorAndRetry() {
        Server server; int calls = 0;
        server.handler = [&](Request) {
            ++calls;
            if (calls == 1) return Response{200, {{"data", QJsonObject{}}}};
            if (calls == 2) return Response{403, {{"message", "Denied"}}};
            return users({});
        };
        NorthApi api(nullptr, server.base()); api.usePersonalToken("token");
        ReactionUsersList list(&api, ReactionUsersList::Likes); list.setPost("100"); list.show();
        auto *retry = list.findChild<QPushButton *>("reactionUsersRetry");
        QTRY_VERIFY(retry->isVisible()); QTest::mouseClick(retry, Qt::LeftButton);
        QTRY_COMPARE(calls, 2); QTRY_VERIFY(retry->isVisible());
        // A malformed 2xx response must not keep a retry trapped in the read cache.
        QTest::mouseClick(retry, Qt::LeftButton);
        QTRY_COMPARE(calls, 3); QTRY_VERIFY(retry->isHidden());
        QCOMPARE(list.findChild<QLabel *>("reactionUsersNotice")->text(), QString::fromUtf8("まだいいねされていません"));
    }
    void retryAfterAndPopupClearedOnPostChangeOrLogout() {
        Server server; int calls = 0;
        server.handler = [&](Request request) {
            if (request.url.path().endsWith("/retweets")) return users({});
            if (++calls == 1) return Response{429, {}, {{"Retry-After", "1"}}};
            return users({user("liker")});
        };
        NorthApi api(nullptr, server.base()); api.usePersonalToken("token");
        PostReactions panel(&api); panel.setPost(post()); panel.show();
        auto *likes = panel.findChild<QPushButton *>("showLikeUsers"); QTest::mouseClick(likes, Qt::LeftButton);
        QTRY_VERIFY(panel.findChild<QDialog *>("likeUsersDialog"));
        auto *dialog = panel.findChild<QDialog *>("likeUsersDialog");
        auto *retry = dialog->findChild<QPushButton *>("reactionUsersRetry");
        QTRY_VERIFY(retry->isVisible()); QVERIFY(!retry->isEnabled());
        QTest::mouseClick(retry, Qt::LeftButton); QCOMPARE(calls, 1);
        QTRY_VERIFY_WITH_TIMEOUT(retry->isEnabled(), 1800);
        QTest::mouseClick(retry, Qt::LeftButton); QTRY_VERIFY(identity(dialog, "liker"));
        panel.setPost(post("200")); QTRY_VERIFY(!panel.findChild<QDialog *>("likeUsersDialog"));
        QTest::mouseClick(likes, Qt::LeftButton); QTRY_VERIFY(panel.findChild<QDialog *>("likeUsersDialog"));
        api.logout(); QTRY_VERIFY(!panel.findChild<QDialog *>("likeUsersDialog")); QVERIFY(panel.isHidden());
    }
    void unavailablePostsNeverRequestUsers() {
        Server server; NorthApi api(nullptr, server.base()); api.usePersonalToken("token");
        PostReactions panel(&api); panel.show();
        for (const auto &key : {"deleted", "unavailable", "hiddenReason"}) {
            auto item = post(); item.insert(key, key == QString("hiddenReason") ? QJsonValue("MUTED") : QJsonValue(true));
            panel.setPost(item); QVERIFY(panel.isHidden());
        }
        QTest::qWait(30); QCOMPARE(server.requests.size(), 0);
    }
};
QTEST_MAIN(ReactionTests)
#include "tst_reactions.moc"
