#include "settingsdialog.h"
#include "icons.h"
#include "northapi.h"
#include "preferences.h"
#include "theme.h"
#include "windowchrome.h"
#include <QButtonGroup>
#include <QComboBox>
#include <QCheckBox>
#include <QDialog>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QScrollArea>
#include <QSignalBlocker>
#include <QToolButton>
#include <QVBoxLayout>

namespace {
QLabel *label(const QString &value, const char *name = "")
{
    auto *label = new QLabel(value); label->setObjectName(name);
    label->setTextFormat(Qt::PlainText); label->setWordWrap(true); return label;
}
QVBoxLayout *section(QVBoxLayout *layout, const QString &heading)
{
    layout->addWidget(label(heading, "settingsHeading"));
    auto *frame = new QFrame; frame->setObjectName("settingsGroup");
    auto *content = new QVBoxLayout(frame); content->setContentsMargins(16, 16, 16, 16); content->setSpacing(12);
    layout->addWidget(frame); return content;
}
}

void showSettingsDialog(QWidget *parent, NorthApi *api)
{
    if (auto *existing = parent->findChild<QDialog *>("settingsDialog")) {
        existing->show(); existing->raise(); existing->activateWindow(); return;
    }
    auto *dialog = new QDialog(parent); dialog->setObjectName("settingsDialog");
    dialog->setWindowTitle(QObject::tr("設定")); dialog->setAttribute(Qt::WA_DeleteOnClose);
    dialog->setWindowModality(Qt::WindowModal); dialog->resize(560, 680); dialog->setMinimumSize(460, 430);
    auto *outer = new QVBoxLayout(dialog); outer->setContentsMargins(0, 0, 0, 0); outer->setSpacing(0);
    auto *bar = new WindowTitleBar(dialog, false);
    bar->closeButton()->setObjectName("settingsClose"); outer->addWidget(bar);
    auto *scroll = new QScrollArea; scroll->setWidgetResizable(true); scroll->setFrameShape(QFrame::NoFrame);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff); outer->addWidget(scroll);
    auto *canvas = new QWidget; canvas->setObjectName("settingsCanvas"); scroll->setWidget(canvas);
    auto *layout = new QVBoxLayout(canvas); layout->setContentsMargins(24, 22, 24, 24); layout->setSpacing(12);
    auto *appearance = section(layout, QObject::tr("外観"));
    auto *themes = new QHBoxLayout; themes->setSpacing(8); appearance->addLayout(themes);
    auto *group = new QButtonGroup(dialog); group->setExclusive(true);
    auto *manager = ThemeManager::instance();
    const QStringList names{"settingsThemeSystem", "settingsThemeLight", "settingsThemeDark"};
    const QStringList labels{QObject::tr("システム"), QObject::tr("ライト"), QObject::tr("ダーク")};
    const QStringList icons{"monitor", "sun", "moon"};
    for (int i = 0; i < names.size(); ++i) {
        auto *button = new QToolButton; button->setObjectName(names[i]); button->setProperty("themeChoice", true);
        button->setText(labels[i]); button->setIcon(appIcon(icons[i])); button->setIconSize(QSize(24, 24));
        button->setToolButtonStyle(Qt::ToolButtonTextUnderIcon); button->setCheckable(true);
        button->setCursor(Qt::PointingHandCursor); button->setMinimumHeight(78);
        button->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        group->addButton(button, i); themes->addWidget(button); button->setChecked(int(manager->mode()) == i);
        QObject::connect(button, &QToolButton::clicked, dialog, [manager, i] { manager->setMode(ThemeManager::Mode(i)); });
        QObject::connect(manager, &ThemeManager::changed, button, [manager, button, i] { button->setChecked(int(manager->mode()) == i); });
    }
    layout->addSpacing(4);
    auto *display = section(layout, QObject::tr("表示"));
    auto *preferences = Preferences::instance();
    auto *fold = new QCheckBox(QObject::tr("長いポストを3行に折りたたむ")); fold->setObjectName("foldLongPosts");
    fold->setChecked(preferences->foldLongPosts()); display->addWidget(fold);
    auto *previews = new QCheckBox(QObject::tr("リンクのプレビューを表示")); previews->setObjectName("linkPreviews");
    previews->setChecked(preferences->linkPreviews()); display->addWidget(previews);
    QObject::connect(fold, &QCheckBox::toggled, preferences, &Preferences::setFoldLongPosts);
    QObject::connect(previews, &QCheckBox::toggled, preferences, &Preferences::setLinkPreviews);
    QObject::connect(preferences, &Preferences::foldLongPostsChanged, fold, [fold](bool enabled) { const QSignalBlocker blocker(fold); fold->setChecked(enabled); });
    QObject::connect(preferences, &Preferences::linkPreviewsChanged, previews, [previews](bool enabled) { const QSignalBlocker blocker(previews); previews->setChecked(enabled); });
    layout->addSpacing(4);
    auto *notifications = section(layout, QObject::tr("通知"));
    auto *notificationRow = new QHBoxLayout;
    auto *notificationLabels = new QVBoxLayout; notificationLabels->setSpacing(4);
    notificationLabels->addWidget(label(QObject::tr("自動更新")));
    notificationLabels->addWidget(label(QObject::tr("アクティブな間だけ確認"), "muted"));
    notificationRow->addLayout(notificationLabels, 1);
    auto *interval = new QComboBox; interval->setObjectName("notificationInterval");
    interval->setAccessibleName(QObject::tr("通知の自動更新間隔")); interval->setMinimumWidth(140);
    interval->addItem(QObject::tr("15秒ごと"), 15); interval->addItem(QObject::tr("30秒ごと"), 30);
    interval->addItem(QObject::tr("1分ごと"), 60); interval->addItem(QObject::tr("2分ごと"), 120);
    interval->addItem(QObject::tr("5分ごと"), 300); interval->addItem(QObject::tr("手動のみ"), 0);
    auto *arrow = new QLabel(interval); arrow->setAttribute(Qt::WA_TransparentForMouseEvents);
    arrow->setFixedSize(14, 14); arrow->setPixmap(appIcon("chevronDown").pixmap(14, 14));
    auto *arrowLayout = new QHBoxLayout(interval); arrowLayout->setContentsMargins(0, 0, 9, 0);
    arrowLayout->addStretch(); arrowLayout->addWidget(arrow);
    QObject::connect(manager, &ThemeManager::changed, arrow, [arrow] { arrow->setPixmap(appIcon("chevronDown").pixmap(14, 14)); });
    interval->setCurrentIndex(interval->findData(preferences->notificationInterval()));
    QObject::connect(interval, &QComboBox::currentIndexChanged, dialog, [preferences, interval] {
        preferences->setNotificationInterval(interval->currentData().toInt());
    });
    QObject::connect(preferences, &Preferences::notificationIntervalChanged, interval, [interval](int seconds) {
        const QSignalBlocker blocker(interval); interval->setCurrentIndex(interval->findData(seconds));
    });
    notificationRow->addWidget(interval); notifications->addLayout(notificationRow);
    layout->addSpacing(4);
    auto *storage = section(layout, QObject::tr("ストレージ"));
    auto *cacheRow = new QHBoxLayout;
    auto *cacheLabels = new QVBoxLayout; cacheLabels->setSpacing(4);
    cacheLabels->addWidget(label(QObject::tr("キャッシュ")));
    cacheLabels->addWidget(label(QObject::tr("画像と取得結果を一時保存"), "muted")); cacheRow->addLayout(cacheLabels, 1);
    auto *clear = new QPushButton(QObject::tr("消去")); clear->setObjectName("clearCache");
    clear->setAccessibleName(QObject::tr("キャッシュを消去")); clear->setEnabled(api);
    cacheRow->addWidget(clear); storage->addLayout(cacheRow);
    auto *status = label({}, "settingsCacheStatus"); status->hide(); storage->addWidget(status);
    QObject::connect(clear, &QPushButton::clicked, dialog, [api, status] {
        api->clearCaches(); status->setText(QObject::tr("キャッシュを消去しました")); status->show();
    });
    layout->addStretch();
    dialog->open();
}
