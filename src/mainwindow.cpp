#include "mainwindow.h"
#include "icons.h"
#include "post.h"
#include "postcard.h"
#include "messagespage.h"
#include "scrollbehavior.h"
#include "credentialstore.h"
#include "loginpage.h"
#include "theme.h"
#include "aboutdialog.h"
#include "settingsdialog.h"
#include "profilepage.h"
#include "posttime.h"
#include "windowchrome.h"
#include "notificationspage.h"
#include "reactionusers.h"
#include "remoteimage.h"
#include "userrelations.h"
#include "postcomposer.h"
#include <QJsonDocument>
#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QClipboard>
#include <QCloseEvent>
#include <QDesktopServices>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFrame>
#include <QHBoxLayout>
#include <QJsonArray>
#include <QLabel>
#include <QLineEdit>
#include <QMenuBar>
#include <QMessageBox>
#include <QPainter>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QRegularExpression>
#include <QScrollArea>
#include <QScrollBar>
#include <QSettings>
#include <QShortcut>
#include <QSplitter>
#include <QStackedWidget>
#include <QStatusBar>
#include <QStyle>
#include <QTabWidget>
#include <QToolButton>
#include <QVBoxLayout>
#include <memory>

namespace {
QLabel *label(const QString &text, const char *name = "", bool wrap = false)
{
    auto *w = new QLabel(text);
    w->setTextFormat(Qt::PlainText);
    w->setObjectName(name);
    w->setWordWrap(wrap);
    return w;
}
QPushButton *button(const QString &text, const QString &icon = {}, const char *name = "")
{
    auto *w = new QPushButton(text);
    if (!icon.isEmpty()) w->setIcon(appIcon(icon));
    w->setObjectName(name);
    w->setCursor(Qt::PointingHandCursor);
    return w;
}
void clearLayout(QLayout *layout)
{
    while (auto *item = layout->takeAt(0)) {
        if (item->widget()) { item->widget()->hide(); item->widget()->deleteLater(); }
        if (item->layout()) clearLayout(item->layout());
        delete item;
    }
}
class Canvas : public QWidget {
public:
    using QWidget::QWidget;
protected:
    void paintEvent(QPaintEvent *) override {
        QPainter painter(this);
        painter.fillRect(rect(), palette().color(QPalette::Base));
    }
};

}

MainWindow::MainWindow(QWidget *parent, const QUrl &apiBase, CredentialStore *store) : QMainWindow(parent), m_api(this, apiBase)
{
    setWindowTitle("qtNorth");
    setWindowIcon(QIcon(":/assets/qtNorth-icon.svg"));
    resize(1320, 860);
    setMinimumSize(940, 640);
    ThemeManager::instance();
    m_titleBar = new WindowTitleBar(this);
    setMenuBar(m_titleBar);
    m_splitter = new QSplitter(Qt::Horizontal);
    m_splitter->setHandleWidth(1);
    m_splitter->addWidget(makeSidebar());
    m_splitter->addWidget(makeWorkspace());
    m_splitter->addWidget(makeInspector());
    m_splitter->setSizes({190, 890, 240});
    m_splitter->setStretchFactor(0, 0);
    m_splitter->setStretchFactor(1, 1);
    m_splitter->setStretchFactor(2, 0);
    m_splitter->setChildrenCollapsible(false);
    m_credentials = store ? store : createCredentialStore(apiBase, this);
    m_shell = new QStackedWidget;
    m_shell->setObjectName("appPages");
    m_loginPage = new LoginPage;
    m_shell->addWidget(m_loginPage);
    m_shell->addWidget(m_splitter);
    setCentralWidget(m_shell);
    m_splitter->setEnabled(false);
    auto *file = menuBar()->addMenu(tr("ファイル"));
    file->addAction(tr("新規ポスト"), QKeySequence("Ctrl+N"), this, [this] { compose(); });
    file->addAction(tr("ログアウト"), this, &MainWindow::disconnectSession);
    file->addSeparator();
    auto *settingsAction = file->addAction(tr("設定"), QKeySequence("Ctrl+,"), this, [this] { showSettingsDialog(this, &m_api); });
    settingsAction->setObjectName("showSettings");
    file->addSeparator();
    file->addAction(tr("終了"), QKeySequence::Quit, this, &QWidget::close);
    auto *view = menuBar()->addMenu(tr("表示"));
    view->addAction(tr("更新"), QKeySequence("Ctrl+R"), this, &MainWindow::refreshCurrentPage);
    view->addMenu(appearanceMenu(this));
    view->addAction(tr("戻る"), QKeySequence("Alt+Left"), this, &MainWindow::backFromPost);
    view->addAction(tr("検索"), QKeySequence("Ctrl+K"), this, [this] { navigate("search"); m_search->setFocus(); });
    m_inspectorAction = view->addAction(tr("詳細パネルの切り替え"), QKeySequence("Ctrl+\\"), this, &MainWindow::toggleInspector);
    m_inspectorAction->setObjectName("toggleInspector");
    m_inspectorAction->setEnabled(false);
    auto *help = menuBar()->addMenu(tr("ヘルプ"));
    auto *about = help->addAction(tr("qtNorthについて"), this, [this] { showAboutDialog(this); });
    about->setObjectName("showAbout");
    help->addAction(tr("north APIドキュメント"), this, [] { QDesktopServices::openUrl(QUrl("https://api.north.rip/docs")); });
    m_status = label(tr("未接続"));
    connect(UserRelations::forApi(&m_api), &UserRelations::failed, this, [this](const QString &handle, const QString &message) {
        m_status->setText(tr("@%1: %2").arg(handle, message));
    });
    connect(&m_api, &NorthApi::pollUpdated, this, [this](const QString &id, const QJsonObject &poll) {
        if (m_openPost.value("id").toString() == id) m_openPost.insert("poll", poll);
        if (m_selected.value("id").toString() == id) m_selected.insert("poll", poll);
    });
    statusBar()->setSizeGripEnabled(false);
    statusBar()->addWidget(m_status);
    connect(&m_api, &NorthApi::sessionExpired, this, [this](const QString &message) {
        returnToLogin(message, true);
    });
    connect(&m_api, &NorthApi::sessionChanged, this, [this] { if (m_verified || m_usingSaved) saveSession(); });
    m_api.setRefreshGuard([this](const QJsonObject &marker, std::function<void(bool)> ready) {
        m_credentials->write(this, QJsonDocument(marker).toJson(QJsonDocument::Compact), [ready](CredentialResult result) { ready(result.ok()); });
    });
    connect(m_loginPage, &LoginPage::personalTokenRequested, this, [this](const QString &token) {
        ++m_loginAttempt; m_verified = false; m_usingSaved = false;
        m_loginPage->setBusy(true, tr("ログイン中…"));
        m_api.usePersonalToken(token); connected();
    });
    connect(m_loginPage, &LoginPage::deviceLoginRequested, this, [this](const QString &clientId, bool writeAccess, bool messages) {
        ++m_loginAttempt; m_verified = false; m_usingSaved = false;
        m_loginPage->setBusy(true, tr("認証待ち…"));
        m_api.startDeviceLogin(clientId, writeAccess, messages);
    });
    connect(m_loginPage, &LoginPage::savedLoginRequested, this, &MainWindow::restoreSavedLogin);
    connect(m_loginPage, &LoginPage::settingsRequested, this, [this] { showSettingsDialog(this, &m_api); });
    connect(m_loginPage, &LoginPage::forgetRequested, this, [this] { returnToLogin({}, true); });
    connect(m_loginPage, &LoginPage::cancelDeviceRequested, &m_api, &NorthApi::cancelDeviceLogin);
    connect(&m_api, &NorthApi::deviceAuthorization, m_loginPage, &LoginPage::showDeviceCode);
    connect(&m_api, &NorthApi::loginSucceeded, this, &MainWindow::connected);
    connect(&m_api, &NorthApi::loginFailed, this, [this](const QString &message) { m_loginPage->setBusy(false, message); });
    connect(m_credentials, &CredentialStore::idle, this, [this] { if (m_closing) close(); });
    connect(&m_api, &NorthApi::rateLimited, this, [this](int seconds) {
        m_status->setText(tr("待機中 · あと%1秒").arg(seconds));
    });
    QSettings settings;
    if (settings.contains("window/geometry")) restoreGeometry(settings.value("window/geometry").toByteArray());
    if (settings.contains("window/splitter")) m_splitter->restoreState(settings.value("window/splitter").toByteArray());
    navigate("home");
    updateSession();
    m_titleBar->setMenusVisible(false); statusBar()->hide();
    m_loginPage->setBusy(true, tr("ログイン情報を確認中…"));
    QTimer::singleShot(0, this, &MainWindow::restoreSavedLogin);
}

QWidget *MainWindow::makeSidebar()
{
    auto *panel = new QFrame;
    panel->setObjectName("sidebar");
    panel->setMinimumWidth(168);
    panel->setMaximumWidth(290);
    auto *layout = new QVBoxLayout(panel);
    layout->setContentsMargins(8, 10, 8, 8);
    layout->setSpacing(2);
    const QList<QStringList> nav = {
        {"home", tr("ホーム"), "home"}, {"ranked", tr("おすすめ"), "compass"},
        {"notifications", tr("通知"), "bell"},
        {"search", tr("検索"), "search"}, {"profile", tr("プロフィール"), "user"},
        {"messages", tr("メッセージ"), "message"}
    };
    for (const auto &item : nav) {
        auto *w = item[0] == "notifications" ? static_cast<QPushButton *>(m_notificationNav = new NotificationNavButton(panel)) : button(item[1], item[2], "nav");
        w->setCheckable(true);
        w->setProperty("view", item[0]);
        connect(w, &QPushButton::clicked, this, [this, key = item[0]] { navigate(key); });
        layout->addWidget(w);
        m_navButtons.append(w);
    }
    layout->addStretch();
    auto *section = new QFrame;
    section->setObjectName("section");
    auto *account = new QVBoxLayout(section);
    account->setContentsMargins(3, 15, 3, 0);
    account->setSpacing(6);
    m_sessionName = button({}, {}, "authorName");
    m_sessionHandle = button({}, {}, "authorHandle");
    for (auto *identity : {m_sessionName, m_sessionHandle}) connect(identity, &QPushButton::clicked, this, [this] { openProfile(m_me.value("handle").toString()); });
    auto *identityRow = new QHBoxLayout; identityRow->setSpacing(9);
    m_sessionAvatarHost = new QWidget; m_sessionAvatarHost->setFixedSize(40, 40);
    m_sessionAvatarLayout = new QVBoxLayout(m_sessionAvatarHost); m_sessionAvatarLayout->setContentsMargins(0, 0, 0, 0);
    identityRow->addWidget(m_sessionAvatarHost);
    auto *identity = new QVBoxLayout; identity->setSpacing(3);
    for (auto *name : {m_sessionName, m_sessionHandle}) {
        name->setMinimumWidth(0); name->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed); identity->addWidget(name);
    }
    identityRow->addLayout(identity, 1); account->addLayout(identityRow);
    m_loginButton = button(tr("ログイン"), "user");
    connect(m_loginButton, &QPushButton::clicked, this, [this] {
        if (m_api.authenticated()) disconnectSession(); else login();
    });
    account->addWidget(m_loginButton);
    layout->addWidget(section);
    auto *appTools = new QHBoxLayout;
    auto *appearance = appearanceButton(panel);
    auto *settings = button({}, "settings", "tool"); settings->setObjectName("sidebarSettings");
    settings->setToolTip(tr("設定")); settings->setAccessibleName(tr("設定")); settings->setFixedSize(32, 32);
    connect(settings, &QPushButton::clicked, this, [this] { showSettingsDialog(this, &m_api); });
    auto *about = button({}, "about", "tool");
    about->setToolTip(tr("qtNorthについて")); about->setAccessibleName(tr("qtNorthについて"));
    connect(about, &QPushButton::clicked, this, [this] { showAboutDialog(this); });
    appTools->addWidget(settings); appTools->addWidget(appearance); appTools->addWidget(about); appTools->addStretch(); layout->addLayout(appTools);
    return panel;
}

QWidget *MainWindow::makeWorkspace()
{
    auto *workspace = new QWidget;
    workspace->setMinimumWidth(420);
    auto *layout = new QVBoxLayout(workspace);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    auto *toolbar = new QFrame;
    toolbar->setObjectName("toolbar");
    toolbar->setFixedHeight(53);
    auto *tools = new QHBoxLayout(toolbar);
    tools->setContentsMargins(16, 0, 12, 0);
    m_back = button({}, "back", "tool");
    m_back->setObjectName("backFromPost");
    m_back->setToolTip(tr("戻る (Alt+Left)"));
    m_back->setAccessibleName(tr("戻る"));
    m_back->hide();
    connect(m_back, &QPushButton::clicked, this, &MainWindow::backFromPost);
    tools->addWidget(m_back);
    m_breadcrumb = label(tr("ホーム"));
    tools->addWidget(m_breadcrumb);
    tools->addStretch();
    m_refresh = button({}, "refresh", "tool");
    m_refresh->setToolTip(tr("更新 (Ctrl+R)"));
    connect(m_refresh, &QPushButton::clicked, this, &MainWindow::refreshCurrentPage);
    tools->addWidget(m_refresh);
    auto *toggle = button({}, "panel", "tool");
    m_inspectorToggle = toggle;
    toggle->setToolTip(tr("詳細パネル (Ctrl+\\)"));
    connect(toggle, &QPushButton::clicked, this, &MainWindow::toggleInspector);
    tools->addWidget(toggle);
    auto *composeButton = button({}, "compose", "primary");
    composeButton->setIcon(appIcon("compose", "#ffffff"));
    composeButton->setToolTip(tr("新規ポスト (Ctrl+N)"));
    composeButton->setAccessibleName(tr("新規ポスト"));
    connect(composeButton, &QPushButton::clicked, this, [this] { compose(); });
    tools->addWidget(composeButton);
    layout->addWidget(toolbar);
    m_scroll = new QScrollArea;
    m_scroll->setObjectName("timelineScroll");
    m_scroll->setWidgetResizable(true);
    new SmoothScroll(m_scroll);
    m_timelineLoader = new EdgeLoader(m_scroll);
    connect(m_timelineLoader, &EdgeLoader::loadMoreRequested, this, [this] { loadPosts(true); });
    auto *canvas = new Canvas;
    auto *canvasLayout = new QVBoxLayout(canvas);
    canvasLayout->setContentsMargins(0, 0, 0, 0);
    canvasLayout->setSpacing(0);
    m_feed = new QWidget;
    m_feed->setMinimumWidth(350);
    m_feed->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    auto *feedLayout = new QVBoxLayout(m_feed);
    feedLayout->setContentsMargins(0, 0, 0, 0);
    feedLayout->setSpacing(0);
    m_search = new QLineEdit; m_search->setObjectName("postSearch");
    m_search->setPlaceholderText(tr("検索"));
    m_search->setToolTip(tr("from:ユーザー名 / #ハッシュタグ"));
    m_search->setClearButtonEnabled(true);
    m_search->setMinimumHeight(36);
    connect(m_search, &QLineEdit::returnPressed, this, [this] {
        m_query = m_search->text().trimmed();
        if (!m_query.isEmpty()) loadPosts();
    });
    m_inlineComposer = new PostComposer(&m_api, true, {}, m_feed);
    m_inlineComposer->setObjectName("inlineComposer"); m_inlineComposer->hide();
    feedLayout->addWidget(m_inlineComposer);
    connect(m_inlineComposer, &PostComposer::posted, this, [this] {
        m_status->setText(tr("ポストしました"));
        if ((m_view == "home" || m_view == "ranked") && m_content->currentWidget() == m_scroll && !m_postPageVisible) loadPosts();
    });
    feedLayout->addWidget(m_search);
    m_notice = label({}, "notice", true);
    m_notice->setTextInteractionFlags(Qt::TextSelectableByMouse);
    feedLayout->addWidget(m_notice);
    auto *postContainer = new QWidget;
    m_posts = new QVBoxLayout(postContainer);
    m_posts->setContentsMargins(0, 0, 0, 0);
    m_posts->setSpacing(0);
    feedLayout->addWidget(postContainer);
    feedLayout->addStretch(1);
    canvasLayout->addWidget(m_feed);
    m_scroll->setWidget(canvas);
    m_content = new QStackedWidget;
    m_content->setObjectName("workspacePages");
    m_content->addWidget(m_scroll);
    m_postScroll = new QScrollArea;
    m_postScroll->setObjectName("postPage");
    m_postScroll->setWidgetResizable(true);
    new SmoothScroll(m_postScroll);
    m_replyLoader = new EdgeLoader(m_postScroll);
    connect(m_replyLoader, &EdgeLoader::loadMoreRequested, this, [this] { loadReplies(true); });
    auto *postCanvas = new Canvas;
    auto *postOuter = new QVBoxLayout(postCanvas);
    postOuter->setContentsMargins(0, 0, 0, 0);
    postOuter->setSpacing(0);
    auto *postColumn = new QWidget;
    postColumn->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    auto *postLayout = new QVBoxLayout(postColumn);
    postLayout->setContentsMargins(0, 0, 0, 0);
    postLayout->setSpacing(0);
    m_postNotice = label({}, "notice", true);
    m_postNotice->hide();
    postLayout->addWidget(m_postNotice);
    auto *detailContainer = new QWidget;
    m_postContent = new QVBoxLayout(detailContainer);
    m_postContent->setContentsMargins(0, 0, 0, 0);
    m_postContent->setSpacing(0);
    postLayout->addWidget(detailContainer);
    auto *replyContainer = new QWidget;
    m_replyContent = new QVBoxLayout(replyContainer);
    m_replyContent->setContentsMargins(0, 0, 0, 0);
    m_replyContent->setSpacing(0);
    postLayout->addWidget(replyContainer);
    postLayout->addStretch(1);
    postOuter->addWidget(postColumn);
    m_postScroll->setWidget(postCanvas);
    m_content->addWidget(m_postScroll);
    m_messagesPage = new MessagesPage(&m_api);
    connect(m_messagesPage, &MessagesPage::postRequested, this, [this](const QJsonObject &post) { openPost(post); });
    connect(m_messagesPage, &MessagesPage::profileRequested, this, [this](const QString &handle) { openProfile(handle); });
    connect(m_messagesPage, &MessagesPage::loginRequested, this, [this] {
        if (m_api.authenticated()) { disconnectSession(); navigate("messages"); }
        login();
    });
    m_content->addWidget(m_messagesPage);
    m_profilePage = new ProfilePage(&m_api, [this](const QJsonObject &post) { return makeCard(post); });
    connect(m_profilePage, &ProfilePage::profileRequested, this, [this](const QString &handle) { openProfile(handle); });
    connect(m_profilePage, &ProfilePage::titleChanged, this, [this](const QString &title) {
        if (m_content->currentWidget() == m_profilePage) m_breadcrumb->setText(title);
    });
    m_content->addWidget(m_profilePage);
    m_notificationsPage = new NotificationsPage(&m_api, [this](const QJsonObject &post) { return makeCard(post); });
    connect(m_notificationsPage, &NotificationsPage::unreadCountChanged, m_notificationNav, &NotificationNavButton::setUnreadCount);
    connect(m_notificationsPage, &NotificationsPage::postRequested, this, [this](const QJsonObject &post) { openPost(post); });
    connect(m_notificationsPage, &NotificationsPage::profileRequested, this, [this](const QString &handle) { openProfile(handle); });
    connect(m_notificationsPage, &NotificationsPage::loginRequested, this, [this] {
        disconnectSession(); navigate("notifications"); login();
    });
    m_content->addWidget(m_notificationsPage);
    layout->addWidget(m_content);
    return workspace;
}

QWidget *MainWindow::makeInspector()
{
    auto *panel = new QFrame;
    m_inspector = panel;
    panel->setObjectName("inspector");
    panel->setMinimumWidth(245);
    panel->setMaximumWidth(360);
    auto *layout = new QVBoxLayout(panel);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    auto *header = new QFrame;
    header->setObjectName("toolbar");
    header->setFixedHeight(53);
    auto *headerLayout = new QHBoxLayout(header);
    headerLayout->setContentsMargins(20, 0, 20, 0);
    headerLayout->addWidget(label(tr("詳細")));
    layout->addWidget(header);
    auto *scroll = new QScrollArea;
    scroll->setWidgetResizable(true);
    new SmoothScroll(scroll);
    m_details = new QWidget;
    m_details->setObjectName("inspectorContent");
    auto *content = new QVBoxLayout(m_details);
    content->setContentsMargins(16, 12, 16, 12); content->setSpacing(0);
    auto *metadata = new QWidget;
    m_detailLayout = new QVBoxLayout(metadata);
    m_detailLayout->setContentsMargins(0, 0, 0, 0);
    m_detailLayout->setSpacing(8);
    content->addWidget(metadata);
    m_postReactions = new PostReactions(&m_api);
    connect(m_postReactions, &PostReactions::profileRequested, this, [this](const QString &handle) { openProfile(handle); });
    content->addWidget(m_postReactions); content->addStretch();
    scroll->setWidget(m_details);
    layout->addWidget(scroll);
    resetInspector();
    return panel;
}

void MainWindow::resetInspector()
{
    m_selected = {};
    clearLayout(m_detailLayout);
    m_postReactions->setPost({});
    m_inspector->hide();
    if (m_inspectorToggle) m_inspectorToggle->setEnabled(false);
    if (m_inspectorAction) m_inspectorAction->setEnabled(false);
}

void MainWindow::toggleInspector()
{
    if (!m_postPageVisible || m_selected.isEmpty()) return;
    m_inspector->setVisible(!m_inspector->isVisible());
}

void MainWindow::setNotice(const QString &message, bool error)
{
    m_notice->setText(message);
    m_notice->setProperty("error", error);
    m_notice->setVisible(!message.isEmpty());
    m_notice->style()->unpolish(m_notice);
    m_notice->style()->polish(m_notice);
}

QWidget *MainWindow::basePage() const
{
    if (m_view == "messages") return m_messagesPage;
    if (m_view == "notifications") return m_notificationsPage;
    return m_scroll;
}

void MainWindow::navigate(const QString &view)
{
    m_inlineComposer->setVisible(m_verified && (view == "home" || view == "ranked"));
    if (m_verified && view == m_view && m_content->currentWidget() == basePage() && view != "profile") {
        refreshCurrentPage(); return;
    }
    m_view = view;
    SmoothScroll::stopFor(m_scroll);
    m_postPageVisible = false;
    ++m_postGeneration;
    m_replyLoader->reset();
    m_history.clear();
    m_back->hide();
    clearLayout(m_postContent); clearLayout(m_replyContent);
    m_openPost = {};
    m_content->setCurrentWidget(basePage());
    ++m_viewGeneration;
    m_loading = false;
    m_cursor.clear();
    m_timelineLoader->reset();
    m_search->setVisible(view == "search");
    for (auto *nav : m_navButtons) nav->setChecked(nav->property("view").toString() == view);
    const QHash<QString, QString> titles = {{"home", tr("ホーム")}, {"ranked", tr("おすすめ")}, {"search", tr("検索")}, {"profile", tr("プロフィール")}, {"messages", tr("メッセージ")}, {"notifications", tr("通知")}};
    m_breadcrumb->setText(titles.value(view));
    resetInspector();
    clearPosts();
    if (view == "notifications") {
        m_refresh->setEnabled(true); m_status->setText(tr("通知")); m_notificationsPage->reload(); return;
    }
    if (view == "profile" && m_verified) { openProfile(m_me.value("handle").toString(), false); return; }
    if (view == "messages") {
        m_refresh->setEnabled(true);
        m_messagesPage->setSession(m_me);
        m_messagesPage->reload();
        return;
    }
    if (view == "search") m_search->setFocus();
    loadPosts();
}

void MainWindow::clearPosts()
{
    clearLayout(m_posts);
    m_cards.clear();
    m_seen.clear();
}

void MainWindow::loadPosts(bool more)
{
    if (m_loading) return;
    if (more && m_cursor.isEmpty()) return;
    if (!more) m_timelineLoader->reset();
    m_refresh->setEnabled(true);
    if (!m_verified || !m_api.authenticated()) return;
    if (m_view == "search" && m_query.isEmpty()) {
        setNotice(tr("検索語を入力"));
        return;
    }
    m_loading = true;
    m_timelineLoader->setLoading();
    m_refresh->setEnabled(false);
    m_status->setText(tr("読み込み中…"));
    const auto generation = ++m_viewGeneration;
    QString path = "/2/timelines/home";
    QUrlQuery query;
    if (m_view == "search") { path = "/2/tweets/search"; query.addQueryItem("q", m_query); query.addQueryItem("tab", "latest"); }
    else if (m_view == "profile") path = "/2/users/" + QString::fromLatin1(QUrl::toPercentEncoding(m_me.value("handle").toString())) + "/tweets";
    else query.addQueryItem("ranked", m_view == "ranked" ? "1" : "0");
    if (more && !m_cursor.isEmpty()) query.addQueryItem("cursor", m_cursor);
    m_api.request(this, "GET", path, query, {}, [this, generation, more](ApiResult result) {
        if (generation != m_viewGeneration) return;
        const ScrollAnchorGuard keepPosition(m_scroll, more);
        m_loading = false;
        m_refresh->setEnabled(true);
        if (!result.ok()) { m_timelineLoader->fail(result.retryAfter); showError(result.error); return; }
        const auto data = result.data();
        if (!data.value("items").isArray()) { m_timelineLoader->fail(); showError(tr("ポスト一覧の応答形式が不正です。")); return; }
        if (!more) {
            clearPosts(); if (!m_postPageVisible) resetInspector();
            SmoothScroll::stopFor(m_scroll);
            m_scroll->verticalScrollBar()->setValue(0);
        }
        for (const auto &item : data.value("items").toArray()) appendPost(item.toObject());
        m_cursor = data.value("nextCursor").toString();
        m_timelineLoader->setCursor(m_cursor);
        setNotice(m_cards.isEmpty() ? tr("ポストがありません。") : QString());
        m_status->setText(tr("%1件 · %2").arg(m_cards.size()).arg(QTime::currentTime().toString("HH:mm")));
    }, {}, NorthApi::ReadPolicy::Cached);
}

void MainWindow::appendPost(const QJsonObject &post)
{
    const auto id = post.value("id").toString();
    if (id.isEmpty() || m_seen.contains(id)) return;
    m_seen.insert(id);
    auto *card = makeCard(post);
    m_cards.append(card);
    m_posts->addWidget(card);
}

PostCard *MainWindow::makeCard(const QJsonObject &post)
{
    auto *card = new PostCard(post, m_api.authenticated(), &m_api);
    connect(card, &PostCard::selected, this, [this](const QJsonObject &selected) { openPost(selected); });
    connect(card, &PostCard::profileRequested, this, [this](const QString &handle) { openProfile(handle); });
    connect(card, &PostCard::searchRequested, this, [this](const QString &query) {
        m_query = query; m_search->setText(query); navigate("search");
    });
    connect(card, &PostCard::replyRequested, this, &MainWindow::compose);
    connect(card, &PostCard::reactionRequested, this, &MainWindow::react);
    return card;
}

void MainWindow::inspect(const QJsonObject &post)
{
    m_selected = post;
    for (auto &card : m_cards) if (card) card->setSelected(card->post().value("id") == post.value("id"));
    m_inspector->show();
    m_inspectorToggle->setEnabled(true);
    m_inspectorAction->setEnabled(true);
    clearLayout(m_detailLayout);
    const auto author = post.value("author").toObject();
    auto *name = button(author.value("name").toString(), {}, "authorName");
    auto *handle = button("@" + author.value("handle").toString(), {}, "authorHandle");
    for (auto *identity : {name, handle}) connect(identity, &QPushButton::clicked, this, [this, author] { openProfile(author.value("handle").toString()); });
    m_detailLayout->addWidget(name); m_detailLayout->addWidget(handle);
    m_detailLayout->addWidget(new FollowButton(author, &m_api, {}, m_details));
    m_detailLayout->addSpacing(8);
    m_detailLayout->addWidget(postTimeLabel(post.value("createdAt").toString(), "yyyy/MM/dd HH:mm"));
    if (!post.value("source").toString().isEmpty()) {
        m_detailLayout->addWidget(label(post.value("source").toString(), "muted", true));
    }
    const bool unavailable = post.value("deleted").toBool() || post.value("unavailable").toBool() || !post.value("hiddenReason").toString().isEmpty();
    auto *reply = button({}, "reply", "tool");
    reply->setToolTip(tr("返信"));
    reply->setAccessibleName(tr("返信"));
    reply->setEnabled(m_api.authenticated() && !unavailable);
    connect(reply, &QPushButton::clicked, this, [this, post] { compose(post); });
    auto *open = button({}, "external", "tool");
    auto *copy = button({}, "copy", "tool");
    open->setToolTip(tr("northで開く"));
    open->setAccessibleName(tr("northで開く"));
    copy->setToolTip(tr("リンクをコピー"));
    copy->setAccessibleName(tr("リンクをコピー"));
    open->setEnabled(!author.value("handle").toString().isEmpty());
    copy->setEnabled(!author.value("handle").toString().isEmpty());
    connect(open, &QPushButton::clicked, this, [post] { QDesktopServices::openUrl(QUrl(North::postUrl(post))); });
    connect(copy, &QPushButton::clicked, this, [this, post] { QApplication::clipboard()->setText(North::postUrl(post)); m_status->setText(tr("リンクをコピーしました")); });
    auto *actions = new QHBoxLayout;
    actions->addWidget(reply);
    actions->addWidget(open);
    actions->addWidget(copy);
    actions->addStretch();
    m_detailLayout->addLayout(actions);
    m_postReactions->setPost(post);
}

void MainWindow::openPost(const QJsonObject &post, bool pushHistory)
{
    const auto target = North::displayedPost(post);
    if (target.value("id").toString().isEmpty()) return;
    if (pushHistory && m_postPageVisible && target.value("id") == m_openPost.value("id")) return;
    if (pushHistory) m_history.append(currentPage());
    ++m_postGeneration;
    m_replyLoader->reset();
    m_postPageVisible = true;
    m_loadingReplies = false;
    m_openPost = target;
    m_replyIds.clear(); m_replyCursor.clear();
    m_postNotice->hide();
    SmoothScroll::stopFor(m_postScroll);
    clearLayout(m_replyContent);
    m_content->setCurrentWidget(m_postScroll);
    m_back->setVisible(!m_history.isEmpty());
    m_breadcrumb->setText(tr("ポスト"));
    renderPostPage(target);
    inspect(target);
    m_postScroll->verticalScrollBar()->setValue(0);
    loadReplies();
}

void MainWindow::renderPostPage(const QJsonObject &post, const QJsonArray &ancestors)
{
    clearLayout(m_postContent);
    for (const auto &ancestor : ancestors) m_postContent->addWidget(makeCard(ancestor.toObject()));
    auto *card = makeCard(post);
    card->setProperty("pageRole", "main");
    card->setExpandedBody(true);
    card->setSelected(true);
    m_postContent->addWidget(card);
}

void MainWindow::loadReplies(bool more)
{
    if (m_loadingReplies || !m_postPageVisible) return;
    if (more && m_replyCursor.isEmpty()) return;
    m_loadingReplies = true;
    m_replyLoader->setLoading();
    const auto generation = m_postGeneration;
    const QString path = "/2/tweets/" + QString::fromLatin1(QUrl::toPercentEncoding(m_openPost.value("id").toString()));
    QUrlQuery query;
    if (more && !m_replyCursor.isEmpty()) query.addQueryItem("cursor", m_replyCursor);
    m_api.request(this, "GET", path + "/conversation", query, {}, [this, generation, more, path](ApiResult result) {
        if (generation != m_postGeneration || !m_postPageVisible) return;
        const ScrollAnchorGuard keepPosition(m_postScroll, more);
        m_loadingReplies = false;
        if (!result.ok()) {
            m_replyLoader->fail(result.retryAfter);
            if (result.status == 403 && !more) {
                m_api.request(this, "GET", path, {}, {}, [this, generation](ApiResult fallback) {
                    if (generation != m_postGeneration || !m_postPageVisible) return;
                    if (fallback.ok() && !fallback.data().value("id").toString().isEmpty()) {
                        m_openPost = fallback.data(); renderPostPage(m_openPost); inspect(m_openPost);
                    } else if (fallback.status == 403 || fallback.status == 404) {
                        m_openPost = QJsonObject{{"id", m_openPost.value("id")}, {"unavailable", true}};
                        renderPostPage(m_openPost); inspect(m_openPost);
                    }
                    m_postNotice->setText(fallback.ok() ? tr("返信の取得にはOAuthまたは個人トークンが必要です") : fallback.error);
                    m_postNotice->show();
                }, {}, NorthApi::ReadPolicy::Cached);
                return;
            }
            if (result.status == 404) {
                m_openPost = QJsonObject{{"id", m_openPost.value("id")}, {"unavailable", true}};
                renderPostPage(m_openPost); inspect(m_openPost);
                clearLayout(m_replyContent);
            }
            m_postNotice->setText(result.error); m_postNotice->show(); return;
        }
        const auto data = result.data();
        if (!data.value("tweet").isObject() || !data.value("replies").isArray()) {
            m_replyLoader->fail();
            m_postNotice->setText(tr("ポストを取得できませんでした")); m_postNotice->show(); return;
        }
        if (!more) {
            m_openPost = data.value("tweet").toObject();
            renderPostPage(m_openPost, data.value("ancestors").toArray());
            inspect(m_openPost);
            clearLayout(m_replyContent); m_replyIds.clear();
        }
        for (const auto &reply : data.value("replies").toArray()) {
            const auto post = reply.toObject();
            const auto id = post.value("id").toString();
            if (id.isEmpty() || m_replyIds.contains(id)) continue;
            m_replyIds.insert(id); m_replyContent->addWidget(makeCard(post));
        }
        m_replyCursor = data.value("nextCursor").toString();
        m_replyLoader->setCursor(m_replyCursor);
        m_postNotice->hide();
    }, {}, NorthApi::ReadPolicy::Cached);
}

MainWindow::PageState MainWindow::currentPage() const
{
    if (m_postPageVisible) return {"post", {}, m_openPost, m_postScroll->verticalScrollBar()->value()};
    if (m_content->currentWidget() == m_profilePage) return {"profile", m_profilePage->handle(), {}, m_profilePage->position(), m_profilePage->section()};
    return {"base", {}, {}};
}

void MainWindow::openProfile(const QString &handle, bool pushHistory)
{
    if (!m_verified || handle.isEmpty()) return;
    if (pushHistory && m_content->currentWidget() == m_profilePage && m_profilePage->handle().compare(handle, Qt::CaseInsensitive) == 0) return;
    if (pushHistory) m_history.append(currentPage());
    ++m_postGeneration; m_replyLoader->reset(); m_postPageVisible = false;
    clearLayout(m_postContent); clearLayout(m_replyContent); m_openPost = {};
    m_content->setCurrentWidget(m_profilePage);
    m_back->setVisible(!m_history.isEmpty()); m_breadcrumb->setText("@" + handle);
    m_status->setText(tr("プロフィール"));
    resetInspector();
    m_profilePage->openUser(handle, m_me.value("id").toString());
}

void MainWindow::refreshCurrentPage()
{
    if (!m_verified) return;
    m_api.invalidateReads();
    if (m_postPageVisible) { m_postReactions->setPost({}); openPost(m_openPost, false); }
    else if (m_content->currentWidget() == m_profilePage) m_profilePage->reload();
    else if (m_content->currentWidget() == m_notificationsPage) {
        m_notificationsPage->reload(); m_notificationsPage->refreshUnreadCount();
    }
    else if (m_view == "messages") m_messagesPage->reload();
    else loadPosts();
}

void MainWindow::backFromPost()
{
    if (m_history.isEmpty()) return;
    const auto previous = m_history.takeLast();
    if (previous.kind == "post") {
        openPost(previous.post, false);
        m_postScroll->verticalScrollBar()->setValue(previous.position);
        return;
    }
    ++m_postGeneration; m_replyLoader->reset(); m_postPageVisible = false;
    clearLayout(m_postContent); clearLayout(m_replyContent); m_openPost = {};
    m_back->setVisible(!m_history.isEmpty()); resetInspector();
    if (previous.kind == "profile") {
        m_content->setCurrentWidget(m_profilePage);
        m_breadcrumb->setText("@" + previous.handle);
        if (m_profilePage->handle() == previous.handle && m_profilePage->section() == previous.section) m_profilePage->setPosition(previous.position);
        else m_profilePage->openUser(previous.handle, m_me.value("id").toString(), previous.position, previous.section);
        return;
    }
    m_content->setCurrentWidget(basePage());
    const QHash<QString, QString> titles = {{"home", tr("ホーム")}, {"ranked", tr("おすすめ")}, {"search", tr("検索")}, {"profile", tr("プロフィール")}, {"messages", tr("メッセージ")}, {"notifications", tr("通知")}};
    m_breadcrumb->setText(titles.value(m_view));
    if (m_view == "notifications") m_status->setText(tr("通知"));
    if (m_view == "messages") m_inspector->hide();
}

void MainWindow::showError(const QString &message)
{
    setNotice(message, true);
    m_status->setText(tr("読み込みに失敗しました"));
}

void MainWindow::updateSession()
{
    const bool connected = m_api.authenticated();
    m_inlineComposer->setUser(connected ? m_me : QJsonObject{});
    m_sessionName->setText(m_me.value("name").toString());
    m_sessionHandle->setText(connected ? "@" + m_me.value("handle").toString() : QString());
    m_sessionName->setVisible(connected);
    m_sessionHandle->setVisible(connected);
    clearLayout(m_sessionAvatarLayout);
    m_sessionAvatarHost->setVisible(connected);
    if (connected) {
        const auto name = m_me.value("name").toString(), handle = m_me.value("handle").toString();
        m_sessionName->setToolTip("<qt>" + name.toHtmlEscaped() + "</qt>");
        m_sessionHandle->setToolTip("<qt>@" + handle.toHtmlEscaped() + "</qt>");
        QAbstractButton *avatar;
        const auto url = m_me.value("avatarUrl").toString();
        if (!url.isEmpty()) {
            auto *image = new RemoteImage(&m_api, QUrl(url), name);
            image->setCircular(true); image->setPreviewEnabled(false); avatar = image;
        } else avatar = new QPushButton((name.isEmpty() ? handle : name).left(1).toUpper());
        avatar->setObjectName("sessionAvatar"); avatar->setFixedSize(40, 40);
        avatar->setProperty("profileHandle", handle); avatar->setCursor(Qt::PointingHandCursor);
        avatar->setAccessibleName(tr("自分のプロフィール"));
        connect(avatar, &QAbstractButton::clicked, this, [this] { openProfile(m_me.value("handle").toString()); });
        m_sessionAvatarLayout->addWidget(avatar);
    }
    m_loginButton->setText(connected ? tr("ログアウト") : tr("ログイン"));
    m_loginButton->setIcon(appIcon(connected ? "logout" : "user"));
}

void MainWindow::connected()
{
    const auto attempt = m_loginAttempt;
    ++m_viewGeneration;
    m_loading = false;
    clearPosts();
    m_api.request(this, "GET", "/2/users/me", {}, {}, [this, attempt](ApiResult r) {
        if (attempt != m_loginAttempt || m_closing) return;
        if (!r.ok() || r.data().value("handle").toString().isEmpty()) {
            const bool forget = m_usingSaved && (r.status == 401 || r.status == 403);
            returnToLogin(r.error.isEmpty() ? tr("アカウント情報を取得できませんでした") : r.error, forget);
            return;
        }
        m_me = r.data();
        UserRelations::forApi(&m_api)->setViewer(m_me);
        m_verified = true;
        saveSession([this, attempt](bool saved) {
            if (attempt != m_loginAttempt || m_closing) return;
            if (!saved) { returnToLogin(tr("認証情報を保存できませんでした。資格情報ストアを確認してください")); return; }
            m_loginPage->clearCredentials();
            m_loginPage->setBusy(false);
            m_splitter->setEnabled(true);
            m_shell->setCurrentWidget(m_splitter);
            m_titleBar->setMenusVisible(true); statusBar()->show();
            m_notificationsPage->startSession();
            updateSession(); navigate(m_loginView);
        });
    });
}

void MainWindow::disconnectSession()
{
    m_loginView = "home";
    returnToLogin({}, true);
}

void MainWindow::returnToLogin(const QString &message, bool forget)
{
    ++m_loginAttempt;
    m_verified = false; m_usingSaved = false;
    m_api.logout();
    m_me = {};
    ++m_viewGeneration;
    m_loading = false;
    updateSession();
    navigate("home");
    m_status->setText(tr("未接続"));
    m_shell->setCurrentWidget(m_loginPage);
    m_splitter->setEnabled(false);
    m_titleBar->setMenusVisible(false); statusBar()->hide();
    m_loginPage->clearCredentials();
    m_loginPage->setBusy(false, message);
    if (forget) {
        m_savedSession = {};
        const auto attempt = m_loginAttempt;
        m_credentials->erase(this, [this, attempt](CredentialResult result) {
            if (attempt == m_loginAttempt && !result.ok()) m_loginPage->setMessage(tr("保存した認証情報を削除できませんでした"));
        });
    }
    m_loginPage->setSavedAvailable(!m_savedSession.isEmpty());
}

void MainWindow::saveSession(std::function<void(bool)> callback)
{
    const auto session = m_api.sessionData();
    const auto attempt = m_loginAttempt;
    if (session.isEmpty()) { if (callback) callback(false); return; }
    m_credentials->write(this, QJsonDocument(session).toJson(QJsonDocument::Compact), [this, session, attempt, callback](CredentialResult result) {
        if (attempt != m_loginAttempt) return;
        if (result.ok()) m_savedSession = session;
        else if (m_verified && !callback) m_status->setText(tr("認証情報を保存できませんでした"));
        if (callback) callback(result.ok());
    });
}

void MainWindow::restoreSavedLogin()
{
    if (m_closing) return;
    const auto attempt = ++m_loginAttempt;
    m_loginPage->setBusy(true, tr("ログイン情報を確認中…"));
    m_credentials->read(this, [this, attempt](CredentialResult result) {
        if (attempt != m_loginAttempt || m_closing) return;
        if (!result.ok()) { m_loginPage->setBusy(false, result.error); return; }
        if (result.data.isEmpty()) { m_loginPage->setBusy(false); return; }
        const auto state = QJsonDocument::fromJson(result.data).object();
        if (!m_api.restoreSession(state)) {
            returnToLogin(tr("認証情報を再入力してください"), true); return;
        }
        m_savedSession = state;
        m_usingSaved = true;
        m_loginPage->setSavedAvailable(true);
        m_loginPage->setBusy(true, tr("ログイン中…"));
        connected();
    });
}

void MainWindow::login()
{
    m_loginView = m_view;
    returnToLogin();
    if (m_loginView == "messages") m_loginPage->enableMessages();
}

void MainWindow::react(PostCard *card, const QString &kind, bool active)
{
    const auto post = card->post();
    const auto id = post.value("id").toString();
    if (id.isEmpty() || !m_api.authenticated()) return;
    if (!m_api.allowsScope("reactions.write")) { showError(tr("この認証にはリアクションの権限がありません。書き込みを許可して再ログインしてください。")); return; }
    card->setReactionBusy(true);
    QPointer<PostCard> guard(card);
    m_api.request(this, active ? "DELETE" : "POST", "/2/tweets/" + id + "/" + kind, {}, {},
        [this, guard, id, kind](ApiResult r) {
        if (guard) guard->setReactionBusy(false);
        if (!r.ok()) { showError(r.error); return; }
        for (auto *item : findChildren<PostCard *>()) if (item->post().value("id").toString() == id) item->updateReaction(r.data());
        if (m_openPost.value("id").toString() == id) {
            const auto state = r.data();
            for (auto it = state.begin(); it != state.end(); ++it) m_openPost.insert(it.key(), it.value());
        }
        if (m_selected.value("id").toString() == id && guard) {
            if (kind == "retweet") m_postReactions->setPost({});
            inspect(guard->post());
        }
        setNotice({});
    });
}

void MainWindow::compose(const QJsonObject &reply)
{
    if (!m_verified || !m_api.authenticated()) { if (m_shell->currentWidget() != m_loginPage) login(); return; }
    if (!m_api.allowsScope("posts.write")) { showError(tr("この認証には投稿の権限がありません。書き込みを許可して再ログインしてください。")); return; }
    auto *dialog = new QDialog(this);
    dialog->setObjectName("composeDialog"); dialog->setAttribute(Qt::WA_DeleteOnClose);
    dialog->setWindowTitle(reply.isEmpty() ? tr("新規ポスト") : tr("返信")); dialog->resize(620, 390);
    auto *layout = new QVBoxLayout(dialog); layout->setContentsMargins(0, 0, 0, 0); layout->setSpacing(0);
    layout->addWidget(new WindowTitleBar(dialog, false));
    auto *composer = new PostComposer(&m_api, false, reply, dialog); composer->setUser(m_me); layout->addWidget(composer);
    const QString composeView = m_view;
    connect(composer, &PostComposer::posted, dialog, [this, dialog, composeView] {
        dialog->accept(); m_status->setText(tr("ポストしました"));
        if (m_postPageVisible) openPost(m_openPost, false);
        else if (m_content->currentWidget() == m_profilePage) m_profilePage->reload();
        else if (m_content->currentWidget() == m_notificationsPage) m_notificationsPage->reload();
        else if (composeView == m_view && m_view != "messages") loadPosts();
    });
    connect(&m_api, &NorthApi::sessionExpired, dialog, &QDialog::reject);
    connect(&m_api, &NorthApi::loggedOut, dialog, &QDialog::reject);
    dialog->open(); composer->editor()->setFocus();
}

void MainWindow::closeEvent(QCloseEvent *event)
{
    m_closing = true;
    m_api.logout();
    if (m_credentials->busy()) { event->ignore(); setEnabled(false); return; }
    QSettings settings;
    settings.setValue("window/geometry", saveGeometry());
    settings.setValue("window/splitter", m_splitter->saveState());
    QMainWindow::closeEvent(event);
}
