#include "icons.h"
#include <QHash>
#include <QApplication>
#include <QIconEngine>
#include <QPainter>
#include <QPalette>
#include <QSvgRenderer>

namespace {
class ThemeIconEngine : public QIconEngine {
public:
    ThemeIconEngine(QString path, QString color) : m_path(std::move(path)), m_color(std::move(color)) {}
    QIconEngine *clone() const override { return new ThemeIconEngine(m_path, m_color); }
    void paint(QPainter *painter, const QRect &rect, QIcon::Mode mode, QIcon::State) override {
        const auto color = m_color.isEmpty() ? QApplication::palette().color(mode == QIcon::Disabled ? QPalette::Disabled : QPalette::Active, QPalette::ButtonText).name() : m_color;
        const auto svg = QString("<svg xmlns='http://www.w3.org/2000/svg' viewBox='0 0 24 24' fill='none' stroke='%1' stroke-width='1.6' stroke-linecap='round' stroke-linejoin='round'>%2</svg>").arg(color, m_path);
        QSvgRenderer renderer(svg.toUtf8()); renderer.render(painter, rect);
    }
    QPixmap pixmap(const QSize &size, QIcon::Mode mode, QIcon::State state) override {
        QPixmap result(size); result.fill(Qt::transparent);
        QPainter painter(&result); paint(&painter, QRect(QPoint(), size), mode, state);
        return result;
    }
private:
    QString m_path, m_color;
};
}

QIcon appIcon(const QString &name, const QString &color)
{
    static const QHash<QString, QString> paths = {
        {"bell", "<path d='M18 8a6 6 0 0 0-12 0c0 7-3 7-3 9h18c0-2-3-2-3-9M10 21h4'/>"},
        {"minimize", "<path d='M5 12h14'/>"},
        {"maximize", "<rect x='5' y='5' width='14' height='14' rx='1'/>"},
        {"restore", "<path d='M9 7V4h11v11h-3'/><rect x='4' y='8' width='12' height='12' rx='1'/>"},
        {"about", "<circle cx='12' cy='12' r='9'/><path d='M12 11v6m0-10v1'/>"},
        {"sun", "<circle cx='12' cy='12' r='4'/><path d='M12 2v2m0 16v2M2 12h2m16 0h2M4.9 4.9l1.4 1.4m11.4 11.4 1.4 1.4M4.9 19.1l1.4-1.4M17.7 6.3l1.4-1.4'/>"},
        {"moon", "<path d='M21 12.79A9 9 0 1 1 11.21 3 7 7 0 0 0 21 12.79z'/>"},
        {"monitor", "<rect x='3' y='4' width='18' height='13' rx='2'/><path d='M8 21h8m-4-4v4'/>"},
        {"home", "<path d='m3 10 9-7 9 7v10a1 1 0 0 1-1 1h-5v-7H9v7H4a1 1 0 0 1-1-1z'/>"},
        {"compass", "<circle cx='12' cy='12' r='9'/><path d='m16 8-2.5 5.5L8 16l2.5-5.5z'/>"},
        {"search", "<circle cx='10.5' cy='10.5' r='6.5'/><path d='m16 16 5 5'/>"},
        {"user", "<circle cx='12' cy='8' r='3.5'/><path d='M4 21v-2a8 8 0 0 1 16 0v2'/>"},
        {"message", "<rect x='3' y='5' width='18' height='14' rx='2'/><path d='m3 6 9 7 9-7'/>"},
        {"back", "<path d='M19 12H5m6-6-6 6 6 6'/>"},
        {"refresh", "<path d='M20 10a8 8 0 1 0-1 8M20 3v7h-7'/>"},
        {"panel", "<rect x='3' y='4' width='18' height='16' rx='2'/><path d='M15 4v16'/>"},
        {"compose", "<path d='M12 5H5a2 2 0 0 0-2 2v12a2 2 0 0 0 2 2h12a2 2 0 0 0 2-2v-7M9 15l1-4L19 2l3 3-9 9z'/>"},
        {"heart", "<path d='M20.8 4.6a5.5 5.5 0 0 0-7.8 0L12 5.7l-1.1-1.1a5.5 5.5 0 0 0-7.8 7.8L12 21l8.8-8.6a5.5 5.5 0 0 0 0-7.8z'/>"},
        {"repost", "<path d='m4 7 3-3 3 3M7 4v12a2 2 0 0 0 2 2h3m8-1-3 3-3-3m3 3V8a2 2 0 0 0-2-2h-3'/>"},
        {"reply", "<path d='M21 11a8 8 0 0 1-8 8H6l-4 3V11a9 9 0 0 1 19 0z'/>"},
        {"arrow", "<path d='M5 12h14m-6-6 6 6-6 6'/>"},
        {"external", "<path d='M14 3h7v7m0-7L10 14M10 3H5a2 2 0 0 0-2 2v14a2 2 0 0 0 2 2h14a2 2 0 0 0 2-2v-5'/>"},
        {"copy", "<rect x='8' y='8' width='13' height='13' rx='2'/><path d='M16 8V3H3v13h5'/>"},
        {"logout", "<path d='M9 3H3v18h6m5-15 6 6-6 6M8 12h12'/>"},
        {"image", "<rect x='3' y='3' width='18' height='18' rx='2'/><circle cx='8' cy='8' r='1'/><path d='m3 17 5-5 4 4 4-6 5 7'/>"},
        {"list", "<circle cx='4' cy='6' r='1'/><circle cx='4' cy='12' r='1'/><circle cx='4' cy='18' r='1'/><path d='M9 6h12M9 12h12M9 18h12'/>"},
        {"emoji", "<circle cx='12' cy='12' r='9'/><path d='M8 14a4 4 0 0 0 8 0M8 8h.01M16 8h.01'/>"},
        {"bold", "<path d='M7 4h6a4 4 0 0 1 0 8H7zm0 8h7a4 4 0 0 1 0 8H7z' stroke-width='2.2'/>"},
        {"italic", "<path d='M10 4h9M5 20h9M15 4 9 20' stroke-width='2'/>"},
        {"poll", "<path d='M9 6h12M9 12h8M9 18h10'/><rect x='3' y='4' width='3' height='4' rx='.5'/><rect x='3' y='10' width='3' height='4' rx='.5'/><rect x='3' y='16' width='3' height='4' rx='.5'/>"},
        {"chevron", "<path d='m9 5 7 7-7 7'/>"},
        {"chevronDown", "<path d='m6 9 6 6 6-6'/>"},
        {"settings", "<circle cx='12' cy='12' r='3'/><path d='m9 3-.5 2.1-1.4.8L5 5.3 2.5 9.5l1.6 1.5v1.7l-1.6 1.5L5 18.5l2.1-.6 1.4.8.5 2.1h5l.5-2.1 1.4-.8 2.1.6 2.5-4.3-1.6-1.5V11l1.6-1.5L18 5.3l-2.1.6-1.4-.8L14 3z'/>"},
        {"check", "<path d='m5 12 4 4L19 6'/>"},
        {"close", "<path d='m6 6 12 12M18 6 6 18'/>"},
        {"star", "<path d='M12 2c0 9 1 10 9 10-8 0-9 1-9 10 0-9-1-10-9-10 8 0 9-1 9-10z'/>"}
    };
    return QIcon(new ThemeIconEngine(paths.value(name, paths.value("star")), color));
}
