#include "core/provider/ProviderClient.h"

#include "core/catalog/CatalogDetailParser.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QLatin1StringView>
#include <QUrl>
#include <QUrlQuery>

#include <algorithm>
#include <optional>

namespace lmp::core::provider {

namespace {

constexpr qsizetype kMaxStatusBodyBytes = 1'000'000;
constexpr qsizetype kMaxCapabilities = 128;
constexpr qsizetype kMaxProviderEntries = 64;

template <typename T>
[[nodiscard]] QFuture<Result<T>> failedFuture(Failure failure)
{
    return QtFuture::makeReadyValueFuture(Result<T>(std::move(failure)));
}

// Header values must not carry line breaks or other control characters,
// which could end the header early.
[[nodiscard]] bool isSafeHeaderText(const QString& value)
{
    return std::none_of(value.cbegin(), value.cend(), [](QChar character) {
        const char16_t unit = character.unicode();
        return unit < 0x20 || unit == 0x7f;
    });
}

// The array under `key`: empty when the key is missing, nothing when the
// value is not an array or holds more than `maximum` entries.
[[nodiscard]] std::optional<QJsonArray> boundedArray(const QJsonObject& object, QLatin1StringView key,
                                                     qsizetype maximum)
{
    const auto found = object.constFind(key);
    if (found == object.constEnd()) {
        return QJsonArray();
    }
    if (!found->isArray()) {
        return std::nullopt;
    }
    QJsonArray array = found->toArray();
    if (array.size() > maximum) {
        return std::nullopt;
    }
    return array;
}

void appendNames(const QJsonArray& array, QStringList& names)
{
    for (const QJsonValue& value : array) {
        if (value.isString() && !names.contains(value.toString())) {
            names.append(value.toString());
        }
    }
}

[[nodiscard]] std::optional<Failure> validateCall(const QString& normalizedBase, const ProviderConnection& connection,
                                                  const ProviderCall& call)
{
    if (!isUsableProviderBaseUrl(normalizedBase)) {
        return Failure::of(FailureKind::NotAvailable, QStringLiteral("provider address is not usable"));
    }
    if (connection.apiKey.isEmpty()) {
        return Failure::of(FailureKind::NotAvailable, QStringLiteral("provider key is missing"));
    }
    if (!isSafeHeaderText(connection.apiKey)) {
        return Failure::of(FailureKind::Unknown, QStringLiteral("provider key is not valid header text"));
    }
    if (!call.pathAndQuery.startsWith(QLatin1StringView("/v1/"))) {
        return Failure::of(FailureKind::Unknown, QStringLiteral("provider path is invalid"));
    }
    return std::nullopt;
}

} // namespace

QString normalizeProviderBaseUrl(const QString& savedBase)
{
    QString base = savedBase.isEmpty() ? QString(DefaultProviderBaseUrl) : savedBase;
    while (!base.isEmpty() && (base.back() == QLatin1Char('/') || base.back() == QLatin1Char('\\'))) {
        base.chop(1);
    }
    return base;
}

bool isUsableProviderBaseUrl(const QString& normalizedBase)
{
    if (normalizedBase.isEmpty()) {
        return false;
    }
    const QUrl url(normalizedBase, QUrl::StrictMode);
    if (!url.isValid() || url.isRelative()) {
        return false;
    }
    const QString scheme = url.scheme();
    if (scheme != QLatin1StringView("http") && scheme != QLatin1StringView("https")) {
        return false;
    }
    return !url.host().isEmpty() && url.userInfo().isEmpty() && !url.hasQuery() && !url.hasFragment();
}

QStringList parseProviderCapabilities(const QByteArray& body)
{
    if (body.isEmpty() || body.size() > kMaxStatusBodyBytes) {
        return {};
    }
    const QJsonDocument document = QJsonDocument::fromJson(body);
    if (!document.isObject()) {
        return {};
    }
    const QJsonObject root = document.object();
    const std::optional<QJsonArray> direct = boundedArray(root, QLatin1StringView("capabilities"), kMaxCapabilities);
    if (!direct) {
        return {};
    }
    QStringList names;
    appendNames(*direct, names);

    const std::optional<QJsonArray> providers =
        boundedArray(root, QLatin1StringView("providers"), kMaxProviderEntries);
    if (!providers) {
        return names;
    }
    for (const QJsonValue& entry : *providers) {
        if (!entry.isObject()) {
            continue;
        }
        const std::optional<QJsonArray> nested =
            boundedArray(entry.toObject(), QLatin1StringView("capabilities"), kMaxCapabilities);
        if (nested) {
            appendNames(*nested, names);
        }
    }
    return names;
}

ProviderClient::ProviderClient(net::IHttpTransport& transport)
    : m_transport(transport)
{
}

QFuture<Result<net::HttpResponse>> ProviderClient::send(const ProviderConnection& connection, const ProviderCall& call)
{
    const QString base = normalizeProviderBaseUrl(connection.baseUrl);
    if (std::optional<Failure> invalid = validateCall(base, connection, call)) {
        return failedFuture<net::HttpResponse>(std::move(*invalid));
    }
    const QUrl url(base + call.pathAndQuery, QUrl::StrictMode);
    if (!url.isValid()) {
        return failedFuture<net::HttpResponse>(
            Failure::of(FailureKind::Unknown, QStringLiteral("provider request address is invalid")));
    }

    net::HttpRequest request;
    request.method = call.method;
    request.url = url;
    request.timeout = call.timeout;
    request.redirects = net::RedirectPolicy::Refuse;
    request.maxResponseBytes = call.maxResponseBytes;
    request.headers.append({QByteArrayLiteral("Authorization"), "Bearer " + connection.apiKey.toUtf8()});
    request.headers.append({QByteArrayLiteral("Accept"), QByteArrayLiteral("application/json")});
    if (!call.jsonBody.isEmpty()) {
        request.headers.append({QByteArrayLiteral("Content-Type"), QByteArrayLiteral("application/json; charset=utf-8")});
        request.body = call.jsonBody;
    }
    return m_transport.send(request);
}

QFuture<Result<ProviderStatus>> ProviderClient::fetchStatus(const ProviderConnection& connection)
{
    ProviderCall call;
    call.pathAndQuery = QStringLiteral("/v1/providers");
    // A plain transform of the answer: it captures nothing that could be
    // gone when it runs, so it needs no context object.
    return send(connection, call).then([](const Result<net::HttpResponse>& response) -> Result<ProviderStatus> {
        if (!response) {
            return response.failure();
        }
        return ProviderStatus{parseProviderCapabilities(response.value().body)};
    });
}

namespace {

[[nodiscard]] QString encoded(const QString& value)
{
    return QString::fromLatin1(QUrl::toPercentEncoding(value));
}

[[nodiscard]] QFuture<Result<QByteArray>> refusedCall(FailureKind kind, const QString& detail)
{
    return QtFuture::makeReadyValueFuture(Result<QByteArray>(Failure::of(kind, detail)));
}

[[nodiscard]] QByteArray urlBody(const QString& url)
{
    return QJsonDocument(QJsonObject{{QStringLiteral("url"), url}}).toJson(QJsonDocument::Compact);
}

} // namespace

QFuture<Result<QByteArray>> ProviderClient::body(const ProviderConnection& connection, const ProviderCall& call)
{
    return send(connection, call).then([](const Result<net::HttpResponse>& response) -> Result<QByteArray> {
        if (!response) {
            return response.failure();
        }
        return response.value().body;
    });
}

QFuture<Result<QByteArray>> ProviderClient::search(const ProviderConnection& connection, const QString& query)
{
    ProviderCall call;
    call.pathAndQuery = QStringLiteral("/v1/search?q=") + encoded(query);
    return body(connection, call);
}

QFuture<Result<QByteArray>> ProviderClient::detail(const ProviderConnection& connection,
                                                   const QStringList& capabilities, QLatin1StringView capability,
                                                   const QString& path,
                                                   const account::LibraryArtistIdentity& identity)
{
    if (!capabilities.contains(QString(capability))) {
        return refusedCall(FailureKind::NotAvailable, QStringLiteral("The provider does not offer this page."));
    }
    const std::optional<QUrlQuery> query = catalog::detailIdentityQuery(identity);
    if (!query) {
        return refusedCall(FailureKind::Unknown, QStringLiteral("This identity cannot be looked up."));
    }
    ProviderCall call;
    call.pathAndQuery = path + QLatin1Char('?') + query->toString(QUrl::FullyEncoded);
    call.timeout = DetailTimeout;
    return body(connection, call);
}

QFuture<Result<QByteArray>> ProviderClient::artistDetail(const ProviderConnection& connection,
                                                         const QStringList& capabilities,
                                                         const account::LibraryArtistIdentity& identity)
{
    return detail(connection, capabilities, ArtistDetailCapability, QStringLiteral("/v1/artists/detail"), identity);
}

QFuture<Result<QByteArray>> ProviderClient::albumDetail(const ProviderConnection& connection,
                                                        const QStringList& capabilities,
                                                        const account::LibraryArtistIdentity& identity)
{
    return detail(connection, capabilities, AlbumDetailCapability, QStringLiteral("/v1/albums/detail"), identity);
}

QFuture<Result<QByteArray>> ProviderClient::related(const ProviderConnection& connection, const QString& sourceUrl)
{
    ProviderCall call;
    call.pathAndQuery = QStringLiteral("/v1/related?sourceUrl=") + encoded(sourceUrl);
    return body(connection, call);
}

QFuture<Result<QByteArray>> ProviderClient::resolve(const ProviderConnection& connection, const QString& url)
{
    ProviderCall call;
    call.method = net::HttpMethod::Post;
    call.pathAndQuery = QStringLiteral("/v1/resolve");
    call.jsonBody = urlBody(url);
    return body(connection, call);
}

QFuture<Result<QByteArray>> ProviderClient::lyrics(const ProviderConnection& connection,
                                                   const ProviderLyricsQuery& query)
{
    QString path = QStringLiteral("/v1/lyrics?artist=") + encoded(query.artist) + QStringLiteral("&title=")
        + encoded(query.title);
    if (!query.album.isEmpty()) {
        path += QStringLiteral("&album=") + encoded(query.album);
    }
    if (query.durationMs > 0) {
        path += QStringLiteral("&durationMs=") + QString::number(query.durationMs);
    }
    if (!query.sourceUrl.isEmpty()) {
        path += QStringLiteral("&sourceUrl=") + encoded(query.sourceUrl);
    }
    ProviderCall call;
    call.pathAndQuery = path;
    return body(connection, call);
}

QFuture<Result<QByteArray>> ProviderClient::importAlbum(const ProviderConnection& connection, const QString& url)
{
    ProviderCall call;
    call.method = net::HttpMethod::Post;
    call.pathAndQuery = QStringLiteral("/v1/import-album");
    call.jsonBody = urlBody(url);
    return body(connection, call);
}

} // namespace lmp::core::provider
