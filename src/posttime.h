#pragma once
#include <QString>
class QLabel;

QLabel *postTimeLabel(const QString &createdAt, const QString &format = QStringLiteral("MM/dd HH:mm"));
