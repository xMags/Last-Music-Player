#include "core/db/AccountPlaylistPage.h"

#include "core/text/CaseFold.h"
#include <QLatin1StringView>

#include <algorithm>

namespace lmp::core::db {

namespace {

bool lessByTitle(const Track& left, const Track& right)
{
    return foldCaseAscii(left.title) < foldCaseAscii(right.title);
}

bool lessByArtist(const Track& left, const Track& right)
{
    const QString leftArtist = foldCaseAscii(left.artist);
    const QString rightArtist = foldCaseAscii(right.artist);
    if (leftArtist != rightArtist) {
        return leftArtist < rightArtist;
    }
    return lessByTitle(left, right);
}

bool longerFirst(const Track& left, const Track& right)
{
    if (left.durationSeconds != right.durationSeconds) {
        return left.durationSeconds > right.durationSeconds;
    }
    return lessByTitle(left, right);
}

bool newerFirst(const Track& left, const Track& right)
{
    if (left.dateAddedSortKey != right.dateAddedSortKey) {
        return left.dateAddedSortKey > right.dateAddedSortKey;
    }
    return lessByTitle(left, right);
}

} // namespace

TrackPage pageAccountPlaylistTracks(QList<Track> tracks, const TrackQuery& query)
{
    if (!query.searchText.isEmpty()) {
        const QString needle = foldCaseAscii(query.searchText);
        tracks.removeIf([&needle](const Track& track) {
            return !foldCaseAscii(track.title).contains(needle) && !foldCaseAscii(track.artist).contains(needle)
                && !foldCaseAscii(track.album).contains(needle);
        });
    }

    const QString sort = foldCaseAscii(query.sort);
    if (sort == QLatin1StringView("title")) {
        std::stable_sort(tracks.begin(), tracks.end(), lessByTitle);
    } else if (sort == QLatin1StringView("artist")) {
        std::stable_sort(tracks.begin(), tracks.end(), lessByArtist);
    } else if (sort == QLatin1StringView("duration")) {
        std::stable_sort(tracks.begin(), tracks.end(), longerFirst);
    } else if (sort == QLatin1StringView("dateadded")) {
        std::stable_sort(tracks.begin(), tracks.end(), newerFirst);
    }

    TrackPage page;
    page.totalCount = static_cast<int>(tracks.size());
    for (const Track& track : tracks) {
        page.totalSeconds += track.durationSeconds;
    }

    const int offset = std::max(0, query.offset);
    const qsizetype begin = std::min(offset, page.totalCount);
    qsizetype end = tracks.size();
    if (query.limit > 0) {
        end = std::min(end, begin + query.limit);
    }
    page.tracks = tracks.mid(begin, end - begin);
    for (qsizetype index = 0; index < page.tracks.size(); ++index) {
        page.tracks[index].index = static_cast<int>(begin + index + 1);
    }
    return page;
}

} // namespace lmp::core::db
