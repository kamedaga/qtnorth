#pragma once
#include <QObject>
#include <QPointer>
#include <QQueue>
#include <QUrl>
#include <functional>

struct CredentialResult {
    QByteArray data;
    QString error;
    bool ok() const { return error.isEmpty(); }
};

class CredentialStore : public QObject {
    Q_OBJECT
public:
    using Callback = std::function<void(CredentialResult)>;
    enum Action { Read, Write, Erase };
    CredentialStore(QString key, QObject *parent = nullptr);
    void read(QObject *context, Callback callback);
    void write(QObject *context, const QByteArray &data, Callback callback);
    void erase(QObject *context, Callback callback);
    bool busy() const { return m_running || !m_queue.isEmpty(); }
    QString key() const { return m_key; }
signals:
    void idle();
protected:
    virtual void perform(Action action, const QByteArray &data, Callback callback) = 0;
private:
    struct Task { Action action; QByteArray data; QPointer<QObject> context; Callback callback; };
    void enqueue(Task task);
    void pump();
    QString m_key;
    QQueue<Task> m_queue;
    bool m_running = false;
};

CredentialStore *createCredentialStore(const QUrl &apiBase, QObject *parent = nullptr);
