#pragma once

#include <QList>
#include <QString>
#include <QStringView>

namespace lmp::core {

enum class BrowseResultKind {
    Song,
    Album,
    Playlist,
    Artist,
};

// One search hit as the search screen received it.
struct BrowseResultInput {
    BrowseResultKind kind = BrowseResultKind::Song;
    QString title;
    QString artist;
    QString album;
    QString year;
    QString duration;
    QString key;
    qsizetype count = 0;
};

// One card or row of the grouped results. Indices point into the inputs.
struct BrowseResultEntity {
    BrowseResultKind kind = BrowseResultKind::Song;
    QString title;
    QString subtitle;
    QString meta;
    QString key;
    qsizetype representativeIndex = 0;
    QList<qsizetype> memberIndices;
};

struct BrowseResultGroup {
    BrowseResultKind kind = BrowseResultKind::Song;
    QList<BrowseResultEntity> items;
};

struct GroupedBrowseResults {
    QList<BrowseResultGroup> groups;
    qsizetype displayedEntityCount = 0;
};

// Groups search hits into songs, albums, playlists and artists (that order;
// empty groups are left out). Only identity fields match the query: a song by
// title, artist or album; an album by album, artist or year; a playlist by
// its name; an artist by name. Albums and artists are built from the song
// hits. A blank query gives no groups. `preserveUrlLookupSong` keeps every
// song hit, for a pasted link whose resolved song cannot match the link text.
[[nodiscard]] GroupedBrowseResults buildGroupedBrowseResults(const QList<BrowseResultInput>& inputs,
                                                             QStringView query, bool preserveUrlLookupSong);

enum class BrowseViewPreference {
    Grid,
    List,
};

// "list" (any case, surrounding spaces ignored) is List; anything else Grid.
[[nodiscard]] BrowseViewPreference parseBrowseViewPreference(QStringView value);
[[nodiscard]] QString browseViewPreferenceTag(BrowseViewPreference value);

} // namespace lmp::core
