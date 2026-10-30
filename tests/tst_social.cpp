#include "northapi.h"
#include "pollwidget.h"
#include "postcard.h"
#include "userrelations.h"
#include "testserver.h"
#include <QDateTime>
#include <QJsonArray>
#include <QLabel>
#include <QProgressBar>
#include <QRadioButton>
#include <QVBoxLayout>
#include <QtTest>

namespace {
QJsonObject poll(bool voted = false, bool ended = false) {
    return {{"id", "poll"}, {"endsAt", QDateTime::currentDateTimeUtc().addSecs(3600).toString(Qt::ISODate)},
        {"ended", ended}, {"totalVotes", voted ? 1 : 0}, {"viewerOptionId", voted ? QJsonValue("a") : QJsonValue(QJsonValue::Null)},
        {"options", QJsonArray{QJsonObject{{"id", "a"}, {"label", "寿司"}, {"voteCount", voted ? 1 : 0}, {"percent", voted ? 100 : 0}},
            QJsonObject{{"id", "b"}, {"label", "天ぷら"}, {"voteCount", 0}, {"percent", 0}}}}};
}
QJsonObject user(bool following = false, bool isPrivate = false) {
    return {{"id", "other-id"}, {"handle", "other"}, {"name", "Other"}, {"following", following},
        {"protected", isPrivate}, {"followerCount", following ? 11 : 10}};
}
QJsonObject post(bool voted = false) { return {{"id", "1"}, {"text", "どちらが好き？"}, {"author", user()}, {"poll", poll(voted)}}; }
QList<QLabel *> visibleResults(QWidget &widget) {
    QList<QLabel *> list; for (auto *label : widget.findChildren<QLabel *>("pollResultLabel")) if (label->isVisible()) list.append(label); return list;
}
void viewer(UserRelations *relations) { relations->setViewer({{"id", "me"}, {"handle", "me"}, {"followingCount", 3}}); }
}

class SocialTests : public QObject {
    Q_OBJECT
private slots:
    void votingUpdatesEveryCopyAndNeverOpensThePost() {
        Server server;
        server.handler = [](Request request) {
            if (request.method == "POST" && request.url.path() == "/2/tweets/1/poll/vote") return Response{200, {{"data", post(true)}}, {}, 60};
            return Response{404, {}};
        };
        NorthApi api(nullptr, server.base()); api.usePersonalToken("test-token");
        QWidget window; auto *layout = new QVBoxLayout(&window);
        auto *first = new PostCard(post(), true, &api); auto *second = new PostCard(post(), true, &api);
        layout->addWidget(first); layout->addWidget(second); window.resize(600, 750); window.show();
        QSignalSpy selected(first, &PostCard::selected);
        auto *vote = first->findChild<QPushButton *>("pollVote"); QVERIFY(vote); QVERIFY(!vote->isEnabled());
        QCOMPARE(server.requests.size(), 0);
        QTest::mouseClick(first->findChildren<QRadioButton *>("pollOption").first(), Qt::LeftButton);
        QVERIFY(vote->isEnabled()); QTest::mouseClick(vote, Qt::LeftButton);
        QVERIFY(api.pollBusy("1"));
        api.votePoll("1", "b"); // A second copy must not start a competing vote.
        QTRY_COMPARE(visibleResults(*second).size(), 2);
        QCOMPARE(server.requests.size(), 1); QCOMPARE(server.requests.first().body.value("optionId").toString(), "a");
        QVERIFY(!server.requests.first().headers.value("idempotency-key").isEmpty());
        QCOMPARE(second->post().value("poll").toObject().value("viewerOptionId").toString(), "a");
        QCOMPARE(selected.count(), 0);
    }
    void voteRetryKeepsTheSameOperationKey() {
        Server server; int votes = 0;
        server.handler = [&](Request) { return ++votes == 1 ? Response{500, {{"error", "temporary"}}} : Response{200, {{"data", post(true)}}}; };
        NorthApi api(nullptr, server.base()); api.usePersonalToken("test-token");
        QSignalSpy failed(&api, &NorthApi::pollFailed), changed(&api, &NorthApi::pollUpdated);
        api.votePoll("1", "a"); QTRY_COMPARE(failed.count(), 1);
        api.votePoll("1", "a"); QTRY_COMPARE(changed.count(), 1);
        QCOMPARE(server.requests.size(), 2);
        QCOMPARE(server.requests[0].headers.value("idempotency-key"), server.requests[1].headers.value("idempotency-key"));
    }
    void endingPollFetchesFinalResultsOnce() {
        Server server;
        auto ending = poll(); ending.insert("endsAt", QDateTime::currentDateTimeUtc().addMSecs(250).toString(Qt::ISODateWithMs));
        auto final = post(); auto state = poll(false, true); state.insert("totalVotes", 7); final.insert("poll", state);
        server.handler = [final](Request) { return Response{200, {{"data", final}}, {}, 80}; };
        NorthApi api(nullptr, server.base()); api.usePersonalToken("test-token");
        QWidget window; auto *layout = new QVBoxLayout(&window);
        auto *first = new PollWidget("1", ending, true, &api); auto *second = new PollWidget("1", ending, true, &api);
        layout->addWidget(first); layout->addWidget(second); window.show();
        QTRY_COMPARE(visibleResults(*first).size(), 2); QTRY_COMPARE(visibleResults(*second).size(), 2);
        QCOMPARE(server.requests.size(), 1); QCOMPARE(server.requests.first().method, "GET");
        QVERIFY(first->findChild<QLabel *>("pollStatus")->text().contains("7票"));
    }
    void followingAndUnfollowingAreSharedAndVerified() {
        Server server; bool following = false;
        server.handler = [&](Request r) {
            if (r.url.path().endsWith("/follow")) { following = r.method == "POST"; return Response{200, {{"data", QJsonObject{{"ok", true}}}}, {}, 40}; }
            return Response{200, {{"data", user(following)}}};
        };
        NorthApi api(nullptr, server.base()); api.usePersonalToken("test-token"); auto *relations = UserRelations::forApi(&api); viewer(relations);
        QWidget window; auto *layout = new QVBoxLayout(&window);
        auto *first = new FollowButton(user(), &api, {}, &window); auto *second = new FollowButton(user(), &api, {}, &window);
        layout->addWidget(first); layout->addWidget(second); window.show();
        QCOMPARE(server.requests.size(), 0);
        QTest::mouseClick(first, Qt::LeftButton); QVERIFY(!second->isEnabled()); relations->toggle("other");
        QTRY_VERIFY(second->isEnabled() && second->text() == "フォロー解除"); QCOMPARE(server.requests.size(), 2);
        QCOMPARE(relations->user("me").value("followingCount").toInt(), 4);
        QVERIFY(!server.requests.first().headers.value("idempotency-key").isEmpty());
        QTest::mouseClick(second, Qt::LeftButton);
        QTRY_VERIFY(first->isEnabled() && first->text() == "フォロー"); QCOMPARE(server.requests.size(), 4);
        QCOMPARE(server.requests[2].method, "DELETE"); QCOMPARE(relations->user("me").value("followingCount").toInt(), 3);
        // A stale post snapshot cannot revert the acknowledged relationship.
        relations->observe(user(true)); QCOMPARE(first->text(), "フォロー");
    }
    void privateFollowIsARequestAndCanBeCancelled() {
        Server server;
        server.handler = [](Request r) { return r.url.path().endsWith("/follow") ? Response{200, {{"data", QJsonObject{{"ok", true}}}}} : Response{200, {{"data", user(false, true)}}}; };
        NorthApi api(nullptr, server.base()); api.usePersonalToken("test-token"); auto *relations = UserRelations::forApi(&api); viewer(relations);
        QWidget window; auto *layout = new QVBoxLayout(&window); auto *button = new FollowButton(user(false, true), &api, {}, &window); layout->addWidget(button); window.show();
        QTest::mouseClick(button, Qt::LeftButton);
        QTRY_VERIFY(button->isEnabled() && button->text() == "申請を取り消す");
        QVERIFY(!relations->user("other").value("following").toBool());
        QTest::mouseClick(button, Qt::LeftButton); QTRY_VERIFY(button->isEnabled() && button->text() == "フォロー申請");
        QCOMPARE(server.requests[2].method, "DELETE");
    }
    void uncertainFollowConfirmationRetriesOnlyTheRead() {
        Server server; int reads = 0;
        server.handler = [&](Request r) {
            if (r.method == "POST") return Response{200, {{"data", QJsonObject{{"ok", true}}}}};
            return ++reads == 1 ? Response{500, {{"error", "temporary"}}} : Response{200, {{"data", user(true)}}};
        };
        NorthApi api(nullptr, server.base()); api.usePersonalToken("test-token"); auto *relations = UserRelations::forApi(&api); viewer(relations);
        QWidget window; auto *layout = new QVBoxLayout(&window); auto *button = new FollowButton(user(), &api, {}, &window); layout->addWidget(button); window.show();
        QTest::mouseClick(button, Qt::LeftButton); QTRY_VERIFY(button->isEnabled() && button->text() == "状態を確認");
        QTest::mouseClick(button, Qt::LeftButton); QTRY_VERIFY(button->isEnabled() && button->text() == "フォロー解除");
        QCOMPARE(server.requests.size(), 3); QCOMPARE(server.requests.last().method, "GET");
    }
    void logoutDiscardsLateRelationshipAndPollResponses() {
        Server server; server.handler = [](Request r) { return r.url.path().endsWith("/vote") ? Response{200, {{"data", post(true)}}, {}, 150} : Response{200, {{"data", QJsonObject{{"ok", true}}}}, {}, 150}; };
        NorthApi api(nullptr, server.base()); api.usePersonalToken("first"); auto *relations = UserRelations::forApi(&api); viewer(relations); relations->observe(user());
        QSignalSpy polls(&api, &NorthApi::pollUpdated);
        relations->toggle("other"); api.votePoll("1", "a"); QTRY_COMPARE(server.requests.size(), 2);
        api.usePersonalToken("second"); QTest::qWait(200);
        QVERIFY(relations->user("other").isEmpty()); QCOMPARE(polls.count(), 0); QVERIFY(!api.pollBusy("1"));
    }
    void readOnlyOAuthDoesNotOfferWritesOrFollowingOnSelf() {
        Server server; NorthApi api(nullptr, server.base());
        QVERIFY(api.restoreSession({{"version", 1}, {"origin", server.base().toString()}, {"kind", "oauth"}, {"client_id", "client"},
            {"access_token", "test-token"}, {"refresh_token", "refresh"}, {"expires_at", QDateTime::currentDateTimeUtc().addSecs(3600).toString(Qt::ISODateWithMs)}, {"scope", "users.read posts.read"}}));
        auto *relations = UserRelations::forApi(&api); viewer(relations);
        QWidget window; auto *layout = new QVBoxLayout(&window);
        auto *other = new FollowButton(user(), &api, {}, &window); layout->addWidget(other);
        auto me = user(); me.insert("id", "me"); me.insert("handle", "me"); auto *self = new FollowButton(me, &api, {}, &window); layout->addWidget(self);
        auto *widget = new PollWidget("1", poll(), true, &api); layout->addWidget(widget); window.show();
        QVERIFY(!other->isEnabled()); QVERIFY(!self->isVisible());
        for (auto *option : widget->findChildren<QRadioButton *>("pollOption")) QVERIFY(!option->isEnabled());
        api.votePoll("1", "a"); relations->toggle("other"); QTest::qWait(30); QCOMPARE(server.requests.size(), 0);
    }
};
QTEST_MAIN(SocialTests)
#include "tst_social.moc"
