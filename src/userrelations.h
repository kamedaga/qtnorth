#pragma once
#include <QObject>
#include <QHash>
#include <QJsonObject>
#include <QPushButton>
#include <functional>
class NorthApi;

class UserRelations : public QObject {
    Q_OBJECT
public:
    static UserRelations *forApi(NorthApi *api);
    void setViewer(QJsonObject user);
    QString viewerId() const { return m_viewer.value("id").toString(); }
    void observe(const QJsonObject &user, bool fresh = false, quint64 requestVersion = 0);
    QJsonObject user(const QString &handle) const;
    bool busy(const QString &handle) const;
    bool pending(const QString &handle) const;
    bool needsVerification(const QString &handle) const;
    QString error(const QString &handle) const;
    quint64 version(const QString &handle) const;
    void toggle(const QString &handle);
signals:
    void changed(QString handle);
    void viewerChanged();
    void failed(QString handle, QString message);
private:
    explicit UserRelations(NorthApi *api);
    struct State {
        QJsonObject user;
        bool busy = false, pending = false, verify = false, intent = false, before = false, keyIntent = false;
        QString error;
        QByteArray key;
        quint64 version = 0;
    };
    void perform(const QString &handle, bool follow);
    void refresh(const QString &handle, std::function<void(bool)> done);
    void finish(const QString &handle);
    NorthApi *m_api;
    QHash<QString, State> m_states;
    QJsonObject m_viewer;
    quint64 m_generation = 0;
};

class FollowButton : public QPushButton {
    Q_OBJECT
public:
    FollowButton(const QJsonObject &user, NorthApi *api, const QString &selfId = {}, QWidget *parent = nullptr);
private:
    void updateState();
    UserRelations *m_relations;
    NorthApi *m_api;
    QString m_handle, m_userId, m_selfId;
};
