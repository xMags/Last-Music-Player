#include "core/playback/PlaybackSourceRouter.h"

#include "core/mode/ModeService.h"
#include "core/net/RemoteUrlPolicy.h"

#include <QFileInfo>
#include <QFuture>

namespace lmp::core {

PlaybackSourceRouter::PlaybackSourceRouter(TrackSourceResolver& local, TrackSourceResolver& account,
                                           TrackSourceResolver& provider, const mode::ModeService& mode)
    : m_local(local)
    , m_account(account)
    , m_provider(provider)
    , m_mode(mode)
{
}

bool PlaybackSourceRouter::isRemote(const Track& track)
{
    return track.isRemote() || net::isHttpUrl(track.filePath) || net::isHttpUrl(track.sourceUrl);
}

bool PlaybackSourceRouter::playsFromDisk(const Track& track)
{
    // A song with its own file here: a file of this PC, or a downloaded
    // song (which keeps its address, and stays an account song when it came
    // from the account). The file wins while it is there.
    return !track.filePath.isEmpty() && !net::isHttpUrl(track.filePath) && QFileInfo(track.filePath).isFile();
}

bool PlaybackSourceRouter::canPlay(const Track& track) const
{
    if (!isRemote(track) || playsFromDisk(track)) {
        return true;
    }
    if (m_downloaded && !m_downloaded(track).isEmpty()) {
        return true;
    }
    return m_mode.accountSelected() || m_mode.apiKeySelected();
}

TrackSourceResolver* PlaybackSourceRouter::remoteResolver(Result<QUrl>& refusal) const
{
    if (m_mode.accountSelected()) {
        return &m_account;
    }
    if (m_mode.apiKeySelected()) {
        return &m_provider;
    }
    refusal = Failure::of(FailureKind::NotAvailable, QStringLiteral("remote songs need a music source"));
    return nullptr;
}

QFuture<Result<QUrl>> PlaybackSourceRouter::resolve(const Track& track)
{
    if (!isRemote(track) || playsFromDisk(track)) {
        return m_local.resolve(track);
    }
    if (m_downloaded) {
        if (const QString file = m_downloaded(track); !file.isEmpty()) {
            return QtFuture::makeReadyValueFuture(Result<QUrl>(QUrl::fromLocalFile(file)));
        }
    }
    Result<QUrl> refusal = Failure::of(FailureKind::Unknown);
    if (TrackSourceResolver* resolver = remoteResolver(refusal)) {
        return resolver->resolve(track);
    }
    return QtFuture::makeReadyValueFuture(refusal);
}

QFuture<Result<QUrl>> PlaybackSourceRouter::refresh(const Track& track)
{
    if (!isRemote(track) || playsFromDisk(track)) {
        return m_local.refresh(track);
    }
    Result<QUrl> refusal = Failure::of(FailureKind::Unknown);
    if (TrackSourceResolver* resolver = remoteResolver(refusal)) {
        return resolver->refresh(track);
    }
    return QtFuture::makeReadyValueFuture(refusal);
}

} // namespace lmp::core
