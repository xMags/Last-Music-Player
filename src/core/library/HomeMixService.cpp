#include "core/library/HomeMixService.h"
#include "core/model/CatalogSourceKey.h"
#include <algorithm>
#include <QSet>

#include <utility>

#include "core/db/GroupQueries.h"
#include "core/db/LibraryQueries.h"
#include "core/playback/LastTrackIdentity.h"

namespace lmp::core {

HomeMixService::HomeMixService(db::LibraryQueries& library, db::GroupQueries& groups, Clock today, QObject* parent)
    : QObject(parent)
    , m_library(library)
    , m_groups(groups)
    , m_today(std::move(today))
{
}

HomeMixInputs HomeMixService::readInputs()
{
    HomeMixInputs inputs;
    db::TrackQuery seed;
    seed.sort = QStringLiteral("DateAdded");
    seed.limit = HomeMixBuilder::CatalogSeedSize;
    inputs.catalog = m_library.loadTrackPage(seed).tracks;
    inputs.recentHistory = m_library.loadHistoryTracks();
    for (const db::PlaybackStat& stat : m_library.loadPlaybackStats()) {
        const QString key = homeHistoryKey(stat.track);
        if (key.isEmpty()) {
            continue;
        }
        inputs.playCounts.insert(key, stat.playCount);
        inputs.lastPlayedOrder.insert(key, stat.lastPlayedOrder);
    }
    inputs.rankedGenres = m_groups.loadTopGenres(HomeMixBuilder::GenreCount);
    for (const QString& genre : std::as_const(inputs.rankedGenres)) {
        inputs.genrePools.insert(genre, m_library.loadTracksForGroup(QStringLiteral("genre"), genre));
    }
    return inputs;
}

void HomeMixService::setAccountSource(AccountSource source)
{
    m_accountSource = std::move(source);
}

void HomeMixService::refresh(bool force)
{
    if (const std::optional<home::HomeLibrary> account = m_accountSource ? m_accountSource() : std::nullopt) {
        const QString key = QStringLiteral("account:") + home::homeMixKey(*account, m_today());
        if (!force && key == m_key) {
            return;
        }
        // The mixes are a function of the account's songs and the day: the
        // same songs on the same day make the same mixes. One change reaches
        // several refreshes (Home's and the library views'), and only the
        // first has anything to derive.
        if (key == m_key && m_derivedFrom == account) {
            return;
        }
        m_key = key;
        m_derivedFrom = account;
        m_mixes = home::deriveHomeModel(*account).mixes;
        emit mixesChanged();
        return;
    }
    m_derivedFrom.reset();
    // "Music" is any song in the library, as the current app's mixes come
    // from the whole catalogue (not only history and likes).
    db::TrackQuery probe;
    probe.limit = 1;
    const bool hasMusic = m_library.loadTrackPage(probe).totalCount > 0;
    const QString key = QStringLiteral("local:") + HomeMixBuilder::refreshKey(m_today(), hasMusic);
    if (!force && key == m_key) {
        return;
    }
    m_key = key;
    const HomeMixInputs inputs = readInputs();
    m_mixes = HomeMixBuilder::build(inputs);
    ++m_fillGeneration;
    emit mixesChanged();
    if (m_remoteSearch && m_remoteActive && m_remoteActive()) {
        // The listener's most played artists, best first.
        QHash<QString, int> counts;
        QStringList order;
        for (const Track& track : inputs.recentHistory) {
            const QString artist = track.artist.trimmed();
            if (artist.isEmpty()) {
                continue;
            }
            if (!counts.contains(artist)) {
                order.append(artist);
            }
            ++counts[artist];
        }
        std::stable_sort(order.begin(), order.end(),
                         [&counts](const QString& a, const QString& b) { return counts.value(a) > counts.value(b); });
        fillFromRemote(order);
    }
}

void HomeMixService::setRemoteFill(RemoteSearch search, std::function<bool()> active)
{
    m_remoteSearch = std::move(search);
    m_remoteActive = std::move(active);
}

QList<HomeMixService::FillRequest> HomeMixService::fillRequests(const QList<HomeMix>& mixes,
                                                                const QStringList& topArtists)
{
    const auto artistOr = [&topArtists](qsizetype index, const char* fallback) {
        return index < topArtists.size() && !topArtists.at(index).isEmpty() ? topArtists.at(index)
                                                                            : QString::fromLatin1(fallback);
    };
    const auto genreOr = [&mixes](const QString& id, const QString& fallback) {
        for (const HomeMix& mix : mixes) {
            if (mix.id == id && !mix.genre.trimmed().isEmpty()) {
                return mix.genre.trimmed() + QStringLiteral(" songs");
            }
        }
        return fallback;
    };
    return {
        {QStringLiteral("daily1"), genreOr(QStringLiteral("daily1"), artistOr(0, "popular songs")), false},
        {QStringLiteral("daily2"), genreOr(QStringLiteral("daily2"), artistOr(1, "chill songs")), false},
        {QStringLiteral("daily3"), genreOr(QStringLiteral("daily3"), artistOr(2, "rock alternative songs")), false},
        {QStringLiteral("daily4"), genreOr(QStringLiteral("daily4"), artistOr(3, "top hits")), false},
        {QStringLiteral("daily5"), genreOr(QStringLiteral("daily5"), artistOr(4, "feel good songs")), false},
        {QStringLiteral("repeat"), artistOr(0, "popular songs"), false},
        {QStringLiteral("discover"),
         topArtists.isEmpty() ? QStringLiteral("new music") : topArtists.first() + QStringLiteral(" new songs"), true},
    };
}

QList<Track> HomeMixService::joinFill(const QList<Track>& mix, const QList<Track>& found, bool prepend)
{
    QList<Track> joined;
    QSet<QString> keys;
    const auto append = [&joined, &keys](const Track& track) {
        if (joined.size() >= FilledMixSize || track.unavailable
            || (track.filePath.isEmpty() && track.sourceUrl.isEmpty())) {
            return;
        }
        const QString key = catalogSourceKey(track);
        if (key.isEmpty() || keys.contains(key)) {
            return;
        }
        keys.insert(key);
        joined.append(track);
    };
    const QList<Track>& first = prepend ? found : mix;
    const QList<Track>& second = prepend ? mix : found;
    for (const Track& track : first) {
        append(track);
    }
    for (const Track& track : second) {
        append(track);
    }
    return joined;
}

void HomeMixService::fillFromRemote(const QStringList& topArtists)
{
    const quint64 generation = m_fillGeneration;
    for (const FillRequest& request : fillRequests(m_mixes, topArtists)) {
        m_remoteSearch(request.query).then(this, [this, generation, request](const Result<QList<Track>>& found) {
            if (generation != m_fillGeneration || !found || found.value().isEmpty()
                || !(m_remoteActive && m_remoteActive())) {
                return;
            }
            for (HomeMix& mix : m_mixes) {
                if (mix.id == request.id) {
                    // At most eight of the provider's songs per mix, as before.
                    mix.tracks = joinFill(mix.tracks, found.value().mid(0, 8), request.prepend);
                    emit mixesChanged();
                    return;
                }
            }
        });
    }
}

std::optional<HomeMix> HomeMixService::mix(const QString& id) const
{
    for (const HomeMix& mix : m_mixes) {
        if (mix.id == id) {
            return mix;
        }
    }
    return std::nullopt;
}

} // namespace lmp::core
