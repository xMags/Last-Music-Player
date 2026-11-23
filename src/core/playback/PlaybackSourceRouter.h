#pragma once

#include "core/playback/TrackSourceResolver.h"

namespace lmp::core {

namespace mode {
class ModeService;
}

// Sends each song to the resolver for where it lives. A song from this PC
// goes to the local resolver. A remote song (kind "remote", or an http(s)
// path or source address) goes to the account resolver in Account mode and
// to the provider resolver in API key mode; in LocalOnly mode it cannot play
// (NotAvailable).
//
// GUI thread only.
class PlaybackSourceRouter final : public TrackSourceResolver {
public:
    // Every dependency must outlive this object.
    PlaybackSourceRouter(TrackSourceResolver& local, TrackSourceResolver& account, TrackSourceResolver& provider,
                         const mode::ModeService& mode);

    // Whether the router treats `track` as remote.
    [[nodiscard]] static bool isRemote(const Track& track);

    [[nodiscard]] QFuture<Result<QUrl>> resolve(const Track& track) override;
    [[nodiscard]] QFuture<Result<QUrl>> refresh(const Track& track) override;

private:
    [[nodiscard]] TrackSourceResolver* remoteResolver(Result<QUrl>& refusal) const;

    TrackSourceResolver& m_local;
    TrackSourceResolver& m_account;
    TrackSourceResolver& m_provider;
    const mode::ModeService& m_mode;
};

} // namespace lmp::core
