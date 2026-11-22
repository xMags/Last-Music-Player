#include "core/library/RecentSearchStore.h"

#include "core/settings/SettingKeys.h"
#include "core/settings/SettingsStore.h"
#include "core/text/CaseFold.h"

#include <QChar>

#include <algorithm>

namespace lmp::core {

namespace keys = settings::keys;

QList<QString> RecentSearchStore::record(const QList<QString>& history, const QString& query, qsizetype limit)
{
    const QString trimmedQuery = query.trimmed();
    if (trimmedQuery.isEmpty() || limit <= 0) {
        return limit <= 0 ? QList<QString>() : history;
    }

    QList<QString> result;
    result.reserve(std::min(limit, history.size() + 1));
    result.append(trimmedQuery);
    const QString foldedQuery = foldCaseAscii(trimmedQuery);
    for (const QString& existing : history) {
        if (result.size() >= limit) {
            break;
        }
        QString cleaned = existing.trimmed();
        if (cleaned.isEmpty() || foldCaseAscii(cleaned) == foldedQuery) {
            continue;
        }
        result.append(std::move(cleaned));
    }
    return result;
}

QString RecentSearchStore::encode(const QList<QString>& history)
{
    QString encoded;
    for (qsizetype index = 0; index < history.size(); ++index) {
        if (index > 0) {
            encoded.append(QLatin1Char('\n'));
        }
        for (const QChar character : history[index]) {
            if (character == QLatin1Char('\\')) {
                encoded.append(QStringLiteral("\\\\"));
            } else if (character == QLatin1Char('\n') || character == QLatin1Char('\r')) {
                encoded.append(QStringLiteral("\\n"));
            } else {
                encoded.append(character);
            }
        }
    }
    return encoded;
}

QList<QString> RecentSearchStore::decode(const QString& encoded, qsizetype limit)
{
    QList<QString> result;
    QString current;
    bool escaped = false;
    const auto append = [&result, &current, limit]() {
        QString cleaned = current.trimmed();
        current.clear();
        if (!cleaned.isEmpty() && result.size() < limit) {
            result.append(std::move(cleaned));
        }
    };

    for (const QChar character : encoded) {
        if (escaped) {
            // "\n" is a line break; any other escaped character stands for itself.
            current.append(character == QLatin1Char('n') ? QChar(u'\n') : character);
            escaped = false;
            continue;
        }
        if (character == QLatin1Char('\\')) {
            escaped = true;
            continue;
        }
        if (character == QLatin1Char('\n')) {
            append();
            if (result.size() >= limit) {
                return result;
            }
            continue;
        }
        current.append(character);
    }
    if (escaped) {
        current.append(QLatin1Char('\\'));
    }
    append();
    return result;
}

QList<QString> RecentSearchStore::load(const SettingsStore& settings)
{
    return decode(settings.string(keys::RecentSearches, QString()));
}

void RecentSearchStore::save(SettingsStore& settings, const QList<QString>& history)
{
    settings.setString(keys::RecentSearches, encode(history));
}

void RecentSearchStore::clear(SettingsStore& settings)
{
    settings.setString(keys::RecentSearches, QString());
}

} // namespace lmp::core
