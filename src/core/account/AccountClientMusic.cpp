// AccountClient's music calls: library sync, playlists, songs and media.
#include "core/account/AccountClient.h"

#include <QJsonDocument>
#include <QJsonObject>
#include <QLatin1StringView>
#include <QUrl>

#include <optional>

namespace lmp::core::account {

namespace {

template <typename T>
[[nodiscard]] QFuture<Result<T>> refused(const QString& detail)
{
    return QtFuture::makeReadyValueFuture(Result<T>(Failure::of(FailureKind::Unknown, detail)));
}

// A value placed in a path or a query: every character outside the
// unreserved set is percent-encoded.
[[nodiscard]] QString encoded(const QString& value)
{
    return QString::fromLatin1(QUrl::toPercentEncoding(value));
}

[[nodiscard]] QString playlistPath(const QString& playlistId)
{
    return QStringLiteral("/v1/music/playlists/") + encoded(playlistId);
}

[[nodiscard]] AccountCall makeCall(net::HttpMethod method, QString pathAndQuery, const QString& bearerSession,
                                   QByteArray jsonBody = {})
{
    AccountCall call;
    call.method = method;
    call.pathAndQuery = std::move(pathAndQuery);
    call.bearerSession = bearerSession;
    call.jsonBody = std::move(jsonBody);
    return call;
}

[[nodiscard]] Result<void> toVoid(const Result<net::HttpResponse>& response)
{
    if (!response) {
        return response.failure();
    }
    return Result<void>::success();
}

[[nodiscard]] Result<QByteArray> toBody(const Result<net::HttpResponse>& response)
{
    if (!response) {
        return response.failure();
    }
    return response.value().body;
}

[[nodiscard]] Result<PortablePreferences> toPreferences(const Result<net::HttpResponse>& response)
{
    if (!response) {
        return response.failure();
    }
    return parsePortablePreferences(response.value().body);
}

[[nodiscard]] QByteArray urlBody(const QString& url, bool lowQuality)
{
    QJsonObject payload{{QStringLiteral("url"), url}};
    if (lowQuality) {
        payload.insert(QStringLiteral("quality"), QStringLiteral("low"));
    }
    return QJsonDocument(payload).toJson(QJsonDocument::Compact);
}

} // namespace

QFuture<Result<ParsedAccountLibrary>> AccountClient::fetchLibrary(const QString& bearerSession)
{
    const AccountCall call = makeCall(net::HttpMethod::Get,
                                      QStringLiteral("/v1/music/library?historyLimit=%1").arg(LibraryHistoryLimit),
                                      bearerSession);
    return send(call).then([](const Result<net::HttpResponse>& response) -> Result<ParsedAccountLibrary> {
        if (!response) {
            return response.failure();
        }
        return parseAccountLibrary(response.value().body);
    });
}

QFuture<Result<void>> AccountClient::setLiked(const QString& bearerSession, const QByteArray& trackJson, bool liked,
                                              const QString& remoteId)
{
    if (liked) {
        std::optional<QByteArray> body = wrapTrackRequest(trackJson);
        if (!body) {
            return refused<void>(QStringLiteral("account track payload is invalid"));
        }
        return send(makeCall(net::HttpMethod::Post, QStringLiteral("/v1/music/liked"), bearerSession, std::move(*body)))
            .then(toVoid);
    }
    if (remoteId.isEmpty()) {
        return refused<void>(QStringLiteral("account track id is missing"));
    }
    return send(makeCall(net::HttpMethod::Delete, QStringLiteral("/v1/music/liked?id=") + encoded(remoteId),
                         bearerSession))
        .then(toVoid);
}

QFuture<Result<QStringList>> AccountClient::postHistoryBatch(const QString& bearerSession, const QByteArray& batchJson,
                                                             const QStringList& submittedEventIds)
{
    const AccountCall call =
        makeCall(net::HttpMethod::Post, QStringLiteral("/v1/music/history/batch"), bearerSession, batchJson);
    return send(call).then([submittedEventIds](const Result<net::HttpResponse>& response) -> Result<QStringList> {
        if (!response) {
            return response.failure();
        }
        return parseAcknowledgedHistoryEvents(response.value().body, submittedEventIds);
    });
}

QFuture<Result<PortablePreferences>> AccountClient::fetchPreferences(const QString& bearerSession)
{
    return send(makeCall(net::HttpMethod::Get, QStringLiteral("/v1/music/preferences"), bearerSession))
        .then(toPreferences);
}

QFuture<Result<PortablePreferences>> AccountClient::putPreferences(const QString& bearerSession,
                                                                   const QByteArray& preferencesJson)
{
    return send(makeCall(net::HttpMethod::Put, QStringLiteral("/v1/music/preferences"), bearerSession, preferencesJson))
        .then(toPreferences);
}

QFuture<Result<AccountPlaylist>> AccountClient::createPlaylist(const QString& bearerSession,
                                                               const QByteArray& playlistJson)
{
    const AccountCall call =
        makeCall(net::HttpMethod::Post, QStringLiteral("/v1/music/playlists"), bearerSession, playlistJson);
    return send(call).then([](const Result<net::HttpResponse>& response) -> Result<AccountPlaylist> {
        if (!response) {
            return response.failure();
        }
        std::optional<AccountPlaylist> playlist = parsePlaylistAnswer(response.value().body);
        if (!playlist) {
            return Failure::of(FailureKind::InvalidResponse, QStringLiteral("playlist answer names no playlist"));
        }
        return *playlist;
    });
}

QFuture<Result<void>> AccountClient::updatePlaylist(const QString& bearerSession, const QString& playlistId,
                                                    const QByteArray& playlistJson)
{
    if (playlistId.isEmpty()) {
        return refused<void>(QStringLiteral("playlist id is missing"));
    }
    return send(makeCall(net::HttpMethod::Patch, playlistPath(playlistId), bearerSession, playlistJson)).then(toVoid);
}

QFuture<Result<void>> AccountClient::deletePlaylist(const QString& bearerSession, const QString& playlistId)
{
    if (playlistId.isEmpty()) {
        return refused<void>(QStringLiteral("playlist id is missing"));
    }
    return send(makeCall(net::HttpMethod::Delete, playlistPath(playlistId), bearerSession)).then(toVoid);
}

QFuture<Result<void>> AccountClient::addPlaylistTrack(const QString& bearerSession, const QString& playlistId,
                                                      const QByteArray& trackJson)
{
    std::optional<QByteArray> body = wrapTrackRequest(trackJson);
    if (playlistId.isEmpty() || !body) {
        return refused<void>(QStringLiteral("playlist or track payload is invalid"));
    }
    return send(makeCall(net::HttpMethod::Post, playlistPath(playlistId) + QStringLiteral("/tracks"), bearerSession,
                         std::move(*body)))
        .then(toVoid);
}

QFuture<Result<void>> AccountClient::removePlaylistTrack(const QString& bearerSession, const QString& playlistId,
                                                         const QString& trackId)
{
    if (playlistId.isEmpty() || trackId.isEmpty()) {
        return refused<void>(QStringLiteral("playlist or track id is missing"));
    }
    return send(makeCall(net::HttpMethod::Delete,
                         playlistPath(playlistId) + QStringLiteral("/tracks/") + encoded(trackId), bearerSession))
        .then(toVoid);
}

QFuture<Result<QByteArray>> AccountClient::resolve(const QString& bearerSession, const QString& sourceUrl,
                                                   bool lowQuality)
{
    return send(makeCall(net::HttpMethod::Post, QStringLiteral("/v1/music/resolve"), bearerSession,
                         urlBody(sourceUrl, lowQuality)))
        .then(toBody);
}

QFuture<Result<QByteArray>> AccountClient::related(const QString& bearerSession, const QString& sourceUrl)
{
    return send(makeCall(net::HttpMethod::Get, QStringLiteral("/v1/music/related?sourceUrl=") + encoded(sourceUrl),
                         bearerSession))
        .then(toBody);
}

QFuture<Result<QByteArray>> AccountClient::importCollection(const QString& bearerSession, const QString& sourceUrl)
{
    return send(makeCall(net::HttpMethod::Post, QStringLiteral("/v1/music/import"), bearerSession,
                         urlBody(sourceUrl, false)))
        .then(toBody);
}

QFuture<Result<QByteArray>> AccountClient::artwork(const QString& bearerSession, const QString& artworkUrl)
{
    AccountCall call =
        makeCall(net::HttpMethod::Get, QStringLiteral("/v1/music/artwork?url=") + encoded(artworkUrl), bearerSession);
    call.maxResponseBytes = MaxArtworkBytes;
    return send(call).then(toBody);
}

QFuture<Result<int>> AccountClient::probeStream(const QString& bearerSession, const QString& streamUrl)
{
    AccountCall call =
        makeCall(net::HttpMethod::Get, QStringLiteral("/v1/music/stream?url=") + encoded(streamUrl), bearerSession);
    call.headersOnly = true;
    return send(call).then([](const Result<net::HttpResponse>& response) -> Result<int> {
        if (!response) {
            return response.failure();
        }
        return response.value().status;
    });
}

} // namespace lmp::core::account
