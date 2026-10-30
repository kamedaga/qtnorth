#pragma once
#include <QCache>
#include <QElapsedTimer>
#include <QFrame>
#include <QHash>
#include <QNetworkAccessManager>
#include <QPointer>
#include <QQueue>
#include <QUrl>
#include <QTimer>
#include <functional>
#include <optional>
class NorthApi;
class QLabel;
class QVBoxLayout;

struct LinkPreviewData {
    QString title, description, author, date, error;
    QUrl image;
    bool xPost = false;
    bool canceled = false;
    bool youTube = false;
};

class LinkPreviewService : public QObject {
    Q_OBJECT
public:
    using Callback = std::function<void(LinkPreviewData)>;
    explicit LinkPreviewService(NorthApi *api, QUrl testOrigin = {});
    ~LinkPreviewService() override;
    static LinkPreviewService *forApi(NorthApi *api);
    void fetch(QObject *context, QUrl url, Callback callback, bool refresh = false);
    void clear();
    static LinkPreviewData parseHtml(const QByteArray &bytes, const QUrl &url);
    static LinkPreviewData parseX(const QByteArray &bytes);
    static LinkPreviewData parseYouTube(const QByteArray &bytes);
    bool allowed(const QUrl &url) const;
private:
    struct Waiter { QPointer<QObject> context; Callback callback; };
    struct Entry { LinkPreviewData data; qint64 saved; };
    struct Job { QUrl key, target; bool xPost; quint64 generation; bool youTube = false; };
    void pump();
    void download(Job job, QUrl url, int redirects = 0);
    void finish(const Job &job, LinkPreviewData data);
    QNetworkAccessManager m_network;
    QCache<QUrl, Entry> m_cache{2 * 1024 * 1024};
    QHash<QUrl, QList<Waiter>> m_waiters;
    QQueue<Job> m_queue;
    QList<QPointer<QNetworkReply>> m_replies;
    QElapsedTimer m_clock;
    QUrl m_testOrigin;
    quint64 m_generation = 0;
    int m_active = 0;
    qint64 m_blockedUntil = 0;
    QTimer m_resume;
};

class LinkPreview : public QFrame {
    Q_OBJECT
public:
    LinkPreview(QUrl url, NorthApi *api, QWidget *parent = nullptr, LinkPreviewService *service = nullptr);
signals:
    void openRequested(QUrl url);
protected:
    void showEvent(QShowEvent *event) override;
    bool eventFilter(QObject *object, QEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
private:
    void maybeLoad();
    void render(const LinkPreviewData &data);
    QUrl m_url;
    QPointer<NorthApi> m_api;
    QPointer<LinkPreviewService> m_service;
    QVBoxLayout *m_layout;
    bool m_started = false, m_observing = false;
    bool m_pressed = false;
    bool m_refreshNext = false;
    QPoint m_pressPosition;
    std::optional<LinkPreviewData> m_pending;
};
