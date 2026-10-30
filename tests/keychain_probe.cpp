// Opt-in desktop integration check. Uses a random, disposable key and dummy data.
#include "credentialstore.h"
#include <QCoreApplication>
#include <QEventLoop>
#include <QTextStream>
#include <QTimer>
#include <QUuid>

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    app.setApplicationName("qtNorth-keychain-probe");
    QObject context;
    QEventLoop loop;
    const auto id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    auto *store = createCredentialStore(QUrl("https://keychain-probe.qtnorth.invalid/" + id), &context);
    const auto dummy = ("qtNorth dummy probe " + id).toUtf8();
    int code = 1;
    QTimer timeout; timeout.setSingleShot(true);
    QObject::connect(&timeout, &QTimer::timeout, &loop, [&] {
        QTextStream(stderr) << "Credential store timed out. Check the session D-Bus and unlocked keyring." << Qt::endl;
        loop.quit();
    });
    timeout.start(15000);
    store->write(&context, dummy, [&](CredentialResult written) {
        if (!written.ok()) { QTextStream(stderr) << written.error << Qt::endl; loop.quit(); return; }
        store->read(&context, [&](CredentialResult read) {
            const bool matches = read.ok() && read.data == dummy;
            store->erase(&context, [&, matches](CredentialResult erased) {
                if (matches && erased.ok()) {
                    code = 0;
                    QTextStream(stdout) << "Credential store write/read/delete verified (dummy data only)." << Qt::endl;
                } else QTextStream(stderr) << "Credential store round trip failed." << Qt::endl;
                timeout.stop(); loop.quit();
            });
        });
    });
    loop.exec();
    return code;
}
