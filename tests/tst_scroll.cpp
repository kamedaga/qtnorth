#include "scrollbehavior.h"
#include "messagespage.h"
#include "northapi.h"
#include "testserver.h"
#include <QApplication>
#include <QLabel>
#include <QListWidget>
#include <QScrollArea>
#include <QScrollBar>
#include <QVBoxLayout>
#include <QWheelEvent>
#include <QtTest>

namespace {
void wheel(QWidget *target, int angle, int pixels = 0)
{
    const QPoint local(40, 40);
    QWheelEvent event(local, target->mapToGlobal(local), QPoint(0, pixels), QPoint(0, angle),
                      Qt::NoButton, Qt::NoModifier, Qt::NoScrollPhase, false);
    QApplication::sendEvent(target, &event);
}
struct ScrollFixture {
    QScrollArea area;
    QWidget *content = new QWidget;
    explicit ScrollFixture(int height = 3000) {
        area.resize(400, 300);
        content->setFixedSize(300, height);
        area.setWidget(content);
        area.show();
    }
    QScrollBar *bar() { return area.verticalScrollBar(); }
};
QLabel *visibleLabel(QWidget &page, const QString &text) {
    for (auto *label : page.findChildren<QLabel *>()) if (label->isVisible() && label->text() == text) return label;
    return nullptr;
}
}

class ScrollTests : public QObject {
    Q_OBJECT
private slots:
    void profileTabsOnlyChangeForHorizontalWheelInput() {
        QScrollArea area; area.setWidgetResizable(true); area.resize(600, 320);
        auto *canvas = new QWidget; auto *layout = new QVBoxLayout(canvas);
        auto *tabs = new HorizontalWheelTabBar;
        tabs->addTab("Posts"); tabs->addTab("Following"); tabs->addTab("Followers"); layout->addWidget(tabs);
        auto *content = new QWidget; content->setMinimumHeight(2000); layout->addWidget(content);
        area.setWidget(canvas); SmoothScroll smooth(&area); area.show();
        QVERIFY(QTest::qWaitForWindowExposed(&area));
        QTRY_VERIFY(area.verticalScrollBar()->maximum() > 0);
        auto send = [tabs](QPoint angle, QPoint pixels = {}, Qt::KeyboardModifiers modifiers = Qt::NoModifier, Qt::ScrollPhase phase = Qt::NoScrollPhase) {
            const auto local = tabs->rect().center();
            QWheelEvent event(local, tabs->mapToGlobal(local), pixels, angle, Qt::NoButton, modifiers, phase, false);
            QApplication::sendEvent(tabs, &event);
        };
        QSignalSpy changed(tabs, &QTabBar::currentChanged);
        send({0, -120}); QCOMPARE(tabs->currentIndex(), 0);
        QTRY_VERIFY(area.verticalScrollBar()->value() > 0);
        send({0, -120}, {}, Qt::ShiftModifier); QCOMPARE(tabs->currentIndex(), 0);
        smooth.stop(); area.verticalScrollBar()->setValue(0);
        send({-120, 0}); QCOMPARE(tabs->currentIndex(), 1);
        send({-120, 0}); QCOMPARE(tabs->currentIndex(), 2);
        send({-120, 0}); QCOMPARE(tabs->currentIndex(), 2);
        send({120, 0}); QCOMPARE(tabs->currentIndex(), 1);
        send({-20, -120}); QCOMPARE(tabs->currentIndex(), 1);
        send({-40, 0}); send({-40, 0}); QCOMPARE(tabs->currentIndex(), 1);
        send({-40, 0}); QCOMPARE(tabs->currentIndex(), 2);
        send({}, {30, 0}, Qt::NoModifier, Qt::ScrollBegin); QCOMPARE(tabs->currentIndex(), 2);
        send({}, {30, 0}, Qt::NoModifier, Qt::ScrollUpdate); QCOMPARE(tabs->currentIndex(), 1);
        send({}, {120, 0}, Qt::NoModifier, Qt::ScrollMomentum); QCOMPARE(tabs->currentIndex(), 1);
        send({}, {}, Qt::NoModifier, Qt::ScrollEnd);
        QTest::mouseClick(tabs, Qt::LeftButton, {}, tabs->tabRect(0).center()); QCOMPARE(tabs->currentIndex(), 0);
        send({-120, 0}, {-15, 0}); QCOMPARE(tabs->currentIndex(), 1);
        send({120, 0}, {15, 0}); QCOMPARE(tabs->currentIndex(), 0);
        QCOMPARE(changed.count(), 8);
    }
    void wheelAnimationAndPixelInput() {
        ScrollFixture f;
        SmoothScroll smooth(&f.area);
        QVERIFY(QTest::qWaitForWindowExposed(&f.area));
        const int step = std::max(1, QApplication::wheelScrollLines()) * 20;
        wheel(f.area.viewport(), -120);
        QCOMPARE(f.bar()->value(), 0);
        QTest::qWait(45);
        QVERIFY(f.bar()->value() > 0);
        QVERIFY(f.bar()->value() < step);
        QTRY_COMPARE(f.bar()->value(), step);
        wheel(f.area.viewport(), -120);
        wheel(f.area.viewport(), -120);
        QTRY_COMPARE(f.bar()->value(), step * 3);
        wheel(f.area.viewport(), -120);
        QTest::qWait(30);
        const int reverseAt = f.bar()->value();
        wheel(f.area.viewport(), 120);
        QTRY_COMPARE(f.bar()->value(), reverseAt - step);
        wheel(f.area.viewport(), -120);
        wheel(f.area.viewport(), 0, -17);
        const int pixelPosition = f.bar()->value();
        QTest::qWait(200);
        QCOMPARE(f.bar()->value(), pixelPosition);
        QCOMPARE(pixelPosition, reverseAt - step + 17);
    }
    void draggingScrollBarStopsAnimation() {
        ScrollFixture f;
        SmoothScroll smooth(&f.area);
        QVERIFY(QTest::qWaitForWindowExposed(&f.area));
        wheel(f.area.viewport(), -360);
        f.bar()->setSliderDown(true);
        f.bar()->setValue(500);
        f.bar()->setSliderDown(false);
        QTest::qWait(220);
        QCOMPARE(f.bar()->value(), 500);
    }
    void appendedRowsKeepTheVisibleAnchorAndWheelMomentum() {
        QScrollArea area; area.setWidgetResizable(true); area.resize(450, 300);
        auto *canvas = new QWidget; auto *layout = new QVBoxLayout(canvas);
        layout->setContentsMargins(0, 0, 0, 0); layout->setSpacing(0);
        auto *header = new QWidget; header->setFixedHeight(64); layout->addWidget(header);
        QList<QWidget *> rows;
        auto append = [&] { auto *row = new QWidget; row->setFixedHeight(100); row->setProperty("scrollAnchor", true); layout->addWidget(row); rows.append(row); };
        for (int i = 0; i < 20; ++i) append();
        area.setWidget(canvas); SmoothScroll smooth(&area); area.show();
        QVERIFY(QTest::qWaitForWindowExposed(&area));
        auto *bar = area.verticalScrollBar(); QTRY_VERIFY(bar->maximum() > 1500); bar->setValue(650);
        const int before = rows[6]->mapTo(area.viewport(), QPoint()).y(); const int maximum = bar->maximum();
        {
            const ScrollAnchorGuard keep(&area);
            header->setFixedHeight(79);
            for (int i = 0; i < 15; ++i) append();
        }
        QTRY_VERIFY(bar->maximum() > maximum + 1400); QTest::qWait(30);
        QCOMPARE(rows[6]->mapTo(area.viewport(), QPoint()).y(), before);
        QCOMPARE(bar->value(), 665);
        QVERIFY(area.viewport()->updatesEnabled());
        const int step = std::max(1, QApplication::wheelScrollLines()) * 20;
        wheel(area.viewport(), -120); QTest::qWait(30);
        {
            const ScrollAnchorGuard keep(&area); header->setFixedHeight(94); append();
        }
        QTest::qWait(220);
        QCOMPARE(bar->value(), 665 + step + 15);
        QCOMPARE(rows[6]->mapTo(area.viewport(), QPoint()).y(), before - step);
    }
    void bottomLoadingIsSingleFlightAndStopsAtEnd() {
        ScrollFixture f;
        EdgeLoader loader(&f.area);
        QSignalSpy more(&loader, &EdgeLoader::loadMoreRequested);
        QVERIFY(QTest::qWaitForWindowExposed(&f.area));
        loader.setCursor("first");
        QTest::qWait(35);
        QCOMPARE(more.count(), 0);
        f.bar()->setValue(f.bar()->maximum());
        QTRY_COMPARE(more.count(), 1);
        f.bar()->setValue(f.bar()->maximum() - 1);
        f.bar()->setValue(f.bar()->maximum());
        QTest::qWait(40);
        QCOMPARE(more.count(), 1);
        f.content->setFixedHeight(4000);
        loader.setCursor("second");
        QTest::qWait(40);
        QCOMPARE(more.count(), 1);
        f.bar()->setValue(f.bar()->maximum());
        QTRY_COMPARE(more.count(), 2);
        loader.setCursor("second"); // A repeated server cursor must not loop.
        wheel(f.area.viewport(), -120);
        QTest::qWait(40);
        QCOMPARE(more.count(), 2);
        loader.setCursor({});
        wheel(f.area.viewport(), -120);
        QTest::qWait(30);
        QCOMPARE(more.count(), 2);
    }
    void failuresWaitForUserAndHonorRetryAfter() {
        ScrollFixture f;
        EdgeLoader loader(&f.area);
        QSignalSpy more(&loader, &EdgeLoader::loadMoreRequested);
        QVERIFY(QTest::qWaitForWindowExposed(&f.area));
        f.bar()->setValue(f.bar()->maximum());
        loader.setCursor("retry");
        QTRY_COMPARE(more.count(), 1);
        loader.fail();
        f.content->setFixedHeight(2999);
        QTest::qWait(60);
        QCOMPARE(more.count(), 1);
        wheel(f.area.viewport(), -120);
        QTRY_COMPARE(more.count(), 2);
        loader.fail(1);
        wheel(f.area.viewport(), -120);
        QTest::qWait(50);
        QCOMPARE(more.count(), 2);
        QTest::qWait(1000);
        QCOMPARE(more.count(), 2);
        wheel(f.area.viewport(), -120);
        QTRY_COMPARE(more.count(), 3);
    }
    void underfilledPagesAreBoundedAndHiddenPagesDoNotLoad() {
        ScrollFixture f(20);
        EdgeLoader loader(&f.area);
        QSignalSpy more(&loader, &EdgeLoader::loadMoreRequested);
        connect(&loader, &EdgeLoader::loadMoreRequested, &loader, [&] { loader.setCursor(QString::number(more.count())); });
        QVERIFY(QTest::qWaitForWindowExposed(&f.area));
        loader.setCursor("start");
        QTRY_COMPARE(more.count(), 5);
        QTest::qWait(100);
        QCOMPARE(more.count(), 5);
        wheel(f.area.viewport(), -120);
        QTRY_COMPARE(more.count(), 10);
        f.area.hide(); loader.reset(); loader.setCursor("hidden");
        QTest::qWait(50);
        QCOMPARE(more.count(), 10);
    }
    void olderMessagesKeepTheirVisiblePosition() {
        Server server;
        const QJsonObject user{{"id", "me"}, {"name", "Me"}, {"handle", "me"}};
        const QJsonObject conversation{{"id", "chat"}, {"name", "Haru"}, {"participants", QJsonArray{user}}};
        int historyRequests = 0;
        server.handler = [&](Request request) {
            QJsonObject data;
            if (request.url.path() == "/2/dm/conversations") {
                data.insert("items", QJsonArray{conversation});
            } else {
                ++historyRequests;
                const bool older = QUrlQuery(request.url).hasQueryItem("cursor");
                QJsonArray messages;
                for (int i = 0; i < 20; ++i) messages.append(QJsonObject{
                    {"id", (older ? "old-" : "new-") + QString::number(i)},
                    {"text", (older ? "older " : "newer ") + QString::number(i)},
                    {"createdAt", QDateTime::fromString("2026-10-27T12:00:00Z", Qt::ISODate).addSecs((older ? -1200 : 0) + i).toString(Qt::ISODate)},
                    {"sender", QJsonObject{{"id", "other"}}}
                });
                data.insert("items", messages);
                data.insert("nextCursor", older ? QJsonValue(QJsonValue::Null) : QJsonValue("older+/="));
            }
            return Response{200, {{"data", data}}};
        };
        NorthApi api(nullptr, server.base()); api.usePersonalToken("token");
        MessagesPage page(&api); page.resize(850, 500); page.setSession(user); page.show(); page.reload();
        auto *list = page.findChild<QListWidget *>("conversationList");
        QTRY_COMPARE(list->count(), 1); list->setCurrentRow(0);
        auto *history = page.findChild<QScrollArea *>("messageHistory");
        auto *bar = history->verticalScrollBar();
        QTRY_VERIFY(visibleLabel(page, "newer 0"));
        QTRY_VERIFY(bar->maximum() > 200);
        QTRY_COMPARE(bar->value(), bar->maximum());
        QCOMPARE(historyRequests, 1);
        bar->setValue(0);
        const int anchor = visibleLabel(page, "newer 0")->mapTo(history->viewport(), QPoint()).y();
        QTRY_COMPARE(historyRequests, 2);
        QTRY_VERIFY(visibleLabel(page, "older 0"));
        QTRY_VERIFY(bar->value() > 200);
        const int after = visibleLabel(page, "newer 0")->mapTo(history->viewport(), QPoint()).y();
        QVERIFY2(std::abs(after - anchor) <= 2, qPrintable(QString("anchor moved from %1 to %2").arg(anchor).arg(after)));
        QCOMPARE(QUrlQuery(server.requests.last().url).queryItemValue("cursor", QUrl::FullyDecoded), "older+/=");
    }
};
QTEST_MAIN(ScrollTests)
#include "tst_scroll.moc"
