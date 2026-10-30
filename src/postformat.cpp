#include "postformat.h"
#include <QHash>
#include <QRegularExpression>

namespace {
QString withoutFormatCharacters(QString text)
{
    static const QRegularExpression invisible(QStringLiteral(R"(\p{Cf})"));
    return text.remove(invisible);
}
QString colorValue(QString value)
{
    static const QHash<QString, QString> colors{
        {"赤", "#f4212e"}, {"red", "#f4212e"}, {"青", "#1d9bf0"}, {"blue", "#1d9bf0"},
        {"緑", "#00ba7c"}, {"green", "#00ba7c"}, {"黄", "#ffd400"}, {"yellow", "#ffd400"},
        {"橙", "#ff7a00"}, {"orange", "#ff7a00"}, {"紫", "#7856ff"}, {"purple", "#7856ff"},
        {"桃", "#f91880"}, {"pink", "#f91880"}, {"水", "#5ac8fa"}, {"skyblue", "#5ac8fa"},
        {"白", "#ffffff"}, {"white", "#ffffff"}, {"黒", "#000000"}, {"black", "#000000"},
        {"灰", "#8b98a5"}, {"gray", "#8b98a5"}, {"grey", "#8b98a5"},
        {"金", "#d4af37"}, {"gold", "#d4af37"}, {"銀", "#c0c0c0"}, {"silver", "#c0c0c0"}
    };
    value = withoutFormatCharacters(value).trimmed();
    if (value.startsWith('"') || value.startsWith('\'')) value.remove(0, 1);
    if (value.endsWith('"') || value.endsWith('\'')) value.chop(1);
    value = value.trimmed().toLower();
    if (value.isEmpty() || value.size() > 20) return {};
    static const QRegularExpression hex("^#?(?:[0-9a-f]{3}|[0-9a-f]{6})$");
    if (!hex.match(value).hasMatch()) return colors.value(value);
    return value.startsWith('#') ? value : "#" + value;
}
enum Kind { Color, Bold, Italic };
struct Tag {
    int start, end;
    Kind kind;
    QString color;
    bool closing, matched = false;
};
}

QList<North::TextRun> North::postTextRuns(const QString &text)
{
    // Parse only north's three formatting primitives; all other text stays literal.
    static const QRegularExpression syntax(QStringLiteral(R"(<(/?)\p{Cf}*([^<>=\s/]{1,10})\p{Cf}*\s*(?:=\s*([^<>]{0,24}?)\s*)?>)"));
    static const QHash<QString, Kind> aliases{
        {"色", Color}, {"color", Color}, {"太字", Bold}, {"bold", Bold}, {"b", Bold},
        {"斜体", Italic}, {"italic", Italic}, {"i", Italic}
    };
    QList<Tag> tags;
    auto matches = syntax.globalMatch(text);
    while (matches.hasNext()) {
        const auto match = matches.next();
        const auto name = withoutFormatCharacters(match.captured(2)).toLower();
        if (!aliases.contains(name)) continue;
        const auto kind = aliases.value(name);
        const bool closing = !match.captured(1).isEmpty(), hasValue = match.capturedStart(3) >= 0;
        if ((closing || kind != Color) && hasValue) continue;
        const auto color = kind == Color && !closing ? colorValue(match.captured(3)) : QString();
        if (kind == Color && !closing && color.isEmpty()) continue;
        tags.append({int(match.capturedStart()), int(match.capturedEnd()), kind, color, closing});
    }
    QList<int> openings;
    for (int i = 0; i < tags.size(); ++i) {
        if (!tags[i].closing) { openings.append(i); continue; }
        for (int j = int(openings.size()) - 1; j >= 0; --j) {
            if (tags[openings[j]].kind != tags[i].kind) continue;
            tags[openings[j]].matched = tags[i].matched = true;
            openings.resize(j); break;
        }
    }
    QList<TextRun> runs;
    QList<Tag> active;
    auto append = [&](const QString &part) {
        if (part.isEmpty()) return;
        TextStyle style;
        for (const auto &tag : active) {
            if (tag.kind == Color) style.color = tag.color;
            else if (tag.kind == Bold) style.bold = true;
            else style.italic = true;
        }
        if (!runs.isEmpty() && runs.last().style == style) runs.last().text += part;
        else runs.append({part, style});
    };
    int position = 0;
    for (const auto &tag : tags) {
        append(text.mid(position, tag.start - position));
        if (!tag.matched) append(text.mid(tag.start, tag.end - tag.start));
        else if (!tag.closing) active.append(tag);
        else for (int i = int(active.size()) - 1; i >= 0; --i) {
            if (active[i].kind == tag.kind) { active.removeAt(i); break; }
        }
        position = tag.end;
    }
    append(text.mid(position));
    if (runs.isEmpty()) runs.append({text, {}});
    return runs;
}
