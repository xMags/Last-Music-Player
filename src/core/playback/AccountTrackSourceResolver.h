#pragma once

#include "core/playback/TrackSourceResolver.h"

#include <QString>
#include <QtGlobal>

#include <functional>

namespace lmp::platform {
class INetworkStatus;
}

namespace lmp::core {

class SettingsStore;

namespace remote {
class RemoteMusicService;
} // namespace remote

// Remote songs in Account mode. A stream address the song already carries is
// played as it is when it belongs to the media origin and its token is still
// fresh; otherwise the service resolves the song's source address. With the
// DataSaver setting on and a metered connection the low quality is asked
// for. Only a resolved stream address of the media origin with a fresh
// stream token is accepted.
//
// The call goes through the remote facade (remote::RemoteMusicService),
// which refuses it outside a usable Account or API key mode, turns an answer
// for an older mode, owner or session into Cancelled, and signs a session
// out on a 401.
//
// Failures: NotFound when the song has no source address or the answer holds
// no acceptable stream; NotAvailable without a session; Cancelled when the
// source changed while the call ran; the call's own failure otherwise.
//
// Threading: GUI thread; continuations capture no object of their own.
class AccountTrackSourceResolver final : public TrackSourceResolver {
public:
    struct Dependencies {
        remote::RemoteMusicService& remote;
        SettingsStore& settings;
        platform::INetworkStatus& network;
        // The configured media origin (PrivateConfig::accountMediaOrigin).
        QString mediaOrigin;
        // Milliseconds since the epoch, for the token freshness check.
        std::function<qint64()> now;
    };

    // Every dependency must outlive this object.
    explicit AccountTrackSourceResolver(Dependencies dependencies);

    [[nodiscard]] QFuture<Result<QUrl>> resolve(const Track& track) override;
    [[nodiscard]] QFuture<Result<QUrl>> refresh(const Track& track) override;

private:
    [[nodiscard]] QFuture<Result<QUrl>> resolveRemotely(const Track& track);

    Dependencies m_deps;
};

} // namespace lmp::core
