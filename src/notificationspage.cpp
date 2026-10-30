#include "notificationspage.h"
#include "icons.h"
#include "northapi.h"
#include "preferences.h"
#include "postcard.h"
#include "posttime.h"
#include "remoteimage.h"
#include "scrollbehavior.h"
#include <QApplication>
#include <QFrame>
#include <QHBoxLayout>
#include <QHash>
#include <QJsonArray>
#include <QLabel>
#include <QMouseEvent>
#include <QScrollArea>
#include <QScrollBar>
#include <QStyle>
#include <QTabBar>
#include <QVBoxLayout>
#include <algorithm>

namespace {
void empty(QLayout *layout)
{
    while (auto *item = layout->takeAt(0)) {
        if (auto *widget = item->widget()) { widget->hide(); widget->deleteLater(); }
        delete item;
    }
}
QLabel *text(const QString &value, const char *name = "")
{
    auto *label = new QLabel(value); label->setTextFormat(Qt::PlainText);
    label->setObjectName(name); label->setWordWrap(true); label->setMinimumWidth(0); return label;
}
class NotificationRow : public QFrame {
public:
    std::function<void()> open;
protected:
    void mousePressEvent(QMouseEvent *event) override {
        m_pressed = event->button() == Qt::LeftButton;
        m_position = event->globalPosition().toPoint(); QFrame::mousePressEvent(event);
    }
    void mouseReleaseEvent(QMouseEvent *event) override {
        if (m_pressed && event->button() == Qt::LeftButton && open &&
            (event->globalPosition().toPoint() - m_position).manhattanLength() < QApplication::startDragDistance()) open();
        m_pressed = false; QFrame::mouseReleaseEvent(event);
    }
private:
    QPoint m_position;
    bool m_pressed = false;
};
}

NotificationNavButton::NotificationNavButton(QWidget *parent) : QPushButton(appIcon("bell"), tr("通知"), parent)
{
    setObjectName("nav"); setProperty("view", "notifications"); setCheckable(true);
    setCursor(Qt::PointingHandCursor);
    m_badge = new QLabel(this); m_badge->setObjectName("notificationBadge");
    m_badge->setFixedSize(24, 24); m_badge->setAlignment(Qt::AlignCenter);
    m_badge->setAttribute(Qt::WA_TransparentForMouseEvents); m_badge->hide();
    setUnreadCount(0);
}

void NotificationNavButton::setUnreadCount(qint64 count)
{
    m_badge->setText(count > 99 ? "99+" : QString::number(count));
    m_badge->setVisible(count > 0);
    setAccessibleName(count > 0 ? tr("通知、未読%1件").arg(count) : tr("通知"));
    setToolTip(count > 0 ? tr("未読の通知：%1件").arg(count) : tr("通知"));
}

void NotificationNavButton::resizeEvent(QResizeEvent *event)
{
    QPushButton::resizeEvent(event);
    m_badge->move(width() - m_badge->width() - 12, (height() - m_badge->height()) / 2);
}

NotificationsPage::NotificationsPage(NorthApi *api, CardFactory factory, QWidget *parent)
    : QWidget(parent), m_api(api), m_factory(std::move(factory))
{
    setObjectName("notificationsPage");
    auto *layout = new QVBoxLayout(this); layout->setContentsMargins(0, 0, 0, 0); layout->setSpacing(0);
    auto *toolbar = new QFrame; toolbar->setObjectName("toolbar");
    auto *tools = new QHBoxLayout(toolbar); tools->setContentsMargins(0, 0, 12, 0);
    m_tabs = new QTabBar; m_tabs->setObjectName("notificationTabs"); m_tabs->setDrawBase(false);
    m_tabs->addTab(tr("すべて")); m_tabs->addTab(tr("認証済み")); m_tabs->addTab(tr("メンション"));
    tools->addWidget(m_tabs); tools->addStretch();
    m_markRead = new QPushButton(tr("すべて既読")); m_markRead->setObjectName("markNotificationsRead");
    m_markRead->setToolTip(tr("通知をすべて既読にする")); tools->addWidget(m_markRead); layout->addWidget(toolbar);
    connect(m_tabs, &QTabBar::currentChanged, this, [this] { loadItems(); });
    connect(m_markRead, &QPushButton::clicked, this, &NotificationsPage::markAllRead);
    m_new = new QPushButton(tr("新しい通知を表示")); m_new->hide(); layout->addWidget(m_new);
    connect(m_new, &QPushButton::clicked, this, &NotificationsPage::reload);
    auto *noticeRow = new QHBoxLayout;
    m_notice = text({}, "notice"); noticeRow->addWidget(m_notice, 1);
    m_login = new QPushButton(tr("再ログイン")); noticeRow->addWidget(m_login); layout->addLayout(noticeRow);
    connect(m_login, &QPushButton::clicked, this, &NotificationsPage::loginRequested);
    m_scroll = new QScrollArea; m_scroll->setObjectName("notificationsScroll"); m_scroll->setWidgetResizable(true);
    new SmoothScroll(m_scroll); m_loader = new EdgeLoader(m_scroll);
    connect(m_loader, &EdgeLoader::loadMoreRequested, this, [this] { loadItems(true); });
    auto *canvas = new QWidget; canvas->setObjectName("notificationsCanvas");
    auto *content = new QVBoxLayout(canvas); content->setContentsMargins(0, 0, 0, 0); content->setSpacing(0);
    auto *items = new QWidget; m_items = new QVBoxLayout(items); m_items->setContentsMargins(0, 0, 0, 0); m_items->setSpacing(0);
    content->addWidget(items); content->addStretch(); m_scroll->setWidget(canvas); layout->addWidget(m_scroll, 1);
    m_poll.setSingleShot(true);
    connect(&m_poll, &QTimer::timeout, this, [this] {
        if (window()->isMinimized()) scheduleCountPoll(); else refreshUnreadCount(false);
    });
    connect(qApp, &QGuiApplication::applicationStateChanged, this, [this](Qt::ApplicationState state) {
        if (state == Qt::ApplicationActive && m_session) refreshUnreadCount(false);
        else m_poll.stop();
    });
    connect(Preferences::instance(), &Preferences::notificationIntervalChanged, this, [this](int seconds) {
        m_poll.stop();
        if (seconds > 0 && m_session && !window()->isMinimized() && qApp->applicationState() == Qt::ApplicationActive)
            refreshUnreadCount(false);
    });
    connect(m_api, &NorthApi::loggedOut, this, &NotificationsPage::clear);
    clear();
}

void NotificationsPage::clear()
{
    ++m_generation; ++m_listGeneration; ++m_countVersion;
    m_poll.stop(); m_session = false; m_denied = false; m_loading = false; m_loaded = false;
    m_lastCount.invalidate();
    m_countLoading = false; m_countAgain = false; m_marking = false; m_anyUnread = false; m_failures = 0;
    m_unread = 0; m_cursor.clear(); m_markKey.clear(); m_seen.clear(); m_loader->reset();
    empty(m_items); showNotice({}); m_new->hide(); m_login->hide(); m_markRead->hide();
    emit unreadCountChanged(0);
}

void NotificationsPage::startSession()
{
    clear(); m_session = true;
    m_denied = !m_api->allowsScope("notifications.read");
    updateControls();
    if (!m_denied) refreshUnreadCount();
}

void NotificationsPage::showNotice(const QString &message) { m_notice->setText(message); m_notice->setVisible(!message.isEmpty()); }

void NotificationsPage::updateControls()
{
    m_login->setVisible(m_session && m_denied);
    m_markRead->setVisible(m_session && !m_denied && m_api->allowsScope("notifications.write"));
    m_markRead->setEnabled(!m_marking && (m_unread > 0 || m_anyUnread));
}

void NotificationsPage::scheduleCountPoll(int delay)
{
    const int interval = Preferences::instance()->notificationInterval() * 1000;
    if (m_session && !m_denied && interval > 0 && qApp->applicationState() == Qt::ApplicationActive)
        m_poll.start(delay < 0 ? interval : delay);
}

void NotificationsPage::refreshUnreadCount(bool force)
{
    if (!m_session || m_denied || !m_api->authenticated()) return;
    const int interval = Preferences::instance()->notificationInterval() * 1000;
    if (!force && interval == 0) return;
    if (m_countLoading || m_marking) {
        // Only a write requires another count after the current request.
        if (m_marking || m_countRequestVersion != m_countVersion) m_countAgain = true;
        return;
    }
    if (!force && m_lastCount.isValid() && m_lastCount.elapsed() < interval) {
        scheduleCountPoll(int(interval - m_lastCount.elapsed())); return;
    }
    m_poll.stop(); m_countLoading = true; m_countAgain = false;
    const auto generation = m_generation, version = m_countVersion;
    m_countRequestVersion = version;
    m_api->request(this, "GET", "/2/notifications/unread_count", {}, {}, [this, generation, version](ApiResult result) {
        if (generation != m_generation) return;
        m_countLoading = false;
        if (version == m_countVersion) {
            const auto value = result.data().value("count");
            const auto count = value.toInteger(-1);
            if (result.ok() && value.isDouble() && count >= 0) {
                const bool hadCount = m_lastCount.isValid();
                const auto previous = m_unread; m_unread = count; m_failures = 0;
                m_lastCount.start();
                emit unreadCountChanged(count); updateControls();
                if (hadCount && count != previous && m_loaded) {
                    if (isVisible() && m_scroll->verticalScrollBar()->value() == 0 && !m_loading) loadItems();
                    else if (count > previous) m_new->show();
                }
                scheduleCountPoll();
            } else if (result.status == 403) {
                m_denied = true; updateControls();
                showNotice(tr("通知の権限が必要です。再ログインしてください"));
            } else if (result.status != 401 && result.status != 404) {
                ++m_failures;
                const int seconds = std::clamp(std::max(result.retryAfter, std::min(300, 60 * (1 << std::min(m_failures, 3)))), 60, 86400);
                scheduleCountPoll(std::max(seconds, Preferences::instance()->notificationInterval()) * 1000);
            }
        }
        if (m_countAgain && !m_marking && !m_denied) refreshUnreadCount();
    });
}

void NotificationsPage::reload()
{
    loadItems(); refreshUnreadCount(false);
}

void NotificationsPage::loadItems(bool more)
{
    if (!m_session) return;
    if (m_denied || !m_api->allowsScope("notifications.read")) {
        m_denied = true; showNotice(tr("通知の権限が必要です。再ログインしてください")); updateControls(); return;
    }
    if (more && (m_loading || m_cursor.isEmpty())) return;
    if (!more) {
        ++m_listGeneration; m_loader->reset(); m_seen.clear(); m_cursor.clear(); m_anyUnread = false; m_loaded = false;
        empty(m_items); m_new->hide(); SmoothScroll::stopFor(m_scroll); m_scroll->verticalScrollBar()->setValue(0);
    }
    m_loading = true; m_loader->setLoading(); showNotice(tr("読み込み中…"));
    const auto generation = m_generation, listGeneration = m_listGeneration;
    QUrlQuery query; const QStringList tabs{"all", "verified", "mentions"}; query.addQueryItem("tab", tabs[m_tabs->currentIndex()]);
    if (more) query.addQueryItem("cursor", m_cursor);
    m_api->request(this, "GET", "/2/notifications", query, {}, [this, generation, listGeneration](ApiResult result) {
        if (generation != m_generation || listGeneration != m_listGeneration) return;
        m_loading = false;
        if (!result.ok() || !result.data().value("items").isArray()) {
            m_loader->fail(result.retryAfter); showNotice(result.error.isEmpty() ? tr("通知を取得できませんでした") : result.error);
            if (result.status == 403) { m_denied = true; m_poll.stop(); updateControls(); }
            return;
        }
        for (const auto &value : result.data().value("items").toArray()) {
            const auto notification = value.toObject(); const auto id = notification.value("id").toString();
            if (id.isEmpty() || m_seen.contains(id)) continue;
            m_seen.insert(id); if (!notification.value("read").toBool()) m_anyUnread = true;
            appendNotification(notification);
        }
        m_loaded = true; m_cursor = result.data().value("nextCursor").toString(); m_loader->setCursor(m_cursor);
        showNotice(m_seen.isEmpty() ? tr("通知はありません") : QString()); updateControls();
    });
}

void NotificationsPage::appendNotification(const QJsonObject &notification)
{
    const auto kind = notification.value("kind").toString();
    const auto actors = notification.value("actors").toArray();
    const auto tweet = notification.value("tweet").toObject();
    const bool singlePost = !tweet.value("id").toString().isEmpty() && notification.value("groupCount").toInt(1) <= 1 && notification.value("targetCount").toInt(1) <= 1;
    auto *row = new NotificationRow; row->setObjectName("notificationRow");
    row->setProperty("unread", !notification.value("read").toBool()); row->setProperty("notificationId", notification.value("id").toString());
    if (singlePost) row->open = [this, tweet] { emit postRequested(tweet); };
    else if (kind == "FOLLOW" && !actors.isEmpty()) {
        const auto handle = actors[0].toObject().value("handle").toString();
        if (!handle.isEmpty()) row->open = [this, handle] { emit profileRequested(handle); };
    }
    if (row->open) row->setCursor(Qt::PointingHandCursor);
    auto *layout = new QHBoxLayout(row); layout->setContentsMargins(16, 12, 16, 12); layout->setSpacing(12);
    const auto iconName = kind == "LIKE" ? "heart" : kind == "RETWEET" ? "repost" : kind == "FOLLOW" ? "user" : kind == "POST" ? "bell" : "reply";
    const auto color = kind == "LIKE" ? "#e2547b" : kind == "RETWEET" ? "#16866a" : "#1c71d8";
    auto *icon = new QLabel; icon->setPixmap(appIcon(iconName, color).pixmap(24, 24)); layout->addWidget(icon, 0, Qt::AlignTop);
    auto *body = new QVBoxLayout; body->setSpacing(8); layout->addLayout(body, 1);
    auto *head = new QHBoxLayout; head->setSpacing(8);
    for (int i = 0; i < std::min(3, int(actors.size())); ++i) {
        const auto actor = actors[i].toObject(); const auto handle = actor.value("handle").toString();
        if (!actor.value("avatarUrl").toString().isEmpty()) {
            auto *avatar = new RemoteImage(m_api, QUrl(actor.value("avatarUrl").toString()), actor.value("name").toString());
            avatar->setCircular(true); avatar->setFixedSize(32, 32); avatar->setPreviewEnabled(false);
            connect(avatar, &QAbstractButton::clicked, this, [this, handle] { emit profileRequested(handle); }); head->addWidget(avatar);
        } else {
            auto *avatar = new QPushButton(actor.value("name").toString().left(1).toUpper()); avatar->setObjectName("notificationAvatar");
            avatar->setFixedSize(32, 32); avatar->setCursor(Qt::PointingHandCursor);
            connect(avatar, &QPushButton::clicked, this, [this, handle] { emit profileRequested(handle); }); head->addWidget(avatar);
        }
    }
    head->addStretch(); head->addWidget(postTimeLabel(notification.value("createdAt").toString())); body->addLayout(head);
    if (!actors.isEmpty()) {
        const auto actor = actors[0].toObject();
        auto *name = new QPushButton(actor.value("name").toString(actor.value("handle").toString()).replace('&', "&&"));
        name->setObjectName("authorName"); name->setCursor(Qt::PointingHandCursor);
        name->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
        connect(name, &QPushButton::clicked, this, [this, actor] { emit profileRequested(actor.value("handle").toString()); });
        body->addWidget(name);
    }
    const QHash<QString, QString> descriptions{{"FOLLOW", tr("フォローしました")}, {"LIKE", tr("いいねしました")},
        {"RETWEET", tr("リポストしました")}, {"REPLY", tr("返信しました")}, {"MENTION", tr("あなたにメンションしました")},
        {"QUOTE", tr("引用しました")}, {"POST", tr("新しくポストしました")}};
    QString description = descriptions.value(kind, tr("新しい通知"));
    const int actorCount = notification.value("actorCount").toInt(int(actors.size()));
    if (actorCount > 1) description = tr("ほか%1人が ").arg(actorCount - 1) + description;
    const int groupCount = notification.value("groupCount").toInt(1);
    if (groupCount > 1) description += tr("（%1件）").arg(groupCount);
    body->addWidget(text(description, "notificationText"));
    if (singlePost) {
        auto *card = m_factory(tweet);
        card->layout()->setContentsMargins(0, 8, 0, 0);
        card->setStyleSheet("QFrame#postCard { background: transparent; border: none; }");
        body->addWidget(card);
    }
    m_items->addWidget(row);
}

void NotificationsPage::markAllRead()
{
    if (!m_session || m_marking || !m_api->allowsScope("notifications.write")) return;
    m_marking = true; ++m_countVersion; m_poll.stop(); updateControls();
    if (m_markKey.isEmpty()) m_markKey = NorthApi::newIdempotencyKey();
    const auto generation = m_generation;
    m_api->request(this, "POST", "/2/notifications/read", {}, {}, [this, generation](ApiResult result) {
        if (generation != m_generation) return;
        m_marking = false;
        if (result.ok()) {
            m_markKey.clear();
            for (auto *row : findChildren<QFrame *>("notificationRow")) {
                row->setProperty("unread", false); row->style()->unpolish(row); row->style()->polish(row); row->update();
            }
            m_anyUnread = false; showNotice({});
        } else showNotice(result.error);
        updateControls(); refreshUnreadCount();
    }, m_markKey);
}
