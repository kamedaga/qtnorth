#include "mainwindow.h"
#include "memorystore.h"
#include "notificationspage.h"
#include "postcard.h"
#include "profilepage.h"
#include "theme.h"
#include "preferences.h"
#include "testserver.h"
#include <QDir>
#include <QLabel>
#include <QLineEdit>
#include <QScrollArea>
#include <QScrollBar>
#include <QScopeGuard>
#include <QSettings>
#include <QStackedWidget>
#include <QTabBar>
#include <QTabWidget>
#include <QTemporaryDir>
#include <QtTest>

namespace {
QJsonObject actor() { return {{"id", "haru"}, {"handle", "haru"}, {"name", "Haru"}}; }
QJsonObject post() {
    return {{"id", "post-1"}, {"text", QString::fromUtf8("Qtでつくる、日々のためのアプリ。")}, {"author", QJsonObject{{"id", "me"}, {"handle", "me"}, {"name", "My account"}}},
        {"createdAt", QDateTime::currentDateTimeUtc().addSecs(-300).toString(Qt::ISODate)}};
}
QJsonObject notification(const QString &id, const QString &kind = "FOLLOW", bool read = false) {
    return {{"id", id}, {"kind", kind}, {"actors", QJsonArray{actor()}}, {"actorCount", 1}, {"groupCount", 1}, {"targetCount", kind == "FOLLOW" ? 0 : 1},
        {"tweet", kind == "FOLLOW" ? QJsonValue(QJsonValue::Null) : QJsonValue(post())}, {"read", read},
        {"createdAt", QDateTime::currentDateTimeUtc().addSecs(-120).toString(Qt::ISODate)}};
}
QJsonObject oauth(const QUrl &origin, const QString &scope) {
    return {{"version", 1}, {"origin", origin.toString()}, {"kind", "oauth"}, {"client_id", "client"}, {"access_token", "test-token"},
        {"refresh_token", "test-refresh"}, {"expires_at", QDateTime::currentDateTimeUtc().addSecs(3600).toString(Qt::ISODateWithMs)}, {"scope", scope}};
}
void capture(QWidget &widget, const QString &name) {
    const auto dir = qEnvironmentVariable("QTNORTH_CAPTURE_DIR");
    if (!dir.isEmpty()) { QTest::qWait(50); QDir().mkpath(dir); widget.grab().save(dir + "/" + name + ".png"); }
}
QList<QFrame *> rows(QWidget &page) { return page.findChildren<QFrame *>("notificationRow"); }
}

class NotificationTests : public QObject {
    Q_OBJECT
    QTemporaryDir m_settings;
private slots:
    void initTestCase() {
        QCoreApplication::setOrganizationName("qtNorth-tests"); QCoreApplication::setApplicationName("notifications");
        QSettings::setDefaultFormat(QSettings::IniFormat);
        QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, m_settings.path());
        ThemeManager::instance()->setMode(ThemeManager::Light);
    }
    void backgroundCountsAreThrottledAndManualRefreshStaysFresh() {
        Server server; int counts = 0;
        server.handler = [&](Request request) {
            if (request.url.path().endsWith("unread_count")) {
                ++counts; return Response{200, {{"data", QJsonObject{{"count", counts}}}}, {}, 50};
            }
            return Response{200, {{"data", QJsonObject{{"items", QJsonArray{}}}}}};
        };
        NorthApi api(nullptr, server.base()); api.usePersonalToken("test-token");
        NotificationsPage page(&api, [&api](const QJsonObject &item) { return new PostCard(item, false, &api); });
        QSignalSpy updates(&page, &NotificationsPage::unreadCountChanged);
        page.startSession(); page.reload();
        for (int i = 0; i < 10; ++i) page.refreshUnreadCount(false);
        QTRY_VERIFY(!updates.isEmpty() && updates.last()[0].toLongLong() == 1);
        QTest::qWait(80); QCOMPARE(counts, 1);
        for (int i = 0; i < 10; ++i) page.refreshUnreadCount(false);
        QTest::qWait(30); QCOMPARE(counts, 1);
        page.refreshUnreadCount();
        QTRY_COMPARE(updates.last()[0].toLongLong(), 2); QCOMPARE(counts, 2);
    }
    void manualPollingStillAllowsLoginAndExplicitRefreshAfterFailure() {
        auto *preferences = Preferences::instance(); const auto previous = preferences->notificationInterval();
        const auto restore = qScopeGuard([=] { preferences->setNotificationInterval(previous); });
        preferences->setNotificationInterval(0);
        Server server; int counts = 0;
        server.handler = [&](Request) {
            ++counts;
            if (counts == 1) return Response{500, {{"error", "temporary"}}};
            return Response{200, {{"data", QJsonObject{{"count", 4}}}}};
        };
        NorthApi api(nullptr, server.base()); api.usePersonalToken("test-token");
        NotificationsPage page(&api, [&api](const QJsonObject &item) { return new PostCard(item, false, &api); });
        QSignalSpy updates(&page, &NotificationsPage::unreadCountChanged);
        page.startSession(); QTRY_COMPARE(counts, 1); QTest::qWait(100);
        for (int i = 0; i < 5; ++i) page.refreshUnreadCount(false);
        QTest::qWait(100); QCOMPARE(counts, 1);
        page.refreshUnreadCount(); QTRY_VERIFY(!updates.isEmpty() && updates.last()[0].toLongLong() == 4);
        QCOMPARE(counts, 2);
        preferences->setNotificationInterval(5); QCOMPARE(preferences->notificationInterval(), 0);
    }
    void badgeNavigationGroupingAndExplicitRead() {
        Server server; qint64 unread = 2; bool read = false; int marked = 0;
        auto grouped = notification("group", "RETWEET", true); grouped.insert("groupCount", 4); grouped.insert("targetCount", 2);
        grouped.insert("tweet", QJsonValue::Null); grouped.insert("actorCount", 3);
        server.handler = [&](Request request) {
            const auto path = request.url.path();
            if (path == "/2/users/me") return Response{200, {{"data", QJsonObject{{"id", "me"}, {"handle", "me"}, {"name", "My account"}}}}};
            if (path == "/2/notifications/unread_count") return Response{200, {{"data", QJsonObject{{"count", unread}}}}};
            if (path == "/2/notifications/read") { ++marked; unread = 0; read = true; return Response{200, {{"data", QJsonObject{{"marked", 2}}}}}; }
            if (path == "/2/notifications") return Response{200, {{"data", QJsonObject{{"items", QJsonArray{
                notification("follow", "FOLLOW", read), notification("like", "LIKE", true), grouped, notification("mention", "MENTION", read)}}}}}};
            if (path.endsWith("/conversation")) return Response{200, {{"data", QJsonObject{{"tweet", post()}, {"ancestors", QJsonArray{}}, {"replies", QJsonArray{}}}}}};
            if (path == "/2/users/haru") return Response{200, {{"data", actor()}}};
            return Response{200, {{"data", QJsonObject{{"items", QJsonArray{}}}}}};
        };
        MemoryStore store; MainWindow window(nullptr, server.base(), &store); window.show();
        QTRY_VERIFY(window.findChild<QTabWidget *>("loginMethods")->isEnabled());
        window.findChild<QTabWidget *>("loginMethods")->setCurrentIndex(1);
        window.findChild<QLineEdit *>("personalToken")->setText("test-token");
        QTest::mouseClick(window.findChild<QPushButton *>("tokenLogin"), Qt::LeftButton);
        auto *badge = window.findChild<QLabel *>("notificationBadge"); QVERIFY(badge);
        QTRY_VERIFY(badge->isVisible()); QCOMPARE(badge->text(), "2"); QCOMPARE(badge->width(), badge->height());
        auto *nav = qobject_cast<QPushButton *>(badge->parentWidget()); QVERIFY(nav);
        QTest::mouseClick(nav, Qt::LeftButton, {}, badge->geometry().center());
        auto *pages = window.findChild<QStackedWidget *>("workspacePages");
        auto *page = window.findChild<NotificationsPage *>(); QCOMPARE(pages->currentWidget(), page);
        QTRY_COMPARE(rows(*page).size(), 4); QCOMPARE(page->findChildren<PostCard *>().size(), 2);
        QCOMPARE(marked, 0); QCOMPARE(badge->text(), "2");
        QVERIFY(window.findChild<QWidget *>("inspector")->isHidden());
        capture(window, "notifications");
        ThemeManager::instance()->setMode(ThemeManager::Dark); capture(window, "notifications-dark");
        ThemeManager::instance()->setMode(ThemeManager::Light);
        auto *card = page->findChild<PostCard *>(); QVERIFY(card);
        QTest::mouseClick(card->findChild<QLabel *>("postText"), Qt::LeftButton, {}, QPoint(12, 8));
        QTRY_COMPARE(pages->currentWidget()->objectName(), "postPage");
        QTest::mouseClick(window.findChild<QPushButton *>("backFromPost"), Qt::LeftButton);
        QCOMPARE(pages->currentWidget(), page);
        QTest::mouseClick(page->findChild<QPushButton *>("authorName"), Qt::LeftButton);
        QTRY_COMPARE(pages->currentWidget()->objectName(), "profilePage");
        QTRY_COMPARE(window.findChild<ProfilePage *>()->handle(), "haru");
        QTest::mouseClick(window.findChild<QPushButton *>("backFromPost"), Qt::LeftButton);
        QCOMPARE(pages->currentWidget(), page);
        QTest::mouseClick(page->findChild<QPushButton *>("markNotificationsRead"), Qt::LeftButton);
        QTRY_COMPARE(marked, 1); QTRY_VERIFY(badge->isHidden());
        for (const auto &request : server.requests) if (request.url.path() == "/2/notifications/read") QVERIFY(!request.headers.value("idempotency-key").isEmpty());
        unread = 105; page->refreshUnreadCount(); QTRY_COMPARE(badge->text(), "99+"); QVERIFY(badge->isVisible());
        QVERIFY(nav->toolTip().contains("105"));
        unread = -1; page->refreshUnreadCount(); QTest::qWait(50); QCOMPARE(badge->text(), "99+");
        unread = 0; page->refreshUnreadCount(); QTRY_VERIFY(badge->isHidden());
        QPushButton *logout = nullptr;
        for (auto *button : window.findChildren<QPushButton *>()) if (button->text() == QString::fromUtf8("ログアウト")) logout = button;
        QVERIFY(logout); QTest::mouseClick(logout, Qt::LeftButton);
        QTRY_VERIFY(rows(*page).isEmpty()); QVERIFY(badge->isHidden());
    }
    void paginationAndFilterChangesDiscardLatePages() {
        Server server; int continuations = 0;
        server.handler = [&](Request request) {
            if (request.url.path().endsWith("unread_count")) return Response{200, {{"data", QJsonObject{{"count", 1}}}}};
            const QUrlQuery query(request.url);
            if (query.queryItemValue("tab") == "mentions") return Response{200, {{"data", QJsonObject{{"items", QJsonArray{notification("current", "MENTION")}}}}}};
            if (query.hasQueryItem("cursor")) { ++continuations; return Response{200, {{"data", QJsonObject{{"items", QJsonArray{notification("late")}}}}}, {}, 150}; }
            QJsonArray items; for (int i = 0; i < 30; ++i) items.append(notification(QString::number(i)));
            return Response{200, {{"data", QJsonObject{{"items", items}, {"nextCursor", "notify+/="}}}}};
        };
        NorthApi api(nullptr, server.base()); api.usePersonalToken("test-token");
        NotificationsPage page(&api, [&api](const QJsonObject &item) { return new PostCard(item, false, &api); });
        page.resize(700, 650); page.show(); page.startSession(); page.reload();
        QTRY_COMPARE(rows(page).size(), 30);
        auto *bar = page.findChild<QScrollArea *>("notificationsScroll")->verticalScrollBar();
        QTRY_VERIFY(bar->maximum() > 0); bar->setValue(bar->maximum()); QTRY_COMPARE(continuations, 1);
        page.findChild<QTabBar *>("notificationTabs")->setCurrentIndex(2);
        QTRY_COMPARE(rows(page).size(), 1); QTest::qWait(180);
        QCOMPARE(rows(page).size(), 1); QCOMPARE(rows(page).first()->property("notificationId").toString(), "current");
        for (const auto &request : server.requests) if (QUrlQuery(request.url).hasQueryItem("cursor"))
            QCOMPARE(QUrlQuery(request.url).queryItemValue("cursor", QUrl::FullyDecoded), "notify+/=");
    }
    void scopesPreventUnauthorizedRequests() {
        for (const bool allowRead : {false, true}) {
            Server server;
            server.handler = [](Request request) {
                if (request.url.path().endsWith("unread_count")) return Response{200, {{"data", QJsonObject{{"count", 0}}}}};
                return Response{200, {{"data", QJsonObject{{"items", QJsonArray{}}}}}};
            };
            NorthApi api(nullptr, server.base());
            QVERIFY(api.restoreSession(oauth(server.base(), allowRead ? "users.read posts.read notifications.read" : "users.read posts.read")));
            NotificationsPage page(&api, [&api](const QJsonObject &item) { return new PostCard(item, false, &api); });
            page.resize(700, 500); page.show(); page.startSession(); page.reload();
            QVERIFY(page.findChild<QPushButton *>("markNotificationsRead")->isHidden());
            if (allowRead) QTRY_VERIFY(!server.requests.isEmpty());
            else { QTest::qWait(30); QCOMPARE(server.requests.size(), 0); }
            for (const auto &request : server.requests) QCOMPARE(request.method, "GET");
            if (!allowRead) {
                QSignalSpy login(&page, &NotificationsPage::loginRequested);
                QPushButton *button = nullptr;
                for (auto *candidate : page.findChildren<QPushButton *>()) if (candidate->text() == QString::fromUtf8("再ログイン")) button = candidate;
                QVERIFY(button); QVERIFY(button->isVisible()); QTest::mouseClick(button, Qt::LeftButton); QCOMPARE(login.count(), 1);
            }
        }
    }
    void readingDoesNotResurrectAnOlderCount() {
        Server server; bool read = false; int countRequests = 0, marked = 0;
        server.handler = [&](Request request) {
            if (request.url.path().endsWith("unread_count")) {
                ++countRequests; return Response{200, {{"data", QJsonObject{{"count", read ? 0 : 5}}}}, {}, read ? 0 : 150};
            }
            if (request.url.path().endsWith("/read")) { read = true; ++marked; return Response{200, {{"data", QJsonObject{{"marked", 5}}}}}; }
            return Response{200, {{"data", QJsonObject{{"items", QJsonArray{notification("one")}}}}}};
        };
        NorthApi api(nullptr, server.base()); api.usePersonalToken("test-token");
        NotificationsPage page(&api, [&api](const QJsonObject &item) { return new PostCard(item, false, &api); });
        page.resize(700, 500); page.show();
        QSignalSpy counts(&page, &NotificationsPage::unreadCountChanged);
        page.startSession(); page.reload();
        auto *mark = page.findChild<QPushButton *>("markNotificationsRead"); QTRY_VERIFY(mark->isEnabled());
        QTest::mouseClick(mark, Qt::LeftButton); QTRY_COMPARE(marked, 1);
        QTRY_VERIFY(countRequests >= 2); QTest::qWait(180);
        QVERIFY(!counts.isEmpty());
        for (const auto &values : counts) QCOMPARE(values[0].toLongLong(), 0);
    }
    void logoutDropsAnotherAccountsLateCount() {
        Server server;
        server.handler = [](Request request) {
            const bool old = request.headers.value("authorization") == "Bearer old";
            return Response{200, {{"data", QJsonObject{{"count", old ? 99 : 3}}}}, {}, old ? 150 : 0};
        };
        NorthApi api(nullptr, server.base()); api.usePersonalToken("old");
        NotificationsPage page(&api, [&api](const QJsonObject &item) { return new PostCard(item, false, &api); });
        QSignalSpy counts(&page, &NotificationsPage::unreadCountChanged);
        page.startSession(); QTRY_COMPARE(server.requests.size(), 1);
        api.usePersonalToken("new"); page.startSession();
        QTRY_VERIFY(!counts.isEmpty() && counts.last()[0].toLongLong() == 3);
        QTest::qWait(180);
        for (const auto &values : counts) QVERIFY(values[0].toLongLong() != 99);
        api.logout(); QCOMPARE(counts.last()[0].toLongLong(), 0);
    }
};
QTEST_MAIN(NotificationTests)
#include "tst_notifications.moc"
