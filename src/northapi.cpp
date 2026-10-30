#include "northapi.h"
#include <QHttpMultiPart>
#include <QJsonArray>
#include <QJsonDocument>
#include <QNetworkReply>
#include <QUuid>
#include <algorithm>
#include <limits>
#include <utility>
#include <memory>

NorthApi::NorthApi(QObject *parent, QUrl base) : QObject(parent), m_network(this), m_base(std::move(base))
{
    m_clock.start();
    m_pollTimer.setSingleShot(true);
    m_refreshTimer.setSingleShot(true);
    connect(&m_pollTimer, &QTimer::timeout, this, &NorthApi::pollDevice);
    connect(&m_refreshTimer, &QTimer::timeout, this, &NorthApi::refresh);
}

QByteArray NorthApi::newIdempotencyKey()
{
    return QUuid::createUuid().toString(QUuid::WithoutBraces).toLatin1();
}

void NorthApi::votePoll(const QString &postId, const QString &optionId)
{
    if (!allowsScope("polls.vote") || postId.isEmpty() || optionId.isEmpty()) {
        emit pollFailed(postId, tr("投票の権限が必要です。書き込みを許可してOAuthで再ログインしてください")); return;
    }
    if (m_pollVotes.contains(postId)) return;
    auto &operation = m_pollKeys[postId];
    if (operation.first != optionId || operation.second.isEmpty()) operation = {optionId, newIdempotencyKey()};
    const auto key = operation.second; const auto generation = m_generation;
    m_pollVotes.insert(postId); emit pollBusyChanged(postId, true);
    request(this, "POST", "/2/tweets/" + QString::fromLatin1(QUrl::toPercentEncoding(postId)) + "/poll/vote", {}, {{"optionId", optionId}},
        [this, postId, generation](ApiResult result) {
        if (generation != m_generation) return;
        m_pollVotes.remove(postId); emit pollBusyChanged(postId, false);
        const auto post = result.data();
        if (!result.ok() || post.value("id").toString() != postId || !post.value("poll").isObject()) {
            emit pollFailed(postId, result.error.isEmpty() ? tr("投票結果を確認できませんでした。同じ選択肢で再試行するか、ポストを再読み込みしてください") : result.error); return;
        }
        m_pollKeys.remove(postId); emit pollUpdated(postId, post.value("poll").toObject());
    }, key);
}

void NorthApi::refreshPoll(const QString &postId)
{
    if (!allowsScope("posts.read") || postId.isEmpty() || m_pollReads.contains(postId)) return;
    m_pollReads.insert(postId); const auto generation = m_generation;
    request(this, "GET", "/2/tweets/" + QString::fromLatin1(QUrl::toPercentEncoding(postId)), {}, {}, [this, postId, generation](ApiResult result) {
        if (generation != m_generation) return;
        m_pollReads.remove(postId);
        if (result.ok() && result.data().value("id").toString() == postId && result.data().value("poll").isObject())
            emit pollUpdated(postId, result.data().value("poll").toObject());
        else emit pollFailed(postId, tr("投票結果を更新できませんでした。ポストを再読み込みしてください"));
    });
}

void NorthApi::logout()
{
    ++m_generation;
    clearCaches(); m_readWaiters.clear(); m_mediaWaiters.clear();
    cancelDeviceLogin();
    m_refreshTimer.stop();
    m_accessToken.clear();
    m_refreshToken.clear();
    m_scopes.clear();
    m_pollVotes.clear(); m_pollReads.clear(); m_pollKeys.clear();
    m_scopesKnown = false;
    m_isOAuth = false;
    m_accessExpiry = {};
    m_refreshing = false;
    m_pending.clear();
    const auto replies = m_replies;
    m_replies.clear();
    for (const auto &reply : replies) if (reply) reply->abort();
    emit loggedOut();
}

void NorthApi::usePersonalToken(const QString &token)
{
    logout();
    m_accessToken = token.trimmed();
    emit sessionChanged();
}

QJsonObject NorthApi::sessionData() const
{
    if (!authenticated()) return {};
    QJsonObject state{{"version", 1}, {"origin", m_base.toString()},
        {"kind", m_isOAuth ? "oauth" : "personal"}, {"access_token", m_accessToken}};
    if (m_isOAuth) {
        QStringList scopes(m_scopes.begin(), m_scopes.end()); scopes.sort();
        state.insert("client_id", m_clientId);
        state.insert("refresh_token", m_refreshToken);
        state.insert("expires_at", m_accessExpiry.toUTC().toString(Qt::ISODateWithMs));
        state.insert("scope", scopes.join(' '));
    }
    return state;
}

bool NorthApi::restoreSession(const QJsonObject &state)
{
    if (state.value("version").toInt() != 1 || state.value("origin").toString() != m_base.toString()
        || state.value("access_token").toString().isEmpty()) return false;
    const auto kind = state.value("kind").toString();
    const auto expiry = QDateTime::fromString(state.value("expires_at").toString(), Qt::ISODateWithMs);
    if (kind != "personal" && kind != "oauth") return false;
    if (kind == "oauth" && (state.value("client_id").toString().isEmpty()
        || state.value("refresh_token").toString().isEmpty() || !expiry.isValid())) return false;
    logout();
    m_accessToken = state.value("access_token").toString();
    m_isOAuth = kind == "oauth";
    if (m_isOAuth) {
        m_clientId = state.value("client_id").toString();
        m_refreshToken = state.value("refresh_token").toString();
        m_accessExpiry = expiry;
        m_scopesKnown = true;
        for (const auto &scope : state.value("scope").toString().split(' ', Qt::SkipEmptyParts)) m_scopes.insert(scope);
        const qint64 delay = QDateTime::currentDateTimeUtc().msecsTo(expiry.addSecs(-60));
        m_refreshTimer.start(int(std::clamp<qint64>(delay, 0, 86400000)));
    }
    return true;
}

void NorthApi::cancelDeviceLogin()
{
    ++m_deviceGeneration;
    m_pollTimer.stop();
    m_deviceCode.clear();
}

void NorthApi::startDeviceLogin(const QString &clientId, bool writeAccess, bool messages)
{
    logout();
    m_clientId = clientId.trimmed();
    if (m_clientId.isEmpty()) { emit loginFailed(tr("Client IDを入力してください。")); return; }
    const auto deviceGeneration = m_deviceGeneration;
    QString scopes = "users.read posts.read notifications.read";
    if (writeAccess) scopes += " posts.write reactions.write notifications.write polls.vote follows.write media.write";
    if (messages) scopes += " dm.read dm.write dm.conversations.write";
    send(this, "POST", "/oauth/device/code", {}, {{"client_id", m_clientId}, {"scope", scopes}},
         [this, deviceGeneration](ApiResult r) {
        if (deviceGeneration != m_deviceGeneration) return;
        if (!r.ok()) { emit loginFailed(r.error); return; }
        const auto j = r.json;
        m_deviceCode = j.value("device_code").toString();
        auto url = QUrl(j.value("verification_uri_complete").toString());
        if (url.isEmpty()) url = QUrl(j.value("verification_uri").toString());
        const auto host = url.host();
        if (m_deviceCode.isEmpty() || j.value("user_code").toString().isEmpty() ||
            url.scheme() != "https" || (host != "north.rip" && host != "api.north.rip") ||
            !url.userInfo().isEmpty() || j.value("expires_in").toInt() <= 0) {
            cancelDeviceLogin();
            emit loginFailed(tr("認証サーバーの応答が不正です。")); return;
        }
        m_interval = std::clamp(j.value("interval").toInt(5), 5, 600);
        m_deviceExpiry = QDateTime::currentDateTimeUtc().addSecs(std::min(j.value("expires_in").toInt(), 600));
        emit deviceAuthorization(j.value("user_code").toString(), url);
        m_pollTimer.start(m_interval * 1000);
    }, false);
}

void NorthApi::pollDevice()
{
    if (m_deviceCode.isEmpty()) return;
    if (QDateTime::currentDateTimeUtc() >= m_deviceExpiry) {
        cancelDeviceLogin(); emit loginFailed(tr("認証コードの期限が切れました。やり直してください。")); return;
    }
    const auto deviceGeneration = m_deviceGeneration;
    send(this, "POST", "/oauth/token", {},
         {{"client_id", m_clientId}, {"device_code", m_deviceCode},
          {"grant_type", "urn:ietf:params:oauth:grant-type:device_code"}},
         [this, deviceGeneration](ApiResult r) {
        if (deviceGeneration != m_deviceGeneration) return;
        const auto error = r.json.value("error").toString();
        if (error == "authorization_pending" || error == "slow_down" || r.status == 429) {
            if (error == "slow_down") m_interval = std::min(m_interval + 5, 600);
            m_pollTimer.start(std::max(m_interval, r.retryAfter) * 1000);
            return;
        }
        cancelDeviceLogin();
        if (!r.ok() || !acceptTokens(r.json)) {
            emit loginFailed(r.error.isEmpty() ? tr("認証情報を取得できませんでした。") : r.error); return;
        }
        emit loginSucceeded();
    }, false);
}

bool NorthApi::acceptTokens(const QJsonObject &j)
{
    if (j.value("access_token").toString().isEmpty() || j.value("refresh_token").toString().isEmpty()
        || j.value("token_type").toString().compare("Bearer", Qt::CaseInsensitive) != 0
        || j.value("expires_in").toInt() <= 0) return false;
    m_accessToken = j.value("access_token").toString();
    m_isOAuth = true;
    m_refreshToken = j.value("refresh_token").toString();
    m_scopesKnown = true;
    m_scopes.clear();
    for (const auto &scope : j.value("scope").toString().split(' ', Qt::SkipEmptyParts)) m_scopes.insert(scope);
    const int expires = std::min(j.value("expires_in").toInt(), 86400);
    m_accessExpiry = QDateTime::currentDateTimeUtc().addSecs(expires);
    m_refreshTimer.start(std::max(1, expires - 60) * 1000);
    emit sessionChanged();
    return true;
}

void NorthApi::failSession(const QString &reason)
{
    const auto pending = std::move(m_pending);
    logout();
    emit sessionExpired(reason);
    for (const auto &p : pending) if (p.context) p.fail(reason);
}

void NorthApi::refresh()
{
    if (m_refreshing || m_refreshToken.isEmpty()) return;
    m_refreshing = true;
    const auto refreshToken = std::exchange(m_refreshToken, {});
    const auto generation = m_generation;
    const QPointer<NorthApi> guard(this);
    auto exchange = [this, guard, generation, refreshToken](bool ready) {
        if (!guard || generation != m_generation) return;
        if (!ready) { failSession(tr("認証情報を保存できませんでした。再ログインしてください。")); return; }
        send(this, "POST", "/oauth/token", {},
         {{"client_id", m_clientId}, {"grant_type", "refresh_token"}, {"refresh_token", refreshToken}},
         [this](ApiResult r) {
        m_refreshing = false;
        if (!r.ok() || !acceptTokens(r.json)) {
            failSession(tr("認証を更新できませんでした。再ログインしてください。")); return;
        }
        const auto pending = std::move(m_pending);
        m_pending.clear();
        for (const auto &p : pending) if (p.context) p.resume();
        }, false);
    };
    // Persist a marker before consuming a rotating refresh token. A crash or
    // ambiguous response must never replay the previously stored refresh token.
    if (m_refreshGuard) m_refreshGuard({{"version", 1}, {"origin", m_base.toString()}, {"kind", "refresh_pending"}}, exchange);
    else exchange(true);
}

void NorthApi::request(QObject *context, const QByteArray &method, const QString &path,
                       const QUrlQuery &query, const QJsonObject &body, Callback callback,
                       const QByteArray &idempotencyKey, ReadPolicy policy)
{
    const QByteArray key = method == "POST" && idempotencyKey.isEmpty() ? newIdempotencyKey() : idempotencyKey;
    if (needsRefresh()) {
        const QPointer<QObject> guard(context);
        m_pending.append({context, [this, guard, method, path, query, body, callback, key, policy] {
            if (guard) request(guard, method, path, query, body, callback, key, policy);
        }, [callback](QString reason) { callback({401, {}, reason, 0}); }});
        refresh();
        return;
    }
    if (method == "GET" && policy == ReadPolicy::Cached && authenticated() && path.startsWith("/2/")
        && path != "/2/users/me" && path != "/2/notifications/unread_count") {
        readCached(context, path, query, std::move(callback)); return;
    }
    if (method != "GET") {
        invalidateReads();
    }
    send(context, method, path, query, body, std::move(callback), true, key);
}

void NorthApi::invalidateReads() { ++m_readEpoch; m_readCache.clear(); }

void NorthApi::uploadImage(QObject *context, QByteArray bytes, QByteArray mimeType, Callback callback)
{
    if (needsRefresh()) {
        const QPointer<QObject> guard(context);
        m_pending.append({context, [this, guard, bytes, mimeType, callback] {
            if (guard) uploadImage(guard, bytes, mimeType, callback);
        }, [callback](QString reason) { callback({401, {}, reason, 0}); }});
        refresh(); return;
    }
    if (bytes.isEmpty() || bytes.size() > 100 * 1024 * 1024
        || !QList<QByteArray>{"image/jpeg", "image/png", "image/webp", "image/gif"}.contains(mimeType)) {
        QTimer::singleShot(0, context, [callback] { callback({400, {}, tr("対応していない画像か、画像サイズが大きすぎます"), 0}); });
        return;
    }
    auto upload = std::make_shared<ImageUpload>(ImageUpload{std::move(bytes), std::move(mimeType)});
    // Uploads have no idempotency support. Retry only after an explicit user action.
    send(context, "POST", "/2/media/upload", {}, {}, std::move(callback), true, {}, std::move(upload));
}

void NorthApi::clearCaches()
{
    invalidateReads(); ++m_mediaEpoch; m_mediaCache.clear();
    emit cachesCleared();
}

void NorthApi::readCached(QObject *context, const QString &path, const QUrlQuery &query, Callback callback)
{
    QUrl url(m_base); url.setPath(path); url.setQuery(query);
    const auto key = url.toString(QUrl::FullyEncoded);
    const auto generation = m_generation, epoch = m_readEpoch;
    const QPointer<QObject> guard(context);
    if (auto *entry = m_readCache.object(key); entry && m_clock.elapsed() - entry->saved < 30000) {
        const auto result = entry->result;
        QTimer::singleShot(0, context, [this, guard, generation, callback, result] {
            if (guard && generation == m_generation) callback(result);
        });
        return;
    }
    const auto flightKey = QString::number(epoch) + ':' + key;
    auto &waiters = m_readWaiters[flightKey]; waiters.append({context, std::move(callback)});
    if (waiters.size() != 1) return;
    // The transport belongs to the session so deleting one card cannot cancel
    // the result needed by another card or page.
    send(this, "GET", path, query, {}, [this, key, flightKey, generation, epoch](ApiResult result) {
        const auto waiters = m_readWaiters.take(flightKey);
        if (generation != m_generation) return;
        if (result.ok() && epoch == m_readEpoch) {
            const auto cost = QJsonDocument(result.json).toJson(QJsonDocument::Compact).size() * 2 + key.size() * 2 + 128;
            if (cost <= m_readCache.maxCost()) m_readCache.insert(key, new ReadEntry{result, m_clock.elapsed()}, int(cost));
        }
        for (const auto &waiter : waiters) {
            if (generation != m_generation) break;
            if (waiter.context) waiter.callback(result);
        }
    }, true);
}

bool NorthApi::needsRefresh() const
{
    return m_refreshing || (m_accessExpiry.isValid() && QDateTime::currentDateTimeUtc() >= m_accessExpiry.addSecs(-60));
}

namespace {
bool sameOrigin(const QUrl &a, const QUrl &b)
{
    return a.scheme() == b.scheme() && a.host() == b.host()
        && a.port(a.scheme() == "https" ? 443 : 80) == b.port(b.scheme() == "https" ? 443 : 80);
}
}

void NorthApi::fetchMedia(QObject *context, const QUrl &url, MediaCallback callback)
{
    const auto resolved = m_base.resolved(url);
    if (sameOrigin(resolved, m_base) && resolved.path().startsWith("/media/") && needsRefresh()) {
        const QPointer<QObject> guard(context);
        m_pending.append({context, [this, guard, resolved, callback] {
            if (guard) fetchMedia(guard, resolved, callback);
        }, [callback](QString reason) { callback({401, {}, reason}); }});
        refresh();
        return;
    }
    const auto generation = m_generation;
    const QPointer<QObject> guard(context);
    if (auto *entry = m_mediaCache.object(resolved); entry && m_clock.elapsed() - entry->saved < 300000) {
        const auto result = entry->result;
        QTimer::singleShot(0, context, [this, guard, generation, callback, result] {
            if (guard && generation == m_generation) callback(result);
        });
        return;
    }
    auto &waiters = m_mediaWaiters[resolved]; waiters.append({context, std::move(callback)});
    if (waiters.size() != 1) return;
    const auto epoch = m_mediaEpoch;
    downloadMedia(this, resolved, [this, resolved, generation, epoch](MediaResult result) {
        const auto waiters = m_mediaWaiters.take(resolved);
        if (generation != m_generation) return;
        const auto cost = result.bytes.size() + resolved.toEncoded().size() * 2 + 128;
        if (result.ok() && epoch == m_mediaEpoch && cost <= m_mediaCache.maxCost())
            m_mediaCache.insert(resolved, new MediaEntry{result, m_clock.elapsed()}, int(cost));
        for (const auto &waiter : waiters) {
            if (generation != m_generation) break;
            if (waiter.context) waiter.callback(result);
        }
    }, 0);
}

void NorthApi::downloadMedia(QObject *context, const QUrl &url, MediaCallback callback, int redirects)
{
    const QPointer<QObject> guard(context);
    const auto generation = m_generation;
    auto reject = [&](const QString &error, int status = 0) {
        QTimer::singleShot(0, context, [this, guard, generation, callback, error, status] {
            if (guard && generation == m_generation) callback({status, {}, error});
        });
    };
    const bool ownOrigin = sameOrigin(url, m_base);
    if (!url.isValid() || url.host().isEmpty() || !url.userInfo().isEmpty()
        || (url.scheme() != "https" && !(ownOrigin && m_base.scheme() == "http"))) {
        reject(tr("画像URLを開けません。")); return;
    }
    const bool withBearer = ownOrigin && url.path().startsWith("/media/");
    if (withBearer && !authenticated()) { reject(tr("画像の表示にはログインが必要です。"), 401); return; }
    if (m_blockedUntil > QDateTime::currentDateTimeUtc()) { reject(tr("しばらく待ってから再試行してください。"), 429); return; }
    QNetworkRequest request(url);
    request.setRawHeader("Accept", "image/*");
    request.setRawHeader("User-Agent", "qtNorth/0.1.0");
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::ManualRedirectPolicy);
    request.setAttribute(QNetworkRequest::CookieLoadControlAttribute, QNetworkRequest::Manual);
    request.setAttribute(QNetworkRequest::CookieSaveControlAttribute, QNetworkRequest::Manual);
    request.setAttribute(QNetworkRequest::CacheLoadControlAttribute, QNetworkRequest::AlwaysNetwork);
    request.setAttribute(QNetworkRequest::CacheSaveControlAttribute, false);
    request.setTransferTimeout(30000);
    if (withBearer) request.setRawHeader("Authorization", "Bearer " + m_accessToken.toUtf8());
    auto *reply = m_network.get(request);
    m_replies.append(reply);
    constexpr qint64 maximumBytes = 32 * 1024 * 1024;
    reply->setReadBufferSize(maximumBytes + 1);
    auto bytes = std::make_shared<QByteArray>();
    auto tooLarge = std::make_shared<bool>(false);
    connect(reply, &QNetworkReply::metaDataChanged, reply, [reply, tooLarge] {
        if (reply->header(QNetworkRequest::ContentLengthHeader).toLongLong() > maximumBytes) {
            *tooLarge = true; reply->abort();
        }
    });
    connect(reply, &QIODevice::readyRead, reply, [reply, bytes, tooLarge] {
        bytes->append(reply->read(maximumBytes + 1 - bytes->size()));
        if (bytes->size() > maximumBytes) { *tooLarge = true; reply->abort(); }
    });
    connect(context, &QObject::destroyed, reply, &QNetworkReply::abort);
    connect(reply, &QNetworkReply::finished, this, [this, reply, guard, generation, callback, bytes, tooLarge, url, withBearer, redirects] {
        m_replies.removeAll(reply);
        reply->deleteLater();
        if (!guard || generation != m_generation) return;
        const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        const QUrl target = reply->attribute(QNetworkRequest::RedirectionTargetAttribute).toUrl();
        if (status >= 300 && status < 400 && !target.isEmpty() && !*tooLarge) {
            const auto next = url.resolved(target);
            // Private media must never be redirected into another origin.
            if (redirects >= 3 || (withBearer && !sameOrigin(next, m_base))) {
                callback({status, {}, tr("画像の転送先を開けません。")}); return;
            }
            downloadMedia(guard, next, callback, redirects + 1); return;
        }
        if (*tooLarge) { callback({status, {}, tr("画像のサイズが大きすぎます。")}); return; }
        if (reply->error() != QNetworkReply::NoError || status < 200 || status >= 300) {
            callback({status, {}, tr("画像を読み込めませんでした。")}); return;
        }
        bytes->append(reply->read(maximumBytes + 1 - bytes->size()));
        if (bytes->size() > maximumBytes) { callback({status, {}, tr("画像のサイズが大きすぎます。")}); return; }
        callback({status, *bytes, {}});
    });
}

void NorthApi::send(QObject *context, const QByteArray &method, const QString &path,
                    const QUrlQuery &query, const QJsonObject &body, Callback callback,
                    bool authenticatedRequest, const QByteArray &key, std::shared_ptr<const ImageUpload> upload)
{
    const QPointer<QObject> guard(context);
    const auto generation = m_generation;
    auto reject = [&](const QString &message, int status, int retry = 0) {
        QTimer::singleShot(0, context, [this, guard, generation, callback, message, status, retry] {
            if (guard && generation == m_generation) callback({status, {}, message, retry});
        });
    };
    if (authenticatedRequest && m_accessToken.isEmpty()) { reject(tr("ログインしてください。"), 401); return; }
    if (authenticatedRequest && !path.startsWith("/2/")) { reject(tr("未対応のAPIパスです。"), 400); return; }
    if (m_blockedUntil > QDateTime::currentDateTimeUtc()) {
        const int seconds = int(QDateTime::currentDateTimeUtc().secsTo(m_blockedUntil)) + 1;
        reject(tr("あと%1秒待ってから再試行してください。").arg(seconds), 429, seconds); return;
    }
    QUrl url(m_base);
    url.setPath(path);
    url.setQuery(query);
    QNetworkRequest req(url);
    req.setRawHeader("Accept", "application/json");
    req.setRawHeader("User-Agent", "qtNorth/0.1.0");
    req.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::ManualRedirectPolicy);
    req.setAttribute(QNetworkRequest::CookieLoadControlAttribute, QNetworkRequest::Manual);
    req.setAttribute(QNetworkRequest::CookieSaveControlAttribute, QNetworkRequest::Manual);
    req.setTransferTimeout(30000);
    if (authenticatedRequest) req.setRawHeader("Authorization", "Bearer " + m_accessToken.toUtf8());
    if (!key.isEmpty()) req.setRawHeader("Idempotency-Key", key);
    QByteArray payload;
    if (!upload && method != "GET" && method != "DELETE") {
        req.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
        payload = QJsonDocument(body).toJson(QJsonDocument::Compact);
    }
    QNetworkReply *reply;
    if (upload) {
        req.setRawHeader("X-North-Media-Purpose", "posts");
        auto *multipart = new QHttpMultiPart(QHttpMultiPart::FormDataType);
        QHttpPart file;
        const auto extension = upload->mimeType.mid(6) == "jpeg" ? QByteArray("jpg") : upload->mimeType.mid(6);
        file.setHeader(QNetworkRequest::ContentDispositionHeader, QString::fromLatin1("form-data; name=\"file\"; filename=\"image." + extension + "\""));
        file.setHeader(QNetworkRequest::ContentTypeHeader, QString::fromLatin1(upload->mimeType));
        file.setBody(upload->bytes); multipart->append(file);
        reply = m_network.post(req, multipart); multipart->setParent(reply);
        connect(context, &QObject::destroyed, reply, &QNetworkReply::abort);
    } else reply = m_network.sendCustomRequest(req, method, payload);
    m_replies.append(reply);
    connect(reply, &QNetworkReply::finished, this, [this, reply, guard, callback, generation, authenticatedRequest, method] {
        m_replies.removeAll(reply);
        reply->deleteLater();
        if (generation != m_generation) return;
        if (authenticatedRequest && method != "GET") invalidateReads();
        if (!guard) return;
        ApiResult result;
        result.status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        QJsonParseError parseError;
        const auto bytes = reply->readAll();
        const auto document = QJsonDocument::fromJson(bytes, &parseError);
        result.json = document.object();
        if (result.status < 200 || result.status >= 300 || reply->error() != QNetworkReply::NoError) {
            const auto errors = result.json.value("errors").toArray();
            QStringList messages;
            for (const auto &e : errors) messages << e.toObject().value("message").toString();
            result.error = messages.join("\n");
            if (result.error.isEmpty()) result.error = result.json.value("error_description").toString();
            if (result.error.isEmpty()) result.error = result.json.value("error").toString();
            if (result.error.isEmpty()) result.error = result.status ? tr("APIエラー (HTTP %1)").arg(result.status) : tr("通信できませんでした。接続を確認してください。");
            if (result.status == 429 || result.status == 503) {
                bool numeric = false;
                const auto retryHeader = reply->rawHeader("Retry-After");
                int retry = retryHeader.toInt(&numeric);
                if (!numeric) retry = int(QDateTime::currentDateTimeUtc().secsTo(QDateTime::fromString(QString::fromLatin1(retryHeader), Qt::RFC2822Date)));
                result.retryAfter = std::clamp(retry > 0 ? retry : 5, 1, 86400);
                m_blockedUntil = QDateTime::currentDateTimeUtc().addSecs(result.retryAfter);
                result.error += tr("\n%1秒後に再試行できます。").arg(result.retryAfter);
                emit rateLimited(result.retryAfter);
            }
        } else if ((parseError.error != QJsonParseError::NoError || !document.isObject()) && result.status != 204) {
            result.error = tr("サーバーからJSON以外の応答が返りました。");
        }
        callback(result);
        if (authenticatedRequest && result.status == 401 && generation == m_generation)
            failSession(tr("認証が失効しました。再ログインしてください。"));
    });
}
