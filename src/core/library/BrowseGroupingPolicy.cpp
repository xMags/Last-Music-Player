#include "core/library/BrowseGroupingPolicy.h"

#include "core/text/CaseFold.h"

#include <QHash>
#include <QLatin1StringView>
#include <QSet>

namespace lmp::core {

namespace {

// Trimmed and lower-cased: the form every match and identity uses here.
QString fold(QStringView value)
{
    return foldCaseAscii(value.trimmed());
}

bool contains(const QString& field, const QString& foldedQuery)
{
    return !field.isEmpty() && fold(field).contains(foldedQuery);
}

bool matchesSong(const BrowseResultInput& input, const QString& query)
{
    return contains(input.title, query) || contains(input.artist, query) || contains(input.album, query);
}

bool matchesAlbum(const BrowseResultInput& input, const QString& query)
{
    return contains(input.album, query) || contains(input.artist, query) || contains(input.year, query);
}

QString joinedSubtitle(const QString& first, const QString& second)
{
    if (first.isEmpty()) {
        return second;
    }
    if (second.isEmpty()) {
        return first;
    }
    return first + QStringLiteral(" · ") + second;
}

QString countLabel(qsizetype count, QLatin1StringView singular, QLatin1StringView plural)
{
    return QString::number(count) + QLatin1Char(' ') + (count == 1 ? singular : plural);
}

BrowseResultGroup songs(const QList<BrowseResultInput>& inputs, const QString& query, bool preserveUrlLookupSong)
{
    BrowseResultGroup group{BrowseResultKind::Song, {}};
    for (qsizetype index = 0; index < inputs.size(); ++index) {
        const BrowseResultInput& input = inputs[index];
        if (input.kind != BrowseResultKind::Song || (!preserveUrlLookupSong && !matchesSong(input, query))) {
            continue;
        }
        group.items.append({BrowseResultKind::Song, input.title, joinedSubtitle(input.artist, input.album),
                            input.duration, input.key, index, {index}});
    }
    return group;
}

BrowseResultGroup albums(const QList<BrowseResultInput>& inputs, const QString& query)
{
    BrowseResultGroup group{BrowseResultKind::Album, {}};
    QHash<QString, qsizetype> positions;
    for (qsizetype index = 0; index < inputs.size(); ++index) {
        const BrowseResultInput& input = inputs[index];
        if (input.kind != BrowseResultKind::Song || input.album.isEmpty() || !matchesAlbum(input, query)) {
            continue;
        }

        const QString identity = fold(input.album) + QLatin1Char('\n') + fold(input.artist);
        const auto found = positions.constFind(identity);
        if (found != positions.cend()) {
            group.items[found.value()].memberIndices.append(index);
            continue;
        }

        positions.insert(identity, group.items.size());
        group.items.append({BrowseResultKind::Album, input.album, joinedSubtitle(input.artist, input.year), {},
                            input.album, index, {index}});
    }
    return group;
}

BrowseResultGroup playlists(const QList<BrowseResultInput>& inputs, const QString& query)
{
    BrowseResultGroup group{BrowseResultKind::Playlist, {}};
    for (qsizetype index = 0; index < inputs.size(); ++index) {
        const BrowseResultInput& input = inputs[index];
        if (input.kind != BrowseResultKind::Playlist || !contains(input.title, query)) {
            continue;
        }
        group.items.append({BrowseResultKind::Playlist, input.title,
                            countLabel(input.count, QLatin1StringView("track"), QLatin1StringView("tracks")), {},
                            input.key, index, {index}});
    }
    return group;
}

BrowseResultGroup artists(const QList<BrowseResultInput>& inputs, const QString& query)
{
    BrowseResultGroup group{BrowseResultKind::Artist, {}};
    QHash<QString, qsizetype> positions;
    // Distinct folded album names per artist entity, parallel to group.items.
    QList<QSet<QString>> albumsPerArtist;
    for (qsizetype index = 0; index < inputs.size(); ++index) {
        const BrowseResultInput& input = inputs[index];
        if (input.kind != BrowseResultKind::Song || input.artist.isEmpty() || !contains(input.artist, query)) {
            continue;
        }

        const QString identity = fold(input.artist);
        const QString album = fold(input.album);
        const auto found = positions.constFind(identity);
        if (found != positions.cend()) {
            const qsizetype position = found.value();
            group.items[position].memberIndices.append(index);
            if (!album.isEmpty()) {
                albumsPerArtist[position].insert(album);
            }
            continue;
        }

        positions.insert(identity, group.items.size());
        group.items.append({BrowseResultKind::Artist, input.artist, {}, {}, input.artist, index, {index}});
        albumsPerArtist.append(QSet<QString>());
        if (!album.isEmpty()) {
            albumsPerArtist.last().insert(album);
        }
    }

    for (qsizetype index = 0; index < group.items.size(); ++index) {
        group.items[index].subtitle =
            countLabel(albumsPerArtist[index].size(), QLatin1StringView("album"), QLatin1StringView("albums"));
    }
    return group;
}

} // namespace

GroupedBrowseResults buildGroupedBrowseResults(const QList<BrowseResultInput>& inputs, QStringView query,
                                               bool preserveUrlLookupSong)
{
    GroupedBrowseResults result;
    const QString foldedQuery = fold(query);
    if (foldedQuery.isEmpty()) {
        return result;
    }

    QList<BrowseResultGroup> groups;
    groups.append(songs(inputs, foldedQuery, preserveUrlLookupSong));
    groups.append(albums(inputs, foldedQuery));
    groups.append(playlists(inputs, foldedQuery));
    groups.append(artists(inputs, foldedQuery));
    for (BrowseResultGroup& group : groups) {
        if (group.items.isEmpty()) {
            continue;
        }
        result.displayedEntityCount += group.items.size();
        result.groups.append(std::move(group));
    }
    return result;
}

BrowseViewPreference parseBrowseViewPreference(QStringView value)
{
    return fold(value) == QLatin1StringView("list") ? BrowseViewPreference::List : BrowseViewPreference::Grid;
}

QString browseViewPreferenceTag(BrowseViewPreference value)
{
    return value == BrowseViewPreference::List ? QStringLiteral("list") : QStringLiteral("grid");
}

} // namespace lmp::core
