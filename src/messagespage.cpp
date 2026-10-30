#include "messagespage.h"
#include "icons.h"
#include "northapi.h"
#include "post.h"
#include "remoteimage.h"
#include "scrollbehavior.h"
#include "windowchrome.h"
#include <QDateTime>
#include <QDialog>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QRegularExpression>
#include <QScrollArea>
#include <QScrollBar>
#include <QShortcut>
#include <QSignalBlocker>
#include <QSplitter>
#include <QVBoxLayout>
#include <algorithm>

namespace {
QLabel *textLabel(const QString &text, const char *name = "") {
    auto *label = new QLabel(text);
    label->setTextFormat(Qt::PlainText); label->setWordWrap(true); label->setObjectName(name);
    return label;
}
void clear(QLayout *layout) {
    while (auto *item = layout->takeAt(0)) {
        if (item->widget()) { item->widget()->hide(); item->widget()->deleteLater(); }
        if (item->layout()) clear(item->layout());
        delete item;
    }
}
QString segment(const QString &value) { return QString::fromLatin1(QUrl::toPercentEncoding(value)); }
}

MessagesPage::MessagesPage(NorthApi *api, QWidget *parent) : QWidget(parent), m_api(api)
{
    setObjectName("messagesPage");
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    auto *noticeRow = new QHBoxLayout;
    m_notice = textLabel({}, "notice");
    m_login = new QPushButton(tr("ログイン"));
    connect(m_login, &QPushButton::clicked, this, &MessagesPage::loginRequested);
    noticeRow->addWidget(m_notice, 1); noticeRow->addWidget(m_login);
    layout->addLayout(noticeRow);
    auto *splitter = new QSplitter;
    auto *inbox = new QWidget;
    inbox->setMinimumWidth(165);
    auto *inboxLayout = new QVBoxLayout(inbox);
    inboxLayout->setContentsMargins(0, 0, 0, 0);
    inboxLayout->setSpacing(0);
    auto *inboxHead = new QFrame;
    inboxHead->setObjectName("toolbar");
    inboxHead->setFixedHeight(53);
    auto *inboxHeader = new QHBoxLayout(inboxHead);
    inboxHeader->setContentsMargins(12, 0, 12, 0);
    inboxHeader->addWidget(textLabel(tr("会話"), "authorName"));
    inboxHeader->addStretch();
    m_new = new QPushButton(appIcon("compose"), QString());
    m_new->setObjectName("tool");
    m_new->setToolTip(tr("新しいメッセージ")); m_new->setAccessibleName(tr("新しいメッセージ"));
    connect(m_new, &QPushButton::clicked, this, &MessagesPage::newConversation);
    inboxHeader->addWidget(m_new);
    inboxLayout->addWidget(inboxHead);
    m_list = new QListWidget;
    m_list->setObjectName("conversationList");
    m_list->setWordWrap(true);
    m_list->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
    new SmoothScroll(m_list);
    m_conversationLoader = new EdgeLoader(m_list);
    connect(m_conversationLoader, &EdgeLoader::loadMoreRequested, this, [this] { loadConversations(true); });
    connect(m_list, &QListWidget::currentItemChanged, this, [this](QListWidgetItem *item) {
        if (item) openConversation(item->data(Qt::UserRole).toJsonObject());
    });
    inboxLayout->addWidget(m_list);
    splitter->addWidget(inbox);
    auto *chat = new QWidget;
    auto *chatLayout = new QVBoxLayout(chat);
    chatLayout->setContentsMargins(0, 0, 0, 0);
    chatLayout->setSpacing(0);
    m_chatTitle = new QPushButton(tr("会話を選択"));
    m_chatTitle->setObjectName("chatTitle");
    connect(m_chatTitle, &QPushButton::clicked, this, [this] {
        const auto handle = m_chatTitle->property("profileHandle").toString();
        if (!handle.isEmpty()) emit profileRequested(handle);
    });
    m_chatTitle->setFixedHeight(53);
    chatLayout->addWidget(m_chatTitle);
    m_scroll = new QScrollArea;
    m_scroll->setWidgetResizable(true);
    m_scroll->setObjectName("messageHistory");
    new SmoothScroll(m_scroll);
    m_historyLoader = new EdgeLoader(m_scroll, EdgeLoader::Top);
    connect(m_historyLoader, &EdgeLoader::loadMoreRequested, this, [this] { loadMessages(true); });
    auto *thread = new QWidget;
    thread->setObjectName("messageThread");
    m_bubbles = new QVBoxLayout(thread);
    m_bubbles->setAlignment(Qt::AlignTop);
    m_bubbles->setContentsMargins(16, 12, 16, 8);
    m_bubbles->setSpacing(8);
    m_scroll->setWidget(thread);
    chatLayout->addWidget(m_scroll, 1);
    auto *composerBox = new QFrame;
    composerBox->setObjectName("dmComposer");
    auto *composer = new QHBoxLayout(composerBox);
    composer->setContentsMargins(12, 10, 12, 10);
    composer->setSpacing(6);
    m_editor = new QPlainTextEdit;
    m_editor->setObjectName("messageEditor");
    m_editor->setPlaceholderText(tr("メッセージ"));
    m_editor->setMaximumHeight(86);
    m_editor->setMinimumHeight(42);
    connect(m_editor, &QPlainTextEdit::textChanged, this, &MessagesPage::updateComposer);
    m_send = new QPushButton(appIcon("arrow"), QString());
    m_send->setObjectName("sendMessage");
    m_send->setFixedSize(36, 36);
    m_send->setToolTip(tr("送信 (Ctrl+Enter)")); m_send->setAccessibleName(tr("送信"));
    connect(m_send, &QPushButton::clicked, this, &MessagesPage::sendMessage);
    auto *shortcut = new QShortcut(QKeySequence("Ctrl+Return"), m_editor);
    connect(shortcut, &QShortcut::activated, this, [this] { if (m_send->isEnabled()) sendMessage(); });
    composer->addWidget(m_editor, 1); composer->addWidget(m_send);
    chatLayout->addWidget(composerBox);
    splitter->addWidget(chat);
    splitter->setStretchFactor(0, 0); splitter->setStretchFactor(1, 1);
    splitter->setSizes({220, 650});
    layout->addWidget(splitter, 1);
    connect(m_api, &NorthApi::loggedOut, this, &MessagesPage::reset);
    reset();
}

void MessagesPage::notice(const QString &text)
{
    m_notice->setText(text); m_notice->setVisible(!text.isEmpty());
}

void MessagesPage::reset()
{
    ++m_generation; ++m_chatGeneration;
    m_loading = false; m_loadingMessages = false; m_sending = false; m_positioning = false;
    m_list->clear(); m_list->setEnabled(true);
    m_conversation = {}; m_messages = {};
    m_drafts.clear(); m_operations.clear();
    m_editor->clear(); clear(m_bubbles);
    m_messageCursor.clear(); m_conversationCursor.clear();
    m_conversationLoader->reset(); m_historyLoader->reset(); m_login->hide();
    m_chatTitle->setText(tr("会話を選択"));
    m_chatTitle->setProperty("profileHandle", QString());
    updateComposer();
}

void MessagesPage::setSession(const QJsonObject &user)
{
    if (m_user.value("id") != user.value("id")) reset();
    m_user = user;
    updateComposer();
}

QString MessagesPage::conversationName(const QJsonObject &conversation) const
{
    if (!conversation.value("name").toString().isEmpty()) return conversation.value("name").toString();
    QStringList names;
    for (const auto &participant : conversation.value("participants").toArray()) {
        const auto user = participant.toObject();
        if (user.value("id") != m_user.value("id")) names << user.value("name").toString(user.value("handle").toString());
    }
    return names.isEmpty() ? tr("会話") : names.join(", ");
}

void MessagesPage::addConversation(const QJsonObject &conversation)
{
    if (conversation.value("id").toString().isEmpty()) return;
    for (int row = 0; row < m_list->count(); ++row)
        if (m_list->item(row)->data(Qt::UserRole).toJsonObject().value("id") == conversation.value("id")) return;
    auto name = conversationName(conversation);
    const int unread = conversation.value("unreadCount").toInt();
    if (unread > 0) name += "  · " + QString::number(unread);
    auto *item = new QListWidgetItem(name, m_list);
    item->setData(Qt::UserRole, conversation);
    item->setSizeHint(QSize(150, 48));
}

void MessagesPage::reload()
{
    if (m_sending) return;
    m_login->hide();
    if (!m_api->authenticated() || !m_api->allowsScope("dm.read")) {
        reset();
        notice(m_api->authenticated() ? tr("メッセージの権限が必要です") : tr("ログインしてください"));
        m_login->setText(m_api->authenticated() ? tr("再ログイン") : tr("ログイン"));
        m_login->show();
        return;
    }
    loadConversations();
    if (!m_conversation.isEmpty()) loadMessages();
}

void MessagesPage::loadConversations(bool more)
{
    if (m_loading) return;
    if (more && m_conversationCursor.isEmpty()) return;
    if (!more) m_conversationLoader->reset();
    m_loading = true; m_conversationLoader->setLoading();
    const auto generation = m_generation;
    QUrlQuery query;
    if (more) query.addQueryItem("cursor", m_conversationCursor);
    m_api->request(this, "GET", "/2/dm/conversations", query, {}, [this, generation, more](ApiResult result) {
        if (generation != m_generation) return;
        m_loading = false;
        if (!result.ok()) {
            m_conversationLoader->fail(result.retryAfter);
            notice(result.error);
            if (result.status == 403) { m_login->setText(tr("再ログイン")); m_login->show(); }
            return;
        }
        if (!result.data().value("items").isArray()) { m_conversationLoader->fail(); notice(tr("会話を取得できませんでした")); return; }
        if (!more) { QSignalBlocker blocker(m_list); m_list->clear(); }
        for (const auto &conversation : result.data().value("items").toArray()) addConversation(conversation.toObject());
        m_conversationCursor = result.data().value("nextCursor").toString();
        m_conversationLoader->setCursor(m_conversationCursor);
        notice(m_list->count() ? QString() : tr("会話なし"));
        updateComposer();
    });
}

void MessagesPage::openConversation(const QJsonObject &conversation)
{
    if (m_sending) return;
    if (!m_conversation.isEmpty()) m_drafts[m_conversation.value("id").toString()] = m_editor->toPlainText();
    ++m_chatGeneration;
    m_positioning = false;
    m_historyLoader->reset();
    SmoothScroll::stopFor(m_scroll);
    m_loadingMessages = false;
    m_conversation = conversation; m_messages = {}; m_messageCursor.clear();
    clear(m_bubbles);
    m_chatTitle->setText(conversationName(conversation));
    m_chatTitle->setProperty("profileHandle", QString());
    if (!conversation.value("group").toBool()) {
        for (const auto &value : conversation.value("participants").toArray()) {
            const auto participant = value.toObject();
            if (participant.value("id") != m_user.value("id")) m_chatTitle->setProperty("profileHandle", participant.value("handle").toString());
        }
    }
    m_chatTitle->setCursor(m_chatTitle->property("profileHandle").toString().isEmpty() ? Qt::ArrowCursor : Qt::PointingHandCursor);
    m_editor->setPlainText(m_drafts.value(conversation.value("id").toString()));
    updateComposer();
    loadMessages();
}

void MessagesPage::loadMessages(bool more)
{
    const auto id = m_conversation.value("id").toString();
    if (id.isEmpty()) return;
    if (more && (m_loadingMessages || m_messageCursor.isEmpty())) return;
    if (!more) m_historyLoader->reset();
    m_historyLoader->setLoading();
    const auto generation = m_chatGeneration;
    const auto requestNumber = ++m_messageRequest;
    m_loadingMessages = true;
    updateComposer();
    QUrlQuery query;
    if (more) query.addQueryItem("cursor", m_messageCursor);
    m_api->request(this, "GET", "/2/dm/conversations/" + segment(id) + "/messages", query, {},
        [this, generation, requestNumber, more](ApiResult result) {
        if (generation != m_chatGeneration || requestNumber != m_messageRequest) return;
        m_loadingMessages = false;
        updateComposer();
        if (!result.ok()) { m_historyLoader->fail(result.retryAfter); notice(result.error); return; }
        if (!result.data().value("items").isArray()) { m_historyLoader->fail(); notice(tr("メッセージを取得できませんでした")); return; }
        if (!more) m_messages = {};
        for (const auto &message : result.data().value("items").toArray()) {
            bool exists = false;
            for (const auto &old : m_messages) if (old.toObject().value("id") == message.toObject().value("id")) { exists = true; break; }
            if (!exists) m_messages.append(message);
        }
        if (result.data().value("conversation").isObject()) m_conversation = result.data().value("conversation").toObject();
        m_messageCursor = result.data().value("nextCursor").toString();
        notice({}); renderMessages(more); updateComposer();
        m_historyLoader->setCursor(m_messageCursor);
    });
}

void MessagesPage::renderMessages(bool older)
{
    m_positioning = true;
    m_historyLoader->setPaused(true);
    SmoothScroll::stopFor(m_scroll);
    auto *bar = m_scroll->verticalScrollBar();
    const int oldMaximum = bar->maximum(), oldPosition = bar->value();
    clear(m_bubbles);
    QList<QJsonObject> sorted;
    for (const auto &item : m_messages) sorted << item.toObject();
    std::stable_sort(sorted.begin(), sorted.end(), [](const QJsonObject &a, const QJsonObject &b) {
        return QDateTime::fromString(a.value("createdAt").toString(), Qt::ISODate) < QDateTime::fromString(b.value("createdAt").toString(), Qt::ISODate);
    });
    for (const auto &message : sorted) {
        auto *rowWidget = new QWidget;
        auto *row = new QHBoxLayout(rowWidget);
        row->setContentsMargins(0, 0, 0, 0);
        const bool mine = message.value("sender").toObject().value("id").toString() == m_user.value("id").toString();
        auto *bubble = new QFrame;
        bubble->setObjectName("dmBubble"); bubble->setProperty("mine", mine);
        bubble->setMaximumWidth(520);
        auto *body = new QVBoxLayout(bubble);
        body->setContentsMargins(14, 10, 14, 10);
        if (m_conversation.value("group").toBool()) {
            const auto sender = message.value("sender").toObject();
            auto *name = new QPushButton(sender.value("name").toString()); name->setObjectName("authorName");
            name->setCursor(Qt::PointingHandCursor);
            connect(name, &QPushButton::clicked, this, [this, sender] { emit profileRequested(sender.value("handle").toString()); });
            body->addWidget(name);
        }
        auto *text = textLabel(message.value("text").toString(), "messageText");
        text->setTextInteractionFlags(Qt::TextSelectableByMouse);
        body->addWidget(text);
        for (const auto &value : message.value("media").toArray()) {
            const auto media = value.toObject();
            const auto url = (media.value("kind") == "VIDEO" ? media.value("thumbnailUrl") : media.value("url")).toString();
            if (url.isEmpty()) continue;
            auto reveal = [this, body, media, url] {
                auto *image = new RemoteImage(m_api, QUrl(url), media.value("altText").toString());
                image->setImageSize(QSize(media.value("width").toInt(), media.value("height").toInt()));
                body->addWidget(image);
            };
            if (media.value("sensitive").toBool() || !media.value("warning").toString().isEmpty()) {
                auto *warning = new QPushButton(tr("センシティブ · 表示")); body->addWidget(warning);
                connect(warning, &QPushButton::clicked, this, [warning, reveal] { warning->hide(); reveal(); warning->deleteLater(); });
            } else reveal();
        }
        if (message.value("tweet").isObject()) {
            const auto post = message.value("tweet").toObject();
            auto *shared = new QPushButton(North::plainPostText(post).left(50));
            connect(shared, &QPushButton::clicked, this, [this, post] { emit postRequested(post); });
            body->addWidget(shared);
        }
        body->addWidget(textLabel(QDateTime::fromString(message.value("createdAt").toString(), Qt::ISODate).toLocalTime().toString("HH:mm"), "muted"));
        if (mine) row->addStretch(1);
        row->addWidget(bubble, 4);
        if (!mine) row->addStretch(1);
        m_bubbles->addWidget(rowWidget);
    }
    const auto generation = m_chatGeneration;
    QTimer::singleShot(16, this, [this, generation, older, oldPosition, oldMaximum] {
        if (generation != m_chatGeneration) return;
        auto *scrollbar = m_scroll->verticalScrollBar();
        scrollbar->setValue(older ? oldPosition + scrollbar->maximum() - oldMaximum : scrollbar->maximum());
        m_positioning = false;
        m_historyLoader->setPaused(m_sending);
    });
}

void MessagesPage::updateComposer()
{
    m_historyLoader->setPaused(m_sending || m_positioning);
    const auto text = m_editor->toPlainText();
    const bool writable = !m_loadingMessages && !m_conversation.isEmpty() && m_api->allowsScope("dm.write") && !m_conversation.value("request").toBool();
    m_editor->setEnabled(writable && !m_sending);
    m_send->setEnabled(writable && !m_sending && !text.trimmed().isEmpty() && text.size() <= 10000);
    m_new->setEnabled(!m_sending && m_api->allowsScope("dm.conversations.write"));
}

void MessagesPage::sendMessage()
{
    if (!m_send->isEnabled() || m_sending) return;
    const auto text = m_editor->toPlainText();
    const auto id = m_conversation.value("id").toString();
    auto &operation = m_operations[id];
    if (operation.text != text || operation.key.isEmpty()) operation = {text, NorthApi::newIdempotencyKey()};
    m_sending = true; m_list->setEnabled(false); updateComposer(); notice({});
    const auto generation = m_chatGeneration;
    m_api->request(this, "POST", "/2/dm/conversations/" + segment(id) + "/messages", {}, {{"text", text}},
        [this, generation, id](ApiResult result) {
        if (generation != m_chatGeneration) return;
        m_sending = false; m_list->setEnabled(true);
        if (!result.ok() || result.data().value("id").toString().isEmpty()) {
            notice(result.error.isEmpty() ? tr("送信結果を確認できませんでした") : result.error);
            updateComposer(); return;
        }
        m_messages.append(result.data());
        m_drafts.remove(id); m_operations.remove(id); m_editor->clear();
        renderMessages(); updateComposer();
    }, operation.key);
}

void MessagesPage::newConversation()
{
    if (!m_new->isEnabled()) return;
    auto *dialog = new QDialog(this);
    dialog->setObjectName("newConversationDialog"); dialog->setWindowTitle(tr("新しいメッセージ"));
    dialog->setAttribute(Qt::WA_DeleteOnClose); dialog->setWindowModality(Qt::WindowModal);
    dialog->resize(400, 210);
    auto *root = new QVBoxLayout(dialog); root->setContentsMargins(0, 0, 0, 0); root->setSpacing(0);
    root->addWidget(new WindowTitleBar(dialog, false));
    auto *body = new QWidget;
    auto *layout = new QVBoxLayout(body); layout->setContentsMargins(20, 16, 20, 20); layout->setSpacing(12);
    auto *input = new QLineEdit; input->setPlaceholderText(tr("ユーザー名")); input->setMaxLength(16);
    auto *error = textLabel({}, "notice"); error->hide();
    layout->addWidget(input); layout->addWidget(error);
    auto *actions = new QHBoxLayout;
    auto *cancel = new QPushButton(tr("キャンセル"));
    auto *create = new QPushButton(tr("会話を開く")); create->setObjectName("primary"); create->setEnabled(false);
    actions->addStretch(); actions->addWidget(cancel); actions->addWidget(create); layout->addLayout(actions); root->addWidget(body);
    connect(input, &QLineEdit::textChanged, dialog, [input, create] { create->setEnabled(!input->text().trimmed().isEmpty()); });
    connect(cancel, &QPushButton::clicked, dialog, &QDialog::reject);
    connect(m_api, &NorthApi::loggedOut, dialog, &QDialog::reject);
    const auto generation = m_generation;
    auto submit = [this, dialog, input, error, generation] {
        if (dialog->property("submitting").toBool()) return;
        if (generation != m_generation) { dialog->reject(); return; }
        auto handle = input->text().trimmed();
        if (handle.startsWith('@')) handle.remove(0, 1);
        if (handle.isEmpty() || handle.size() > 15 || handle.contains(QRegularExpression("[^A-Za-z0-9_]"))) {
            error->setText(tr("ユーザー名を確認してください")); error->show(); return;
        }
        dialog->setProperty("submitting", true); dialog->accept();
        m_new->setEnabled(false);
        m_api->request(this, "POST", "/2/dm/conversations", {}, {{"handle", handle}}, [this, generation](ApiResult result) {
            if (generation != m_generation) return;
            updateComposer();
            if (!result.ok()) { notice(result.error); return; }
            addConversation(result.data());
            for (int row = 0; row < m_list->count(); ++row)
                if (m_list->item(row)->data(Qt::UserRole).toJsonObject().value("id") == result.data().value("id")) { m_list->setCurrentRow(row); break; }
        });
    };
    connect(create, &QPushButton::clicked, dialog, submit);
    connect(input, &QLineEdit::returnPressed, dialog, submit);
    connect(dialog, &QDialog::finished, this, [this, dialog] { if (!dialog->property("submitting").toBool()) updateComposer(); });
    m_new->setEnabled(false);
    dialog->open(); input->setFocus();
}
