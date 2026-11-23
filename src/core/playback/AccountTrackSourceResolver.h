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

namespace account {
class AccountClient;
class AccountSessionService;
} // namespace account

// Remote songs in Account mode. A stream address the song already carries is
// played as it is when it belongs to the media origin and its token is still
// fresh; otherwise the service resolves the song's source address. With the
// DataSaver setting on and a metered connection the low quality is asked
// for. Only a resolved stream address of the media origin with a fresh
// stream token is accepted.
//
// Failures: NotFound when the song has no source address or the answer holds
// no acceptable stream; NotAvailable without a session; Cancelled when the
// session changed while the call ran; the call's own failure otherwise. A 401
// is reported to the session, which signs out if that session is still the
// current one.
//
// Threading: GUI thread; continuations run there with the session as their
// context, so they never outlive it.
class AccountTrackSourceResolver final : public TrackSourceResolver {
public:
    struct Dependencies {
        account::AccountClient& client;
        account::AccountSessionService& session;
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
