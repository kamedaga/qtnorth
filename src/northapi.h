#pragma once
#include <QDateTime>
#include <QCache>
#include <QElapsedTimer>
#include <QHash>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QPointer>
#include <QSet>
#include <QTimer>
#include <QUrlQuery>
#include <functional>
#include <memory>

struct ApiResult {
    int status = 0;
    QJsonObject json;
    QString error;
    int retryAfter = 0;
    bool ok() const { return error.isEmpty() && status >= 200 && status < 300; }
    QJsonObject data() const { return json.value("data").toObject(); }
};

struct MediaResult {
    int status = 0;
    QByteArray bytes;
    QString error;
    bool ok() const { return error.isEmpty() && status >= 200 && status < 300; }
};

class NorthApi : public QObject {
    Q_OBJECT
public:
    using Callback = std::function<void(ApiResult)>;
    using MediaCallback = std::function<void(MediaResult)>;
    using RefreshGuard = std::function<void(const QJsonObject &, std::function<void(bool)>)>;
    enum class ReadPolicy { Fresh, Cached };
    explicit NorthApi(QObject *parent = nullptr, QUrl base = QUrl("https://api.north.rip"));
    bool authenticated() const { return !m_accessToken.isEmpty(); }
    bool allowsScope(const QString &scope) const { return authenticated() && (!m_scopesKnown || m_scopes.contains(scope)); }
    void usePersonalToken(const QString &token);
    void startDeviceLogin(const QString &clientId, bool writeAccess, bool messages = false);
    void cancelDeviceLogin();
    void logout();
    QJsonObject sessionData() const;
    bool restoreSession(const QJsonObject &session);
    void setRefreshGuard(RefreshGuard guard) { m_refreshGuard = std::move(guard); }
    void request(QObject *context, const QByteArray &method, const QString &path,
                 const QUrlQuery &query, const QJsonObject &body, Callback callback,
                 const QByteArray &idempotencyKey = {}, ReadPolicy policy = ReadPolicy::Fresh);
    void invalidateReads();
    void clearCaches();
    static QByteArray newIdempotencyKey();
    void fetchMedia(QObject *context, const QUrl &url, MediaCallback callback);
    void uploadImage(QObject *context, QByteArray bytes, QByteArray mimeType, Callback callback);
    void votePoll(const QString &postId, const QString &optionId);
    void refreshPoll(const QString &postId);
    bool pollBusy(const QString &postId) const { return m_pollVotes.contains(postId); }
signals:
    void pollUpdated(QString postId, QJsonObject poll);
    void pollBusyChanged(QString postId, bool busy);
    void pollFailed(QString postId, QString message);
    void cachesCleared();
    void loggedOut();
    void deviceAuthorization(QString code, QUrl verificationUrl);
    void loginSucceeded();
    void loginFailed(QString message);
    void sessionExpired(QString message);
    void rateLimited(int seconds);
    void sessionChanged();
private:
    struct ImageUpload { QByteArray bytes, mimeType; };
    void send(QObject *context, const QByteArray &method, const QString &path,
              const QUrlQuery &query, const QJsonObject &body, Callback callback,
              bool authenticated, const QByteArray &key = {}, std::shared_ptr<const ImageUpload> upload = {});
    void pollDevice();
    bool acceptTokens(const QJsonObject &json);
    void refresh();
    void failSession(const QString &reason);
    void downloadMedia(QObject *context, const QUrl &url, MediaCallback callback, int redirects);
    bool needsRefresh() const;
    void readCached(QObject *context, const QString &path, const QUrlQuery &query, Callback callback);
    struct ReadEntry { ApiResult result; qint64 saved; };
    struct MediaEntry { MediaResult result; qint64 saved; };
    struct ReadWaiter { QPointer<QObject> context; Callback callback; };
    struct MediaWaiter { QPointer<QObject> context; MediaCallback callback; };
    QElapsedTimer m_clock;
    QCache<QString, ReadEntry> m_readCache{4 * 1024 * 1024};
    QCache<QUrl, MediaEntry> m_mediaCache{32 * 1024 * 1024};
    QHash<QString, QList<ReadWaiter>> m_readWaiters;
    QHash<QUrl, QList<MediaWaiter>> m_mediaWaiters;
    quint64 m_readEpoch = 0;
    quint64 m_mediaEpoch = 0;
    QNetworkAccessManager m_network;
    QUrl m_base;
    QString m_accessToken, m_refreshToken, m_clientId, m_deviceCode;
    QDateTime m_deviceExpiry, m_accessExpiry, m_blockedUntil;
    QTimer m_pollTimer, m_refreshTimer;
    int m_interval = 5;
    quint64 m_generation = 0, m_deviceGeneration = 0;
    bool m_refreshing = false;
    bool m_scopesKnown = false;
    bool m_isOAuth = false;
    RefreshGuard m_refreshGuard;
    QSet<QString> m_scopes;
    QSet<QString> m_pollVotes;
    QSet<QString> m_pollReads;
    QHash<QString, QPair<QString, QByteArray>> m_pollKeys;
    struct Pending {
        QPointer<QObject> context;
        std::function<void()> resume;
        std::function<void(QString)> fail;
    };
    QList<Pending> m_pending;
    QList<QPointer<QNetworkReply>> m_replies;
};
