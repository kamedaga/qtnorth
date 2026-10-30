#pragma once
#include <QList>
#include <QString>

namespace North {
struct TextStyle {
    QString color;
    bool bold = false, italic = false;
    bool operator==(const TextStyle &other) const {
        return color == other.color && bold == other.bold && italic == other.italic;
    }
};
struct TextRun { QString text; TextStyle style; };
QList<TextRun> postTextRuns(const QString &text);
}
