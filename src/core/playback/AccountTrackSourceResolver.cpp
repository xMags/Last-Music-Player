#include "core/playback/AccountTrackSourceResolver.h"

#include "core/account/AccountClient.h"
#include "core/account/AccountSessionService.h"
#include "core/account/AccountUrlPolicy.h"
#include "core/net/RemoteUrlPolicy.h"
#include "core/remote/MediaAddresses.h"
#include "core/settings/SettingKeys.h"
#include "core/settings/SettingsStore.h"
#include "platform/INetworkStatus.h"

#include <QDateTime>
#include <QFuture>
#include <QUrl>

#include <optional>
#include <utility>

namespace lmp::core {

namespace {

[[nodiscard]] QFuture<Result<QUrl>> ready(Result<QUrl> result)
{
    return QtFuture::makeReadyValueFuture(std::move(result));
}

} // namespace

AccountTrackSourceResolver::AccountTrackSourceResolver(Dependencies dependencies)
    : m_deps(std::move(dependencies))
{
    if (!m_deps.now) {
        m_deps.now = [] { return QDateTime::currentMSecsSinceEpoch(); };
    }
}

QFuture<Result<QUrl>> AccountTrackSourceResolver::resolve(const Track& track)
{
    const QString trusted = remote::trustedAccountStreamUrl(track.filePath, m_deps.mediaOrigin, m_deps.now());
    if (!trusted.isEmpty()) {
        return ready(QUrl(trusted, QUrl::StrictMode));
    }
    return resolveRemotely(track);
}

QFuture<Result<QUrl>> AccountTrackSourceResolver::refresh(const Track& track)
{
    return resolveRemotely(track);
}

QFuture<Result<QUrl>> AccountTrackSourceResolver::resolveRemotely(const Track& track)
{
    if (!net::isHttpUrl(track.sourceUrl)) {
        return ready(Failure::of(FailureKind::NotFound, QStringLiteral("song has no source address")));
    }
    const std::optional<account::AccountOperationContext> context = m_deps.session.captureOperation();
    if (!context) {
        return ready(Failure::of(FailureKind::NotAvailable, QStringLiteral("no account session")));
    }
    const bool lowQuality = m_deps.settings.boolean(settings::keys::DataSaver, false) && m_deps.network.metered();

    account::AccountSessionService* session = &m_deps.session;
    const QString mediaOrigin = m_deps.mediaOrigin;
    const std::function<qint64()> now = m_deps.now;
    return m_deps.client.resolve(context->bearerSession, track.sourceUrl, lowQuality)
        .then(session, [session, context = *context, mediaOrigin, now](const Result<QByteArray>& answer) -> Result<QUrl> {
            if (!session->isCurrent(context)) {
                return Failure::of(FailureKind::Cancelled, QStringLiteral("account session changed"));
            }
            if (!answer) {
                if (answer.failure().kind == FailureKind::Unauthorized) {
                    session->handleUnauthorized(context);
                }
                return answer.failure();
            }
            const QString stream = remote::parseResolvedStream(answer.value()).streamUrl;
            if (!account::isTrustedAccountMediaUrl(stream, mediaOrigin, account::AccountMediaScope::Stream, now())) {
                return Failure::of(FailureKind::NotFound, QStringLiteral("no playable stream in the answer"));
            }
            return QUrl(stream, QUrl::StrictMode);
        });
}

} // namespace lmp::core
