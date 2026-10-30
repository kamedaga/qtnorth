#pragma once
#include "northapi.h"
#include <QMainWindow>
#include <QJsonArray>
#include <QSet>

class QLabel;
class QLineEdit;
class QPlainTextEdit;
class QPushButton;
class QScrollArea;
class QSplitter;
class QVBoxLayout;
class PostCard;
class QStackedWidget;
class MessagesPage;
class EdgeLoader;
class CredentialStore;
class LoginPage;
class ProfilePage;
class QAction;
class WindowTitleBar;
class NotificationsPage;
class NotificationNavButton;
class PostReactions;

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(QWidget *parent = nullptr, const QUrl &apiBase = QUrl("https://api.north.rip"), CredentialStore *store = nullptr);
protected:
    void closeEvent(QCloseEvent *event) override;
private:
    QWidget *makeSidebar();
    QWidget *makeWorkspace();
    QWidget *makeInspector();
    void login();
    void restoreSavedLogin();
    void returnToLogin(const QString &message = {}, bool forget = false);
    void saveSession(std::function<void(bool)> callback = {});
    void connected();
    void disconnectSession();
    void navigate(const QString &view);
    QWidget *basePage() const;
    void loadPosts(bool more = false);
    void clearPosts();
    void appendPost(const QJsonObject &post);
    void inspect(const QJsonObject &post);
    void openPost(const QJsonObject &post, bool pushHistory = true);
    void openProfile(const QString &handle, bool pushHistory = true);
    void refreshCurrentPage();
    struct PageState { QString kind, handle; QJsonObject post; int position = 0, section = 0; };
    PageState currentPage() const;
    void renderPostPage(const QJsonObject &post, const QJsonArray &ancestors = {});
    void loadReplies(bool more = false);
    void backFromPost();
    PostCard *makeCard(const QJsonObject &post);
    void resetInspector();
    void toggleInspector();
    void compose(const QJsonObject &reply = {});
    void react(PostCard *card, const QString &kind, bool active);
    void showError(const QString &message);
    void updateSession();
    void setNotice(const QString &message, bool error = false);

    NorthApi m_api;
    WindowTitleBar *m_titleBar = nullptr;
    CredentialStore *m_credentials = nullptr;
    LoginPage *m_loginPage = nullptr;
    QStackedWidget *m_shell = nullptr;
    QJsonObject m_savedSession;
    quint64 m_loginAttempt = 0;
    bool m_verified = false, m_usingSaved = false, m_closing = false;
    QSplitter *m_splitter = nullptr;
    QWidget *m_inspector = nullptr, *m_details = nullptr, *m_feed = nullptr;
    QVBoxLayout *m_posts = nullptr, *m_detailLayout = nullptr;
    PostReactions *m_postReactions = nullptr;
    QScrollArea *m_scroll = nullptr;
    QStackedWidget *m_content = nullptr;
    MessagesPage *m_messagesPage = nullptr;
    ProfilePage *m_profilePage = nullptr;
    NotificationsPage *m_notificationsPage = nullptr;
    NotificationNavButton *m_notificationNav = nullptr;
    QScrollArea *m_postScroll = nullptr;
    QVBoxLayout *m_postContent = nullptr, *m_replyContent = nullptr;
    QPushButton *m_back = nullptr;
    EdgeLoader *m_timelineLoader = nullptr, *m_replyLoader = nullptr;
    QLabel *m_postNotice = nullptr;
    QJsonObject m_openPost;
    QList<PageState> m_history;
    QSet<QString> m_replyIds;
    QString m_replyCursor, m_loginView = "home";
    quint64 m_postGeneration = 0;
    bool m_postPageVisible = false, m_loadingReplies = false;
    QLabel *m_breadcrumb = nullptr, *m_notice = nullptr;
    QPushButton *m_sessionName = nullptr, *m_sessionHandle = nullptr;
    QWidget *m_sessionAvatarHost = nullptr;
    QVBoxLayout *m_sessionAvatarLayout = nullptr;
    QLabel *m_status = nullptr;
    QLineEdit *m_search = nullptr;
    QPushButton *m_refresh = nullptr, *m_loginButton = nullptr;
    QPushButton *m_inspectorToggle = nullptr;
    QAction *m_inspectorAction = nullptr;
    QList<QPushButton *> m_navButtons;
    QList<QPointer<PostCard>> m_cards;
    QSet<QString> m_seen;
    QJsonObject m_me, m_selected;
    QString m_view = "home", m_cursor, m_query;
    quint64 m_viewGeneration = 0;
    bool m_loading = false;
};
