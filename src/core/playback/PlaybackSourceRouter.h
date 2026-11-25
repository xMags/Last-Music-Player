#pragma once

#include "core/playback/TrackSourceResolver.h"

#include <QString>

#include <functional>

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

    // A finished download of a remote song plays before anything is asked
    // of the network, in every mode (the earlier versions' order).
    void setDownloadLookup(std::function<QString(const Track&)> lookup) { m_downloaded = std::move(lookup); }

    // Whether the router treats `track` as remote.
    [[nodiscard]] static bool isRemote(const Track& track);
    // A song with its own file on this PC (a downloaded song too, which keeps
    // the address it came from, and its account identity): played from the
    // file while it is there.
    [[nodiscard]] static bool playsFromDisk(const Track& track);

    [[nodiscard]] QFuture<Result<QUrl>> resolve(const Track& track) override;
    [[nodiscard]] QFuture<Result<QUrl>> refresh(const Track& track) override;

private:
    [[nodiscard]] TrackSourceResolver* remoteResolver(Result<QUrl>& refusal) const;

    TrackSourceResolver& m_local;
    TrackSourceResolver& m_account;
    TrackSourceResolver& m_provider;
    const mode::ModeService& m_mode;
    std::function<QString(const Track&)> m_downloaded;
};

} // namespace lmp::core
