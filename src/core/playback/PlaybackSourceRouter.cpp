#include "core/playback/PlaybackSourceRouter.h"

#include "core/mode/ModeService.h"
#include "core/net/RemoteUrlPolicy.h"

#include <QFuture>

namespace lmp::core {

PlaybackSourceRouter::PlaybackSourceRouter(TrackSourceResolver& local, TrackSourceResolver& account,
                                           const mode::ModeService& mode)
    : m_local(local)
    , m_account(account)
    , m_mode(mode)
{
}

bool PlaybackSourceRouter::isRemote(const Track& track)
{
    return track.isRemote() || net::isHttpUrl(track.filePath) || net::isHttpUrl(track.sourceUrl);
}

TrackSourceResolver* PlaybackSourceRouter::remoteResolver(Result<QUrl>& refusal) const
{
    if (m_mode.accountSelected()) {
        return &m_account;
    }
    if (!m_mode.apiKeySelected()) {
        refusal = Failure::of(FailureKind::NotAvailable, QStringLiteral("remote songs need a music source"));
        return nullptr;
    }
    refusal = Failure::of(FailureKind::NotFound, QStringLiteral("no resolver for this source"));
    return nullptr;
}

QFuture<Result<QUrl>> PlaybackSourceRouter::resolve(const Track& track)
{
    if (!isRemote(track)) {
        return m_local.resolve(track);
    }
    Result<QUrl> refusal = Failure::of(FailureKind::Unknown);
    if (TrackSourceResolver* resolver = remoteResolver(refusal)) {
        return resolver->resolve(track);
    }
    return QtFuture::makeReadyValueFuture(refusal);
}

QFuture<Result<QUrl>> PlaybackSourceRouter::refresh(const Track& track)
{
    if (!isRemote(track)) {
        return m_local.refresh(track);
    }
    Result<QUrl> refusal = Failure::of(FailureKind::Unknown);
    if (TrackSourceResolver* resolver = remoteResolver(refusal)) {
        return resolver->refresh(track);
    }
    return QtFuture::makeReadyValueFuture(refusal);
}

} // namespace lmp::core
