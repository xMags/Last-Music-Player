#include "core/db/TrackRows.h"

#include "core/db/LibraryGroupingSql.h"
#include "core/net/ProviderUrlCredential.h"

#include <QLatin1StringView>

#include <utility>

namespace lmp::core::db::track_rows {

namespace {

constexpr QLatin1StringView kRemote{"remote"};

// The value sqlite3_column_int would give: the low 32 bits of the integer.
int columnInt(const SqliteStatement& statement, int column)
{
    return static_cast<int>(statement.columnInt64(column));
}

} // namespace

Track trackFromStatement(const SqliteStatement& statement)
{
    Track track;
    track.rowId = statement.columnInt64(0);
    const QString sourceKind = statement.columnText(1);
    track.sourceKind = sourceKind;
    track.origin = statement.columnText(2);
    track.sourceUrl = statement.columnText(3);
    track.filePath = net::removeLegacyProviderUrlCredential(statement.columnText(4));
    track.title = statement.columnText(5);
    track.artist = statement.columnText(6);
    track.album = statement.columnText(7);
    track.genre = statement.columnText(8);
    track.durationSeconds = statement.columnDouble(9);
    track.artworkUrl = net::removeLegacyProviderUrlCredential(statement.columnText(10));
    track.dateAddedSortKey = statement.columnDouble(11);
    track.dateAdded = statement.columnText(12);
    track.durationText = statement.columnText(13);
    track.liked = columnInt(statement, 14) != 0;
    track.sourceLabel = sourceLabelFor(sourceKind);
    // A remote row without a stored path plays from its source address,
    // except an account row, whose address is resolved when it plays.
    if (sourceKind == kRemote && track.filePath.isEmpty() && track.origin != QLatin1StringView("account")) {
        track.filePath = track.sourceUrl;
    }
    track.remoteId = statement.columnText(15);
    track.resumePositionSeconds = statement.columnDouble(16);
    track.sourceTag = statement.columnText(17);
    track.catalogId = statement.columnText(18);
    track.albumCatalogId = statement.columnText(19);
    track.artistCatalogId = statement.columnText(20);
    return track;
}

QList<Track> readTracks(SqliteStatement& statement, int firstIndex)
{
    QList<Track> tracks;
    int index = firstIndex;
    while (statement.step().valueOr(false)) {
        Track track = trackFromStatement(statement);
        track.index = index++;
        tracks.append(std::move(track));
    }
    return tracks;
}

QString storedFilePathFor(const Track& track, const QString& sourceKind)
{
    if (sourceKind == kRemote) {
        return {};
    }
    return net::removeLegacyProviderUrlCredential(track.filePath);
}

QString sourceLabelFor(const QString& sourceKind)
{
    return sourceKind == kRemote ? QStringLiteral("Remote") : QStringLiteral("Local");
}

QString mixLabelFor(int remoteCount, int count)
{
    if (remoteCount == 0) {
        return QStringLiteral("Local");
    }
    return remoteCount == count ? QStringLiteral("Remote") : QStringLiteral("Mixed");
}

QString songCountText(int count)
{
    return QString::number(count) + (count == 1 ? QStringLiteral(" song") : QStringLiteral(" songs"));
}

std::string trackOrderClause(const QString& filter, const QString& sort)
{
    const QString normalizedFilter = filter.toLower();
    const QString normalizedSort = sort.toLower();

    // An explicitly requested play-count order wins over any filter.
    if (normalizedSort == QLatin1StringView("mostplayed")) {
        return "ORDER BY PlayCount DESC, LastPlayedOrder DESC, Title COLLATE NOCASE ASC";
    }

    // The history and most-played views share a sort control offering
    // title, artist and duration, and each has its own natural default
    // under the fourth option: most recent, and most played.
    if (normalizedFilter == QLatin1StringView("history") || normalizedFilter == QLatin1StringView("most")) {
        if (normalizedSort == QLatin1StringView("title")) {
            return "ORDER BY Title COLLATE NOCASE ASC";
        }
        if (normalizedSort == QLatin1StringView("artist")) {
            return "ORDER BY Artist COLLATE NOCASE ASC, Title COLLATE NOCASE ASC";
        }
        if (normalizedSort == QLatin1StringView("duration")) {
            return "ORDER BY DurationSeconds ASC, Title COLLATE NOCASE ASC";
        }
        return normalizedFilter == QLatin1StringView("most")
            ? "ORDER BY PlayCount DESC, LastPlayedOrder DESC, Title COLLATE NOCASE ASC"
            : "ORDER BY LastPlayedOrder DESC, Title COLLATE NOCASE ASC";
    }
    if (normalizedSort == QLatin1StringView("title")) {
        return "ORDER BY Title COLLATE NOCASE ASC";
    }
    if (normalizedSort == QLatin1StringView("artist")) {
        return "ORDER BY Artist COLLATE NOCASE ASC, Title COLLATE NOCASE ASC";
    }
    if (normalizedSort == QLatin1StringView("duration")) {
        return "ORDER BY DurationSeconds DESC, Title COLLATE NOCASE ASC";
    }
    return "ORDER BY DateAddedSortKey DESC, Title COLLATE NOCASE ASC";
}

std::string trackFilterClause(const QString& filter)
{
    const QString normalizedFilter = filter.toLower();
    if (normalizedFilter == QLatin1StringView("history")) {
        return " AND COALESCE(LastPlayedOrder,0)>0";
    }
    if (normalizedFilter == QLatin1StringView("most")) {
        return " AND COALESCE(PlayCount,0)>0";
    }
    if (normalizedFilter == QLatin1StringView("fav") || normalizedFilter == QLatin1StringView("liked")) {
        return " AND COALESCE(IsLiked,0)=1";
    }
    return {};
}

std::string groupFilterClause(const QString& groupKind)
{
    namespace grouping = library_grouping_sql;
    const QString normalizedKind = groupKind.toLower();
    if (normalizedKind == QLatin1StringView("album")) {
        return " AND " + grouping::GroupMatchPredicate(grouping::GroupKind::Album, "?3");
    }
    if (normalizedKind == QLatin1StringView("artist")) {
        return " AND " + grouping::GroupMatchPredicate(grouping::GroupKind::Artist, "?3");
    }
    if (normalizedKind == QLatin1StringView("genre")) {
        return " AND " + grouping::GroupMatchPredicate(grouping::GroupKind::Genre, "?3");
    }
    return {};
}

QString escapedLikePattern(const QString& text)
{
    QString pattern;
    pattern.reserve(text.size() + 2);
    pattern.append(QLatin1Char('%'));
    for (const QChar character : text) {
        if (character == QLatin1Char('!') || character == QLatin1Char('%') || character == QLatin1Char('_')) {
            pattern.append(QLatin1Char('!'));
        }
        pattern.append(character);
    }
    pattern.append(QLatin1Char('%'));
    return pattern;
}

} // namespace lmp::core::db::track_rows
