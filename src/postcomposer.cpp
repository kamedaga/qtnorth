#include "postcomposer.h"
#include "northapi.h"
#include "icons.h"
#include "post.h"
#include "remoteimage.h"
#include <QAbstractTextDocumentLayout>
#include <QBuffer>
#include <QComboBox>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QImageReader>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QScrollArea>
#include <QScreen>
#include <QShortcut>
#include <QSignalBlocker>
#include <QTextBlock>
#include <QTextFragment>
#include <QTextList>
#include <QTimer>
#include <QVBoxLayout>
#include <algorithm>
#include <cmath>

ComposerEditor::ComposerEditor(QWidget *parent) : QTextEdit(parent)
{
    setObjectName("composeText"); setAcceptRichText(false);
    setPlaceholderText(tr("いまどうしてる？")); setTabChangesFocus(true);
    document()->setDocumentMargin(0);
}

QString ComposerEditor::northText() const
{
    QString result;
    for (auto block = document()->begin(); block.isValid(); block = block.next()) {
        if (block != document()->begin()) result += '\n';
        if (block.textList()) result += QStringLiteral("• ");
        for (auto it = block.begin(); !it.atEnd(); ++it) {
            const auto fragment = it.fragment(); if (!fragment.isValid()) continue;
            const auto format = fragment.charFormat();
            const bool bold = format.fontWeight() >= QFont::Bold, italic = format.fontItalic();
            if (bold) result += QStringLiteral("<太字>");
            if (italic) result += QStringLiteral("<斜体>");
            auto text = fragment.text(); text.replace(QChar::LineSeparator, '\n'); result += text;
            if (italic) result += QStringLiteral("</斜体>");
            if (bold) result += QStringLiteral("</太字>");
        }
    }
    return result;
}

void ComposerEditor::toggleBold()
{
    QTextCharFormat format; format.setFontWeight(currentCharFormat().fontWeight() >= QFont::Bold ? QFont::Normal : QFont::Bold);
    mergeCurrentCharFormat(format); setFocus();
}
void ComposerEditor::toggleItalic()
{
    QTextCharFormat format; format.setFontItalic(!currentCharFormat().fontItalic());
    mergeCurrentCharFormat(format); setFocus();
}
void ComposerEditor::toggleList()
{
    auto cursor = textCursor(); cursor.beginEditBlock();
    if (cursor.currentList()) {
        const int start = cursor.selectionStart(), end = cursor.selectionEnd();
        for (auto block = document()->findBlock(start); block.isValid() && block.position() <= end; block = block.next()) {
            if (block.position() == end && end > start) break;
            if (auto *list = block.textList()) { list->remove(block); QTextCursor item(block); auto format = block.blockFormat(); format.setIndent(0); item.setBlockFormat(format); }
        }
    } else { QTextListFormat format; format.setStyle(QTextListFormat::ListDisc); format.setIndent(1); cursor.createList(format); }
    cursor.endEditBlock(); setTextCursor(cursor); setFocus();
}

namespace {
QLabel *plain(const QString &value, const char *name = "") {
    auto *label = new QLabel(value); label->setObjectName(name); label->setTextFormat(Qt::PlainText); return label;
}
void clearLayout(QLayout *layout) {
    while (auto *item = layout->takeAt(0)) { if (auto *widget = item->widget()) { widget->hide(); widget->deleteLater(); } delete item; }
}
}

PostComposer::PostComposer(NorthApi *api, bool compact, QJsonObject reply, QWidget *parent)
    : QFrame(parent), m_api(api), m_reply(std::move(reply))
{
    setObjectName("postComposer"); setProperty("postComposer", true); setProperty("compact", compact);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    auto *outer = new QHBoxLayout(this); outer->setContentsMargins(16, 12, 16, 12); outer->setSpacing(12);
    m_avatarHost = new QWidget; m_avatarHost->setFixedWidth(40);
    m_avatarLayout = new QVBoxLayout(m_avatarHost); m_avatarLayout->setContentsMargins(0, 0, 0, 0);
    outer->addWidget(m_avatarHost, 0, Qt::AlignTop);
    m_avatarHost->setVisible(!compact);
    auto *body = new QVBoxLayout; body->setSpacing(10); outer->addLayout(body, 1);
    if (!m_reply.isEmpty()) body->addWidget(plain(tr("@%1 に返信").arg(m_reply.value("author").toObject().value("handle").toString()), "muted"));
    m_editor = new ComposerEditor; m_editor->setMinimumHeight(compact ? 30 : 130); m_editor->setMaximumHeight(compact ? 260 : 500);
    body->addWidget(m_editor);
    if (compact) connect(m_editor->document()->documentLayout(), &QAbstractTextDocumentLayout::documentSizeChanged, this, [this] {
        QTimer::singleShot(0, this, [this] {
            const int lineHeight = m_editor->fontMetrics().lineSpacing() + 8;
            m_editor->setFixedHeight(std::clamp(int(std::ceil(m_editor->document()->size().height())) + 8, lineHeight, 260));
        });
    });
    m_imageHost = new QWidget; m_imageLayout = new QGridLayout(m_imageHost);
    m_imageLayout->setContentsMargins(0, 0, 0, 0); m_imageLayout->setSpacing(6); m_imageHost->hide(); body->addWidget(m_imageHost);
    m_pollOptions = new QWidget; auto *pollLayout = new QVBoxLayout(m_pollOptions); pollLayout->setContentsMargins(0, 0, 0, 0); pollLayout->setSpacing(6);
    for (int i = 0; i < 4; ++i) {
        auto *choice = new QLineEdit; choice->setObjectName(QString("pollOption%1Input").arg(i + 1)); choice->setMaxLength(25);
        choice->setPlaceholderText(i < 2 ? tr("選択肢 %1").arg(i + 1) : tr("選択肢 %1（任意）").arg(i + 1));
        choice->setAccessibleName(choice->placeholderText()); pollLayout->addWidget(choice); m_choices.append(choice);
        connect(choice, &QLineEdit::textChanged, this, &PostComposer::validate);
    }
    m_duration = new QComboBox; m_duration->setObjectName("pollDuration"); m_duration->setAccessibleName(tr("投票期間"));
    for (const auto &entry : QList<QPair<QString, int>>{{tr("5分"), 5}, {tr("30分"), 30}, {tr("1時間"), 60}, {tr("6時間"), 360}, {tr("1日"), 1440}, {tr("3日"), 4320}, {tr("7日"), 10080}})
        m_duration->addItem(entry.first, entry.second);
    m_duration->setCurrentIndex(m_duration->findData(1440)); pollLayout->addWidget(m_duration);
    m_pollOptions->hide(); body->addWidget(m_pollOptions);
    m_error = plain({}, "notice"); m_error->setWordWrap(true); m_error->hide(); body->addWidget(m_error);
    auto *tools = new QHBoxLayout; tools->setSpacing(2);
    auto tool = [this, tools](const char *name, const char *icon, const QString &description) {
        auto *button = new QPushButton(appIcon(icon), {}); button->setObjectName(name); button->setProperty("composerTool", true);
        button->setFixedSize(30, 32); button->setIconSize(QSize(19, 19)); button->setToolTip(description); button->setAccessibleName(description);
        button->setCursor(Qt::PointingHandCursor); button->setFocusPolicy(Qt::NoFocus); tools->addWidget(button); return button;
    };
    m_attach = tool("composeAttach", "image", tr("画像を添付"));
    m_list = tool("composeList", "list", tr("箇条書き")); m_list->setCheckable(true);
    m_emoji = tool("composeEmoji", "emoji", tr("絵文字を選択"));
    m_bold = tool("composeBold", "bold", tr("太字（Ctrl+B）")); m_bold->setCheckable(true);
    m_italic = tool("composeItalic", "italic", tr("イタリック（Ctrl+I）")); m_italic->setCheckable(true);
    m_poll = tool("composePoll", "poll", tr("アンケート")); m_poll->setCheckable(true);
    tools->addStretch(); m_count = plain("0 / 280", "composeCount"); tools->addWidget(m_count); tools->addSpacing(8);
    m_send = new QPushButton(m_reply.isEmpty() ? tr("投稿") : tr("返信")); m_send->setObjectName("composeSend"); m_send->setCursor(Qt::PointingHandCursor);
    tools->addWidget(m_send); body->addLayout(tools);
    connect(m_attach, &QPushButton::clicked, this, &PostComposer::pickImages);
    connect(m_list, &QPushButton::clicked, m_editor, &ComposerEditor::toggleList);
    connect(m_bold, &QPushButton::clicked, m_editor, &ComposerEditor::toggleBold);
    connect(m_italic, &QPushButton::clicked, m_editor, &ComposerEditor::toggleItalic);
    connect(m_emoji, &QPushButton::clicked, this, &PostComposer::showEmojiPicker);
    connect(m_poll, &QPushButton::toggled, this, [this](bool enabled) { m_pollOptions->setVisible(enabled); validate(); });
    connect(m_send, &QPushButton::clicked, this, &PostComposer::submit);
    connect(m_editor, &QTextEdit::textChanged, this, &PostComposer::validate);
    auto updateFormats = [this] {
        const QSignalBlocker bold(m_bold), italic(m_italic), list(m_list);
        m_bold->setChecked(m_editor->currentCharFormat().fontWeight() >= QFont::Bold);
        m_italic->setChecked(m_editor->currentCharFormat().fontItalic()); m_list->setChecked(m_editor->textCursor().currentList());
    };
    connect(m_editor, &QTextEdit::cursorPositionChanged, this, updateFormats);
    connect(m_editor, &QTextEdit::currentCharFormatChanged, this, updateFormats);
    auto shortcut = [this](const QKeySequence &key, std::function<void()> action) {
        auto *shortcut = new QShortcut(key, m_editor); shortcut->setContext(Qt::WidgetShortcut);
        connect(shortcut, &QShortcut::activated, this, [this, action] { if (!m_busy && m_api->allowsScope("posts.write")) action(); });
    };
    shortcut(QKeySequence::Bold, [this] { m_editor->toggleBold(); }); shortcut(QKeySequence::Italic, [this] { m_editor->toggleItalic(); });
    shortcut(QKeySequence(Qt::CTRL | Qt::Key_Return), [this] { if (m_send->isEnabled()) submit(); });
    shortcut(QKeySequence(Qt::CTRL | Qt::Key_Enter), [this] { if (m_send->isEnabled()) submit(); });
    connect(api, &NorthApi::loggedOut, this, [this] { ++m_attempt; m_busy = false; clearDraft(); setUser({}); });
    connect(api, &NorthApi::sessionChanged, this, &PostComposer::validate);
    validate();
}

void PostComposer::setUser(const QJsonObject &user)
{
    clearLayout(m_avatarLayout);
    const auto name = user.value("name").toString(), handle = user.value("handle").toString();
    const auto url = user.value("avatarUrl").toString();
    if (user.isEmpty() || property("compact").toBool()) { m_avatarHost->hide(); validate(); return; }
    m_avatarHost->show(); QWidget *avatar;
    if (!url.isEmpty()) { auto *image = new RemoteImage(m_api, QUrl(url), name); image->setCircular(true); image->setPreviewEnabled(false); avatar = image; }
    else { auto *initial = new QPushButton((name.isEmpty() ? handle : name).left(1).toUpper()); initial->setObjectName("sessionAvatar"); initial->setFocusPolicy(Qt::NoFocus); avatar = initial; }
    avatar->setFixedSize(40, 40); m_avatarLayout->addWidget(avatar); validate();
}

void PostComposer::validate()
{
    const auto text = m_editor->northText(); const int weight = North::textWeight(text);
    m_count->setText(QString::number(weight) + " / 280"); m_count->setProperty("overLimit", weight > 280);
    m_count->setStyleSheet(weight > 280 ? "color:#dc3545;" : QString());
    int options = 0; QSet<QString> labels;
    for (auto *choice : m_choices) if (!choice->text().trimmed().isEmpty()) { ++options; labels.insert(choice->text().trimmed().toLower()); }
    const bool validPoll = !m_poll->isChecked() || (m_images.isEmpty() && options >= 2 && labels.size() == options
        && !m_choices[0]->text().trimmed().isEmpty() && !m_choices[1]->text().trimmed().isEmpty());
    const bool writable = m_api->allowsScope("posts.write"), editable = !m_busy && writable;
    m_send->setEnabled(editable && (!text.trimmed().isEmpty() || !m_images.isEmpty()) && weight <= 280 && validPoll);
    m_send->setText(m_busy ? tr("送信中…") : m_reply.isEmpty() ? tr("投稿") : tr("返信"));
    m_send->setToolTip(writable ? tr("Ctrl+Enterで投稿") : tr("投稿を許可してOAuthで再ログインしてください"));
    m_editor->setReadOnly(!editable); m_pollOptions->setEnabled(editable); m_imageHost->setEnabled(editable);
    for (auto *tool : {m_list, m_emoji, m_bold, m_italic}) tool->setEnabled(editable);
    m_poll->setEnabled(editable && m_images.isEmpty());
    m_attach->setEnabled(editable && m_api->allowsScope("media.write") && !m_poll->isChecked() && m_images.size() < 4);
    m_attach->setToolTip(!m_api->allowsScope("media.write") ? tr("画像添付を許可してOAuthで再ログインしてください")
        : m_poll->isChecked() ? tr("画像とアンケートは同時に添付できません") : tr("画像を添付（4枚まで）"));
}

void PostComposer::showError(const QString &message) { m_error->setText(message); m_error->setVisible(!message.isEmpty()); }

void PostComposer::clearDraft()
{
    if (m_picker) m_picker->close();
    if (m_filePicker) m_filePicker->reject();
    m_images.clear(); renderImages(); m_editor->clear(); m_editor->setCurrentCharFormat(QTextCharFormat());
    m_poll->setChecked(false); for (auto *choice : m_choices) choice->clear();
    m_duration->setCurrentIndex(m_duration->findData(1440)); m_key.clear(); m_lastPayload.clear(); m_pendingBody = {}; showError({}); validate();
}

void PostComposer::pickImages()
{
    if (!m_attach->isEnabled()) return;
    if (m_filePicker) { m_filePicker->raise(); return; }
    auto *picker = new QFileDialog(this, tr("画像を添付")); picker->setAttribute(Qt::WA_DeleteOnClose);
    m_filePicker = picker;
    picker->setFileMode(QFileDialog::ExistingFiles); picker->setNameFilter(tr("画像 (*.jpg *.jpeg *.png *.webp *.gif)"));
    const auto attempt = m_attempt;
    connect(picker, &QFileDialog::filesSelected, this, [this, attempt](const QStringList &paths) { if (attempt == m_attempt) addImages(paths); }); picker->open();
}

bool PostComposer::addImages(const QStringList &paths)
{
    if (paths.isEmpty()) return false;
    if (m_busy || !m_api->allowsScope("posts.write") || !m_api->allowsScope("media.write")) { showError(tr("画像添付を許可して再ログインしてください")); return false; }
    if (m_poll->isChecked()) { showError(tr("画像とアンケートは同時に添付できません")); return false; }
    if (m_images.size() + paths.size() > 4) { showError(tr("画像は4枚まで添付できます")); return false; }
    QVector<Image> pending;
    for (const auto &path : paths) {
        QFile file(path);
        if (!file.open(QIODevice::ReadOnly) || file.size() <= 0 || file.size() > 100 * 1024 * 1024) { showError(tr("画像を開けないか、100MBを超えています")); return false; }
        const auto bytes = file.read(100 * 1024 * 1024 + 1);
        if (bytes.size() != file.size()) { showError(tr("画像を読み込めませんでした")); return false; }
        QBuffer buffer; buffer.setData(bytes); buffer.open(QIODevice::ReadOnly); QImageReader reader(&buffer); reader.setAutoTransform(true);
        const auto format = reader.format().toLower(); const auto size = reader.size();
        if (!QList<QByteArray>{"jpeg", "jpg", "png", "webp", "gif"}.contains(format) || !size.isValid()) { showError(tr("JPEG・PNG・WebP・GIFの画像を選んでください")); return false; }
        const auto mime = "image/" + (format == "jpg" ? QByteArray("jpeg") : format);
        reader.setScaledSize(size.scaled(256, 160, Qt::KeepAspectRatio)); const auto preview = reader.read();
        if (preview.isNull()) { showError(tr("この画像を読み込めませんでした")); return false; }
        pending.append({QFileInfo(path).fileName(), {}, bytes, mime, preview});
    }
    bool gif = false; for (const auto &image : m_images) gif |= image.mime == "image/gif";
    for (const auto &image : pending) gif |= image.mime == "image/gif";
    if (gif && m_images.size() + pending.size() > 1) { showError(tr("GIFは1枚だけで添付してください")); return false; }
    m_images += pending; renderImages(); showError({}); validate(); return true;
}

void PostComposer::renderImages()
{
    clearLayout(m_imageLayout);
    for (int i = 0; i < m_images.size(); ++i) {
        auto *tile = new QFrame; tile->setObjectName("composeImage"); auto *layout = new QVBoxLayout(tile); layout->setContentsMargins(6, 6, 6, 6); layout->setSpacing(3);
        auto *head = new QHBoxLayout; auto *name = plain(m_images[i].name); name->setMinimumWidth(0); name->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred); name->setMaximumHeight(20); name->setToolTip(m_images[i].name);
        head->addWidget(name, 1); auto *remove = new QPushButton(appIcon("close"), {}); remove->setObjectName("removeComposeImage");
        remove->setFixedSize(24, 24); remove->setAccessibleName(tr("画像を取り消す")); remove->setToolTip(tr("画像を取り消す")); head->addWidget(remove); layout->addLayout(head);
        auto *image = plain({}); image->setPixmap(QPixmap::fromImage(m_images[i].preview).scaled(180, 110, Qt::KeepAspectRatio, Qt::SmoothTransformation));
        image->setMinimumWidth(0); image->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred); image->setFixedHeight(110); image->setAlignment(Qt::AlignCenter); layout->addWidget(image);
        connect(remove, &QPushButton::clicked, this, [this, i] { if (!m_busy && i < m_images.size()) { m_images.removeAt(i); renderImages(); validate(); } });
        m_imageLayout->addWidget(tile, i / 2, i % 2);
    }
    m_imageHost->setVisible(!m_images.isEmpty());
}

void PostComposer::showEmojiPicker()
{
    if (m_picker) { m_picker->close(); return; }
    auto *picker = new QFrame(this, Qt::Popup); m_picker = picker; picker->setAttribute(Qt::WA_DeleteOnClose); picker->setObjectName("emojiPicker");
    auto *layout = new QVBoxLayout(picker); auto *search = new QLineEdit; search->setObjectName("emojiSearch"); search->setPlaceholderText(tr("絵文字を検索")); layout->addWidget(search);
    auto *area = new QScrollArea; area->setWidgetResizable(true); area->setMinimumSize(290, 220); auto *content = new QWidget; auto *grid = new QGridLayout(content); grid->setSpacing(3); area->setWidget(content); layout->addWidget(area);
    const QList<QPair<QString, QString>> emojis{
        {"😀", "笑顔 smile happy"}, {"😄", "笑顔 smile"}, {"😂", "笑い 涙 laugh"}, {"🤣", "大笑い laugh"}, {"😊", "笑顔 smile"}, {"🥰", "好き love"},
        {"😍", "好き love heart"}, {"😘", "キス kiss"}, {"😎", "サングラス cool"}, {"🤔", "考える think"}, {"🫡", "敬礼 salute"}, {"😭", "泣く cry"},
        {"😢", "悲しい sad"}, {"😅", "汗 sweat"}, {"😴", "眠い sleep"}, {"😱", "驚き scream"}, {"🥺", "お願い please"}, {"🥳", "お祝い party"},
        {"👍", "いいね good thumbs"}, {"👎", "よくない bad"}, {"👏", "拍手 clap"}, {"🙏", "ありがとう お願い thanks"}, {"👋", "手 挨拶 wave"}, {"🙌", "万歳 hooray"},
        {"❤️", "ハート 好き heart love"}, {"💙", "青 ハート blue heart"}, {"💕", "好き ハート love"}, {"💯", "満点 hundred"}, {"✨", "きらきら sparkle"}, {"🔥", "炎 fire"},
        {"🎉", "お祝い party"}, {"🎂", "誕生日 ケーキ cake birthday"}, {"🎁", "プレゼント gift"}, {"🌸", "桜 花 flower"}, {"🌈", "虹 rainbow"}, {"☀️", "太陽 晴れ sun"},
        {"🌙", "月 夜 moon"}, {"⭐", "星 star"}, {"🐱", "猫 cat"}, {"🐶", "犬 dog"}, {"🐧", "ペンギン penguin"}, {"🍀", "クローバー lucky"},
        {"🍣", "寿司 sushi"}, {"🍜", "ラーメン ramen"}, {"🍕", "ピザ pizza"}, {"☕", "コーヒー coffee"}, {"🍺", "ビール beer"}, {"🍰", "ケーキ cake"},
        {"🎵", "音楽 music"}, {"🎮", "ゲーム game"}, {"📷", "写真 カメラ camera"}, {"💻", "パソコン computer"}, {"🚀", "ロケット rocket"}, {"✅", "チェック check"}
    };
    auto rebuild = [this, grid, emojis, picker](const QString &query) {
        for (int row = 0; row < grid->rowCount(); ++row) grid->setRowStretch(row, 0);
        clearLayout(grid); int index = 0;
        for (const auto &entry : emojis) {
            if (!query.trimmed().isEmpty() && !(entry.first + entry.second).contains(query.trimmed(), Qt::CaseInsensitive)) continue;
            auto *button = new QPushButton(entry.first); button->setObjectName("emojiChoice"); button->setFixedSize(42, 38); button->setToolTip(entry.second); button->setAccessibleName(entry.second);
            grid->addWidget(button, index / 6, index % 6); ++index;
            connect(button, &QPushButton::clicked, this, [this, emoji = entry.first, picker] {
                if (!m_busy && m_api->allowsScope("posts.write")) m_editor->insertPlainText(emoji);
                picker->close(); m_editor->setFocus();
            });
        }
        if (!index) grid->addWidget(plain(tr("見つかりませんでした")), 0, 0, 1, 6);
        grid->setRowStretch((index + 5) / 6, 1);
    };
    connect(search, &QLineEdit::textChanged, picker, rebuild); rebuild({});
    picker->adjustSize(); auto position = m_emoji->mapToGlobal(QPoint(0, m_emoji->height()));
    const auto available = m_emoji->screen()->availableGeometry();
    position.setX(std::clamp(position.x(), available.left(), std::max(available.left(), available.right() - picker->width())));
    if (position.y() + picker->height() > available.bottom()) position.setY(std::max(available.top(), m_emoji->mapToGlobal(QPoint()).y() - picker->height()));
    picker->move(position); picker->show(); search->setFocus();
}

void PostComposer::submit()
{
    validate(); if (!m_send->isEnabled()) return;
    m_busy = true; const auto attempt = ++m_attempt; showError({});
    m_mediaDeadline = QDeadlineTimer(180000);
    m_pendingBody = {{"text", m_editor->northText()}};
    if (!m_reply.isEmpty()) m_pendingBody.insert("reply", QJsonObject{{"in_reply_to_tweet_id", m_reply.value("id")}});
    if (m_poll->isChecked()) {
        QJsonArray options; for (auto *choice : m_choices) if (!choice->text().trimmed().isEmpty()) options.append(choice->text().trimmed());
        m_pendingBody.insert("poll", QJsonObject{{"options", options}, {"durationMinutes", m_duration->currentData().toInt()}});
    }
    validate(); uploadNext(attempt);
}

void PostComposer::uploadNext(quint64 attempt)
{
    if (attempt != m_attempt || !m_busy) return;
    for (int i = 0; i < m_images.size(); ++i) {
        if (m_images[i].pending) { waitForImage(i, attempt); return; }
        if (!m_images[i].id.isEmpty()) continue;
        showError(tr("画像をアップロード中… %1 / %2").arg(i + 1).arg(m_images.size()));
        m_api->uploadImage(this, m_images[i].bytes, m_images[i].mime, [this, attempt, i](ApiResult result) {
            if (attempt != m_attempt) return;
            const auto media = result.data().isEmpty() ? result.json : result.data(); const auto id = media.value("id").toString();
            if (!result.ok() || id.isEmpty() || media.value("status").toString() == "FAILED") { m_busy = false; showError(result.error.isEmpty() ? tr("画像をアップロードできませんでした") : result.error); validate(); return; }
            m_images[i].id = id; m_images[i].pending = media.value("status").toString() == "PENDING"; uploadNext(attempt);
        }); return;
    }
    if (!m_images.isEmpty()) { QJsonArray ids; for (const auto &image : m_images) ids.append(image.id); m_pendingBody.insert("media", QJsonObject{{"media_ids", ids}}); }
    sendPost(attempt);
}

void PostComposer::waitForImage(int index, quint64 attempt, int delay)
{
    if (m_mediaDeadline.hasExpired() || m_mediaDeadline.remainingTime() < delay) {
        m_busy = false; showError(tr("画像を処理中です。時間をおいてから再試行してください")); validate(); return;
    }
    showError(tr("画像を処理中…"));
    QTimer::singleShot(delay, this, [this, index, attempt] {
        if (attempt != m_attempt || !m_busy) return;
        m_api->request(this, "GET", "/2/media/" + QString::fromLatin1(QUrl::toPercentEncoding(m_images[index].id)), {}, {}, [this, index, attempt](ApiResult result) {
            if (attempt != m_attempt) return;
            if (result.status == 429 || result.status == 503) { waitForImage(index, attempt, std::max(1500, result.retryAfter * 1000)); return; }
            const auto media = result.data();
            if (result.ok() && media.value("id").toString() == m_images[index].id && media.value("status").toString() == "PENDING") { waitForImage(index, attempt); return; }
            if (!result.ok() || media.value("id").toString() != m_images[index].id || media.value("status").toString() == "FAILED") {
                if (result.status == 404 || media.value("status").toString() == "FAILED") { m_images[index].id.clear(); m_images[index].pending = false; }
                m_busy = false; showError(result.error.isEmpty() ? tr("画像の処理に失敗しました") : result.error); validate(); return;
            }
            m_images[index].pending = false; uploadNext(attempt);
        });
    });
}

void PostComposer::sendPost(quint64 attempt)
{
    const auto payload = QJsonDocument(m_pendingBody).toJson(QJsonDocument::Compact);
    if (m_key.isEmpty() || payload != m_lastPayload) m_key = NorthApi::newIdempotencyKey();
    m_lastPayload = payload; showError({});
    m_api->request(this, "POST", "/2/tweets", {}, m_pendingBody, [this, attempt](ApiResult result) {
        if (attempt != m_attempt) return;
        m_busy = false;
        if (!result.ok() || result.data().value("id").toString().isEmpty()) {
            showError((result.error.isEmpty() ? tr("投稿の結果を確認できませんでした") : result.error)
                + tr("\n結果が不明な場合は、northで確認するか同じ内容で再試行してください。")); validate(); return;
        }
        const auto post = result.data(); clearDraft(); emit posted(post);
    }, m_key);
}
