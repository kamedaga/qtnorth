#include "postcard.h"
#include "icons.h"
#include "post.h"
#include "posttime.h"
#include "posttext.h"
#include "linkpreview.h"
#include "remoteimage.h"
#include <QApplication>
#include <QHBoxLayout>
#include <QGridLayout>
#include <QDesktopServices>
#include <QSet>
#include <QJsonArray>
#include <QLabel>
#include <QMouseEvent>
#include <QPushButton>
#include <QRegularExpression>
#include <QUrlQuery>
#include <QStyle>
#include <QTimer>
#include <QVBoxLayout>

namespace {
class IdentityLabel : public QPushButton {
public:
    using QPushButton::QPushButton;
    QSize minimumSizeHint() const override { return {0, QPushButton::minimumSizeHint().height()}; }
};
QLabel *plainLabel(const QString &text, const char *name = "")
{
    auto *label = new QLabel(text);
    label->setTextFormat(Qt::PlainText);
    label->setObjectName(name);
    return label;
}
}

PostCard::PostCard(QJsonObject post, bool interactive, NorthApi *api, QWidget *parent)
    : QFrame(parent), m_post(North::displayedPost(post))
{
    setObjectName("postCard");
    setProperty("selected", false);
    setCursor(Qt::PointingHandCursor);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Minimum);
    auto *outer = new QVBoxLayout(this);
    outer->setContentsMargins(16, 12, 16, 12);
    outer->setSpacing(2);
    if (!post.value("retweetOf").toObject().isEmpty()) {
        const auto author = post.value("author").toObject();
        auto *context = new QPushButton(tr("%1 がリポスト").arg(author.value("name").toString()));
        context->setObjectName("repostContext");
        context->setIcon(appIcon("repost")); context->setIconSize(QSize(14, 14));
        context->setCursor(Qt::PointingHandCursor);
        context->setProperty("profileHandle", author.value("handle").toString());
        connect(context, &QPushButton::clicked, this, [this, author] { emit profileRequested(author.value("handle").toString()); });
        auto *contextRow = new QHBoxLayout; contextRow->setContentsMargins(42, 0, 0, 2);
        contextRow->addWidget(context, 0, Qt::AlignLeft); contextRow->addStretch(); outer->addLayout(contextRow);
    }
    const auto author = m_post.value("author").toObject();
    const QString name = author.value("name").toString();
    auto *head = new QHBoxLayout;
    QWidget *avatar = nullptr;
    if (!author.value("avatarUrl").toString().isEmpty()) {
        auto *image = new RemoteImage(api, QUrl(author.value("avatarUrl").toString()), name);
        image->setPreviewEnabled(false);
        image->setCircular(true);
        connect(image, &QAbstractButton::clicked, this, [this, author] { emit profileRequested(author.value("handle").toString()); });
        avatar = image;
    } else {
        auto *initial = new QPushButton(name.left(1).toUpper());
        initial->setObjectName("authorAvatar");
        connect(initial, &QPushButton::clicked, this, [this, author] { emit profileRequested(author.value("handle").toString()); });
        avatar = initial;
    }
    avatar->setProperty("profileHandle", author.value("handle").toString());
    avatar->setAccessibleName(tr("@%1 のプロフィール").arg(author.value("handle").toString()));
    avatar->setFixedSize(48, 48);
    auto *row = new QHBoxLayout;
    row->setSpacing(12);
    row->addWidget(avatar, 0, Qt::AlignTop);
    auto *layout = new QVBoxLayout;
    layout->setSpacing(4);
    row->addLayout(layout, 1);
    outer->addLayout(row);
    auto *identity = new QHBoxLayout;
    identity->setSpacing(4);
    auto *authorName = new IdentityLabel(name);
    authorName->setObjectName("authorName");
    auto *handle = new IdentityLabel("@" + author.value("handle").toString());
    handle->setObjectName("authorHandle");
    for (auto *identityLabel : {authorName, handle}) {
        identityLabel->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Preferred);
        identityLabel->setToolTip(identityLabel->text());
        identityLabel->setCursor(Qt::PointingHandCursor);
        identityLabel->setProperty("profileHandle", author.value("handle").toString());
        connect(identityLabel, &QPushButton::clicked, this, [this, author] { emit profileRequested(author.value("handle").toString()); });
        identity->addWidget(identityLabel);
    }
    identity->addStretch(1);
    head->addLayout(identity, 1);
    auto *time = postTimeLabel(m_post.value("createdAt").toString());
    head->addWidget(time, 0, Qt::AlignTop);
    layout->addLayout(head);
    addText(layout, m_post, api);
    const bool hidden = !m_post.value("hiddenReason").toString().isEmpty()
        || m_post.value("deleted").toBool() || m_post.value("unavailable").toBool();
    if (!hidden && m_post.value("quoted").isObject()) {
        const auto quoted = m_post.value("quoted").toObject();
        auto *quoteFrame = new QFrame; quoteFrame->setObjectName("quote");
        auto *quoteLayout = new QVBoxLayout(quoteFrame);
        auto *quotedName = new IdentityLabel(quoted.value("author").toObject().value("name").toString());
        quotedName->setObjectName("authorName");
        connect(quotedName, &QPushButton::clicked, this, [this, quoted] { emit profileRequested(quoted.value("author").toObject().value("handle").toString()); });
        quoteLayout->addWidget(quotedName);
        addText(quoteLayout, quoted, api);
        layout->addWidget(quoteFrame);
        if (quoted.value("hiddenReason").toString().isEmpty() && !quoted.value("unavailable").toBool() && !quoted.value("deleted").toBool()) addMedia(layout, quoted, api);
    }
    if (!hidden) addMedia(layout, m_post, api);
    auto *actions = new QHBoxLayout;
    actions->setSpacing(12);
    auto *reply = new QPushButton(appIcon("reply"), QString::number(m_post.value("replyCount").toInt()));
    m_repost = new QPushButton;
    m_like = new QPushButton;
    for (auto *button : {reply, m_repost, m_like}) {
        button->setObjectName("reaction");
        button->setCursor(Qt::PointingHandCursor);
        button->setEnabled(interactive && !hidden);
        actions->addWidget(button);
    }
    reply->setToolTip(tr("返信"));
    connect(reply, &QPushButton::clicked, this, [this] { emit replyRequested(m_post); });
    connect(m_like, &QPushButton::clicked, this, [this] { emit reactionRequested(this, "like", m_post.value("liked").toBool()); });
    connect(m_repost, &QPushButton::clicked, this, [this] { emit reactionRequested(this, "retweet", m_post.value("retweeted").toBool()); });
    actions->addStretch();
    auto *detail = new QPushButton(appIcon("chevron"), QString());
    detail->setObjectName("reaction");
    detail->setToolTip(tr("詳細"));
    detail->setAccessibleName(tr("詳細"));
    connect(detail, &QPushButton::clicked, this, [this] { emit selected(m_post); });
    actions->addWidget(detail);
    layout->addLayout(actions);
    updateButtons();
}

void PostCard::updateButtons()
{
    const bool liked = m_post.value("liked").toBool(), reposted = m_post.value("retweeted").toBool();
    m_like->setIcon(appIcon("heart", liked ? "#e2547b" : "#888888"));
    m_like->setText(QString::number(m_post.value("likeCount").toInt()));
    m_like->setToolTip(liked ? tr("いいねを取り消す") : tr("いいね"));
    m_repost->setIcon(appIcon("repost", reposted ? "#16866a" : "#888888"));
    m_repost->setText(QString::number(m_post.value("retweetCount").toInt()));
    m_repost->setToolTip(reposted ? tr("リポストを取り消す") : tr("リポスト"));
}

void PostCard::updateReaction(const QJsonObject &state)
{
    for (auto it = state.begin(); it != state.end(); ++it) m_post.insert(it.key(), it.value());
    updateButtons();
}

void PostCard::setReactionBusy(bool busy) { m_like->setEnabled(!busy); m_repost->setEnabled(!busy); }

void PostCard::setSelected(bool selected)
{
    setProperty("selected", selected);
    style()->unpolish(this);
    style()->polish(this);
}

void PostCard::mousePressEvent(QMouseEvent *event)
{
    m_pressed = event->button() == Qt::LeftButton;
    m_pressPosition = event->globalPosition().toPoint();
    QFrame::mousePressEvent(event);
}

void PostCard::mouseReleaseEvent(QMouseEvent *event)
{
    if (m_pressed && event->button() == Qt::LeftButton &&
        (event->globalPosition().toPoint() - m_pressPosition).manhattanLength() < QApplication::startDragDistance()) emit selected(m_post);
    m_pressed = false;
    QFrame::mouseReleaseEvent(event);
}

bool PostCard::eventFilter(QObject *object, QEvent *event)
{
    if (!m_clickTargets.contains(object)) return QFrame::eventFilter(object, event);
    if (event->type() == QEvent::MouseButtonPress) {
        object->setProperty("linkActivated", false);
        auto *mouse = static_cast<QMouseEvent *>(event);
        m_pressed = mouse->button() == Qt::LeftButton;
        m_pressPosition = mouse->globalPosition().toPoint();
    } else if (event->type() == QEvent::MouseButtonRelease) {
        auto *mouse = static_cast<QMouseEvent *>(event);
        if (m_pressed && mouse->button() == Qt::LeftButton &&
            (mouse->globalPosition().toPoint() - m_pressPosition).manhattanLength() < QApplication::startDragDistance()) {
            const QPointer<QLabel> text(qobject_cast<QLabel *>(object));
            const auto target = m_clickTargets.value(object);
            QTimer::singleShot(0, this, [this, text, target] {
                if (text && !text->hasSelectedText() && !text->property("linkActivated").toBool()) emit selected(target);
            });
        }
        m_pressed = false;
    }
    return QFrame::eventFilter(object, event);
}

void PostCard::setExpandedBody(bool expanded)
{
    for (auto *text : findChildren<PostText *>()) text->setFoldingAllowed(!expanded);
}

void PostCard::addText(QVBoxLayout *layout, const QJsonObject &post, NorthApi *api)
{
    const auto plain = North::plainPostText(post);
    auto *body = new PostText(plain); body->installEventFilter(this); m_clickTargets.insert(body, post);
    layout->addWidget(body);
    connect(body, &QLabel::linkActivated, this, [this, body](const QString &href) {
        body->setProperty("linkActivated", true);
        const QUrl url(href);
        if (url.scheme() == "qtnorth-profile") {
            static const QRegularExpression handle("^[A-Za-z0-9_]+$");
            if (handle.match(url.path()).hasMatch()) emit profileRequested(url.path());
            return;
        }
        if (url.scheme() == "qtnorth-search") {
            const auto query = QUrlQuery(url).queryItemValue("q", QUrl::FullyDecoded);
            if (query.size() > 1 && (query.startsWith('#') || query.startsWith('$'))) emit searchRequested(query);
            return;
        }
        if ((url.scheme() == "http" || url.scheme() == "https") && url.userInfo().isEmpty()) QDesktopServices::openUrl(url);
    });
    auto *expand = new QPushButton(tr("続きを読む")); expand->setObjectName("expandPost");
    expand->setCursor(Qt::PointingHandCursor); expand->hide(); layout->addWidget(expand, 0, Qt::AlignLeft);
    connect(body, &PostText::foldStateChanged, expand, [expand](bool available, bool expanded) {
        expand->setVisible(available); expand->setText(expanded ? tr("折りたたむ") : tr("続きを読む"));
    });
    connect(expand, &QPushButton::clicked, body, &PostText::toggleExpanded);
    if (!api || post.value("deleted").toBool() || post.value("unavailable").toBool() || !post.value("hiddenReason").toString().isEmpty()) return;
    QSet<QUrl> seen;
    for (const auto &link : North::textLinks(body->displayText())) {
        auto key = North::canonicalXPost(link.url);
        if (key.isEmpty()) key = North::canonicalYouTubeVideo(link.url);
        if (key.isEmpty()) key = link.url.adjusted(QUrl::RemoveFragment);
        if (seen.contains(key)) continue;
        seen.insert(key); layout->addWidget(new LinkPreview(link.url, api, this));
        if (seen.size() >= 2) break;
    }
}

void PostCard::addMedia(QVBoxLayout *layout, const QJsonObject &post, NorthApi *api)
{
    const auto media = post.value("media").toArray();
    if (media.isEmpty()) return;
    auto *container = new QWidget;
    auto *grid = new QGridLayout(container);
    grid->setContentsMargins(0, 0, 0, 0);
    grid->setSpacing(2);
    int index = 0;
    for (const auto &value : media) {
        if (index >= 4) break;
        const auto item = value.toObject();
        const bool video = item.value("kind").toString() == "VIDEO";
        const QString url = (video ? item.value("thumbnailUrl") : item.value("url")).toString();
        auto *cell = new QWidget;
        auto *content = new QVBoxLayout(cell);
        content->setContentsMargins(0, 0, 0, 0);
        auto reveal = [this, content, item, url, api, video] {
            if (url.isEmpty()) {
                content->addWidget(plainLabel(video ? tr("動画") : tr("画像なし"), "muted")); return;
            }
            auto *image = new RemoteImage(api, QUrl(url), item.value("altText").toString());
            image->setImageSize(QSize(item.value("width").toInt(), item.value("height").toInt()));
            if (video) image->setToolTip(tr("動画のサムネイル"));
            content->addWidget(image);
        };
        if (item.value("sensitive").toBool() || !item.value("warning").toString().isEmpty()) {
            auto *warning = new QPushButton(tr("センシティブ · 表示"));
            warning->setObjectName("sensitiveMedia");
            warning->setMinimumHeight(120);
            content->addWidget(warning);
            connect(warning, &QPushButton::clicked, this, [warning, reveal] { warning->hide(); reveal(); warning->deleteLater(); });
        } else reveal();
        grid->addWidget(cell, index / 2, media.size() > 1 ? index % 2 : 0);
        ++index;
    }
    layout->addWidget(container);
}
