#include "reactionusers.h"
#include "northapi.h"
#include "remoteimage.h"
#include "scrollbehavior.h"
#include "windowchrome.h"
#include <QDialog>
#include <QFrame>
#include <QJsonArray>
#include <QLabel>
#include <QPushButton>
#include <QScrollArea>
#include <QScrollBar>
#include <QShowEvent>
#include <QTimer>
#include <QVBoxLayout>

namespace {
QLabel *plainLabel(const QString &text, const char *name)
{
    auto *label = new QLabel(text); label->setObjectName(name);
    label->setTextFormat(Qt::PlainText); label->setWordWrap(true); return label;
}
void clearRows(QLayout *layout)
{
    while (auto *item = layout->takeAt(0)) {
        if (auto *widget = item->widget()) { widget->hide(); widget->deleteLater(); }
        delete item;
    }
}
}

ReactionUsersList::ReactionUsersList(NorthApi *api, Kind kind, QWidget *parent)
    : QWidget(parent), m_api(api), m_kind(kind)
{
    setObjectName(kind == Reposts ? "repostUsers" : "likeUsers");
    auto *layout = new QVBoxLayout(this); layout->setContentsMargins(0, 0, 0, 0); layout->setSpacing(6);
    m_scroll = new QScrollArea; m_scroll->setObjectName("reactionUsersScroll");
    m_scroll->setWidgetResizable(true); m_scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_scroll->setMinimumHeight(80);
    new SmoothScroll(m_scroll);
    m_loader = new EdgeLoader(m_scroll);
    connect(m_loader, &EdgeLoader::loadMoreRequested, this, [this] { load(true); });
    auto *canvas = new QWidget; canvas->setObjectName("reactionUsersCanvas");
    auto *content = new QVBoxLayout(canvas); content->setContentsMargins(0, 0, 0, 0); content->setSpacing(0);
    auto *rows = new QWidget; m_rows = new QVBoxLayout(rows);
    m_rows->setContentsMargins(0, 0, 0, 0); m_rows->setSpacing(0);
    content->addWidget(rows); content->addStretch();
    m_scroll->setWidget(canvas); layout->addWidget(m_scroll, 1);
    m_notice = plainLabel({}, "reactionUsersNotice"); m_notice->hide(); layout->addWidget(m_notice);
    m_retry = new QPushButton(tr("再取得")); m_retry->setObjectName("reactionUsersRetry");
    m_retry->setCursor(Qt::PointingHandCursor); m_retry->hide(); layout->addWidget(m_retry, 0, Qt::AlignLeft);
    connect(m_retry, &QPushButton::clicked, this, [this] { load(m_failedMore); });
    connect(m_api, &NorthApi::loggedOut, this, [this] { setPost({}); });
}

void ReactionUsersList::setPost(const QString &id)
{
    if (m_postId == id) return;
    ++m_generation; m_postId = id; m_cursor.clear(); m_seen.clear();
    m_loading = m_loaded = m_failedMore = false;
    m_loader->reset(); clearRows(m_rows); m_notice->hide(); m_retry->hide();
    SmoothScroll::stopFor(m_scroll); m_scroll->verticalScrollBar()->setValue(0);
    if (isVisible() && !id.isEmpty()) load();
}

void ReactionUsersList::showEvent(QShowEvent *event)
{
    QWidget::showEvent(event);
    if (!m_loaded && !m_loading && !m_postId.isEmpty()) load();
}

void ReactionUsersList::load(bool more)
{
    if (m_loading || m_postId.isEmpty() || !isVisible() || !m_api->allowsScope("posts.read")
        || (more && m_cursor.isEmpty())) return;
    if (QDateTime::currentDateTimeUtc() < m_retryAt) {
        m_loaded = true; m_failedMore = more;
        m_notice->setText(tr("しばらく待ってから再取得してください")); m_notice->show();
        m_retry->setEnabled(false); m_retry->show();
        const auto generation = m_generation;
        QTimer::singleShot(int(QDateTime::currentDateTimeUtc().msecsTo(m_retryAt)) + 1, Qt::PreciseTimer, this, [this, generation] {
            if (generation == m_generation) m_retry->setEnabled(true);
        });
        return;
    }
    m_loading = true; m_loader->setLoading(); m_retry->hide();
    m_notice->setText(tr("読み込み中…")); m_notice->show();
    const auto generation = m_generation;
    QUrlQuery query; if (more) query.addQueryItem("cursor", m_cursor);
    const QString path = "/2/tweets/" + QString::fromLatin1(QUrl::toPercentEncoding(m_postId))
        + (m_kind == Reposts ? "/retweets" : "/likes");
    m_api->request(this, "GET", path, query, {}, [this, generation, more](ApiResult result) {
        if (generation != m_generation) return;
        m_loading = false; m_loaded = true;
        if (!result.ok() || !result.data().value("items").isArray()) {
            // A malformed successful response must not be reused by a retry.
            if (result.ok()) m_api->invalidateReads();
            m_failedMore = more; m_loader->fail(result.retryAfter);
            m_notice->setText(result.error.isEmpty() ? tr("一覧を取得できませんでした") : result.error);
            m_retryAt = QDateTime::currentDateTimeUtc().addSecs(result.retryAfter);
            m_retry->setEnabled(result.retryAfter == 0); m_retry->show();
            if (result.retryAfter > 0) QTimer::singleShot(result.retryAfter * 1000, Qt::PreciseTimer, this, [this, generation] {
                if (generation == m_generation) m_retry->setEnabled(true);
            });
            return;
        }
        for (const auto &value : result.data().value("items").toArray()) {
            const auto user = value.toObject();
            const auto id = user.value("id").toString(), handle = user.value("handle").toString();
            if (id.isEmpty() || handle.isEmpty() || m_seen.contains(id)) continue;
            m_seen.insert(id); appendUser(user);
        }
        m_cursor = result.data().value("nextCursor").toString();
        m_loader->setCursor(m_cursor);
        m_notice->setText(m_kind == Reposts ? tr("まだリポストされていません") : tr("まだいいねされていません"));
        m_notice->setVisible(m_seen.isEmpty());
    }, {}, NorthApi::ReadPolicy::Cached);
}

void ReactionUsersList::appendUser(const QJsonObject &user)
{
    const auto handle = user.value("handle").toString();
    const auto name = user.value("name").toString().isEmpty() ? handle : user.value("name").toString();
    auto *row = new QFrame; row->setObjectName("reactionUserRow"); row->setProperty("profileHandle", handle);
    auto *layout = new QHBoxLayout(row); layout->setContentsMargins(0, 8, 4, 8); layout->setSpacing(9);
    QAbstractButton *avatar;
    if (!user.value("avatarUrl").toString().isEmpty()) {
        auto *image = new RemoteImage(m_api, QUrl(user.value("avatarUrl").toString()), name);
        image->setCircular(true); image->setPreviewEnabled(false); avatar = image;
    } else {
        auto *fallback = new QPushButton(name.left(1).toUpper()); fallback->setObjectName("reactionAvatar"); avatar = fallback;
    }
    avatar->setFixedSize(36, 36); avatar->setCursor(Qt::PointingHandCursor);
    avatar->setAccessibleName(tr("%1のプロフィール").arg(name));
    avatar->setProperty("profileHandle", handle);
    connect(avatar, &QAbstractButton::clicked, this, [this, handle] { emit profileRequested(handle); });
    layout->addWidget(avatar, 0, Qt::AlignTop);
    auto *identity = new QVBoxLayout; identity->setSpacing(3);
    for (const auto &entry : {qMakePair(name, "authorName"), qMakePair("@" + handle, "authorHandle")}) {
        auto *button = new QPushButton(entry.first); button->setObjectName(entry.second);
        button->setProperty("profileHandle", handle); button->setCursor(Qt::PointingHandCursor);
        // Keep long account names from widening the narrow inspector.
        button->setMinimumWidth(0); button->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
        button->setToolTip("<qt>" + entry.first.toHtmlEscaped() + "</qt>");
        connect(button, &QPushButton::clicked, this, [this, handle] { emit profileRequested(handle); });
        identity->addWidget(button);
    }
    layout->addLayout(identity, 1); m_rows->addWidget(row);
}

PostReactions::PostReactions(NorthApi *api, QWidget *parent) : QWidget(parent), m_api(api)
{
    setObjectName("postReactions");
    auto *layout = new QVBoxLayout(this); layout->setContentsMargins(0, 12, 0, 0); layout->setSpacing(10);
    m_likes = new QPushButton(tr("いいねした人")); m_likes->setObjectName("showLikeUsers");
    m_likes->setCursor(Qt::PointingHandCursor); layout->addWidget(m_likes);
    layout->addWidget(plainLabel(tr("リポストした人"), "reactionUsersHeading"));
    m_reposts = new ReactionUsersList(api, ReactionUsersList::Reposts);
    m_reposts->setFixedHeight(220); layout->addWidget(m_reposts);
    connect(m_reposts, &ReactionUsersList::profileRequested, this, &PostReactions::profileRequested);
    connect(m_likes, &QPushButton::clicked, this, &PostReactions::showLikes);
    connect(m_api, &NorthApi::loggedOut, this, [this] { setPost({}); });
    hide();
}

void PostReactions::setPost(const QJsonObject &post)
{
    const bool unavailable = post.value("deleted").toBool() || post.value("unavailable").toBool()
        || !post.value("hiddenReason").toString().isEmpty() || !m_api->allowsScope("posts.read");
    const auto id = unavailable ? QString() : post.value("id").toString();
    if (id != m_postId) closeLikes();
    m_postId = id; m_reposts->setPost(id); setVisible(!id.isEmpty());
}

void PostReactions::closeLikes()
{
    if (m_dialog) { m_dialog->close(); m_dialog.clear(); }
}

void PostReactions::showLikes()
{
    if (m_postId.isEmpty()) return;
    if (m_dialog) { m_dialog->show(); m_dialog->raise(); m_dialog->activateWindow(); return; }
    auto *dialog = new QDialog(window()); m_dialog = dialog;
    connect(dialog, &QDialog::finished, this, [this, dialog] { if (m_dialog == dialog) m_dialog.clear(); });
    dialog->setObjectName("likeUsersDialog"); dialog->setWindowTitle(tr("いいねした人"));
    dialog->setAttribute(Qt::WA_DeleteOnClose); dialog->setWindowModality(Qt::WindowModal);
    dialog->resize(420, 520); dialog->setMinimumSize(320, 300);
    auto *layout = new QVBoxLayout(dialog); layout->setContentsMargins(0, 0, 0, 0); layout->setSpacing(0);
    auto *bar = new WindowTitleBar(dialog, false); layout->addWidget(bar);
    auto *users = new ReactionUsersList(m_api, ReactionUsersList::Likes);
    auto *content = new QVBoxLayout; content->setContentsMargins(16, 10, 16, 16); content->addWidget(users);
    layout->addLayout(content, 1); users->setPost(m_postId);
    connect(users, &ReactionUsersList::profileRequested, this, [this](const QString &handle) {
        closeLikes(); emit profileRequested(handle);
    });
    dialog->open();
}
