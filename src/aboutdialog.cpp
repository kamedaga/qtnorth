#include "aboutdialog.h"
#include "icons.h"
#include "theme.h"
#include "windowchrome.h"
#include <QApplication>
#include <QClipboard>
#include <QDesktopServices>
#include <QDialog>
#include <QFrame>
#include <QFile>
#include <QDirIterator>
#include <QComboBox>
#include <QPlainTextEdit>
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
    auto *libraries = row(details, QObject::tr("ライセンスと使用ライブラリー"), "chevron");
    separator = new QFrame; separator->setObjectName("aboutSeparator"); separator->setFixedHeight(1); details->addWidget(separator);
    auto *system = row(details, QObject::tr("システム情報"), "chevron");
    separator = new QFrame; separator->setObjectName("aboutSeparator"); separator->setFixedHeight(1); details->addWidget(separator);
    auto *disclaimer = row(details, QObject::tr("免責事項"), "chevron"); disclaimer->setObjectName("aboutDisclaimer");
    layout->addStretch();
    pages->addWidget(main);
    auto addDetails = [pages, back](QPushButton *button, const QString &heading, const QString &body) {
        auto *page = new QWidget; page->setObjectName("aboutPage");
        auto *content = new QVBoxLayout(page); content->setContentsMargins(18, 18, 18, 18); content->setSpacing(20);
        content->addWidget(text(heading, "title"));
        auto *label = text(body); label->setTextInteractionFlags(Qt::TextSelectableByMouse); label->setAlignment(Qt::AlignTop | Qt::AlignLeft);
        auto *scroll = new QScrollArea; scroll->setFrameShape(QFrame::NoFrame); scroll->setWidgetResizable(true);
        scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff); scroll->setWidget(label); content->addWidget(scroll, 1);
        pages->addWidget(page);
        QObject::connect(button, &QPushButton::clicked, pages, [pages, page, back] { pages->setCurrentWidget(page); back->show(); });
    };
    auto *licensePage = new QWidget; licensePage->setObjectName("aboutPage");
    auto *licenseLayout = new QVBoxLayout(licensePage); licenseLayout->setContentsMargins(18, 18, 18, 18); licenseLayout->setSpacing(12);
    licenseLayout->addWidget(text(QObject::tr("ライセンス"), "title"));
    auto *selector = new QComboBox; selector->setObjectName("aboutLicenseSelector"); selector->setAccessibleName(QObject::tr("ライセンスを選択"));
    selector->addItem(QObject::tr("概要 · Qt %1 / QtKeychain").arg(QString::fromLatin1(qVersion())), ":/THIRD_PARTY_NOTICES.txt");
    selector->addItem("qtNorth — 0BSD", ":/LICENSE");
    selector->addItem("Qt 6 — LGPL v3", ":/licenses/LICENSES/LGPL-3.0-only.txt");
    selector->addItem("GNU GPL v3", ":/licenses/LICENSES/GPL-3.0-only.txt");
    selector->addItem("QtKeychain — BSD 3-clause", ":/licenses/QtKeychain.txt");
    selector->addItem(QObject::tr("Qt 6.10.3 — サードパーティー"), ":/licenses/Qt-6.10.3-third-party-notices.txt");
    QStringList noticePaths; QDirIterator files(":/licenses", QDir::Files, QDirIterator::Subdirectories);
    while (files.hasNext()) noticePaths.append(files.next()); noticePaths.sort();
    for (const auto &path : noticePaths) if (selector->findData(path) < 0) selector->addItem(path.mid(QString(":/licenses/").size()), path);
    licenseLayout->addWidget(selector);
    auto *licenseText = new QPlainTextEdit; licenseText->setObjectName("aboutLicenseText"); licenseText->setReadOnly(true); licenseLayout->addWidget(licenseText, 1);
    auto loadLicense = [selector, licenseText] { QFile file(selector->currentData().toString()); licenseText->setPlainText(file.open(QIODevice::ReadOnly) ? QString::fromUtf8(file.readAll()) : QObject::tr("ライセンス文書を読み込めませんでした")); };
    QObject::connect(selector, &QComboBox::currentIndexChanged, licensePage, loadLicense); loadLicense();
    pages->addWidget(licensePage);
    QObject::connect(libraries, &QPushButton::clicked, pages, [pages, licensePage, back] { pages->setCurrentWidget(licensePage); back->show(); });
    addDetails(system, QObject::tr("システム情報"), QString("qtNorth %1\nQt %2\n%3\n%4").arg(QStringLiteral(QTNORTH_VERSION), QString::fromLatin1(qVersion()), QSysInfo::prettyProductName(), QApplication::platformName()));
    QFile disclaimerFile(":/DISCLAIMER.txt");
    if (disclaimerFile.open(QIODevice::ReadOnly)) addDetails(disclaimer, QObject::tr("免責事項"), QString::fromUtf8(disclaimerFile.readAll()));
    else disclaimer->setEnabled(false);
    QObject::connect(back, &QPushButton::clicked, pages, [pages, main, back] { pages->setCurrentWidget(main); back->hide(); });
    dialog->open();
}
