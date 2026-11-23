#include "core/library/ArtistFollowService.h"

#include "core/account/AccountClient.h"
#include "core/account/AccountSessionService.h"
#include "core/account/LibraryArtistContract.h"
#include "core/db/AccountRecords.h"
#include "core/db/FollowedArtistRepository.h"
#include "core/library/UserDataOperationGate.h"
#include "core/mode/ModeService.h"
#include "core/net/RemoteUrlPolicy.h"

#include <QDateTime>

#include <optional>

namespace lmp::core {

namespace {

[[nodiscard]] QFuture<Result<bool>> ready(Result<bool> result)
{
    return QtFuture::makeReadyValueFuture(std::move(result));
}

} // namespace

ArtistFollowService::ArtistFollowService(const Dependencies& dependencies, QObject* parent)
    : QObject(parent)
    , m_deps(dependencies)
{
    // What the service said belongs to the account it said it for.
    connect(&m_deps.session, &account::AccountSessionService::ownerChanged, this, [this]() {
        m_accountFollowed.clear();
        m_pending.clear();
    });
}

bool ArtistFollowService::accountMode() const
{
    return m_deps.mode.accountSelected();
}

bool ArtistFollowService::canFollow(const FollowableArtist& artist) const
{
    if (m_deps.mode.accountSelected()) {
        return account::isValidLibraryArtistPublicId(artist.publicId) && m_deps.session.captureOperation().has_value();
    }
    // A provider keeps no follows; Local mode keeps them on this PC.
    return !m_deps.mode.apiKeySelected() && !artistIdentityKey(artist.identity).isEmpty();
}

bool ArtistFollowService::isFollowed(const FollowableArtist& artist) const
{
    if (accountMode()) {
        return m_accountFollowed.value(artist.publicId, false);
    }
    const QString key = artistIdentityKey(artist.identity);
    return !key.isEmpty() && m_deps.followed.isArtistFollowed(QString(LocalScope), key);
}

void ArtistFollowService::rememberAccountArtist(const QString& publicId, bool followed)
{
    if (!publicId.isEmpty() && !m_pending.contains(publicId)) {
        m_accountFollowed.insert(publicId, followed);
    }
}

QFuture<Result<bool>> ArtistFollowService::setFollowed(const FollowableArtist& artist, bool followed)
{
    if (!canFollow(artist)) {
        return ready(Failure::of(FailureKind::NotAvailable, QStringLiteral("following is not possible here")));
    }

    if (!accountMode()) {
        const std::optional<UserDataOperationGate::Lease> lease = m_deps.gate.tryEnter();
        if (!lease) {
            return ready(Failure::of(FailureKind::Cancelled, QStringLiteral("user data is being cleaned up")));
        }
        const QString key = artistIdentityKey(artist.identity);
        Result<void> stored = Result<void>::success();
        if (followed) {
            db::FollowedArtist entry;
            entry.scopeKey = QString(LocalScope);
            entry.identityKey = key;
            entry.catalogId = artist.identity.catalogId;
            entry.sourceUrl = net::isSafeRemoteUrl(artist.identity.sourceUrl, net::RemoteUrlUse::Durable)
                ? artist.identity.sourceUrl
                : QString();
            entry.name = artist.identity.name;
            entry.provider = artist.identity.provider;
            entry.addedAtUtc = QDateTime::currentDateTimeUtc().toString(Qt::ISODate);
            stored = m_deps.followed.followArtist(entry);
        } else {
            stored = m_deps.followed.unfollowArtist(QString(LocalScope), key);
        }
        if (!stored) {
            return ready(stored.failure());
        }
        emit followChanged(key, followed);
        return ready(followed);
    }

    // One change per artist at a time, as before.
    if (m_pending.contains(artist.publicId)) {
        return ready(Failure::of(FailureKind::Cancelled, QStringLiteral("a change for this artist is on its way")));
    }
    const std::optional<account::AccountOperationContext> context = m_deps.session.captureOperation();
    m_pending.insert(artist.publicId);
    const QString publicId = artist.publicId;
    return m_deps.client.setArtistFollowed(context->bearerSession, publicId, followed)
        .then(this, [this, publicId, followed, context = *context](const Result<void>& result) -> Result<bool> {
            m_pending.remove(publicId);
            if (!m_deps.session.isCurrent(context)) {
                return Failure::of(FailureKind::Cancelled, QStringLiteral("account changed"));
            }
            if (!result) {
                if (result.failure().kind == FailureKind::Unauthorized) {
                    m_deps.session.handleUnauthorized(context);
                }
                return result.failure();
            }
            m_accountFollowed.insert(publicId, followed);
            emit followChanged(publicId, followed);
            return followed;
        });
}

} // namespace lmp::core
