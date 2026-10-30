#pragma once
#include <QFrame>
#include <QJsonObject>
#include <QList>
#include <QPointer>
#include <QTimer>
class NorthApi;
class QVBoxLayout;
class QRadioButton;
class QPushButton;
class QLabel;

class PollWidget : public QFrame {
    Q_OBJECT
public:
    PollWidget(QString postId, QJsonObject poll, bool interactive, NorthApi *api, QWidget *parent = nullptr);
    void setPoll(const QJsonObject &poll);
private:
    void render();
    void updateControls();
    bool ended() const;
    QString m_postId, m_choice;
    QJsonObject m_poll;
    bool m_interactive;
    bool m_awaitingResults = false;
    QPointer<NorthApi> m_api;
    QVBoxLayout *m_layout;
    QList<QRadioButton *> m_options;
    QPushButton *m_vote = nullptr;
    QLabel *m_error = nullptr;
    QTimer m_expiry;
};
