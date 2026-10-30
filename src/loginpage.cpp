#include "loginpage.h"
#include "icons.h"
#include "theme.h"
#include "aboutdialog.h"
#include <QCheckBox>
#include <QDesktopServices>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QRegularExpression>
#include <QSettings>
#include <QTabWidget>
#include <QToolButton>
#include <QVBoxLayout>

LoginPage::LoginPage(QWidget *parent) : QWidget(parent)
{
    setObjectName("loginPage");
    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(12, 8, 12, 12);
    auto *tools = new QHBoxLayout;
    tools->addStretch();
    auto *settings = new QPushButton(appIcon("settings"), {}); settings->setObjectName("loginSettings");
    settings->setFixedSize(32, 32); settings->setAccessibleName(tr("設定")); settings->setToolTip(tr("設定"));
    connect(settings, &QPushButton::clicked, this, &LoginPage::settingsRequested); tools->addWidget(settings);
    tools->addWidget(appearanceButton(this));
    auto *about = new QPushButton(appIcon("about"), {});
    about->setObjectName("loginAbout"); about->setAccessibleName(tr("qtNorthについて")); about->setToolTip(tr("qtNorthについて"));
    connect(about, &QPushButton::clicked, this, [this] { showAboutDialog(window()); }); tools->addWidget(about);
    root->addLayout(tools);
    auto *outer = new QHBoxLayout;
    root->addLayout(outer, 1);
    outer->addStretch();
    auto *panel = new QWidget;
    panel->setMaximumWidth(400);
    panel->setMinimumWidth(320);
    auto *layout = new QVBoxLayout(panel);
    layout->setContentsMargins(24, 32, 24, 32);
    layout->setSpacing(16);
    layout->addStretch();
    auto *logo = new QLabel;
    logo->setPixmap(QIcon(":/assets/qtNorth-icon.svg").pixmap(48, 48));
    logo->setAlignment(Qt::AlignCenter);
    layout->addWidget(logo);
    auto *title = new QLabel(tr("ログイン"));
    title->setAlignment(Qt::AlignCenter);
    title->setStyleSheet("font-size:24px; font-weight:600;");
    layout->addWidget(title);
    auto *saved = new QHBoxLayout;
    m_resume = new QPushButton(tr("保存したアカウントでログイン"));
    m_resume->setObjectName("resumeLogin");
    m_forget = new QPushButton(appIcon("logout"), QString());
    m_forget->setObjectName("forgetLogin");
    m_forget->setToolTip(tr("保存した認証情報を削除"));
    m_forget->setAccessibleName(tr("保存した認証情報を削除"));
    connect(m_resume, &QPushButton::clicked, this, &LoginPage::savedLoginRequested);
    connect(m_forget, &QPushButton::clicked, this, &LoginPage::forgetRequested);
    saved->addWidget(m_resume, 1); saved->addWidget(m_forget);
    layout->addLayout(saved);
    m_tabs = new QTabWidget;
    m_tabs->setObjectName("loginMethods");
    auto *oauth = new QWidget;
    auto *oauthLayout = new QVBoxLayout(oauth);
    oauthLayout->setContentsMargins(0, 14, 0, 0); oauthLayout->setSpacing(12);
    auto *client = new QLineEdit(QSettings().value("oauth/clientId").toString());
    client->setPlaceholderText("Client ID");
    client->setToolTip(tr("northの「設定 › 開発者向け」でアプリを登録"));
    auto *write = new QCheckBox(tr("書き込みを許可")); write->setChecked(true);
    write->setToolTip(tr("投稿・リアクション・通知を既読にする操作"));
    m_messages = new QCheckBox(tr("メッセージを許可"));
    m_messages->setObjectName("allowMessages");
    m_messages->setToolTip(tr("参加している会話の読み取り、会話作成、送信"));
    auto *authorize = new QPushButton(tr("ブラウザーでログイン"));
    authorize->setObjectName("primary");
    oauthLayout->addWidget(client); oauthLayout->addWidget(write); oauthLayout->addWidget(m_messages); oauthLayout->addWidget(authorize);
    connect(authorize, &QPushButton::clicked, this, [this, client, write] {
        if (client->text().trimmed().isEmpty()) { setMessage(tr("Client IDを入力してください")); return; }
        QSettings().setValue("oauth/clientId", client->text().trimmed());
        emit deviceLoginRequested(client->text().trimmed(), write->isChecked(), m_messages->isChecked());
    });
    m_tabs->addTab(oauth, "OAuth");
    auto *personal = new QWidget;
    auto *tokenLayout = new QVBoxLayout(personal);
    tokenLayout->setContentsMargins(0, 14, 0, 0); tokenLayout->setSpacing(12);
    m_token = new QLineEdit;
    m_token->setObjectName("personalToken");
    m_token->setEchoMode(QLineEdit::Password); m_token->setPlaceholderText("nth_oat_…");
    m_token->setToolTip(tr("必要な権限: users.read / posts.read / notifications.read\n投稿: posts.write / reactions.write\n通知の既読: notifications.write\nメッセージ: dm.read / dm.write / dm.conversations.write"));
    auto *submit = new QPushButton(tr("ログイン")); submit->setObjectName("tokenLogin");
    auto submitToken = [this] {
        const auto token = m_token->text().trimmed();
        if (token.isEmpty() || token.contains(QRegularExpression("\\s"))) { setMessage(tr("トークンを入力してください")); return; }
        emit personalTokenRequested(token);
    };
    connect(submit, &QPushButton::clicked, this, submitToken);
    connect(m_token, &QLineEdit::returnPressed, this, submitToken);
    tokenLayout->addWidget(m_token); tokenLayout->addWidget(submit);
    m_tabs->addTab(personal, tr("個人トークン"));
    connect(m_tabs, &QTabWidget::currentChanged, this, [this] { emit cancelDeviceRequested(); m_code->hide(); m_openBrowser->hide(); });
    layout->addWidget(m_tabs);
    m_code = new QLabel; m_code->setTextFormat(Qt::PlainText); m_code->setWordWrap(true);
    m_code->setTextInteractionFlags(Qt::TextSelectableByMouse); m_code->hide();
    m_openBrowser = new QPushButton(tr("認証ページを開く")); m_openBrowser->hide();
    connect(m_openBrowser, &QPushButton::clicked, this, [this] { QDesktopServices::openUrl(m_verificationUrl); });
    layout->addWidget(m_code); layout->addWidget(m_openBrowser);
    m_message = new QLabel;
    m_message->setObjectName("loginMessage"); m_message->setTextFormat(Qt::PlainText); m_message->setWordWrap(true); m_message->hide();
    layout->addWidget(m_message);
    m_cancel = new QPushButton(tr("キャンセル")); m_cancel->hide();
    connect(m_cancel, &QPushButton::clicked, this, [this] {
        emit cancelDeviceRequested(); m_code->hide(); m_openBrowser->hide(); setBusy(false); setMessage({});
    });
    layout->addWidget(m_cancel);
    layout->addStretch();
    outer->addWidget(panel, 1); outer->addStretch();
    setSavedAvailable(false);
}

void LoginPage::setBusy(bool busy, const QString &message)
{
    m_tabs->setEnabled(!busy); m_resume->setEnabled(!busy); m_forget->setEnabled(!busy);
    m_cancel->setVisible(busy && m_code->isVisible());
    setMessage(message);
}

void LoginPage::setMessage(const QString &message) { m_message->setText(message); m_message->setVisible(!message.isEmpty()); }
void LoginPage::setSavedAvailable(bool available) { m_resume->setVisible(available); m_forget->setVisible(available); }
void LoginPage::clearCredentials() { m_token->clear(); m_code->clear(); m_code->hide(); m_openBrowser->hide(); m_cancel->hide(); }
void LoginPage::enableMessages() { m_messages->setChecked(true); }
void LoginPage::showDeviceCode(const QString &code, const QUrl &url)
{
    m_verificationUrl = url;
    m_code->setText(tr("認証コード: %1").arg(code)); m_code->show(); m_openBrowser->show(); m_cancel->show();
    QDesktopServices::openUrl(url);
}
