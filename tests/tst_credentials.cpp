#include "credentialstore.h"
#include "memorystore.h"
#include <QJsonDocument>
#include <QJsonObject>
#include <QUuid>
#include <QtTest>
#include <memory>

class CredentialTests : public QObject {
    Q_OBJECT
private slots:
    void writesAndEraseRemainOrderedAfterContextDies() {
        MemoryStore store;
        auto *context = new QObject;
        bool called = false;
        store.write(context, "old-session", [&](CredentialResult) { called = true; });
        delete context;
        store.write(this, "new-session", [&](CredentialResult result) { QVERIFY(result.ok()); });
        store.erase(this, [&](CredentialResult result) { QVERIFY(result.ok()); });
        QTRY_VERIFY(!store.busy());
        QVERIFY(!called);
        QVERIFY(store.data.isEmpty());
        QCOMPARE(store.writes, QList<QByteArray>({"old-session", "new-session"}));
    }
    void osStoreRoundTripWithDisposableKey() {
        if (!qEnvironmentVariableIsSet("QTNORTH_TEST_OS_STORE")) QSKIP("OS integration is opt-in");
        const QUrl origin("https://" + QUuid::createUuid().toString(QUuid::WithoutBraces) + ".test.invalid");
        std::unique_ptr<CredentialStore> store(createCredentialStore(origin));
        const QByteArray dummy = QJsonDocument(QJsonObject{{"kind", "test-only"}, {"value", QString::fromUtf8("日本語-test")}}).toJson();
        CredentialResult written, read, erased, missing;
        int done = 0;
        store->write(this, dummy, [&](CredentialResult result) { written = result; ++done; });
        store->read(this, [&](CredentialResult result) { read = result; ++done; });
        store->erase(this, [&](CredentialResult result) { erased = result; ++done; });
        store->read(this, [&](CredentialResult result) { missing = result; ++done; });
        QTRY_COMPARE_WITH_TIMEOUT(done, 4, 60000);
        QVERIFY2(written.ok(), qPrintable(written.error));
        QVERIFY2(read.ok(), qPrintable(read.error));
        QCOMPARE(read.data, dummy);
        QVERIFY2(erased.ok(), qPrintable(erased.error));
        QVERIFY2(missing.ok(), qPrintable(missing.error));
        QVERIFY(missing.data.isEmpty());
    }
};
QTEST_GUILESS_MAIN(CredentialTests)
#include "tst_credentials.moc"
