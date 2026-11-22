#pragma once

#include <QList>
#include <QString>

namespace lmp::core {

class SettingsStore;

// The recent searches shown on the empty search screen, newest first, and
// their stored form in the RecentSearches setting.
//
// Stored form: entries joined by a newline. Inside an entry a backslash is
// written as two backslashes and a line break (newline or carriage return) as
// a backslash followed by "n", so pasted multi-line text survives a round trip.
struct RecentSearchStore {
    static constexpr qsizetype Limit = 6;

    // The history after adding `query`: trimmed, put first, and earlier
    // entries that are blank or equal to it ignoring case dropped, at most
    // `limit` long. A blank query leaves the history unchanged; a limit of
    // zero (or less) empties it.
    [[nodiscard]] static QList<QString> record(const QList<QString>& history, const QString& query,
                                               qsizetype limit = Limit);

    [[nodiscard]] static QString encode(const QList<QString>& history);

    // Entries are trimmed and blank ones skipped; at most `limit` are kept.
    [[nodiscard]] static QList<QString> decode(const QString& encoded, qsizetype limit = Limit);

    // The stored history (empty when the setting is missing).
    [[nodiscard]] static QList<QString> load(const SettingsStore& settings);
    // Saves `history` in the stored form.
    static void save(SettingsStore& settings, const QList<QString>& history);
    // Stores an empty history.
    static void clear(SettingsStore& settings);
};

} // namespace lmp::core
