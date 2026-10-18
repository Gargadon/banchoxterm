#pragma once
#include <QJsonArray>
#include <QJsonObject>
#include <QString>

// One key:value pair per line; repeated keys allow several applications/services.
inline QJsonObject parseSessionTags(const QString& text, bool* ok = nullptr) {
    QJsonObject tags;
    bool valid = true;
    for (const QString& line : text.split('\n')) {
        if (line.trimmed().isEmpty())
            continue;
        const int colon = line.indexOf(':');
        const QString key = line.left(colon).trimmed();
        const QString value = line.mid(colon + 1).trimmed();
        if (colon < 1 || key.isEmpty() || value.isEmpty()) {
            valid = false;
            continue;
        }
        auto values = tags.value(key).toArray();
        if (!values.contains(value))
            values.append(value);
        tags.insert(key, values);
    }
    if (ok)
        *ok = valid;
    return tags;
}

inline QString formatSessionTags(const QJsonObject& tags) {
    QStringList lines;
    for (auto it = tags.begin(); it != tags.end(); ++it)
        for (const auto& value : it.value().toArray())
            lines.append(it.key() + ':' + value.toString());
    return lines.join('\n');
}
