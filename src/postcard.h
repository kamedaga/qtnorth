#pragma once
#include <QFrame>
#include <QJsonObject>
#include <QHash>
#include <QPoint>
class QPushButton;
class NorthApi;
class QVBoxLayout;

class PostCard : public QFrame {
    Q_OBJECT
public:
    PostCard(QJsonObject post, bool interactive, NorthApi *api = nullptr, QWidget *parent = nullptr);
    QJsonObject post() const { return m_post; }
    void setSelected(bool selected);
    void updateReaction(const QJsonObject &state);
    void setReactionBusy(bool busy);
    void setExpandedBody(bool expanded);
signals:
    void selected(QJsonObject post);
    void profileRequested(QString handle);
    void searchRequested(QString query);
    void replyRequested(QJsonObject post);
    void reactionRequested(PostCard *card, QString kind, bool active);
protected:
    void mousePressEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    bool eventFilter(QObject *object, QEvent *event) override;
private:
    void updateButtons();
    void addMedia(QVBoxLayout *layout, const QJsonObject &post, NorthApi *api);
    void addText(QVBoxLayout *layout, const QJsonObject &post, NorthApi *api);
    QHash<QObject *, QJsonObject> m_clickTargets;
    QPoint m_pressPosition;
    bool m_pressed = false;
    QJsonObject m_post;
    QPushButton *m_like = nullptr, *m_repost = nullptr;
};
