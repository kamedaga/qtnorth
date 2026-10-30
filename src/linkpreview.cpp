#include "linkpreview.h"
#include "northapi.h"
#include "post.h"
#include "preferences.h"
#include "remoteimage.h"
#include <QAbstractScrollArea>
#include <QApplication>
#include <QDesktopServices>
#include <QHostAddress>
#include <QHBoxLayout>
#include <QJsonDocument>
#include <QLabel>
#include <QNetworkReply>
#include <QMouseEvent>
#include <QPushButton>
#include <QRegularExpression>
#include <QScrollBar>
#include <QShowEvent>
#include <QTextDocumentFragment>
#include <QTimer>
#include <QUrlQuery>
#include <QVBoxLayout>
#include <memory>

namespace {
QString plain(QString html, int limit)
{
    html.replace(QRegularExpression("<br\\s*/?>", QRegularExpression::CaseInsensitiveOption), "\n");
    html.remove(QRegularExpression("<[^>]*>"));
    html.replace('\n', "<br>");
    // Only entity decoding is left; no remote HTML, images or scripts are rendered.
    return QTextDocumentFragment::fromHtml(html).toPlainText().trimmed().left(limit);
}
bool sameOrigin(const QUrl &a, const QUrl &b)
{
    return a.scheme() == b.scheme() && a.host() == b.host() && a.port() == b.port();
}
QLabel *text(const QString &value, const char *name = "")
{
    auto *label = new QLabel(value); label->setTextFormat(Qt::PlainText); label->setWordWrap(true);
    label->setMinimumWidth(0); label->setObjectName(name); return label;
}
void empty(QLayout *layout)
{
    while (auto *item = layout->takeAt(0)) {
        if (auto *widget = item->widget()) { widget->hide(); widget->deleteLater(); }
        if (auto *child = item->layout()) empty(child);
        delete item;
    }
}
}

LinkPreviewService::LinkPreviewService(NorthApi *api, QUrl testOrigin)
    : QObject(api), m_network(this), m_testOrigin(std::move(testOrigin))
{
    setObjectName("linkPreviewService"); m_clock.start();
    m_resume.setSingleShot(true); m_resume.setTimerType(Qt::PreciseTimer);
    connect(&m_resume, &QTimer::timeout, this, &LinkPreviewService::pump);
    connect(api, &NorthApi::cachesCleared, this, &LinkPreviewService::clear);
    connect(Preferences::instance(), &Preferences::linkPreviewsChanged, this, [this](bool enabled) { if (!enabled) clear(); });
}

LinkPreviewService *LinkPreviewService::forApi(NorthApi *api)
{
    if (!api) return nullptr;
    if (auto *existing = api->findChild<LinkPreviewService *>("linkPreviewService", Qt::FindDirectChildrenOnly)) return existing;
    return new LinkPreviewService(api);
}

LinkPreviewService::~LinkPreviewService()
{
    for (const auto &reply : m_replies) if (reply) disconnect(reply, nullptr, this, nullptr);
    clear();
}

bool LinkPreviewService::allowed(const QUrl &url) const
{
    if (!url.isValid() || url.host().isEmpty() || !url.userInfo().isEmpty() || (url.scheme() != "https" && url.scheme() != "http")) return false;
    if (!m_testOrigin.isEmpty() && sameOrigin(url, m_testOrigin)) return true;
    const auto host = url.host().toLower();
    if (!host.contains('.') || host == "localhost" || host.endsWith(".localhost") || host.endsWith(".local") || host.endsWith(".internal")) return false;
    QHostAddress address;
    if (address.setAddress(host)) {
        if (address.protocol() == QAbstractSocket::IPv6Protocol) {
            if (address.isLoopback() || address.isNull() || address.isLinkLocal() || address.isMulticast()
                || address.isInSubnet(QHostAddress("fc00::"), 7) || address.isInSubnet(QHostAddress("::ffff:0:0"), 96)) return false;
        } else {
            const auto ip = address.toIPv4Address();
            if ((ip >> 24) == 0 || (ip >> 24) == 10 || (ip >> 24) == 127 || (ip >> 24) >= 224
                || (ip >> 16) == 0xa9fe || (ip >> 16) == 0xc0a8 || (ip >> 20) == 0xac1
                || (ip >> 22) == 0x191) return false;
        }
    }
    return url.port(-1) == -1 || url.port() == 443 || (url.scheme() == "http" && url.port() == 80);
}

void LinkPreviewService::clear()
{
    ++m_generation; m_cache.clear(); m_queue.clear(); m_waiters.clear(); m_active = 0; m_resume.stop();
    const auto replies = m_replies; m_replies.clear();
    for (const auto &reply : replies) if (reply) reply->abort();
}

void LinkPreviewService::fetch(QObject *context, QUrl url, Callback callback, bool refresh)
{
    url.setFragment({});
    const bool xPost = North::isXPost(url);
    const auto video = North::canonicalYouTubeVideo(url);
    const bool youTube = !video.isEmpty();
    const auto key = xPost ? North::canonicalXPost(url) : youTube ? video : url;
    if (refresh) m_cache.remove(key);
    const auto generation = m_generation;
    const QPointer<QObject> guard(context);
    auto deliver = [this, guard, generation, callback, youTube](LinkPreviewData data) {
        data.youTube = youTube;
        if (guard && generation == m_generation) callback(data);
    };
    if (!allowed(url) || !Preferences::instance()->linkPreviews()) {
        QTimer::singleShot(0, context, [deliver] { deliver({{}, {}, {}, {}, QObject::tr("プレビューを取得できません"), {}, false}); }); return;
    }
    if (auto *entry = m_cache.object(key); entry && m_clock.elapsed() - entry->saved < (entry->data.error.isEmpty() ? 600000 : 30000)) {
        const auto data = entry->data; QTimer::singleShot(0, context, [deliver, data] { deliver(data); }); return;
    }
    auto &waiters = m_waiters[key]; waiters.append({context, std::move(callback)});
    if (waiters.size() != 1) return;
    if (m_queue.size() >= 128) {
        const auto discarded = m_waiters.take(key);
        QTimer::singleShot(0, this, [this, discarded, generation, xPost, youTube] {
            LinkPreviewData data; data.error = tr("プレビューを取得できません"); data.xPost = xPost; data.youTube = youTube;
            for (const auto &waiter : discarded)
                if (waiter.context && generation == m_generation) waiter.callback(data);
        });
        return;
    }
    QUrl target = url;
    if (xPost) {
        target = m_testOrigin.isEmpty() ? QUrl("https://publish.x.com/oembed") : m_testOrigin.resolved(QUrl("/oembed"));
        QUrlQuery query; query.addQueryItem("url", key.toString()); query.addQueryItem("omit_script", "true");
        query.addQueryItem("dnt", "true"); query.addQueryItem("lang", "ja"); target.setQuery(query);
    } else if (youTube) {
        target = m_testOrigin.isEmpty() ? QUrl("https://www.youtube.com/oembed") : m_testOrigin.resolved(QUrl("/youtube/oembed"));
        QUrlQuery query; query.addQueryItem("url", key.toString()); query.addQueryItem("format", "json"); target.setQuery(query);
    }
    m_queue.enqueue({key, target, xPost, generation, youTube}); pump();
}

void LinkPreviewService::pump()
{
    const auto wait = m_blockedUntil - m_clock.elapsed();
    if (wait > 0) {
        if (!m_queue.isEmpty()) m_resume.start(int(wait));
        return;
    }
    while (m_active < 4 && !m_queue.isEmpty()) {
        const auto job = m_queue.dequeue();
        bool alive = false;
        for (const auto &waiter : m_waiters.value(job.key)) if (waiter.context) {
            auto *widget = qobject_cast<QWidget *>(waiter.context.data());
            if (!widget || (widget->isVisible() && !widget->visibleRegion().isEmpty())) { alive = true; break; }
        }
        if (!alive) {
            const auto discarded = m_waiters.take(job.key); LinkPreviewData data; data.canceled = true;
            for (const auto &waiter : discarded) if (waiter.context) waiter.callback(data);
            continue;
        }
        ++m_active; download(job, job.target);
    }
}

void LinkPreviewService::download(Job job, QUrl url, int redirects)
{
    if (job.generation != m_generation) return;
    if (!allowed(url)) { finish(job, {{}, {}, {}, {}, tr("プレビューを取得できません"), {}, job.xPost}); return; }
    const bool json = job.xPost || job.youTube;
    QNetworkRequest request(url); request.setRawHeader("Accept", json ? "application/json" : "text/html,application/xhtml+xml");
    request.setRawHeader("User-Agent", "qtNorth/0.1.0 (link preview)");
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::ManualRedirectPolicy);
    request.setAttribute(QNetworkRequest::CookieLoadControlAttribute, QNetworkRequest::Manual);
    request.setAttribute(QNetworkRequest::CookieSaveControlAttribute, QNetworkRequest::Manual);
    request.setTransferTimeout(10000);
    auto *reply = m_network.get(request); m_replies.append(reply);
    constexpr qint64 limit = 512 * 1024; reply->setReadBufferSize(limit + 1);
    auto bytes = std::make_shared<QByteArray>(); auto tooLarge = std::make_shared<bool>(false);
    auto headComplete = std::make_shared<bool>(false);
    auto stop = [reply] { QTimer::singleShot(0, reply, [reply] { if (!reply->isFinished()) reply->abort(); }); };
    connect(reply, &QNetworkReply::metaDataChanged, reply, [reply, tooLarge, job, stop] {
        if ((job.xPost || job.youTube) && !*tooLarge && reply->header(QNetworkRequest::ContentLengthHeader).toLongLong() > limit) { *tooLarge = true; stop(); }
    });
    connect(reply, &QIODevice::readyRead, reply, [reply, bytes, tooLarge, headComplete, job, stop] {
        if (*headComplete || *tooLarge) return;
        bytes->append(reply->read(limit + 1 - bytes->size()));
        if (!job.xPost && !job.youTube) {
            const auto end = bytes->toLower().indexOf("</head>");
            if (end >= 0 && end + 7 <= limit) {
                bytes->truncate(end + 7); *headComplete = true; stop(); return;
            }
        }
        if (bytes->size() > limit) { *tooLarge = true; stop(); }
    });
    connect(reply, &QNetworkReply::finished, this, [this, reply, bytes, tooLarge, headComplete, job, url, redirects] {
        m_replies.removeAll(reply); reply->deleteLater();
        if (job.generation != m_generation) return;
        const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        if (status == 429 || status == 503) {
            bool numeric = false; int seconds = reply->rawHeader("Retry-After").toInt(&numeric);
            if (!numeric) seconds = int(QDateTime::currentDateTimeUtc().secsTo(QDateTime::fromString(QString::fromLatin1(reply->rawHeader("Retry-After")), Qt::RFC2822Date)));
            seconds = std::clamp(seconds > 0 ? seconds : 60, 1, 86400);
            m_blockedUntil = std::max(m_blockedUntil, m_clock.elapsed() + seconds * 1000LL);
        }
        const auto target = reply->attribute(QNetworkRequest::RedirectionTargetAttribute).toUrl();
        if (!target.isEmpty()) {
            const auto next = url.resolved(target);
            if (redirects < 3 && allowed(next) && !(url.scheme() == "https" && next.scheme() != "https")) {
                download(job, next, redirects + 1); return;
            }
        } else if (!*tooLarge && (reply->error() == QNetworkReply::NoError || *headComplete) && status == 200) {
            if (!*headComplete) bytes->append(reply->read(limit + 1 - bytes->size()));
            const auto type = reply->header(QNetworkRequest::ContentTypeHeader).toString().toLower();
            if (bytes->size() <= limit && ((job.xPost || job.youTube) ? type.startsWith("application/json") : type.startsWith("text/html") || type.startsWith("application/xhtml+xml"))) {
                auto data = job.xPost ? parseX(*bytes) : job.youTube ? parseYouTube(*bytes) : parseHtml(*bytes, url);
                data.xPost = job.xPost;
                if (!data.image.isEmpty() && (!allowed(data.image)
                    || (data.image.scheme() != "https" && (m_testOrigin.isEmpty() || !sameOrigin(data.image, m_testOrigin))))) data.image = QUrl();
                finish(job, data); return;
            }
        }
        finish(job, {{}, {}, {}, {}, tr("プレビューを取得できません"), {}, job.xPost});
    });
}

void LinkPreviewService::finish(const Job &job, LinkPreviewData data)
{
    if (job.generation != m_generation) return;
    data.youTube = job.youTube;
    const auto waiters = m_waiters.take(job.key);
    const int cost = int((data.title.size() + data.description.size() + data.author.size() + data.date.size()
                        + data.error.size() + data.image.toString().size()) * 2 + job.key.toEncoded().size() + 256);
    m_cache.insert(job.key, new Entry{data, m_clock.elapsed()}, cost);
    m_active = std::max(0, m_active - 1);
    for (const auto &waiter : waiters) {
        if (job.generation != m_generation) break;
        if (waiter.context) waiter.callback(data);
    }
    pump();
}

LinkPreviewData LinkPreviewService::parseHtml(const QByteArray &bytes, const QUrl &url)
{
    const auto html = QString::fromUtf8(bytes);
    const QRegularExpression tags(R"(<meta\b((?:"[^"]*"|'[^']*'|[^'">])*)>)", QRegularExpression::CaseInsensitiveOption);
    const QRegularExpression attributes(R"re(([\w:-]+)\s*=\s*(?:"([^"]*)"|'([^']*)'|([^\s'"=<>`]+)))re");
    QHash<QString, QString> meta;
    auto matches = tags.globalMatch(html);
    while (matches.hasNext()) {
        QHash<QString, QString> values; auto attrs = attributes.globalMatch(matches.next().captured(1));
        while (attrs.hasNext()) {
            const auto attr = attrs.next();
            values.insert(attr.captured(1).toLower(), attr.captured(2).isNull() ? (attr.captured(3).isNull() ? attr.captured(4) : attr.captured(3)) : attr.captured(2));
        }
        const auto key = values.value("property", values.value("name")).toLower();
        if (!key.isEmpty() && !meta.contains(key)) meta.insert(key, values.value("content"));
    }
    LinkPreviewData data;
    data.title = plain(meta.value("og:title", meta.value("twitter:title")), 200);
    if (data.title.isEmpty()) {
        const auto title = QRegularExpression("<title\\b[^>]*>([\\s\\S]*?)</title>", QRegularExpression::CaseInsensitiveOption).match(html);
        data.title = plain(title.captured(1), 200);
    }
    data.description = plain(meta.value("og:description", meta.value("description", meta.value("twitter:description"))), 400);
    const auto image = plain(meta.value("og:image", meta.value("twitter:image")), 4096);
    if (!image.isEmpty()) data.image = url.resolved(QUrl(image));
    if (data.title.isEmpty()) data.title = url.host();
    return data;
}

LinkPreviewData LinkPreviewService::parseX(const QByteArray &bytes)
{
    const auto object = QJsonDocument::fromJson(bytes).object();
    LinkPreviewData data; data.xPost = true;
    const auto html = object.value("html").toString();
    const auto body = QRegularExpression("<p\\b[^>]*>([\\s\\S]*?)</p>", QRegularExpression::CaseInsensitiveOption).match(html);
    data.author = plain(object.value("author_name").toString(), 100);
    data.description = plain(body.captured(1), 4000);
    auto dates = QRegularExpression("<a\\b[^>]*>([\\s\\S]*?)</a>", QRegularExpression::CaseInsensitiveOption).globalMatch(html.mid(body.capturedEnd()));
    while (dates.hasNext()) data.date = plain(dates.next().captured(1), 100);
    if (data.author.isEmpty() || data.description.isEmpty()) data.error = tr("この投稿のプレビューは利用できません");
    return data;
}

LinkPreviewData LinkPreviewService::parseYouTube(const QByteArray &bytes)
{
    const auto object = QJsonDocument::fromJson(bytes).object();
    LinkPreviewData data; data.youTube = true;
    data.title = object.value("title").toString().simplified().left(300);
    data.author = object.value("author_name").toString().simplified().left(120);
    const auto image = object.value("thumbnail_url").toString();
    if (image.size() <= 4096) data.image = QUrl(image);
    if (object.value("type").toString() != "video" || data.title.isEmpty()) {
        data.error = tr("この動画のプレビューは利用できません"); data.image = QUrl();
    }
    return data;
}

LinkPreview::LinkPreview(QUrl url, NorthApi *api, QWidget *parent, LinkPreviewService *service)
    : QFrame(parent), m_url(std::move(url)), m_api(api), m_service(service ? service : LinkPreviewService::forApi(api))
{
    setObjectName("linkPreview"); setProperty("xPost", North::isXPost(m_url));
    setProperty("youTube", !North::canonicalYouTubeVideo(m_url).isEmpty());
    setMinimumWidth(0); setToolTip(m_url.toString());
    m_layout = new QVBoxLayout(this); m_layout->setContentsMargins(12, 12, 12, 12); m_layout->setSpacing(7);
    render({{}, {}, {}, {}, tr("プレビューを読み込み中…"), {}, North::isXPost(m_url)});
    if (!Preferences::instance()->linkPreviews()) hide();
    connect(Preferences::instance(), &Preferences::linkPreviewsChanged, this, [this](bool enabled) {
        if (!enabled) { m_started = false; m_pending.reset(); }
        setVisible(enabled); if (enabled) QTimer::singleShot(0, this, &LinkPreview::maybeLoad);
    });
    if (api) {
        connect(api, &NorthApi::loggedOut, this, [this] { m_started = false; empty(m_layout); hide(); });
        connect(api, &NorthApi::cachesCleared, this, [this] { m_started = false; m_pending.reset(); });
    }
    connect(this, &LinkPreview::openRequested, this, [](const QUrl &url) { QDesktopServices::openUrl(url); });
}

void LinkPreview::showEvent(QShowEvent *event)
{
    QFrame::showEvent(event);
    if (!m_observing) {
        for (auto *parent = parentWidget(); parent; parent = parent->parentWidget()) {
            if (auto *area = qobject_cast<QAbstractScrollArea *>(parent)) {
                area->viewport()->installEventFilter(this);
                connect(area->verticalScrollBar(), &QScrollBar::valueChanged, this, &LinkPreview::maybeLoad);
                connect(area->verticalScrollBar(), &QScrollBar::rangeChanged, this, &LinkPreview::maybeLoad);
                m_observing = true; break;
            }
        }
    }
    QTimer::singleShot(0, this, &LinkPreview::maybeLoad);
}

bool LinkPreview::eventFilter(QObject *object, QEvent *event)
{
    if (event->type() == QEvent::Resize || event->type() == QEvent::Show)
        QTimer::singleShot(0, this, &LinkPreview::maybeLoad);
    return QFrame::eventFilter(object, event);
}

void LinkPreview::maybeLoad()
{
    if (!m_service || !Preferences::instance()->linkPreviews() || !isVisible() || visibleRegion().isEmpty()) return;
    if (m_pending) { const auto data = *m_pending; m_pending.reset(); render(data); }
    if (m_started) return;
    m_started = true;
    const bool refresh = m_refreshNext; m_refreshNext = false;
    m_service->fetch(this, m_url, [this](LinkPreviewData data) {
        if (data.canceled) { m_started = false; return; }
        if (!Preferences::instance()->linkPreviews()) return;
        if (isVisible() && !visibleRegion().isEmpty()) render(data); else m_pending = data;
    }, refresh);
}

void LinkPreview::render(const LinkPreviewData &data)
{
    empty(m_layout);
    const bool youTube = data.youTube || property("youTube").toBool();
    if (data.xPost) {
        auto *head = new QHBoxLayout;
        head->addWidget(text(data.author.isEmpty() ? tr("Xのポスト") : data.author, "previewTitle"), 1);
        head->addWidget(text("X", "xLogo")); m_layout->addLayout(head);
        auto *body = text(data.error.isEmpty() ? data.description : data.error, "previewText");
        body->setTextInteractionFlags(Qt::TextSelectableByMouse); body->setMaximumHeight(180); m_layout->addWidget(body);
        if (!data.date.isEmpty()) m_layout->addWidget(text(data.date, "muted"));
    } else {
        auto *row = new QHBoxLayout;
        if (!data.image.isEmpty() && m_api) {
            auto *image = new RemoteImage(m_api, data.image, data.title); image->setPreviewEnabled(false);
            if (!data.title.isEmpty()) image->setToolTip("<qt>" + data.title.toHtmlEscaped() + "</qt>");
            image->setFixedSize(youTube ? QSize(160, 90) : QSize(112, 92)); image->setCropToFill(true); image->setCornerRadius(6);
            image->setPlayIndicator(youTube);
            connect(image, &QAbstractButton::clicked, this, [this] { emit openRequested(m_url); }); row->addWidget(image);
        }
        auto *labels = new QVBoxLayout;
        if (youTube) labels->addWidget(text("YouTube", "youtubeBrand"));
        labels->addWidget(text(data.title.isEmpty() ? (youTube ? tr("YouTubeの動画") : m_url.host()) : data.title, "previewTitle"));
        if (youTube && !data.author.isEmpty()) labels->addWidget(text(data.author, "previewAuthor"));
        if (!youTube || !data.error.isEmpty()) {
            auto *description = text(data.error.isEmpty() ? data.description : data.error, "previewText");
            description->setMaximumHeight(50); labels->addWidget(description);
        }
        row->addLayout(labels, 1); m_layout->addLayout(row);
    }
    auto *open = new QPushButton(data.xPost ? tr("Xで開く") : youTube ? tr("YouTubeで開く") : m_url.host());
    open->setObjectName("previewOpen"); open->setCursor(Qt::PointingHandCursor);
    open->setToolTip(m_url.toString()); open->setAccessibleName(tr("リンクを開く：%1").arg(m_url.toString()));
    connect(open, &QPushButton::clicked, this, [this] { emit openRequested(m_url); }); m_layout->addWidget(open, 0, Qt::AlignLeft);
    if (m_started && !data.error.isEmpty()) {
        auto *retry = new QPushButton(tr("再取得")); retry->setObjectName("previewRetry"); retry->setCursor(Qt::PointingHandCursor);
        connect(retry, &QPushButton::clicked, this, [this] {
            m_started = false; m_refreshNext = true; m_pending.reset();
            render({{}, {}, {}, {}, tr("プレビューを読み込み中…"), {}, North::isXPost(m_url)});
            QTimer::singleShot(0, this, &LinkPreview::maybeLoad);
        });
        m_layout->addWidget(retry, 0, Qt::AlignLeft);
    }
}

void LinkPreview::mousePressEvent(QMouseEvent *event)
{
    m_pressed = event->button() == Qt::LeftButton; m_pressPosition = event->globalPosition().toPoint(); event->accept();
}

void LinkPreview::mouseReleaseEvent(QMouseEvent *event)
{
    if (m_pressed && event->button() == Qt::LeftButton
        && (event->globalPosition().toPoint() - m_pressPosition).manhattanLength() < QApplication::startDragDistance()) emit openRequested(m_url);
    m_pressed = false; event->accept();
}
