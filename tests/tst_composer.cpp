#include "postcomposer.h"
#include "northapi.h"
#include "post.h"
#include "postformat.h"
#include "remoteimage.h"
#include "testserver.h"
#include <QBuffer>
#include <QFile>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QTemporaryDir>
#include <QTextList>
#include <QtTest>

namespace {
QByteArray imageBytes() {
    QImage image(320, 180, QImage::Format_RGB32); image.fill(Qt::blue);
    QByteArray bytes; QBuffer buffer(&bytes); buffer.open(QIODevice::WriteOnly); image.save(&buffer, "PNG"); return bytes;
}
QString writeImage(QTemporaryDir &directory, const QString &name = "photo.png") {
    const auto path = directory.filePath(name); QFile file(path); if (!file.open(QIODevice::WriteOnly)) return {};
    file.write(imageBytes()); return path;
}
QPushButton *button(QWidget &widget, const char *name) { return widget.findChild<QPushButton *>(name); }
QJsonObject oauthSession(const Server &server, const QString &scopes) {
    return {{"version", 1}, {"origin", server.base().toString()}, {"kind", "oauth"}, {"client_id", "client"},
        {"access_token", "test-access"}, {"refresh_token", "test-refresh"}, {"scope", scopes},
        {"expires_at", QDateTime::currentDateTimeUtc().addSecs(3000).toString(Qt::ISODateWithMs)}};
}
}

class ComposerTests : public QObject {
    Q_OBJECT
private slots:
    void inlineEditorStartsAtOneLineAndGrowsWithoutAnAvatar() {
        NorthApi api; api.usePersonalToken("test-token"); PostComposer composer(&api, true);
        composer.setUser({{"id", "me"}, {"name", "Me"}, {"avatarUrl", "https://north.rip/media/avatar.jpg"}});
        composer.resize(600, 200); composer.show(); QVERIFY(QTest::qWaitForWindowExposed(&composer));
        auto *editor = composer.editor();
        QTRY_VERIFY(editor->height() <= editor->fontMetrics().lineSpacing() + 12);
        const int oneLine = editor->height();
        QVERIFY(composer.findChildren<RemoteImage *>().isEmpty());
        editor->setPlainText("one\ntwo\nthree"); QTRY_VERIFY(editor->height() >= oneLine + 2 * editor->fontMetrics().lineSpacing());
        editor->clear(); QTRY_COMPARE(editor->height(), oneLine);
    }
    void formattingSerializesNorthTagsAndReadableBullets() {
        ComposerEditor editor; editor.setPlainText("hello world");
        auto cursor = editor.textCursor(); cursor.setPosition(0); cursor.setPosition(5, QTextCursor::KeepAnchor); editor.setTextCursor(cursor); editor.toggleBold();
        QCOMPARE(editor.northText(), QStringLiteral("<太字>hello</太字> world"));
        // north's input limit applies to the serialized formatting tags too.
        QCOMPARE(North::textWeight(editor.northText()), 24);
        editor.selectAll(); editor.toggleItalic();
        const auto runs = North::postTextRuns(editor.northText()); QCOMPARE(runs.size(), 2);
        QVERIFY(runs[0].style.bold && runs[0].style.italic); QVERIFY(!runs[1].style.bold && runs[1].style.italic);
        ComposerEditor list; list.setPlainText("one\ntwo"); list.selectAll(); list.toggleList();
        QCOMPARE(list.northText(), QStringLiteral("• one\n• two"));
        list.toggleList(); QCOMPARE(list.northText(), QStringLiteral("one\ntwo"));
    }
    void formattingShortcutsWorkInTheEditor() {
        NorthApi api; api.usePersonalToken("test-token"); PostComposer composer(&api, true); composer.resize(600, 400); composer.show();
        QVERIFY(QTest::qWaitForWindowExposed(&composer)); composer.editor()->setFocus(); composer.editor()->setPlainText("hello"); composer.editor()->selectAll();
        QTest::keyClick(composer.editor(), Qt::Key_B, Qt::ControlModifier);
        QCOMPARE(composer.editor()->northText(), QStringLiteral("<太字>hello</太字>"));
        QTest::keyClick(composer.editor(), Qt::Key_I, Qt::ControlModifier);
        QCOMPARE(composer.editor()->northText(), QStringLiteral("<太字><斜体>hello</斜体></太字>"));
    }
    void attachmentsUploadOnceAndRetryTheIdenticalPost() {
        Server server; QList<Request> uploads, posts;
        server.handler = [&](Request request) {
            if (request.url.path() == "/2/media/upload") { uploads.append(request); return Response{201, {{"data", QJsonObject{{"id", "media-1"}}}}, {}, 40}; }
            posts.append(request);
            return posts.size() == 1 ? Response{500, {{"error", "temporary"}}, {}, 40} : Response{201, {{"data", QJsonObject{{"id", "post-1"}}}}};
        };
        NorthApi api(nullptr, server.base()); api.usePersonalToken("test-token");
        PostComposer composer(&api, true); composer.resize(600, 400); composer.show(); QSignalSpy posted(&composer, &PostComposer::posted);
        QTemporaryDir directory; QVERIFY(composer.addImages({writeImage(directory)}));
        auto *send = button(composer, "composeSend"); QVERIFY(send->isEnabled()); // Images alone are valid.
        QVERIFY(!button(composer, "composePoll")->isEnabled());
        QTest::mouseClick(send, Qt::LeftButton); QTest::mouseClick(send, Qt::LeftButton);
        QTRY_COMPARE(posts.size(), 1); QTRY_VERIFY(send->isEnabled()); QCOMPARE(uploads.size(), 1);
        const auto upload = uploads.first();
        QCOMPARE(upload.headers.value("authorization"), "Bearer test-token");
        QCOMPARE(upload.headers.value("x-north-media-purpose"), "posts");
        QVERIFY(upload.headers.value("content-type").startsWith("multipart/form-data; boundary="));
        QVERIFY(!upload.headers.contains("idempotency-key"));
        QVERIFY(upload.rawBody.contains("name=\"file\"")); QVERIFY(upload.rawBody.contains(imageBytes()));
        QCOMPARE(posts.first().body.value("media").toObject().value("media_ids").toArray(), QJsonArray{"media-1"});
        QCOMPARE(posts.first().body.value("text").toString(), QString());
        QTest::mouseClick(send, Qt::LeftButton); QTRY_COMPARE(posted.count(), 1);
        QCOMPARE(uploads.size(), 1); QCOMPARE(posts.size(), 2);
        QCOMPARE(posts[0].headers.value("idempotency-key"), posts[1].headers.value("idempotency-key"));
        QVERIFY(!posts[0].headers.value("idempotency-key").isEmpty()); QVERIFY(!send->isEnabled());
        QVERIFY(composer.editor()->toPlainText().isEmpty()); QVERIFY(composer.findChildren<QFrame *>("composeImage").isEmpty());
    }
    void failedUploadPreservesDraftAndNeverPostsAutomatically() {
        Server server; int uploads = 0, posts = 0;
        server.handler = [&](Request request) {
            if (request.url.path() == "/2/media/upload") { ++uploads; return Response{503, {{"error", "temporary"}}, {{"Retry-After", "1"}}, 20}; }
            ++posts; return Response{201, {{"data", QJsonObject{{"id", "unexpected"}}}}};
        };
        NorthApi api(nullptr, server.base()); api.usePersonalToken("test-token");
        PostComposer composer(&api, true); composer.resize(600, 400); composer.show(); composer.editor()->setPlainText("keep this draft");
        QTemporaryDir directory; QVERIFY(composer.addImages({writeImage(directory)}));
        QTest::mouseClick(button(composer, "composeSend"), Qt::LeftButton); QTRY_VERIFY(uploads == 1 && !composer.busy());
        QCOMPARE(posts, 0); QCOMPARE(composer.editor()->toPlainText(), "keep this draft");
        QCOMPARE(composer.findChildren<QFrame *>("composeImage").size(), 1); QTest::qWait(100); QCOMPARE(uploads, 1);
    }
    void pendingImageIsReadyBeforeThePostIsSent() {
        Server server; QStringList operations;
        server.handler = [&](Request request) {
            operations.append(QString::fromLatin1(request.method) + " " + request.url.path());
            if (request.url.path() == "/2/media/upload") return Response{201, {{"data", QJsonObject{{"id", "media-1"}, {"status", "PENDING"}}}}};
            if (request.url.path() == "/2/media/media-1") return Response{200, {{"data", QJsonObject{{"id", "media-1"}, {"status", "READY"}}}}};
            return Response{201, {{"data", QJsonObject{{"id", "created"}}}}};
        };
        NorthApi api(nullptr, server.base()); api.usePersonalToken("test-token"); PostComposer composer(&api, true); composer.resize(600, 400); composer.show();
        QTemporaryDir directory; QVERIFY(composer.addImages({writeImage(directory)})); QSignalSpy posted(&composer, &PostComposer::posted);
        QTest::mouseClick(button(composer, "composeSend"), Qt::LeftButton); QTRY_COMPARE(posted.count(), 1);
        QCOMPARE(operations, QStringList({"POST /2/media/upload", "GET /2/media/media-1", "POST /2/tweets"}));
    }
    void attachmentLimitsAndRemoval() {
        NorthApi api; api.usePersonalToken("test-token"); PostComposer composer(&api, true); composer.resize(600, 400); composer.show();
        QTemporaryDir directory; const auto image = writeImage(directory);
        QVERIFY(!composer.addImages({image, image, image, image, image})); QVERIFY(!button(composer, "composeSend")->isEnabled());
        QFile invalid(directory.filePath("invalid.png")); QVERIFY(invalid.open(QIODevice::WriteOnly)); invalid.write("not an image"); invalid.close();
        QVERIFY(!composer.addImages({invalid.fileName()}));
        QVERIFY(composer.addImages({image})); QTest::mouseClick(button(composer, "removeComposeImage"), Qt::LeftButton);
        QTRY_VERIFY(composer.findChildren<QFrame *>("composeImage").isEmpty()); QVERIFY(!button(composer, "composeSend")->isEnabled());
    }
    void emojiSearchInsertsAtTheSelection() {
        NorthApi api; api.usePersonalToken("test-token"); PostComposer composer(&api, true); composer.resize(600, 400); composer.show();
        composer.editor()->setPlainText("hello world"); auto cursor = composer.editor()->textCursor(); cursor.setPosition(6); cursor.movePosition(QTextCursor::End, QTextCursor::KeepAnchor); composer.editor()->setTextCursor(cursor);
        QTest::mouseClick(button(composer, "composeEmoji"), Qt::LeftButton); auto *picker = composer.findChild<QFrame *>("emojiPicker"); QVERIFY(picker);
        picker->findChild<QLineEdit *>("emojiSearch")->setText("寿司"); QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
        const auto choices = picker->findChildren<QPushButton *>("emojiChoice"); QCOMPARE(choices.size(), 1);
        QTest::mouseClick(choices.first(), Qt::LeftButton); QCOMPARE(composer.editor()->toPlainText(), QStringLiteral("hello 🍣"));
    }
    void readOnlyAndMissingMediaScopeCannotUpload() {
        Server server; NorthApi api(nullptr, server.base()); QVERIFY(api.restoreSession(oauthSession(server, "users.read posts.read")));
        PostComposer composer(&api, true); composer.editor()->setPlainText("read only");
        QVERIFY(!button(composer, "composeSend")->isEnabled()); QVERIFY(composer.editor()->isReadOnly());
        api.logout(); QVERIFY(api.restoreSession(oauthSession(server, "users.read posts.read posts.write")));
        composer.editor()->setPlainText("text is allowed"); QVERIFY(button(composer, "composeSend")->isEnabled());
        QVERIFY(!button(composer, "composeAttach")->isEnabled());
        QTemporaryDir directory; QVERIFY(!composer.addImages({writeImage(directory)})); QCOMPARE(server.requests.size(), 0);
    }
    void logoutDiscardsDraftAndLateUpload() {
        Server server; int uploads = 0, posts = 0;
        server.handler = [&](Request request) {
            if (request.url.path() == "/2/media/upload") { ++uploads; return Response{201, {{"data", QJsonObject{{"id", "late-media"}}}}, {}, 150}; }
            ++posts; return Response{201, {{"data", QJsonObject{{"id", "unexpected"}}}}};
        };
        NorthApi api(nullptr, server.base()); api.usePersonalToken("test-token"); PostComposer composer(&api, true); composer.resize(600, 400); composer.show();
        composer.editor()->setPlainText("private draft"); QTemporaryDir directory; QVERIFY(composer.addImages({writeImage(directory)}));
        QTest::mouseClick(button(composer, "composeSend"), Qt::LeftButton); QTRY_COMPARE(uploads, 1); api.logout();
        QTest::qWait(200); QCOMPARE(posts, 0); QVERIFY(composer.editor()->toPlainText().isEmpty()); QVERIFY(!composer.busy());
        QVERIFY(!button(composer, "composeSend")->isEnabled());
    }
};
QTEST_MAIN(ComposerTests)
#include "tst_composer.moc"
