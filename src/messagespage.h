#pragma once
#include <QJsonArray>
#include <QJsonObject>
#include <QMap>
#include <QWidget>
class NorthApi;
class QLabel;
class QListWidget;
class QPlainTextEdit;
class QPushButton;
class QScrollArea;
class QVBoxLayout;
class EdgeLoader;

class MessagesPage : public QWidget {
    Q_OBJECT
public:
    explicit MessagesPage(NorthApi *api, QWidget *parent = nullptr);
    void setSession(const QJsonObject &user);
    void reload();
signals:
    void loginRequested();
    void postRequested(QJsonObject post);
    void profileRequested(QString handle);
private:
    void reset();
    void loadConversations(bool more = false);
    void openConversation(const QJsonObject &conversation);
    void loadMessages(bool more = false);
    void renderMessages(bool older = false);
    void sendMessage();
    void newConversation();
    void addConversation(const QJsonObject &conversation);
    void notice(const QString &text);
    QString conversationName(const QJsonObject &conversation) const;
    void updateComposer();
    NorthApi *m_api;
    QJsonObject m_user, m_conversation;
    QJsonArray m_messages;
    QListWidget *m_list;
    QLabel *m_notice;
    QPushButton *m_chatTitle;
    QPushButton *m_login, *m_new, *m_send;
    EdgeLoader *m_conversationLoader, *m_historyLoader;
    QPlainTextEdit *m_editor;
    QScrollArea *m_scroll;
    QVBoxLayout *m_bubbles;
    QString m_conversationCursor, m_messageCursor;
    QMap<QString, QString> m_drafts;
    struct Operation { QString text; QByteArray key; };
    QMap<QString, Operation> m_operations;
    quint64 m_generation = 0, m_chatGeneration = 0, m_messageRequest = 0;
    bool m_loading = false, m_loadingMessages = false, m_sending = false, m_positioning = false;
};
