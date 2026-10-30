#pragma once
#include <QObject>

class Preferences : public QObject {
    Q_OBJECT
public:
    static Preferences *instance();
    int notificationInterval() const;
    void setNotificationInterval(int seconds);
    bool foldLongPosts() const;
    void setFoldLongPosts(bool enabled);
    bool linkPreviews() const;
    void setLinkPreviews(bool enabled);
signals:
    void notificationIntervalChanged(int seconds);
    void foldLongPostsChanged(bool enabled);
    void linkPreviewsChanged(bool enabled);
private:
    explicit Preferences(QObject *parent) : QObject(parent) {}
};
