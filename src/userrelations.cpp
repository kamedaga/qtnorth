#include "userrelations.h"
#include "northapi.h"
#include <algorithm>

UserRelations *UserRelations::forApi(NorthApi *api)
{
    if (auto *store = api->findChild<UserRelations *>("userRelations", Qt::FindDirectChildrenOnly)) return store;
    return new UserRelations(api);
}

UserRelations::UserRelations(NorthApi *api) : QObject(api), m_api(api)
{
    setObjectName("userRelations");
    connect(api, &NorthApi::loggedOut, this, [this] { ++m_generation; m_states.clear(); m_viewer = {}; emit viewerChanged(); });
}

void UserRelations::setViewer(QJsonObject user) { m_viewer = std::move(user); emit viewerChanged(); }
QJsonObject UserRelations::user(const QString &handle) const { return m_states.value(handle.toLower()).user; }
bool UserRelations::busy(const QString &handle) const { return m_states.value(handle.toLower()).busy; }
bool UserRelations::pending(const QString &handle) const { return m_states.value(handle.toLower()).pending; }
bool UserRelations::needsVerification(const QString &handle) const { return m_states.value(handle.toLower()).verify; }
QString UserRelations::error(const QString &handle) const { return m_states.value(handle.toLower()).error; }
quint64 UserRelations::version(const QString &handle) const { return m_states.value(handle.toLower()).version; }

void UserRelations::observe(const QJsonObject &user, bool fresh, quint64 requestVersion)
{
    const auto handle = user.value("handle").toString().toLower();
    if (handle.isEmpty()) return;
    auto &state = m_states[handle];
    if (!state.user.value("id").toString().isEmpty() && !user.value("id").toString().isEmpty()
        && state.user.value("id") != user.value("id")) {
        if (state.busy) return;
        state = State{};
    }
    const auto previous = state.user; const bool wasPending = state.pending;
    const bool relationship = state.user.isEmpty() || (!state.user.contains("following") && !state.busy && state.version == 0)
        || (fresh && !state.busy && requestVersion == state.version);
    for (auto it = user.begin(); it != user.end(); ++it) {
        if (!relationship && (it.key() == "following" || it.key() == "followRequested" || it.key() == "followerCount" || it.key() == "followingCount")) continue;
        state.user.insert(it.key(), it.value());
    }
    if (relationship && user.value("followRequested").isBool()) state.pending = user.value("followRequested").toBool();
    if (state.user.value("following").toBool() || !state.user.value("protected").toBool()) state.pending = false;
    if (fresh && relationship && user.value("id").toString() == viewerId()) m_viewer = state.user;
    if (previous != state.user || wasPending != state.pending) emit changed(handle);
}

void UserRelations::refresh(const QString &handle, std::function<void(bool)> done)
{
    const auto generation = m_generation;
    m_api->request(this, "GET", "/2/users/" + QString::fromLatin1(QUrl::toPercentEncoding(handle)), {}, {},
        [this, handle, generation, done = std::move(done)](ApiResult result) {
        if (generation != m_generation) return;
        auto &state = m_states[handle]; const auto data = result.data();
        const auto expected = state.user.value("id").toString();
        if (!result.ok() || data.value("handle").toString().compare(handle, Qt::CaseInsensitive) != 0
            || !data.value("following").isBool() || (!expected.isEmpty() && data.value("id").toString() != expected)) {
            state.error = result.error.isEmpty() ? tr("フォロー状態を確認できませんでした") : result.error;
            emit failed(handle, state.error); done(false); return;
        }
        state.user = data; state.error.clear(); done(true);
    });
}

void UserRelations::finish(const QString &handle)
{
    auto &state = m_states[handle];
    const bool following = state.user.value("following").toBool();
    state.pending = !following && (state.user.value("followRequested").isBool() ? state.user.value("followRequested").toBool()
        : state.intent && state.user.value("protected").toBool());
    state.user.insert("followRequested", state.pending);
    state.busy = false; state.verify = false; state.key.clear(); state.error.clear(); ++state.version;
    const int delta = int(following) - int(state.before);
    if (delta && m_viewer.contains("followingCount")) {
        m_viewer.insert("followingCount", std::max<qint64>(0, m_viewer.value("followingCount").toInteger() + delta));
        auto &self = m_states[m_viewer.value("handle").toString().toLower()];
        self.user = m_viewer; ++self.version; emit changed(m_viewer.value("handle").toString().toLower());
    }
    emit changed(handle);
}

void UserRelations::perform(const QString &handle, bool follow)
{
    auto &state = m_states[handle];
    state.busy = true; state.error.clear(); state.before = state.user.value("following").toBool(); state.intent = follow; ++state.version;
    if (state.key.isEmpty() || state.keyIntent != follow) { state.key = NorthApi::newIdempotencyKey(); state.keyIntent = follow; }
    const auto generation = m_generation; const auto key = state.key;
    emit changed(handle);
    m_api->request(this, follow ? "POST" : "DELETE", "/2/users/" + QString::fromLatin1(QUrl::toPercentEncoding(handle)) + "/follow", {}, {},
        [this, handle, generation](ApiResult result) {
        if (generation != m_generation) return;
        auto &state = m_states[handle];
        if (!result.ok()) { state.busy = false; state.error = result.error; emit changed(handle); emit failed(handle, state.error); return; }
        state.verify = true;
        refresh(handle, [this, handle](bool ok) {
            if (ok) finish(handle);
            else { m_states[handle].busy = false; emit changed(handle); }
        });
    }, key);
}

void UserRelations::toggle(const QString &value)
{
    const auto handle = value.toLower(); auto &state = m_states[handle];
    if (state.busy || handle.isEmpty() || !m_api->allowsScope("follows.write") || state.user.value("id").toString() == viewerId()) return;
    if (state.verify) {
        state.busy = true; emit changed(handle);
        refresh(handle, [this, handle](bool ok) { if (ok) finish(handle); else { m_states[handle].busy = false; emit changed(handle); } }); return;
    }
    if (!state.user.contains("following")) {
        state.busy = true; emit changed(handle);
        refresh(handle, [this, handle](bool ok) {
            auto &state = m_states[handle]; state.busy = false;
            // A button initially labelled "follow" must never unfollow an already-followed user.
            if (ok && !state.user.value("following").toBool()) perform(handle, true);
            else emit changed(handle);
        }); return;
    }
    perform(handle, !state.user.value("following").toBool() && !state.pending);
}

FollowButton::FollowButton(const QJsonObject &user, NorthApi *api, const QString &selfId, QWidget *parent)
    : QPushButton(parent), m_relations(UserRelations::forApi(api)), m_api(api),
      m_handle(user.value("handle").toString().toLower()), m_userId(user.value("id").toString()), m_selfId(selfId)
{
    setObjectName("followButton"); setProperty("profileHandle", m_handle); setCursor(Qt::PointingHandCursor);
    setSizePolicy(QSizePolicy::Maximum, QSizePolicy::Fixed);
    m_relations->observe(user);
    connect(m_relations, &UserRelations::changed, this, [this](const QString &handle) { if (handle == m_handle) updateState(); });
    connect(m_relations, &UserRelations::viewerChanged, this, &FollowButton::updateState);
    connect(this, &QPushButton::clicked, this, [this] { m_relations->toggle(m_handle); });
    updateState();
}

void FollowButton::updateState()
{
    const auto user = m_relations->user(m_handle);
    const auto self = m_relations->viewerId().isEmpty() ? m_selfId : m_relations->viewerId();
    const bool visible = m_api->authenticated() && !m_handle.isEmpty() && !m_userId.isEmpty() && !self.isEmpty()
        && m_userId != self && user.value("id").toString() == m_userId;
    if (parentWidget() || !visible) setVisible(visible);
    const bool following = user.value("following").toBool(), pending = m_relations->pending(m_handle);
    setText(m_relations->busy(m_handle) ? tr("処理中…") : m_relations->needsVerification(m_handle) ? tr("状態を確認")
        : following ? tr("フォロー解除") : pending ? tr("申請を取り消す") : user.value("protected").toBool() ? tr("フォロー申請") : tr("フォロー"));
    setEnabled(!m_relations->busy(m_handle) && m_api->allowsScope("follows.write") && !user.value("blocking").toBool());
    setAccessibleName(tr("@%1: %2").arg(m_handle, text()));
    setToolTip(!m_api->allowsScope("follows.write") ? tr("フォロー操作には follows.write の権限が必要です。書き込みを許可してOAuthで再ログインしてください") : m_relations->error(m_handle));
}
