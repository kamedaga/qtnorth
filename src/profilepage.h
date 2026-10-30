#pragma once
#include <QJsonObject>
#include <QSet>
#include <QWidget>
#include <functional>
class NorthApi;
class PostCard;
class EdgeLoader;
class QLabel;
class QScrollArea;
class QTabBar;
class QVBoxLayout;

class ProfilePage : public QWidget {
    Q_OBJECT
public:
    using CardFactory = std::function<PostCard *(const QJsonObject &)>;
    ProfilePage(NorthApi *api, CardFactory factory, QWidget *parent = nullptr);
    void openUser(QString handle, QString selfId, int position = 0, int section = 0);
    void reload();
    void clear();
    QString handle() const { return m_handle; }
    int position() const;
    void setPosition(int position);
    int section() const;
signals:
    void profileRequested(QString handle);
    void titleChanged(QString title);
private:
    void renderHeader();
    void loadItems(bool more = false);
    void appendUser(const QJsonObject &user);
    NorthApi *m_api;
    CardFactory m_factory;
    QScrollArea *m_scroll;
    QWidget *m_header;
    QVBoxLayout *m_headerLayout, *m_items;
    QLabel *m_notice;
    QTabBar *m_tabs;
    EdgeLoader *m_loader;
    QJsonObject m_user;
    QSet<QString> m_seen;
    QString m_handle, m_selfId, m_cursor;
    quint64 m_generation = 0, m_listGeneration = 0;
    bool m_loading = false;
    int m_restorePosition = 0;
};
