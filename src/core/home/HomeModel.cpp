#include "core/home/HomeModel.h"

#include <QDateTime>
#include <QHash>
#include <QSet>

#include <algorithm>
#include <cmath>
#include <functional>
#include <limits>

namespace lmp::core::home {

namespace {

const QString kMadeForYou = QStringLiteral("Made for you");
const QString kFromFollowed = QStringLiteral("From artists you follow");

[[nodiscard]] QString trackId(const Track& track)
{
    return track.remoteId.isEmpty() ? track.sourceUrl : track.remoteId;
}

[[nodiscard]] QString canonical(const QString& value)
{
    return value.trimmed().toLower();
}

[[nodiscard]] qint64 timeValue(const QString& value)
{
    if (value.isEmpty()) {
        return 0;
    }
    const QDateTime time = QDateTime::fromString(value, Qt::ISODateWithMs);
    return time.isValid() ? time.toMSecsSinceEpoch() : 0;
}

[[nodiscard]] qint64 addedTime(const Track& track)
{
    for (const QString* value : {&track.addedAt, &track.firstPlayedAt, &track.likedAt, &track.lastPlayedAt}) {
        if (!value->isEmpty()) {
            return timeValue(*value);
        }
    }
    return 0;
}

[[nodiscard]] QString earlier(const QString& a, const QString& b)
{
    if (a.isEmpty()) {
        return b;
    }
    if (b.isEmpty()) {
        return a;
    }
    return timeValue(a) <= timeValue(b) ? a : b;
}

[[nodiscard]] QString later(const QString& a, const QString& b)
{
    if (a.isEmpty()) {
        return b;
    }
    if (b.isEmpty()) {
        return a;
    }
    return timeValue(a) >= timeValue(b) ? a : b;
}

[[nodiscard]] QList<Track> uniqueTracks(const QList<Track>& tracks)
{
    QSet<QString> seen;
    QList<Track> result;
    for (const Track& track : tracks) {
        const QString id = trackId(track);
        if (id.isEmpty() || seen.contains(id)) {
            continue;
        }
        seen.insert(id);
        result.append(track);
    }
    return result;
}

[[nodiscard]] QList<Track> fillMix(std::initializer_list<QList<Track>> groups)
{
    QList<Track> all;
    for (const QList<Track>& group : groups) {
        all += group;
    }
    return uniqueTracks(all).mid(0, OnlineMixLength);
}

[[nodiscard]] QList<Track> tracksByArtist(const QList<Track>& tracks, const QString& artist)
{
    if (artist.isEmpty()) {
        return {};
    }
    const QString wanted = canonical(artist);
    QList<Track> result;
    std::copy_if(tracks.cbegin(), tracks.cend(), std::back_inserter(result),
                 [&wanted](const Track& track) { return canonical(track.artist) == wanted; });
    return result;
}

[[nodiscard]] Track mergeTrack(const Track& left, const Track& right)
{
    Track merged = right;
    const auto either = [](const QString& preferred, const QString& other) {
        return preferred.isEmpty() ? other : preferred;
    };
    merged.title = either(right.title, left.title);
    merged.artist = either(right.artist, left.artist);
    merged.album = either(right.album, left.album);
    merged.sourceUrl = either(right.sourceUrl, left.sourceUrl);
    merged.sourceReference = right.sourceUrl.isEmpty() ? left.sourceReference : right.sourceReference;
    merged.streamUrl = either(right.streamUrl, left.streamUrl);
    merged.artworkUrl = either(right.artworkUrl, left.artworkUrl);
    merged.genre = either(right.genre, left.genre);
    merged.playCount = std::max(left.playCount, right.playCount);
    merged.firstPlayedAt = earlier(left.firstPlayedAt, right.firstPlayedAt);
    merged.lastPlayedAt = later(left.lastPlayedAt, right.lastPlayedAt);
    merged.likedAt = earlier(left.likedAt, right.likedAt);
    merged.addedAt = earlier(left.addedAt, right.addedAt);
    return merged;
}

[[nodiscard]] QList<Track> cloudCatalog(const HomeLibrary& library)
{
    QSet<QString> likedIds;
    for (const Track& track : library.liked) {
        likedIds.insert(trackId(track));
    }
    QList<QString> order;
    QHash<QString, Track> byId;
    for (const QList<Track>* list : {&library.history, &library.liked, &library.playlistTracks}) {
        for (const Track& track : *list) {
            const QString id = trackId(track);
            const auto found = byId.find(id);
            Track merged = found != byId.end() ? mergeTrack(*found, track) : track;
            merged.liked = likedIds.contains(id);
            if (found == byId.end()) {
                order.append(id);
            }
            byId.insert(id, merged);
        }
    }
    QList<Track> catalog;
    for (const QString& id : order) {
        catalog.append(byId.value(id));
    }
    return catalog;
}

template <typename Compare>
[[nodiscard]] QList<Track> sorted(QList<Track> tracks, Compare compare)
{
    std::stable_sort(tracks.begin(), tracks.end(), compare);
    return tracks;
}

} // namespace

QString homeMixKey(const HomeLibrary& library, const QDate& today)
{
    const bool hasMusic = !library.history.isEmpty() || !library.liked.isEmpty() || !library.playlistTracks.isEmpty();
    return QStringLiteral("%1-%2-%3:%4")
        .arg(today.year())
        .arg(today.month())
        .arg(today.day())
        .arg(hasMusic ? QStringLiteral("music") : QStringLiteral("empty"));
}

QStringList rankedValues(const QList<Track>& tracks, bool byGenre)
{
    struct Score {
        QString label;
        double score = 0;
    };
    QList<QString> order;
    QHash<QString, Score> scores;
    const double count = std::max<double>(1, static_cast<double>(tracks.size()));
    for (qsizetype index = 0; index < tracks.size(); ++index) {
        const Track& track = tracks.at(index);
        const QString label = (byGenre ? track.genre : track.artist).trimmed();
        const QString key = canonical(label);
        if (key.isEmpty() || key == QLatin1StringView("unknown artist")) {
            continue;
        }
        if (!scores.contains(key)) {
            order.append(key);
            scores.insert(key, Score{label, 0});
        }
        scores[key].score += std::max(1, track.playCount) + (track.liked ? 2 : 0)
            + std::max(0.0, 1.0 - static_cast<double>(index) / count);
    }
    QList<Score> ranked;
    for (const QString& key : order) {
        ranked.append(scores.value(key));
    }
    std::stable_sort(ranked.begin(), ranked.end(), [](const Score& a, const Score& b) {
        if (a.score != b.score) {
            return a.score > b.score;
        }
        return QString::localeAwareCompare(a.label, b.label) < 0;
    });
    QStringList labels;
    for (const Score& score : ranked) {
        labels.append(score.label);
    }
    return labels;
}

QStringList fillLabels(const QStringList& ranked, const QStringList& fallback, int count)
{
    QSet<QString> seen;
    QStringList labels;
    for (const QString& label : ranked + fallback) {
        if (labels.size() >= count) {
            break;
        }
        const QString key = canonical(label);
        if (key.isEmpty() || seen.contains(key)) {
            continue;
        }
        seen.insert(key);
        labels.append(label);
    }
    return labels;
}

QString mixCaption(const HomeMix& mix)
{
    return mix.genre.isEmpty() ? kMadeForYou : mix.genre;
}

HomeModel deriveHomeModel(const HomeLibrary& library)
{
    HomeModel model;
    const QList<Track> catalog = cloudCatalog(library);
    model.catalog = catalog;
    model.listenAgain = uniqueTracks(library.history).mid(0, 27);
    model.liked = uniqueTracks(library.liked).mid(0, 12);
    const QStringList topArtists = rankedValues(catalog, false);
    const QStringList topGenres = rankedValues(catalog, true);
    const auto artistAt = [&topArtists](int index) { return index < topArtists.size() ? topArtists.at(index) : QString(); };

    QHash<QString, QString> assignedGenres;
    qsizetype genreIndex = 0;
    const auto takeGenre = [&](const QString& id) -> QList<Track> {
        while (genreIndex < topGenres.size()) {
            const QString genre = topGenres.at(genreIndex++);
            QList<Track> tracks;
            std::copy_if(catalog.cbegin(), catalog.cend(), std::back_inserter(tracks),
                         [&genre](const Track& track) { return canonical(track.genre) == canonical(genre); });
            if (!tracks.isEmpty()) {
                assignedGenres.insert(id, genre);
                return tracks;
            }
        }
        return {};
    };

    // Without genres every Daily Mix would fall back to the same catalogue,
    // so each one's fallbacks skip songs an earlier mix already took.
    QSet<QString> usedByDaily;
    const auto unused = [&usedByDaily, &catalog]() {
        QList<Track> result;
        std::copy_if(catalog.cbegin(), catalog.cend(), std::back_inserter(result),
                     [&usedByDaily](const Track& track) { return !usedByDaily.contains(trackId(track)); });
        return result;
    };
    const auto takeDaily = [&usedByDaily](QList<Track> tracks) {
        for (const Track& track : tracks) {
            usedByDaily.insert(trackId(track));
        }
        return tracks;
    };

    const QList<Track> history = model.listenAgain;
    const QList<Track> daily1 = takeDaily(fillMix({takeGenre(QStringLiteral("daily1")), history,
                                                   tracksByArtist(catalog, artistAt(0)), catalog}));
    QList<Track> everyThird;
    {
        const QList<Track> pool = unused();
        for (qsizetype index = 0; index < pool.size(); ++index) {
            if (index % 3 == 1) {
                everyThird.append(pool.at(index));
            }
        }
    }
    const QList<Track> daily2Seed = takeGenre(QStringLiteral("daily2"));
    const QList<Track> daily2 =
        takeDaily(fillMix({daily2Seed, tracksByArtist(catalog, artistAt(1)), everyThird, unused(), catalog}));
    const QList<Track> daily3Seed = takeGenre(QStringLiteral("daily3"));
    QList<Track> daily3Fallback;
    {
        QHash<QString, int> artistBuckets;
        for (const Track& track : unused()) {
            const QString artist = canonical(track.artist);
            const int count = artistBuckets.value(artist);
            if (artist.isEmpty() || count >= 2) {
                continue;
            }
            artistBuckets.insert(artist, count + 1);
            daily3Fallback.append(track);
        }
    }
    const QList<Track> daily3 = takeDaily(fillMix({daily3Seed, daily3Fallback, unused(), catalog}));
    const QList<Track> daily4Seed = takeGenre(QStringLiteral("daily4"));
    const QList<Track> daily4 = takeDaily(fillMix({daily4Seed, tracksByArtist(catalog, artistAt(3)), unused(), catalog}));
    const QList<Track> daily5Seed = takeGenre(QStringLiteral("daily5"));
    const QList<Track> daily5 = takeDaily(fillMix({daily5Seed, tracksByArtist(catalog, artistAt(4)), unused(), catalog}));

    const QList<Track> onRepeat = sorted(catalog, [](const Track& a, const Track& b) {
        if (a.playCount != b.playCount) {
            return a.playCount > b.playCount;
        }
        return timeValue(a.lastPlayedAt) > timeValue(b.lastPlayedAt);
    });
    QSet<QString> familiarGenres;
    for (const QString& genre : topGenres) {
        familiarGenres.insert(canonical(genre));
    }
    const QList<Track> discover = sorted(catalog, [&familiarGenres](const Track& a, const Track& b) {
        const bool aFamiliar = familiarGenres.contains(canonical(a.genre));
        const bool bFamiliar = familiarGenres.contains(canonical(b.genre));
        if (aFamiliar != bFamiliar) {
            return !aFamiliar;
        }
        if (a.playCount != b.playCount) {
            return a.playCount < b.playCount;
        }
        return addedTime(a) > addedTime(b);
    });
    const QList<Track> timeCapsule = sorted(catalog, [](const Track& a, const Track& b) {
        const qint64 left = addedTime(a);
        const qint64 right = addedTime(b);
        if ((left == 0) != (right == 0)) {
            return left != 0;
        }
        return left < right;
    });
    const QList<Track> fresh =
        sorted(catalog, [](const Track& a, const Track& b) { return addedTime(a) > addedTime(b); });

    const QHash<QString, QList<Track>> mixTracks{
        {QStringLiteral("daily1"), daily1},
        {QStringLiteral("daily2"), daily2},
        {QStringLiteral("daily3"), daily3},
        {QStringLiteral("daily4"), daily4},
        {QStringLiteral("daily5"), daily5},
        {QStringLiteral("repeat"), fillMix({onRepeat, history, catalog})},
        {QStringLiteral("discover"), fillMix({discover})},
        {QStringLiteral("timecapsule"), fillMix({timeCapsule})},
        {QStringLiteral("fresh"), fillMix({fresh})},
    };
    for (const QString& id : HomeMixBuilder::ids()) {
        HomeMix mix = HomeMixBuilder::blank(id);
        mix.tracks = mixTracks.value(id);
        mix.genre = assignedGenres.value(id);
        const QStringList artists = rankedValues(mix.tracks, false);
        if (id.startsWith(QLatin1StringView("daily")) && !artists.isEmpty()) {
            mix.description = artists.mid(0, 2).join(QStringLiteral(", "))
                + (artists.size() > 2 ? QStringLiteral(" & more") : QString());
        }
        model.mixes.append(std::move(mix));
    }

    QList<Track> played;
    std::copy_if(catalog.cbegin(), catalog.cend(), std::back_inserter(played),
                 [](const Track& track) { return track.playCount > 0; });
    model.mostPlayed = sorted(played, [](const Track& a, const Track& b) {
                           if (a.playCount != b.playCount) {
                               return a.playCount > b.playCount;
                           }
                           return timeValue(a.lastPlayedAt) > timeValue(b.lastPlayedAt);
                       }).mid(0, 10);
    for (const Track& track : fresh) {
        if (addedTime(track) > 0 && model.recentlyAdded.size() < 27) {
            model.recentlyAdded.append(track);
        }
    }
    return model;
}

HomeLibrary homeLibraryFromSnapshot(const db::AccountLibrarySnapshot& snapshot)
{
    HomeLibrary library;
    QHash<QString, Track> byId;
    QList<Track> played;
    for (const db::AccountTrackSnapshot& entry : snapshot.tracks) {
        Track track = entry.track;
        track.playCount = static_cast<int>(std::clamp<qint64>(entry.playCount, 0, std::numeric_limits<int>::max()));
        track.firstPlayedAt = entry.firstPlayedAtUtc;
        track.lastPlayedAt = entry.lastPlayedAtUtc;
        byId.insert(trackId(track), track);
        if (track.playCount > 0 || !track.lastPlayedAt.isEmpty()) {
            played.append(track);
        }
        if (track.liked) {
            library.liked.append(track);
        }
    }
    std::stable_sort(played.begin(), played.end(), [](const Track& a, const Track& b) {
        return timeValue(a.lastPlayedAt) > timeValue(b.lastPlayedAt);
    });
    library.history = played;
    for (const db::AccountPlaylistSnapshot& playlist : snapshot.playlists) {
        for (const QString& id : playlist.remoteTrackIds) {
            const auto found = byId.constFind(id);
            if (found != byId.cend()) {
                library.playlistTracks.append(*found);
            }
        }
    }
    return library;
}

QList<StarterMixPlan> planStarterMixes(const QStringList& followed)
{
    QStringList trimmed;
    for (const QString& name : followed) {
        trimmed.append(name.trimmed());
    }
    const QStringList names = fillLabels(trimmed, {}, StarterMixCount * 3);
    QList<StarterMixPlan> plans;
    for (int index = 0; index < std::min<int>(StarterMixCount, static_cast<int>(names.size())); ++index) {
        StarterMixPlan plan;
        for (qsizetype at = 0; at < names.size(); ++at) {
            if (at % StarterMixCount == index) {
                plan.artists.append(names.at(at));
            }
        }
        plan.mix = HomeMixBuilder::blank(QStringLiteral("daily%1").arg(index + 1));
        plan.mix.description =
            plan.artists.mid(0, 2).join(QStringLiteral(", ")) + (plan.artists.size() > 2 ? QStringLiteral(" & more") : QString());
        plan.mix.genre = kFromFollowed;
        plans.append(std::move(plan));
    }
    return plans;
}

QList<Track> interleaveMixTracks(const QList<QList<Track>>& groups)
{
    qsizetype longest = 0;
    for (const QList<Track>& group : groups) {
        longest = std::max(longest, group.size());
    }
    QList<Track> turns;
    for (qsizetype index = 0; index < longest; ++index) {
        for (const QList<Track>& group : groups) {
            if (index < group.size()) {
                turns.append(group.at(index));
            }
        }
    }
    return uniqueTracks(turns).mid(0, OnlineMixLength);
}

QList<Track> mergeHomeMixTracks(const QList<Track>& local, const QList<Track>& remote, bool prependRemote)
{
    return prependRemote ? fillMix({remote, local}) : fillMix({local, remote});
}

QString providerQueryForMix(const HomeMix& mix, const QList<Track>& catalog)
{
    const QStringList artists = rankedValues(catalog, false);
    if (mix.id == QLatin1StringView("timecapsule") || mix.id == QLatin1StringView("fresh")) {
        return {};
    }
    if (mix.id == QLatin1StringView("discover")) {
        return artists.isEmpty() ? QStringLiteral("new music") : artists.first() + QStringLiteral(" new songs");
    }
    if (mix.id == QLatin1StringView("repeat")) {
        return artists.isEmpty() ? QStringLiteral("popular songs") : artists.first();
    }
    static const QHash<QString, QString> fallbacks{
        {QStringLiteral("daily1"), QStringLiteral("popular songs")},
        {QStringLiteral("daily2"), QStringLiteral("chill songs")},
        {QStringLiteral("daily3"), QStringLiteral("rock alternative songs")},
        {QStringLiteral("daily4"), QStringLiteral("top hits")},
        {QStringLiteral("daily5"), QStringLiteral("feel good songs")},
    };
    if (!mix.genre.isEmpty() && mix.genre != kMadeForYou) {
        return mix.genre + QStringLiteral(" songs");
    }
    const int dailyIndex = QStringView(mix.id).mid(5).toInt() - 1;
    if (dailyIndex >= 0 && dailyIndex < artists.size()) {
        return artists.at(dailyIndex);
    }
    return fallbacks.value(mix.id, QStringLiteral("popular songs"));
}

} // namespace lmp::core::home
