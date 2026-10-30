#pragma once
#include <QJsonObject>
#include <QElapsedTimer>
#include <QPushButton>
#include <QSet>
#include <QTimer>
#include <QWidget>
#include <functional>
class NorthApi;
class PostCard;
class QLabel;
class QScrollArea;
class QTabBar;
class QVBoxLayout;
class EdgeLoader;

class NotificationNavButton : public QPushButton {
public:
    explicit NotificationNavButton(QWidget *parent = nullptr);
    void setUnreadCount(qint64 count);
protected:
    void resizeEvent(QResizeEvent *event) override;
private:
    QLabel *m_badge;
};

class NotificationsPage : public QWidget {
    Q_OBJECT
public:
    using CardFactory = std::function<PostCard *(const QJsonObject &)>;
    NotificationsPage(NorthApi *api, CardFactory factory, QWidget *parent = nullptr);
    void startSession();
    void clear();
    void reload();
    void refreshUnreadCount(bool force = true);
signals:
    void unreadCountChanged(qint64 count);
    void postRequested(QJsonObject post);
    void profileRequested(QString handle);
    void loginRequested();
private:
    void loadItems(bool more = false);
    void appendNotification(const QJsonObject &notification);
    void markAllRead();
    void updateControls();
    void showNotice(const QString &message);
    void scheduleCountPoll(int delay = -1);
    NorthApi *m_api;
    CardFactory m_factory;
    QScrollArea *m_scroll;
    QVBoxLayout *m_items;
    QTabBar *m_tabs;
    QLabel *m_notice;
    QPushButton *m_login, *m_markRead, *m_new;
    EdgeLoader *m_loader;
    QTimer m_poll;
    QElapsedTimer m_lastCount;
    QSet<QString> m_seen;
    QString m_cursor;
    QByteArray m_markKey;
    qint64 m_unread = 0;
    quint64 m_generation = 0, m_listGeneration = 0, m_countVersion = 0;
    quint64 m_countRequestVersion = 0;
    bool m_session = false, m_denied = false, m_loading = false, m_loaded = false;
    bool m_countLoading = false, m_countAgain = false, m_marking = false, m_anyUnread = false;
    int m_failures = 0;
};
