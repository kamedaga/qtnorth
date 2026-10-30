#include "posttext.h"
#include "post.h"
#include "postformat.h"
#include "preferences.h"
#include "theme.h"
#include <QEvent>
#include <QResizeEvent>
#include <QShowEvent>
#include <QTextBlock>
#include <QTextDocument>
#include <QTextLayout>
#include <QTextOption>
#include <QTimer>
#include <algorithm>
#include <cmath>

PostText::PostText(QString text, QWidget *parent) : QLabel(parent), m_plain(std::move(text))
{
    setObjectName("postText"); setWordWrap(true); setMinimumWidth(0);
    setAlignment(Qt::AlignTop | Qt::AlignLeft);
    setTextInteractionFlags(Qt::TextSelectableByMouse | Qt::LinksAccessibleByMouse);
    setOpenExternalLinks(false); setCursor(Qt::IBeamCursor);
    connect(Preferences::instance(), &Preferences::foldLongPostsChanged, this, [this] {
        m_expanded = false; updateFolding();
    });
    connect(ThemeManager::instance(), &ThemeManager::changed, this, &PostText::refreshText);
    refreshText();
}

void PostText::refreshText()
{
    m_cacheWidth = -1;
    const auto runs = North::postTextRuns(m_plain);
    QString html; m_visible.clear(); bool rich = false;
    const auto blue = palette().color(QPalette::Link).name();
    for (const auto &run : runs) {
        m_visible += run.text;
        auto links = North::textLinks(run.text);
        for (const auto &mention : North::textMentions(run.text))
            links.append({mention.start, mention.length, QUrl("qtnorth-profile:" + mention.handle)});
        links.append(North::textSearchTags(run.text));
        std::sort(links.begin(), links.end(), [](const North::TextLink &a, const North::TextLink &b) { return a.start < b.start; });
        QString part; int position = 0;
        for (const auto &link : links) {
            if (link.start < position) continue;
            part += run.text.mid(position, link.start - position).toHtmlEscaped();
            part += QString("<a href=\"%1\" style=\"color:%2; text-decoration:none\">%3</a>")
                .arg(link.url.toString(QUrl::FullyEncoded).toHtmlEscaped(), blue, run.text.mid(link.start, link.length).toHtmlEscaped());
            position = link.start + link.length;
        }
        part += run.text.mid(position).toHtmlEscaped();
        QStringList style;
        if (!run.style.color.isEmpty()) style << "color:" + run.style.color;
        if (run.style.bold) style << "font-weight:700";
        if (run.style.italic) style << "font-style:italic";
        if (!style.isEmpty()) part = "<span style=\"" + style.join(';') + "\">" + part + "</span>";
        html += part; rich |= !links.isEmpty() || !style.isEmpty();
    }
    if (!rich) { setTextFormat(Qt::PlainText); setText(m_visible); }
    else {
        html.replace('\n', "<br>");
        setTextFormat(Qt::RichText); setText("<span style=\"white-space:pre-wrap\">" + html + "</span>");
    }
    updateFolding();
}

int PostText::collapsedHeight(int width, bool *overflow) const
{
    if (width == m_cacheWidth && font() == m_cacheFont) {
        if (overflow) *overflow = m_cacheOverflow;
        return m_cacheHeight;
    }
    QTextDocument document; document.setDocumentMargin(0); document.setDefaultFont(font());
    QTextOption option; option.setWrapMode(QTextOption::WrapAtWordBoundaryOrAnywhere); document.setDefaultTextOption(option);
    if (textFormat() == Qt::RichText) document.setHtml(text()); else document.setPlainText(m_visible);
    document.setTextWidth(std::max(1, width)); document.documentLayout(); document.size();
    int lines = 0; qreal bottom = 0;
    for (auto block = document.begin(); block.isValid(); block = block.next()) {
        const auto *layout = block.layout();
        for (int i = 0; i < layout->lineCount(); ++i) {
            ++lines;
            if (lines <= 3) bottom = layout->position().y() + layout->lineAt(i).y() + layout->lineAt(i).height();
        }
    }
    m_cacheWidth = width; m_cacheFont = font(); m_cacheOverflow = lines > 3;
    m_cacheHeight = std::max(1, int(std::ceil(bottom)));
    if (overflow) *overflow = m_cacheOverflow;
    return m_cacheHeight;
}

int PostText::heightForWidth(int width) const
{
    const int full = QLabel::heightForWidth(width);
    return m_allowed && !m_expanded && Preferences::instance()->foldLongPosts()
        ? std::min(full, collapsedHeight(width)) : full;
}

QSize PostText::sizeHint() const
{
    auto result = QLabel::sizeHint(); result.setHeight(heightForWidth(std::max(1, result.width()))); return result;
}

void PostText::updateFolding()
{
    if (m_updating) return;
    m_updating = true;
    ensurePolished();
    if (!m_allowed || !Preferences::instance()->foldLongPosts()) {
        setMaximumHeight(QWIDGETSIZE_MAX); updateGeometry(); emit foldStateChanged(false, m_expanded);
        m_updating = false; return;
    }
    bool overflow = false;
    const int height = collapsedHeight(contentsRect().width(), &overflow);
    const bool available = m_allowed && Preferences::instance()->foldLongPosts() && overflow;
    setMaximumHeight(available && !m_expanded ? height : QWIDGETSIZE_MAX);
    updateGeometry(); emit foldStateChanged(available, m_expanded);
    m_updating = false;
}

void PostText::setFoldingAllowed(bool allowed) { m_allowed = allowed; updateFolding(); }
void PostText::toggleExpanded() { m_expanded = !m_expanded; updateFolding(); }
void PostText::showEvent(QShowEvent *event) { QLabel::showEvent(event); updateFolding(); }
void PostText::resizeEvent(QResizeEvent *event) { QLabel::resizeEvent(event); updateFolding(); }
void PostText::changeEvent(QEvent *event)
{
    QLabel::changeEvent(event);
    if (event->type() == QEvent::FontChange || event->type() == QEvent::StyleChange)
        QTimer::singleShot(0, this, &PostText::updateFolding);
}
