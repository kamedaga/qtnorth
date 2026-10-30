#pragma once
#include <QDateTime>
#include <QJsonObject>
#include <QPointer>
#include <QSet>
#include <QWidget>

class NorthApi;
class EdgeLoader;
class QDialog;
class QLabel;
class QPushButton;
class QScrollArea;
class QVBoxLayout;

class ReactionUsersList : public QWidget {
    Q_OBJECT
public:
    enum Kind { Reposts, Likes };
    ReactionUsersList(NorthApi *api, Kind kind, QWidget *parent = nullptr);
    void setPost(const QString &id);
signals:
    void profileRequested(QString handle);
protected:
    void showEvent(QShowEvent *event) override;
private:
    void load(bool more = false);
    void appendUser(const QJsonObject &user);
    NorthApi *m_api;
    Kind m_kind;
    QScrollArea *m_scroll;
    QVBoxLayout *m_rows;
    QLabel *m_notice;
    QPushButton *m_retry;
    EdgeLoader *m_loader;
    QString m_postId, m_cursor;
    QSet<QString> m_seen;
    QDateTime m_retryAt;
    quint64 m_generation = 0;
    bool m_loading = false, m_loaded = false, m_failedMore = false;
};

class PostReactions : public QWidget {
    Q_OBJECT
public:
    explicit PostReactions(NorthApi *api, QWidget *parent = nullptr);
    void setPost(const QJsonObject &post);
signals:
    void profileRequested(QString handle);
private:
    void showLikes();
    void closeLikes();
    NorthApi *m_api;
    ReactionUsersList *m_reposts;
    QPushButton *m_likes;
    QPointer<QDialog> m_dialog;
    QString m_postId;
};
