#include "theme.h"
#include "icons.h"
#include <QActionGroup>
#include <QApplication>
#include <QEvent>
#include <QMenu>
#include <QSettings>
#include <QStyleHints>
#include <QToolButton>

namespace {
QString styleSheet(bool dark)
{
    QString css = R"(
QWidget { color: {text}; font-size: 13px; }
QMainWindow, QDialog, QWidget#loginPage, QWidget#profilePage { background: {bg}; }
QMainWindow[customFrame="true"], QDialog[customFrame="true"] { background: {surface}; border: 1px solid {border}; }
QMainWindow[customFrame="true"][maximized="true"], QDialog[customFrame="true"][maximized="true"] { border: none; }
QMenuBar#windowTitleBar { background: {surface}; border: none; border-bottom: 1px solid {border}; padding: 0; }
QMenuBar#windowTitleBar::item { padding: 12px 10px; border: none; border-radius: 5px; background: transparent; }
QMenuBar#windowTitleBar::item:selected { background: {raised}; }
QLabel#windowCaption { font-weight: 600; }
QPushButton[windowControl="true"] { background: transparent; border: none; border-radius: 5px; padding: 0; }
QPushButton[windowControl="true"]:hover { background: {raised}; }
QPushButton[windowControl="true"]:pressed { background: {hover}; }
QPushButton[closeControl="true"]:hover { background: {errorBg}; }
QWidget#windowResizeEdge { background: transparent; border: none; }
QLabel { background: transparent; }
QPushButton { border: 1px solid {border}; border-radius: 6px; padding: 7px 11px; background: {bg}; }
QPushButton:hover { background: {hover}; }
QPushButton:pressed { background: {raised}; }
QPushButton:disabled { color: {muted}; background: {surface}; }
QPushButton:focus, QLineEdit:focus, QPlainTextEdit:focus { border: 1px solid {accent}; }
QPushButton#primary { color: white; background: {primary}; border-color: {primary}; font-weight: 600; }
QPushButton#primary:hover { background: {primaryHover}; }
QPushButton#primary:disabled { color: {muted}; background: {raised}; border-color: {border}; }
QPushButton#nav { border: none; background: transparent; text-align: left; padding: 9px 12px; }
QPushButton#nav:hover, QPushButton#tool:hover { background: {hover}; }
QPushButton#nav:checked { background: {raised}; font-weight: 600; }
QPushButton#tool, QPushButton#backFromPost, QPushButton#aboutClose, QPushButton#aboutBack { background: transparent; border: none; padding: 7px; }
QPushButton#sidebarSettings, QPushButton#loginSettings { background: transparent; border: none; padding: 6px; border-radius: 6px; }
QPushButton#sidebarSettings:hover, QPushButton#loginSettings:hover { background: {raised}; }
QPushButton#sidebarSettings:focus, QPushButton#loginSettings:focus { border: 1px solid {accent}; }
QToolButton#appearanceButton { background: transparent; border: none; border-radius: 6px; padding: 6px; }
QToolButton#appearanceButton:hover, QToolButton#appearanceButton:pressed { background: {raised}; }
QToolButton#appearanceButton:focus { border: 1px solid {accent}; }
QToolButton#appearanceButton::menu-indicator { image: none; width: 0; height: 0; }
QPushButton#reaction { color: {muted}; border: none; background: transparent; padding: 4px 5px; font-size: 11px; }
QPushButton#reaction:hover { background: {raised}; color: {text}; }
QPushButton#expandPost, QPushButton#previewOpen { color: {accent}; background: transparent; border: none; padding: 3px 0; text-align: left; }
QPushButton#expandPost:hover, QPushButton#previewOpen:hover { text-decoration: underline; }
QFrame#linkPreview { background: {bg}; border: 1px solid {border}; border-radius: 10px; }
QLabel#previewTitle { font-weight: 600; }
QLabel#previewText { color: {muted}; }
QFrame#linkPreview[xPost="true"] QLabel#previewText { color: {text}; font-size: 14px; }
QLabel#xLogo { font-size: 22px; font-weight: 700; }
QLabel#youtubeBrand, QLabel#previewAuthor { color: {muted}; font-size: 12px; }
QLabel#reactionUsersHeading { font-weight: 600; }
QLabel#reactionUsersNotice { color: {muted}; font-size: 12px; }
QWidget#reactionUsersCanvas { background: {bg}; }
QFrame#reactionUserRow { background: transparent; border: none; border-bottom: 1px solid {border}; }
QPushButton#reactionAvatar { background: {raised}; color: {muted}; border: none; border-radius: 18px; padding: 0; font-weight: 600; }
QPushButton#sessionAvatar { background: {raised}; color: {muted}; border: none; border-radius: 20px; padding: 0; font-weight: 600; }
QPushButton#repostContext { background: transparent; border: none; color: {muted}; padding: 0; text-align: left; font-size: 12px; }
QPushButton#repostContext:hover { text-decoration: underline; }
QFrame#sidebar, QFrame#inspector, QFrame#toolbar { background: {bg}; }
QFrame#toolbar, QFrame#postCard, QFrame#userRow { border: none; border-bottom: 1px solid {border}; }
QFrame#section, QFrame#dmComposer { border-top: 1px solid {border}; }
QFrame#postCard, QFrame#userRow { background: {bg}; border-radius: 0px; }
QFrame#postCard:hover, QFrame#postCard[selected="true"], QFrame#userRow:hover { background: {surface}; }
QLabel#notificationBadge { background: #1c71d8; color: #ffffff; border: none; border-radius: 12px; font-size: 10px; font-weight: 700; }
QWidget#notificationsPage, QWidget#notificationsCanvas { background: {bg}; }
QFrame#notificationRow { background: {bg}; border: none; border-bottom: 1px solid {border}; }
QFrame#notificationRow[unread="true"] { background: {unreadBg}; }
QPushButton#notificationAvatar { background: {raised}; color: {muted}; border: none; border-radius: 16px; padding: 0; }
QLabel#notificationText { font-size: 15px; }
QFrame#dmBubble { background: {bg}; border: 1px solid {border}; border-radius: 8px; }
QFrame#dmBubble[mine="true"] { background: {raised}; }
QListWidget#conversationList { background: {bg}; border: none; }
QListWidget#conversationList::item { padding: 10px 12px; border: none; border-bottom: 1px solid {border}; }
QListWidget#conversationList::item:selected { background: {raised}; color: {text}; }
QLabel#muted, QLabel#profileMeta, QLabel#profileHandle { color: {muted}; }
QLabel#muted { font-size: 12px; }
QLabel#eyebrow { color: {muted}; font-size: 10px; font-weight: 600; }
QLabel#authorName, QLabel#profileName { font-weight: 600; }
QLabel#avatar, QPushButton#authorAvatar, QLabel#profileAvatar { background: {raised}; color: {muted}; border: none; border-radius: 24px; font-weight: 600; font-size: 16px; padding: 0; }
QLabel#profileAvatar { border-radius: 62px; font-size: 38px; }
QLabel#title { font-size: 23px; font-weight: 600; }
QLabel#postText, QLabel#profileBio { font-size: 15px; }
QLabel#quote, QFrame#quote { border: 1px solid {border}; border-radius: 6px; padding: 8px; color: {muted}; }
QLabel#notice { background: {raised}; color: {muted}; border-radius: 5px; padding: 10px 12px; }
QLabel#notice[error="true"] { background: {errorBg}; color: {errorText}; }
QWidget#inspectorContent, QWidget#messageThread, QWidget#qt_scrollarea_viewport, QWidget#profileCanvas { background: {bg}; }
QLabel#chatTitle { border-bottom: 1px solid {border}; }
QPushButton#chatTitle { border: none; border-bottom: 1px solid {border}; border-radius: 0; padding: 0 16px; text-align: left; font-weight: 600; }
QPushButton#sendMessage { border: none; background: transparent; border-radius: 18px; padding: 8px; }
QLineEdit, QPlainTextEdit { background: {surface}; border: 1px solid {border}; border-radius: 6px; padding: 8px; selection-background-color: {accent}; selection-color: white; }
QScrollArea { border: none; background: transparent; }
QScrollBar:vertical { background: transparent; width: 9px; margin: 3px; }
QScrollBar::handle:vertical { background: {scroll}; min-height: 28px; border-radius: 2px; }
QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical { height: 0px; }
QScrollBar::add-page:vertical, QScrollBar::sub-page:vertical { background: transparent; }
QSplitter::handle { background: {border}; }
QStatusBar { background: {surface}; border-top: 1px solid {border}; color: {muted}; font-size: 10px; }
QMenuBar { background: {surface}; border-bottom: 1px solid {border}; }
QMenuBar::item { padding: 5px 10px; }
QMenu { background: {bg}; border: 1px solid {border}; padding: 5px; }
QMenu::item { padding: 7px 24px; border-radius: 4px; }
QMenu::item:selected { background: {raised}; }
QMenu::separator { height: 1px; background: {border}; margin: 5px; }
QTabWidget::pane { border: none; border-top: 1px solid {border}; }
QTabBar::tab { padding: 12px 16px; background: transparent; color: {muted}; }
QTabBar::tab:selected { color: {text}; border-bottom: 2px solid {accent}; }
QPushButton#authorName, QPushButton#authorHandle, QPushButton#identity, QPushButton#profileCount { background: transparent; border: none; text-align: left; padding: 0; }
QPushButton#authorName { font-weight: 600; }
QPushButton#authorHandle { color: {muted}; }
QPushButton#authorName:hover, QPushButton#authorHandle:hover, QPushButton#identity:hover, QPushButton#profileCount:hover { text-decoration: underline; }
QLabel#profileName { font-size: 20px; }
QPushButton#profileCount { font-size: 15px; }
QLabel#profileHandle { font-size: 15px; }
QLabel#profileRelation { background: {raised}; color: {muted}; padding: 3px 7px; border-radius: 4px; font-size: 12px; }
QLabel#profileCover { background: {raised}; }
QFrame#profileAvatarFrame { background: {bg}; border: 4px solid {bg}; border-radius: 66px; }
QWidget#aboutPage, QDialog#aboutDialog { background: {surface}; }
QDialog#settingsDialog, QWidget#settingsCanvas { background: {surface}; }
QLabel#settingsHeading { font-size: 13px; font-weight: 600; }
QFrame#settingsGroup { background: {bg}; border: 1px solid {border}; border-radius: 10px; }
QToolButton[themeChoice="true"] { color: {text}; background: {bg}; border: 1px solid {border}; border-radius: 8px; padding: 9px 6px; }
QToolButton[themeChoice="true"]:hover { background: {hover}; }
QToolButton[themeChoice="true"]:checked { color: {accent}; background: {raised}; border: 1px solid {accent}; }
QToolButton[themeChoice="true"]:focus { border: 2px solid {accent}; }
QDialog#settingsDialog QComboBox { background: {bg}; color: {text}; border: 1px solid {border}; border-radius: 6px; padding: 7px 12px; }
QDialog#settingsDialog QComboBox:focus { border-color: {accent}; }
QDialog#settingsDialog QComboBox::drop-down { border: none; width: 24px; }
QDialog#settingsDialog QComboBox::down-arrow { image: none; }
QDialog#settingsDialog QComboBox QAbstractItemView { background: {bg}; color: {text}; border: 1px solid {border}; selection-background-color: {raised}; selection-color: {text}; padding: 4px; }
QLabel#settingsCacheStatus { color: {accent}; font-size: 12px; }
QLabel#aboutTitle { font-size: 26px; font-weight: 700; }
QPushButton#aboutVersion { color: {accent}; background: {raised}; border: none; border-radius: 14px; padding: 5px 15px; font-weight: 600; }
QFrame#aboutGroup { background: {bg}; border: 1px solid {border}; border-radius: 12px; }
QPushButton#aboutRow { background: transparent; border: none; border-radius: 10px; padding: 15px; text-align: left; }
QPushButton#aboutRow:hover { background: {hover}; }
QFrame#aboutSeparator { background: {border}; max-height: 1px; border: none; }
QToolTip { background: {raised}; color: {text}; border: 1px solid {border}; }
)";
    const QList<QPair<QString, QString>> colors = {
        {"bg", dark ? "#15202b" : "#ffffff"}, {"surface", dark ? "#192734" : "#f7f9f9"},
        {"raised", dark ? "#22303c" : "#eff3f4"}, {"hover", dark ? "#263746" : "#f1f3f4"},
        {"text", dark ? "#f3f5f7" : "#0f1419"}, {"muted", dark ? "#a4b3bf" : "#536471"},
        {"border", dark ? "#38444d" : "#cfd9de"}, {"scroll", dark ? "#536471" : "#b8c3ca"},
        {"primary", dark ? "#1b75bb" : "#292929"}, {"primaryHover", dark ? "#2385cf" : "#444444"},
        {"accent", dark ? "#70baff" : "#1c71d8"},
        {"errorBg", dark ? "#44252b" : "#fff0ee"}, {"errorText", dark ? "#ffb4ab" : "#ae4d43"},
        {"unreadBg", dark ? "#182e42" : "#edf6ff"}
    };
    for (const auto &color : colors) css.replace("{" + color.first + "}", color.second);
    return css;
}
}

ThemeManager *ThemeManager::instance()
{
    static ThemeManager *manager = new ThemeManager;
    return manager;
}

ThemeManager::ThemeManager() : QObject(qApp)
{
    const auto stored = QSettings().value("appearance/theme", "system").toString();
    m_mode = stored == "dark" ? Dark : stored == "light" ? Light : System;
    m_systemDark = qApp->palette().color(QPalette::Window).lightness() < 128;
#if QT_VERSION >= QT_VERSION_CHECK(6, 5, 0)
    m_systemDark = qApp->styleHints()->colorScheme() == Qt::ColorScheme::Dark;
    connect(qApp->styleHints(), &QStyleHints::colorSchemeChanged, this, [this](Qt::ColorScheme scheme) {
        m_systemDark = scheme == Qt::ColorScheme::Dark;
        if (m_mode == System) apply();
    });
#endif
    qApp->installEventFilter(this);
    apply();
}

bool ThemeManager::eventFilter(QObject *object, QEvent *event)
{
    if (object == qApp && event->type() == QEvent::ApplicationPaletteChange && !m_applying) {
#if QT_VERSION < QT_VERSION_CHECK(6, 5, 0)
        m_systemDark = qApp->palette().color(QPalette::Window).lightness() < 128;
#endif
        apply();
    }
    return QObject::eventFilter(object, event);
}

bool ThemeManager::isDark() const { return m_mode == Dark || (m_mode == System && m_systemDark); }

void ThemeManager::setMode(Mode mode)
{
    if (m_mode == mode) return;
    m_mode = mode;
    QSettings().setValue("appearance/theme", mode == Dark ? "dark" : mode == Light ? "light" : "system");
    apply();
}

void ThemeManager::apply()
{
    m_applying = true;
    const bool dark = isDark();
    QPalette palette;
    palette.setColor(QPalette::Window, QColor(dark ? "#15202b" : "#ffffff"));
    palette.setColor(QPalette::Base, palette.color(QPalette::Window));
    palette.setColor(QPalette::AlternateBase, QColor(dark ? "#22303c" : "#eff3f4"));
    palette.setColor(QPalette::Button, QColor(dark ? "#192734" : "#f7f9f9"));
    palette.setColor(QPalette::Mid, QColor(dark ? "#38444d" : "#cfd9de"));
    palette.setColor(QPalette::Light, QColor(dark ? "#38444d" : "#ffffff"));
    palette.setColor(QPalette::Dark, QColor(dark ? "#101820" : "#b0bec5"));
    palette.setColor(QPalette::Highlight, QColor(dark ? "#1b75bb" : "#1c71d8"));
    palette.setColor(QPalette::HighlightedText, Qt::white);
    palette.setColor(QPalette::Link, QColor(dark ? "#70baff" : "#1c71d8"));
    for (auto role : {QPalette::WindowText, QPalette::Text, QPalette::ButtonText, QPalette::ToolTipText}) {
        palette.setColor(role, QColor(dark ? "#f3f5f7" : "#0f1419"));
        palette.setColor(QPalette::Disabled, role, QColor(dark ? "#8999a6" : "#687780"));
    }
    palette.setColor(QPalette::PlaceholderText, QColor(dark ? "#a4b3bf" : "#536471"));
    palette.setColor(QPalette::ToolTipBase, palette.color(QPalette::AlternateBase));
    qApp->setPalette(palette);
    qApp->setStyleSheet(styleSheet(dark));
    m_applying = false;
    emit changed();
}

QMenu *appearanceMenu(QWidget *parent)
{
    auto *manager = ThemeManager::instance();
    auto *menu = new QMenu(QObject::tr("外観"), parent);
    auto *group = new QActionGroup(menu);
    const QStringList labels{QObject::tr("システムに合わせる"), QObject::tr("ライト"), QObject::tr("ダーク")};
    const QStringList names{"themeSystem", "themeLight", "themeDark"};
    const QStringList icons{"monitor", "sun", "moon"};
    for (int i = 0; i < labels.size(); ++i) {
        auto *action = menu->addAction(labels[i]);
        action->setIcon(appIcon(icons[i]));
        action->setObjectName(names[i]); action->setCheckable(true); group->addAction(action);
        action->setChecked(int(manager->mode()) == i);
        QObject::connect(action, &QAction::triggered, manager, [manager, i] { manager->setMode(ThemeManager::Mode(i)); });
        QObject::connect(manager, &ThemeManager::changed, action, [manager, action, i] { action->setChecked(int(manager->mode()) == i); });
    }
    return menu;
}

QToolButton *appearanceButton(QWidget *parent)
{
    auto *manager = ThemeManager::instance();
    auto *button = new QToolButton(parent);
    button->setObjectName("appearanceButton");
    button->setAccessibleName(QObject::tr("外観"));
    button->setFixedSize(32, 32); button->setIconSize(QSize(18, 18));
    button->setCursor(Qt::PointingHandCursor);
    button->setPopupMode(QToolButton::InstantPopup);
    button->setMenu(appearanceMenu(button));
    auto update = [manager, button] {
        button->setIcon(appIcon(manager->isDark() ? "moon" : "sun"));
        const auto mode = manager->mode() == ThemeManager::System ? QObject::tr("システムに合わせる")
            : manager->mode() == ThemeManager::Dark ? QObject::tr("ダーク") : QObject::tr("ライト");
        button->setToolTip(QObject::tr("外観：%1").arg(mode));
    };
    QObject::connect(manager, &ThemeManager::changed, button, update);
    update();
    return button;
}
