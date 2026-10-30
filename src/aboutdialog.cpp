#include "aboutdialog.h"
#include "icons.h"
#include "theme.h"
#include "windowchrome.h"
#include <QApplication>
#include <QClipboard>
#include <QDesktopServices>
#include <QDialog>
#include <QFrame>
#include <QLabel>
#include <QPushButton>
#include <QScrollArea>
#include <QStackedWidget>
#include <QSysInfo>
#include <QVBoxLayout>

namespace {
QLabel *text(const QString &value, const char *name = "")
{
    auto *label = new QLabel(value);
    label->setObjectName(name); label->setTextFormat(Qt::PlainText); label->setWordWrap(true);
    return label;
}
QPushButton *row(QVBoxLayout *layout, const QString &title, const QString &icon)
{
    auto *button = new QPushButton(title);
    button->setObjectName("aboutRow");
    button->setCursor(Qt::PointingHandCursor);
    auto *arrow = new QLabel;
    arrow->setPixmap(appIcon(icon).pixmap(18, 18));
    QObject::connect(ThemeManager::instance(), &ThemeManager::changed, arrow, [arrow, icon] { arrow->setPixmap(appIcon(icon).pixmap(18, 18)); });
    arrow->setAttribute(Qt::WA_TransparentForMouseEvents);
    auto *line = new QHBoxLayout(button);
    line->setContentsMargins(15, 0, 15, 0); line->addStretch(); line->addWidget(arrow);
    button->setMinimumHeight(52);
    layout->addWidget(button);
    return button;
}
QVBoxLayout *group(QVBoxLayout *layout)
{
    auto *frame = new QFrame; frame->setObjectName("aboutGroup");
    auto *content = new QVBoxLayout(frame); content->setContentsMargins(0, 0, 0, 0); content->setSpacing(0);
    layout->addWidget(frame);
    return content;
}
}

void showAboutDialog(QWidget *parent)
{
    if (auto *existing = parent->findChild<QDialog *>("aboutDialog")) { existing->show(); existing->raise(); existing->activateWindow(); return; }
    auto *dialog = new QDialog(parent);
    dialog->setObjectName("aboutDialog"); dialog->setWindowTitle(QObject::tr("qtNorthについて"));
    dialog->setAttribute(Qt::WA_DeleteOnClose); dialog->setWindowModality(Qt::WindowModal);
    dialog->resize(410, 610); dialog->setMinimumSize(360, 500);
    auto *outer = new QVBoxLayout(dialog); outer->setContentsMargins(0, 0, 0, 8); outer->setSpacing(4);
    auto *bar = new WindowTitleBar(dialog, false, false);
    auto *back = new QPushButton(appIcon("back"), {}); back->setObjectName("aboutBack"); back->setAccessibleName(QObject::tr("戻る")); back->hide();
    bar->closeButton()->setObjectName("aboutClose");
    bar->setCornerWidget(back, Qt::TopLeftCorner); outer->addWidget(bar);
    auto *pages = new QStackedWidget; pages->setObjectName("aboutPages"); outer->addWidget(pages, 1);
    auto *main = new QWidget; main->setObjectName("aboutPage");
    auto *layout = new QVBoxLayout(main); layout->setContentsMargins(8, 6, 8, 8); layout->setSpacing(14);
    layout->addStretch();
    auto *logo = new QLabel; logo->setPixmap(QIcon(":/assets/qtNorth-icon.svg").pixmap(104, 104)); logo->setAlignment(Qt::AlignCenter);
    layout->addWidget(logo);
    auto *title = text("qtNorth", "aboutTitle"); title->setAlignment(Qt::AlignCenter); layout->addWidget(title);
    auto *description = text(QObject::tr("northのデスクトップクライアント"), "muted"); description->setAlignment(Qt::AlignCenter); layout->addWidget(description);
    auto *version = new QPushButton(QStringLiteral(QTNORTH_VERSION)); version->setObjectName("aboutVersion");
    version->setFixedHeight(30);
    version->setToolTip(QObject::tr("バージョンをコピー"));
    layout->addWidget(version, 0, Qt::AlignHCenter);
    QObject::connect(version, &QPushButton::clicked, dialog, [] { QApplication::clipboard()->setText(QStringLiteral(QTNORTH_VERSION)); });
    layout->addSpacing(14);
    auto *links = group(layout);
    auto *website = row(links, QObject::tr("northのウェブサイト"), "external");
    QObject::connect(website, &QPushButton::clicked, dialog, [] { QDesktopServices::openUrl(QUrl("https://north.rip/")); });
    auto *separator = new QFrame; separator->setObjectName("aboutSeparator"); separator->setFixedHeight(1); links->addWidget(separator);
    auto *api = row(links, QObject::tr("APIドキュメント"), "external");
    QObject::connect(api, &QPushButton::clicked, dialog, [] { QDesktopServices::openUrl(QUrl("https://api.north.rip/docs")); });
    auto *details = group(layout);
    auto *libraries = row(details, QObject::tr("使用ライブラリー"), "chevron");
    separator = new QFrame; separator->setObjectName("aboutSeparator"); separator->setFixedHeight(1); details->addWidget(separator);
    auto *system = row(details, QObject::tr("システム情報"), "chevron");
    layout->addStretch();
    pages->addWidget(main);
    auto addDetails = [pages, back](QPushButton *button, const QString &heading, const QString &body) {
        auto *page = new QWidget; page->setObjectName("aboutPage");
        auto *content = new QVBoxLayout(page); content->setContentsMargins(18, 18, 18, 18); content->setSpacing(20);
        content->addWidget(text(heading, "title"));
        auto *label = text(body); label->setTextInteractionFlags(Qt::TextSelectableByMouse); content->addWidget(label); content->addStretch();
        pages->addWidget(page);
        QObject::connect(button, &QPushButton::clicked, pages, [pages, page, back] { pages->setCurrentWidget(page); back->show(); });
    };
    addDetails(libraries, QObject::tr("使用ライブラリー"), QObject::tr("Qt %1\nQtKeychain").arg(QString::fromLatin1(qVersion())));
    addDetails(system, QObject::tr("システム情報"), QString("qtNorth %1\nQt %2\n%3\n%4").arg(QStringLiteral(QTNORTH_VERSION), QString::fromLatin1(qVersion()), QSysInfo::prettyProductName(), QApplication::platformName()));
    QObject::connect(back, &QPushButton::clicked, pages, [pages, main, back] { pages->setCurrentWidget(main); back->hide(); });
    dialog->open();
}
