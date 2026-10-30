#pragma once
#include "credentialstore.h"
#include <QTimer>

class MemoryStore : public CredentialStore {
public:
    MemoryStore() : CredentialStore("test-only") {}
    QByteArray data;
    bool failWrites = false;
    QList<QByteArray> writes;
protected:
    void perform(Action action, const QByteArray &value, Callback callback) override {
        QTimer::singleShot(0, this, [this, action, value, callback] {
            if (action == Write && failWrites) { callback({{}, "test store unavailable"}); return; }
            if (action == Write) { data = value; writes.append(value); }
            if (action == Erase) data.clear();
            callback({action == Read ? data : QByteArray{}, {}});
        });
    }
};
