#include "core/library/HomeMixBuilder.h"

#include <QSet>

#include <algorithm>
#include <utility>

#include "core/playback/LastTrackIdentity.h"

namespace lmp::core {

namespace {

struct MixMeta {
    const char* id;
    const char* title;
    const char* badge;
    quint32 start;
    quint32 end;
    const char* theme;
};

// home-model.ts MIX_META.
constexpr MixMeta kMixes[] = {
    {"daily1", "Daily Mix 1", "DAILY MIX 1", 0xFF0097B2, 0xFF00B8D4, "Upbeat indie and pop you keep coming back to."},
    {"daily2", "Daily Mix 2", "DAILY MIX 2", 0xFF6B5BFE, 0xFF9B7BFE, "Mellow electronic and chill beats."},
    {"daily3", "Daily Mix 3", "DAILY MIX 3", 0xFFE45757, 0xFFF29A67, "Rock and alternative favourites."},
    {"daily4", "Daily Mix 4", "DAILY MIX 4", 0xFF2E6BE6, 0xFF4FB0FF, "More from a genre you love."},
    {"daily5", "Daily Mix 5", "DAILY MIX 5", 0xFF6A3DE0, 0xFF9B6AF0, "More from a genre you love."},
    {"repeat", "On Repeat", "ON REPEAT", 0xFF1B9A69, 0xFF35C895, "The tracks you've had on loop lately."},
    {"discover", "Discover Weekly", "DISCOVER WEEKLY", 0xFFA542DB, 0xFFE45CB7,
     "Fresh picks from your library, refreshed weekly."},
    {"timecapsule", "Time Capsule", "TIME CAPSULE", 0xFF9B6B42, 0xFFD0A35D, "A trip back through your early library."},
    {"fresh", "Fresh Finds", "FRESH FINDS", 0xFF22A974, 0xFF68C858, "The latest additions to your library."},
};

// IsPlayableHomeTrack: a file on this PC, or a remote song with its address.
bool playable(const Track& track)
{
    return !track.filePath.isEmpty() || (track.isRemote() && !track.sourceUrl.isEmpty());
}

// A mix being filled: twelve playable songs at most, none twice.
class MixFill {
public:
    bool add(const Track& track)
    {
        if (m_tracks.size() >= HomeMixBuilder::MixLength || !playable(track)) {
            return false;
        }
        const QString key = homeHistoryKey(track);
        if (key.isEmpty() || m_keys.contains(key)) {
            return false;
        }
        m_keys.insert(key);
        m_tracks.append(track);
        return true;
    }

    void addAll(const QList<Track>& tracks)
    {
        for (const Track& track : tracks) {
            if (full()) {
                return;
            }
            add(track);
        }
    }

    [[nodiscard]] bool full() const { return m_tracks.size() >= HomeMixBuilder::MixLength; }
    [[nodiscard]] QList<Track> take() { return std::exchange(m_tracks, {}); }

private:
    QList<Track> m_tracks;
    QSet<QString> m_keys;
};

quint32 playsOf(const Track& track, const QHash<QString, quint32>& playCounts)
{
    return playCounts.value(homeHistoryKey(track), 0);
}

} // namespace

QStringList HomeMixBuilder::ids()
{
    QStringList list;
    for (const MixMeta& meta : kMixes) {
        list.append(QString::fromLatin1(meta.id));
    }
    return list;
}

HomeMix HomeMixBuilder::blank(const QString& id)
{
    for (const MixMeta& meta : kMixes) {
        if (id == QLatin1StringView(meta.id)) {
            return {.id = id,
                    .title = QString::fromLatin1(meta.title),
                    .badge = QString::fromLatin1(meta.badge),
                    .colorStart = meta.start,
                    .colorEnd = meta.end,
                    .description = QString::fromLatin1(meta.theme),
                    .genre = {},
                    .tracks = {}};
        }
    }
    return {};
}

QString HomeMixBuilder::refreshKey(const QDate& today, bool hasMusic)
{
    return today.toString(Qt::ISODate) + (hasMusic ? QStringLiteral(":music") : QStringLiteral(":empty"));
}

QStringList HomeMixBuilder::rankedArtists(const QList<Track>& tracks, const QHash<QString, quint32>& playCounts)
{
    QHash<QString, quint32> scores;
    QHash<QString, QString> displayNames;
    for (const Track& track : tracks) {
        const QString artist = canonicalQueueText(track.artist);
        if (artist.isEmpty() || artist == QLatin1StringView("unknown artist")) {
            continue;
        }
        scores[artist] += 1 + playsOf(track, playCounts);
        if (!displayNames.contains(artist)) {
            displayNames.insert(artist, track.artist);
        }
    }
    QList<std::pair<QString, quint32>> ranked;
    ranked.reserve(scores.size());
    for (auto it = scores.cbegin(); it != scores.cend(); ++it) {
        ranked.append({it.key(), it.value()});
    }
    std::sort(ranked.begin(), ranked.end(), [](const auto& left, const auto& right) {
        return left.second != right.second ? left.second > right.second : left.first < right.first;
    });
    QStringList names;
    names.reserve(ranked.size());
    for (const auto& entry : ranked) {
        names.append(displayNames.value(entry.first, entry.first));
    }
    return names;
}

QString HomeMixBuilder::artistLine(const QList<Track>& tracks, const QHash<QString, quint32>& playCounts)
{
    const QStringList artists = rankedArtists(tracks, playCounts);
    QString line = artists.mid(0, 2).join(QStringLiteral(", "));
    if (artists.size() > 2 && !line.isEmpty()) {
        line += QStringLiteral(" & more");
    }
    return line;
}

QList<HomeMix> HomeMixBuilder::build(const HomeMixInputs& inputs)
{
    const QList<Track>& catalog = inputs.catalog;
    const QStringList topArtists = rankedArtists(catalog, inputs.playCounts);

    auto addByArtist = [&catalog](MixFill& mix, const QString& artist) {
        const QString wanted = canonicalQueueText(artist);
        if (wanted.isEmpty()) {
            return;
        }
        for (const Track& track : catalog) {
            if (mix.full()) {
                return;
            }
            if (canonicalQueueText(track.artist) == wanted) {
                mix.add(track);
            }
        }
    };
    // Each slot tops up from its own fifth of the catalogue, wrapping.
    auto topUp = [&catalog](MixFill& mix, int slot) {
        const qsizetype total = catalog.size();
        if (total == 0) {
            return;
        }
        const qsizetype start = (total * slot) / DailyMixCount;
        for (qsizetype step = 0; step < total && !mix.full(); ++step) {
            mix.add(catalog.at((start + step) % total));
        }
    };
    // Each ranked genre goes to one daily slot at most; empty pools are skipped.
    qsizetype genreCursor = 0;
    auto nextGenre = [&inputs, &genreCursor]() -> QString {
        while (genreCursor < inputs.rankedGenres.size()) {
            const QString& genre = inputs.rankedGenres.at(genreCursor++);
            if (!inputs.genrePools.value(genre).isEmpty()) {
                return genre;
            }
        }
        return {};
    };

    QList<HomeMix> mixes;
    for (int slot = 0; slot < DailyMixCount; ++slot) {
        HomeMix mix = blank(QStringLiteral("daily%1").arg(slot + 1));
        MixFill fill;
        const QString genre = nextGenre();
        if (!genre.isEmpty()) {
            fill.addAll(inputs.genrePools.value(genre));
            mix.genre = genre;
        } else if (slot == 0) {
            fill.addAll(inputs.recentHistory);
            if (!topArtists.isEmpty()) {
                addByArtist(fill, topArtists.at(0));
            }
        } else if (slot == 1) {
            if (topArtists.size() > 1) {
                addByArtist(fill, topArtists.at(1));
            }
            for (qsizetype index = 1; index < catalog.size(); index += 3) {
                fill.add(catalog.at(index));
            }
        } else if (slot == 2) {
            // Led by the third artist, which no other slot claims, then at
            // most two songs per artist.
            if (topArtists.size() > 2) {
                addByArtist(fill, topArtists.at(2));
            }
            QHash<QString, int> perArtist;
            for (const Track& track : catalog) {
                int& count = perArtist[canonicalQueueText(track.artist)];
                if (count < 2 && fill.add(track)) {
                    ++count;
                }
            }
        } else if (topArtists.size() > slot) {
            addByArtist(fill, topArtists.at(slot));
        }
        topUp(fill, slot);
        mix.tracks = fill.take();
        // The description is the mix's leading artists: the genre's when it
        // has one, else the mix's own; the theme line only for an empty mix.
        QString line = mix.genre.isEmpty() ? QString()
                                           : artistLine(inputs.genrePools.value(mix.genre), inputs.playCounts);
        if (line.isEmpty()) {
            line = artistLine(mix.tracks, inputs.playCounts);
        }
        if (!line.isEmpty()) {
            mix.description = line;
        }
        mixes.append(std::move(mix));
    }

    auto ordered = [&catalog](auto lessThan) {
        QList<Track> tracks = catalog;
        std::stable_sort(tracks.begin(), tracks.end(), lessThan);
        return tracks;
    };

    {
        HomeMix mix = blank(QStringLiteral("repeat"));
        MixFill fill;
        fill.addAll(ordered([&inputs](const Track& left, const Track& right) {
            const quint32 leftPlays = playsOf(left, inputs.playCounts);
            const quint32 rightPlays = playsOf(right, inputs.playCounts);
            if (leftPlays != rightPlays) {
                return leftPlays > rightPlays;
            }
            return inputs.lastPlayedOrder.value(homeHistoryKey(left), 0)
                > inputs.lastPlayedOrder.value(homeHistoryKey(right), 0);
        }));
        fill.addAll(inputs.recentHistory);
        fill.addAll(catalog);
        mix.tracks = fill.take();
        mixes.append(std::move(mix));
    }
    {
        QSet<QString> topGenres;
        for (const QString& genre : inputs.rankedGenres) {
            topGenres.insert(genre.toLower());
        }
        auto familiar = [&topGenres](const Track& track) {
            const QString genre = track.genre.toLower();
            return !genre.isEmpty() && topGenres.contains(genre);
        };
        HomeMix mix = blank(QStringLiteral("discover"));
        MixFill fill;
        fill.addAll(ordered([&](const Track& left, const Track& right) {
            const bool leftFamiliar = familiar(left);
            const bool rightFamiliar = familiar(right);
            if (leftFamiliar != rightFamiliar) {
                return !leftFamiliar;
            }
            const quint32 leftPlays = playsOf(left, inputs.playCounts);
            const quint32 rightPlays = playsOf(right, inputs.playCounts);
            if (leftPlays != rightPlays) {
                return leftPlays < rightPlays;
            }
            return left.dateAddedSortKey > right.dateAddedSortKey;
        }));
        mix.tracks = fill.take();
        mixes.append(std::move(mix));
    }
    {
        HomeMix mix = blank(QStringLiteral("timecapsule"));
        MixFill fill;
        fill.addAll(ordered([](const Track& left, const Track& right) {
            const bool leftUndated = left.dateAddedSortKey <= 0.0;
            const bool rightUndated = right.dateAddedSortKey <= 0.0;
            if (leftUndated != rightUndated) {
                return !leftUndated;
            }
            return left.dateAddedSortKey < right.dateAddedSortKey;
        }));
        mix.tracks = fill.take();
        mixes.append(std::move(mix));
    }
    {
        HomeMix mix = blank(QStringLiteral("fresh"));
        MixFill fill;
        fill.addAll(catalog);
        mix.tracks = fill.take();
        mixes.append(std::move(mix));
    }
    return mixes;
}

} // namespace lmp::core
