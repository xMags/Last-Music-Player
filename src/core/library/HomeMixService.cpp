#include "core/library/HomeMixService.h"

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
        m_key = key;
        m_mixes = home::deriveHomeModel(*account).mixes;
        emit mixesChanged();
        return;
    }
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
    m_mixes = HomeMixBuilder::build(readInputs());
    emit mixesChanged();
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
