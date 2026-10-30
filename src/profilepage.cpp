#include "profilepage.h"
#include "northapi.h"
#include "postcard.h"
#include "remoteimage.h"
#include "scrollbehavior.h"
#include "userrelations.h"
#include <QDateTime>
#include <QDesktopServices>
#include <QFrame>
#include <QJsonArray>
#include <QLabel>
#include <QLocale>
#include <QPushButton>
#include <QScrollArea>
#include <QScrollBar>
#include <QSignalBlocker>
#include <QTabBar>
#include <QTimer>
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
QLabel *label(const QString &value, const char *name = "")
{
    auto *widget = new QLabel(value); widget->setObjectName(name); widget->setTextFormat(Qt::PlainText);
    widget->setWordWrap(true); return widget;
}
QString count(const QJsonObject &user, const QString &key)
{
    return user.contains(key) ? QLocale().toString(user.value(key).toInteger()) : QStringLiteral("—");
}
}

ProfilePage::ProfilePage(NorthApi *api, CardFactory factory, QWidget *parent)
    : QWidget(parent), m_api(api), m_factory(std::move(factory))
{
    setObjectName("profilePage");
    auto *layout = new QVBoxLayout(this); layout->setContentsMargins(0, 0, 0, 0);
    m_scroll = new QScrollArea; m_scroll->setObjectName("profileScroll"); m_scroll->setWidgetResizable(true);
    new SmoothScroll(m_scroll);
    m_loader = new EdgeLoader(m_scroll);
    connect(m_loader, &EdgeLoader::loadMoreRequested, this, [this] { loadItems(true); });
    auto *canvas = new QWidget; canvas->setObjectName("profileCanvas");
    auto *content = new QVBoxLayout(canvas); content->setContentsMargins(0, 0, 0, 0); content->setSpacing(0);
    m_header = new QWidget;
    m_headerLayout = new QVBoxLayout(m_header); m_headerLayout->setContentsMargins(0, 0, 0, 0); m_headerLayout->setSpacing(0);
    content->addWidget(m_header);
    m_tabs = new HorizontalWheelTabBar; m_tabs->setObjectName("profileTabs"); m_tabs->setExpanding(true);
    m_tabs->setDrawBase(false);
    m_tabs->addTab(tr("ポスト")); m_tabs->addTab(tr("フォロー中")); m_tabs->addTab(tr("フォロワー"));
    content->addWidget(m_tabs);
    connect(m_tabs, &QTabBar::currentChanged, this, [this] { m_restorePosition = 0; loadItems(); });
    m_notice = label({}, "notice"); m_notice->hide(); content->addWidget(m_notice);
    auto *items = new QWidget;
    m_items = new QVBoxLayout(items); m_items->setContentsMargins(0, 0, 0, 0); m_items->setSpacing(0);
    content->addWidget(items); content->addStretch();
    m_scroll->setWidget(canvas); layout->addWidget(m_scroll);
    connect(m_api, &NorthApi::loggedOut, this, &ProfilePage::clear);
    auto *relations = UserRelations::forApi(api);
    connect(relations, &UserRelations::changed, this, [this, relations](const QString &handle) {
        if (handle.compare(m_handle, Qt::CaseInsensitive) != 0 || m_user.isEmpty()) return;
        auto updated = m_user;
        const auto state = relations->user(handle);
        for (const auto &key : {"following", "followerCount", "followingCount", "protected"})
            if (state.contains(key)) updated.insert(key, state.value(key));
        if (updated == m_user) return;
        const bool lostPrivateAccess = m_user.value("protected").toBool() && m_user.value("following").toBool()
            && !updated.value("following").toBool() && m_user.value("id").toString() != m_selfId;
        m_user = updated;
        if (m_headerLayout->count()) { const ScrollAnchorGuard keepPosition(m_scroll); renderHeader(); }
        if (lostPrivateAccess) {
            ++m_listGeneration; m_loading = false; m_seen.clear(); m_cursor.clear(); m_loader->reset(); empty(m_items);
            m_notice->setText(tr("このアカウントの一覧は非公開です")); m_notice->show();
        }
    });
    clear();
}

int ProfilePage::position() const { return m_scroll->verticalScrollBar()->value(); }
void ProfilePage::setPosition(int position) { m_scroll->verticalScrollBar()->setValue(position); }
int ProfilePage::section() const { return m_tabs->currentIndex(); }

void ProfilePage::clear()
{
    ++m_generation; ++m_listGeneration;
    m_loading = false; m_handle.clear(); m_selfId.clear(); m_user = {}; m_cursor.clear(); m_seen.clear();
    m_loader->reset(); empty(m_headerLayout); empty(m_items); m_notice->hide(); m_tabs->hide();
}

void ProfilePage::openUser(QString handle, QString selfId, int position, int section)
{
    clear();
    m_handle = handle; m_selfId = selfId; m_restorePosition = position;
    const QSignalBlocker blocker(m_tabs); m_tabs->setCurrentIndex(std::clamp(section, 0, 2));
    m_notice->setText(tr("読み込み中…")); m_notice->show();
    setPosition(0);
    const auto generation = m_generation;
    const auto relationVersion = UserRelations::forApi(m_api)->version(handle);
    m_api->request(this, "GET", "/2/users/" + QString::fromLatin1(QUrl::toPercentEncoding(handle)), {}, {}, [this, generation, relationVersion](ApiResult result) {
        if (generation != m_generation) return;
        if (!result.ok() || result.data().value("handle").toString().compare(m_handle, Qt::CaseInsensitive) != 0) {
            m_notice->setText(result.error.isEmpty() ? tr("プロフィールを取得できませんでした") : result.error); return;
        }
        m_user = result.data();
        UserRelations::forApi(m_api)->observe(m_user, true, relationVersion);
        renderHeader(); m_tabs->show(); emit titleChanged(m_user.value("name").toString());
        loadItems();
    }, {}, NorthApi::ReadPolicy::Cached);
}

void ProfilePage::reload()
{
    m_api->invalidateReads();
    if (!m_handle.isEmpty()) openUser(m_handle, m_selfId, position(), section());
}

void ProfilePage::renderHeader()
{
    empty(m_headerLayout);
    auto *header = new QWidget;
    auto *layout = new QVBoxLayout(header); layout->setContentsMargins(0, 0, 0, 0); layout->setSpacing(0);
    const auto headerUrl = m_user.value("headerUrl").toString();
    if (!headerUrl.isEmpty()) {
        auto *cover = new RemoteImage(m_api, QUrl(headerUrl), tr("ヘッダー画像"));
        cover->setObjectName("profileCoverImage"); cover->setCropToFill(true); cover->setCornerRadius(0);
        cover->setFixedHeight(200); layout->addWidget(cover);
    } else {
        auto *cover = label({}, "profileCover"); cover->setFixedHeight(200); layout->addWidget(cover);
    }
    auto *info = new QWidget;
    auto *body = new QVBoxLayout(info); body->setContentsMargins(16, 12, 16, 16); body->setSpacing(8);
    auto *relationRow = new QHBoxLayout; relationRow->setContentsMargins(140, 0, 0, 0);
    relationRow->addStretch();
    QStringList relations;
    if (m_user.value("id").toString() == m_selfId) relations << tr("自分のプロフィール");
    else {
        if (m_user.value("following").toBool()) relations << tr("フォロー中");
        if (m_user.value("followedBy").toBool()) relations << tr("フォローされています");
        if (m_user.value("blocking").toBool()) relations << tr("ブロック中");
    }
    if (!relations.isEmpty()) {
        auto *relation = label(relations.join(" · "), "profileRelation");
        relation->setWordWrap(false); relationRow->addWidget(relation);
    }
    relationRow->addWidget(new FollowButton(m_user, m_api, m_selfId, info));
    relationRow->addStrut(36);
    body->addLayout(relationRow); body->addSpacing(12);
    auto name = m_user.value("name").toString();
    if (m_user.value("verified").toBool()) name += tr("  ✓");
    body->addWidget(label(name, "profileName"));
    auto handle = "@" + m_handle;
    if (m_user.value("protected").toBool()) handle += tr(" · 非公開");
    body->addWidget(label(handle, "profileHandle"));
    if (!m_user.value("bio").toString().isEmpty()) {
        auto *bio = label(m_user.value("bio").toString(), "profileBio");
        bio->setTextInteractionFlags(Qt::TextSelectableByMouse); body->addWidget(bio);
    }
    QStringList meta;
    if (!m_user.value("location").toString().isEmpty()) meta << m_user.value("location").toString();
    const auto created = QDateTime::fromString(m_user.value("createdAt").toString(), Qt::ISODate);
    if (created.isValid()) meta << tr("%1から利用").arg(created.toLocalTime().toString("yyyy年M月"));
    if (!meta.isEmpty()) body->addWidget(label(meta.join("  ·  "), "profileMeta"));
    const QUrl website(m_user.value("website").toString());
    if ((website.scheme() == "https" || website.scheme() == "http") && website.isValid() && !website.host().isEmpty() && website.userInfo().isEmpty()) {
        auto *link = new QPushButton(website.toDisplayString()); link->setObjectName("identity");
        link->setCursor(Qt::PointingHandCursor); link->setToolTip(website.toDisplayString());
        connect(link, &QPushButton::clicked, this, [website] { QDesktopServices::openUrl(website); }); body->addWidget(link);
    }
    auto *counts = new QHBoxLayout; counts->setSpacing(20);
    auto *following = new QPushButton(tr("%1 フォロー中").arg(count(m_user, "followingCount")));
    auto *followers = new QPushButton(tr("%1 フォロワー").arg(count(m_user, "followerCount")));
    for (auto *button : {following, followers}) { button->setObjectName("profileCount"); button->setCursor(Qt::PointingHandCursor); counts->addWidget(button); }
    following->setProperty("section", "following"); followers->setProperty("section", "followers");
    connect(following, &QPushButton::clicked, this, [this] { m_tabs->setCurrentIndex(1); });
    connect(followers, &QPushButton::clicked, this, [this] { m_tabs->setCurrentIndex(2); });
    counts->addStretch(); body->addLayout(counts);
    body->addWidget(label(tr("%1 ポスト").arg(count(m_user, "tweetCount")), "muted"));
    layout->addWidget(info);
    auto *avatarFrame = new QFrame(header); avatarFrame->setObjectName("profileAvatarFrame");
    avatarFrame->setGeometry(12, 134, 132, 132);
    auto *avatarLayout = new QVBoxLayout(avatarFrame); avatarLayout->setContentsMargins(0, 0, 0, 0);
    const auto avatarUrl = m_user.value("avatarUrl").toString();
    if (!avatarUrl.isEmpty()) {
        auto *avatar = new RemoteImage(m_api, QUrl(avatarUrl), m_user.value("name").toString());
        avatar->setObjectName("profileAvatarImage"); avatar->setCircular(true); avatar->setFixedSize(124, 124); avatarLayout->addWidget(avatar);
    } else {
        auto *avatar = label(m_user.value("name").toString().left(1).toUpper(), "profileAvatar");
        avatar->setAlignment(Qt::AlignCenter); avatar->setFixedSize(124, 124); avatarLayout->addWidget(avatar);
    }
    avatarFrame->raise(); m_headerLayout->addWidget(header);
}

void ProfilePage::loadItems(bool more)
{
    if (m_user.isEmpty() || (more && (m_loading || m_cursor.isEmpty()))) return;
    if (!more) { ++m_listGeneration; m_loader->reset(); m_seen.clear(); empty(m_items); m_cursor.clear(); }
    m_loading = true; m_loader->setLoading();
    if (!more) { m_notice->setText(tr("読み込み中…")); m_notice->show(); }
    const auto generation = m_generation, listGeneration = m_listGeneration;
    const QString suffix = section() == 1 ? "/following" : section() == 2 ? "/followers" : "/tweets";
    QUrlQuery query; if (more) query.addQueryItem("cursor", m_cursor);
    m_api->request(this, "GET", "/2/users/" + QString::fromLatin1(QUrl::toPercentEncoding(m_handle)) + suffix, query, {},
        [this, generation, listGeneration, more](ApiResult result) {
        if (generation != m_generation || listGeneration != m_listGeneration) return;
        const ScrollAnchorGuard keepPosition(m_scroll, more);
        m_loading = false;
        if (!result.ok() || !result.data().value("items").isArray()) {
            m_loader->fail(result.retryAfter); m_notice->setText(result.error.isEmpty() ? tr("一覧を取得できませんでした") : result.error); return;
        }
        for (const auto &value : result.data().value("items").toArray()) {
            const auto item = value.toObject(); const auto id = item.value("id").toString();
            if (id.isEmpty() || m_seen.contains(id)) continue;
            m_seen.insert(id);
            if (section() == 0) m_items->addWidget(m_factory(item)); else appendUser(item);
        }
        m_cursor = result.data().value("nextCursor").toString();
        m_loader->setCursor(m_cursor);
        QString message;
        if (m_seen.isEmpty()) {
            const bool restricted = m_user.value("protected").toBool() && !m_user.value("following").toBool() && m_user.value("id").toString() != m_selfId;
            message = restricted ? tr("このアカウントの一覧は非公開です") : section() == 0 ? tr("ポストはありません") : tr("アカウントはありません");
        }
        m_notice->setText(message); m_notice->setVisible(!message.isEmpty());
        if (!more) {
            const int position = m_restorePosition; m_restorePosition = 0;
            QTimer::singleShot(0, this, [this, position, generation, listGeneration] {
                if (generation == m_generation && listGeneration == m_listGeneration) setPosition(position);
            });
        }
    });
}

void ProfilePage::appendUser(const QJsonObject &user)
{
    auto *row = new QFrame; row->setObjectName("userRow"); row->setProperty("scrollAnchor", true);
    auto *layout = new QHBoxLayout(row); layout->setContentsMargins(16, 12, 16, 12); layout->setSpacing(12);
    const auto handle = user.value("handle").toString();
    if (!user.value("avatarUrl").toString().isEmpty()) {
        auto *avatar = new RemoteImage(m_api, QUrl(user.value("avatarUrl").toString()), user.value("name").toString());
        avatar->setCircular(true); avatar->setFixedSize(48, 48); avatar->setPreviewEnabled(false);
        connect(avatar, &QAbstractButton::clicked, this, [this, handle] { emit profileRequested(handle); }); layout->addWidget(avatar, 0, Qt::AlignTop);
    } else {
        auto *avatar = new QPushButton(user.value("name").toString().left(1).toUpper());
        avatar->setObjectName("authorAvatar"); avatar->setFixedSize(48, 48); avatar->setCursor(Qt::PointingHandCursor);
        connect(avatar, &QPushButton::clicked, this, [this, handle] { emit profileRequested(handle); }); layout->addWidget(avatar, 0, Qt::AlignTop);
    }
    auto *identity = new QVBoxLayout;
    auto *name = new QPushButton(user.value("name").toString()); name->setObjectName("authorName");
    name->setProperty("profileHandle", handle); name->setCursor(Qt::PointingHandCursor);
    connect(name, &QPushButton::clicked, this, [this, handle] { emit profileRequested(handle); });
    identity->addWidget(name);
    auto *handleButton = new QPushButton("@" + handle); handleButton->setObjectName("authorHandle");
    handleButton->setCursor(Qt::PointingHandCursor);
    connect(handleButton, &QPushButton::clicked, this, [this, handle] { emit profileRequested(handle); }); identity->addWidget(handleButton);
    if (!user.value("bio").toString().isEmpty()) identity->addWidget(label(user.value("bio").toString()));
    layout->addLayout(identity, 1); layout->addWidget(new FollowButton(user, m_api, m_selfId, row)); m_items->addWidget(row);
}
