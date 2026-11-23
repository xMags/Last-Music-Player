#pragma once

#include "core/playback/TrackSourceResolver.h"

#include <QObject>

namespace lmp::core {

class PlayerController;
class StreamCache;
namespace db {
class TrackRepository;
} // namespace db
namespace mode {
class ModeService;
} // namespace mode
namespace remote {
class RemoteMusicService;
} // namespace remote

// Remote songs in API key mode, as the earlier versions played them. A song
// fetched ahead into the stream cache plays from that file. Otherwise the
// provider resolves the song's link and the stream address is built from
// its answer (provider stream address, moved to the current provider, with
// the key); when the provider gives none, the address is built from the
// song's link itself, so a song the provider cannot resolve ahead still
// streams. The song is then fetched into the cache for next time and kept
// in the library's songs, as before (the stored row holds its durable link,
// never the stream address or the key).
//
// Failures: NotFound when no stream address can be built; NotAvailable
// without a provider; Cancelled when the source changed meanwhile.
//
// GUI thread only.
class ProviderTrackSourceResolver final : public TrackSourceResolver {
public:
    struct Dependencies {
        remote::RemoteMusicService& remote;
        mode::ModeService& mode;
        StreamCache& cache;
        db::TrackRepository& tracks;
    };

    // Every dependency must outlive this object.
    explicit ProviderTrackSourceResolver(Dependencies dependencies);

    [[nodiscard]] QFuture<Result<QUrl>> resolve(const Track& track) override;
    [[nodiscard]] QFuture<Result<QUrl>> refresh(const Track& track) override;

private:
    [[nodiscard]] QFuture<Result<QUrl>> resolveRemotely(const Track& track);

    Dependencies m_deps;
};

// Fetches the next three songs of the queue into the stream cache while the
// mode allows it (Capabilities::prefetchUpcomingSongs: API key mode,
// online), each time the song or the queue changes. Songs on this PC are
// skipped.
//
// GUI thread only.
class ProviderStreamPrefetcher final : public QObject {
    Q_OBJECT

public:
    static constexpr qsizetype Lookahead = 3;

    struct Dependencies {
        PlayerController& player;
        remote::RemoteMusicService& remote;
        mode::ModeService& mode;
        StreamCache& cache;
    };

    // Every dependency must outlive this object.
    explicit ProviderStreamPrefetcher(Dependencies dependencies, QObject* parent = nullptr);

    void prefetchUpcoming();

private:
    Dependencies m_deps;
};

} // namespace lmp::core
