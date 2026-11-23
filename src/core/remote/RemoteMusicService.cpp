#include "core/remote/RemoteMusicService.h"

#include "core/account/AccountClient.h"
#include "core/account/AccountSessionService.h"
#include "core/catalog/CatalogClient.h"
#include "core/mode/ModeService.h"
#include "core/provider/ProviderClient.h"


namespace lmp::core::remote {

namespace {

[[nodiscard]] QFuture<Result<QByteArray>> refused(FailureKind kind, const QString& detail)
{
    return QtFuture::makeReadyValueFuture(Result<QByteArray>(Failure::of(kind, detail)));
}

} // namespace

RemoteMusicService::RemoteMusicService(Deps deps)
    : m_deps(deps)
{
}

QString RemoteMusicService::scopeKey() const
{
    const mode::RemoteScope scope = m_deps.mode.captureScope();
    switch (scope.mode) {
    case mode::SourceMode::Account:
        return QStringLiteral("account:") + QString::number(scope.generation);
    case mode::SourceMode::ApiKey:
        return QStringLiteral("provider:") + QString::number(scope.generation);
    case mode::SourceMode::LocalOnly:
        break;
    }
    return {};
}

QFuture<Result<QByteArray>> RemoteMusicService::route(bool allowed, const AccountCall& account,
                                                      const ProviderCall& provider)
{
    if (!allowed) {
        return refused(FailureKind::NotAvailable, QStringLiteral("Not available in this mode."));
    }
    const mode::RemoteScope scope = m_deps.mode.captureScope();
    mode::ModeService* modes = &m_deps.mode;
    switch (scope.mode) {
    case mode::SourceMode::Account: {
        const std::optional<account::AccountOperationContext> context = m_deps.session.captureOperation();
        if (!context) {
            return refused(FailureKind::NotAvailable, QStringLiteral("Not signed in."));
        }
        account::AccountSessionService* session = &m_deps.session;
        return account(context->bearerSession)
            .then(modes, [modes, session, scope, context = *context](const Result<QByteArray>& answer) -> Result<QByteArray> {
                if (!modes->isCurrent(scope) || !session->isCurrent(context)) {
                    return Failure::of(FailureKind::Cancelled, QStringLiteral("The music source changed."));
                }
                if (!answer && answer.failure().kind == FailureKind::Unauthorized) {
                    session->handleUnauthorized(context);
                }
                return answer;
            });
    }
    case mode::SourceMode::ApiKey:
        return provider().then(modes, [modes, scope](const Result<QByteArray>& answer) -> Result<QByteArray> {
            if (!modes->isCurrent(scope)) {
                return Failure::of(FailureKind::Cancelled, QStringLiteral("The music source changed."));
            }
            return answer;
        });
    case mode::SourceMode::LocalOnly:
        break;
    }
    return refused(FailureKind::NotAvailable, QStringLiteral("Not available in this mode."));
}

QFuture<Result<QByteArray>> RemoteMusicService::search(const QString& query)
{
    const QString trimmed = query.trimmed();
    if (trimmed.isEmpty()) {
        return refused(FailureKind::Unknown, QStringLiteral("Empty search."));
    }
    return route(
        m_deps.mode.capabilities().searchOnline,
        [this, trimmed](const QString& bearer) { return m_deps.catalog.search(bearer, trimmed); },
        [this, trimmed]() -> QFuture<Result<QByteArray>> {
            const std::optional<provider::ProviderConnection> connection = m_deps.mode.providerConnection();
            return connection ? m_deps.provider.search(*connection, trimmed)
                              : refused(FailureKind::NotAvailable, QStringLiteral("No provider key."));
        });
}

QFuture<Result<QByteArray>> RemoteMusicService::related(const Track& seed)
{
    const QString link = seed.sourceUrl.trimmed();
    const QString reference = seed.sourceReference.trimmed();
    if (link.isEmpty()) {
        return refused(FailureKind::NotFound, QStringLiteral("This song has no link."));
    }
    return route(
        m_deps.mode.capabilities().playRadio,
        [this, link, reference](const QString& bearer) { return m_deps.catalog.related(bearer, link, reference); },
        [this, link]() -> QFuture<Result<QByteArray>> {
            const std::optional<provider::ProviderConnection> connection = m_deps.mode.providerConnection();
            return connection ? m_deps.provider.related(*connection, link)
                              : refused(FailureKind::NotAvailable, QStringLiteral("No provider key."));
        });
}

QFuture<Result<QByteArray>> RemoteMusicService::resolve(const QString& sourceUrl, bool lowQuality)
{
    const QString link = sourceUrl.trimmed();
    return route(
        m_deps.mode.capabilities().searchOnline,
        [this, link, lowQuality](const QString& bearer) { return m_deps.account.resolve(bearer, link, lowQuality); },
        [this, link]() -> QFuture<Result<QByteArray>> {
            const std::optional<provider::ProviderConnection> connection = m_deps.mode.providerConnection();
            return connection ? m_deps.provider.resolve(*connection, link)
                              : refused(FailureKind::NotAvailable, QStringLiteral("No provider key."));
        });
}

QFuture<Result<QByteArray>> RemoteMusicService::lyrics(const lyrics::LyricsQuery& query)
{
    return route(
        m_deps.mode.capabilities().showLyrics,
        [this, query](const QString& bearer) {
            account::AccountCall call;
            call.method = net::HttpMethod::Get;
            call.pathAndQuery = lyrics::lyricsPathAndQuery(query);
            call.bearerSession = bearer;
            return m_deps.account.send(call).then([](const Result<net::HttpResponse>& response) -> Result<QByteArray> {
                if (!response) {
                    return response.failure();
                }
                return response.value().body;
            });
        },
        [this, query]() -> QFuture<Result<QByteArray>> {
            const std::optional<provider::ProviderConnection> connection = m_deps.mode.providerConnection();
            if (!connection) {
                return refused(FailureKind::NotAvailable, QStringLiteral("No provider key."));
            }
            // The provider takes the song's link itself; its server reference
            // means nothing there.
            return m_deps.provider.lyrics(*connection, {.artist = query.artist,
                                                        .title = query.title,
                                                        .album = query.album,
                                                        .durationMs = query.durationMs,
                                                        .sourceUrl = query.sourceUrl});
        });
}

QFuture<Result<QByteArray>> RemoteMusicService::artistDetail(const account::LibraryArtistIdentity& identity)
{
    return route(
        m_deps.mode.capabilities().openOnlineArtistPages,
        [this, identity](const QString& bearer) { return m_deps.catalog.providerArtist(bearer, identity); },
        [this, identity]() -> QFuture<Result<QByteArray>> {
            const std::optional<provider::ProviderConnection> connection = m_deps.mode.providerConnection();
            return connection
                ? m_deps.provider.artistDetail(*connection, m_deps.mode.providerCapabilities(), identity)
                : refused(FailureKind::NotAvailable, QStringLiteral("No provider key."));
        });
}

QFuture<Result<QByteArray>> RemoteMusicService::albumDetail(const account::LibraryArtistIdentity& identity,
                                                            const QString& artist)
{
    return route(
        m_deps.mode.capabilities().openOnlineAlbumPages,
        [this, identity, artist](const QString& bearer) { return m_deps.catalog.providerAlbum(bearer, identity, artist); },
        [this, identity]() -> QFuture<Result<QByteArray>> {
            const std::optional<provider::ProviderConnection> connection = m_deps.mode.providerConnection();
            return connection
                ? m_deps.provider.albumDetail(*connection, m_deps.mode.providerCapabilities(), identity)
                : refused(FailureKind::NotAvailable, QStringLiteral("No provider key."));
        });
}

QFuture<Result<QByteArray>> RemoteMusicService::importCollection(const QString& url)
{
    const QString link = url.trimmed();
    return route(
        m_deps.mode.capabilities().importFromLink,
        [this, link](const QString& bearer) { return m_deps.account.importCollection(bearer, link); },
        [this, link]() -> QFuture<Result<QByteArray>> {
            const std::optional<provider::ProviderConnection> connection = m_deps.mode.providerConnection();
            return connection ? m_deps.provider.importAlbum(*connection, link)
                              : refused(FailureKind::NotAvailable, QStringLiteral("No provider key."));
        });
}

} // namespace lmp::core::remote
