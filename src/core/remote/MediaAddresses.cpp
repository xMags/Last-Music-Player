#include "core/remote/MediaAddresses.h"

#include "core/account/AccountUrlPolicy.h"
#include "core/net/ProviderUrlCredential.h"
#include "core/net/RemoteUrlPolicy.h"
#include "core/provider/ProviderClient.h"
#include "core/text/CaseFold.h"

#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QJsonValue>
#include <QUrl>
#include <QLatin1StringView>

namespace lmp::core::remote {

namespace {

[[nodiscard]] QString withoutFragment(const QString& url)
{
    const qsizetype fragment = url.indexOf(QLatin1Char('#'));
    return fragment < 0 ? url : url.left(fragment);
}

} // namespace

bool isCurrentProviderPath(const QString& url, const QString& baseUrl, QStringView path)
{
    const QString prefix = foldCaseAscii(provider::normalizeProviderBaseUrl(baseUrl)) + foldCaseAscii(path);
    return foldCaseAscii(withoutFragment(url)).startsWith(prefix);
}

QString accountStreamUrl(const QString& filePath, const QString& mediaOrigin)
{
    if (!net::isHttpUrl(filePath) || !isCurrentProviderPath(filePath, mediaOrigin, u"/v1/stream/")) {
        return {};
    }
    return withoutFragment(net::removeLegacyProviderUrlCredential(filePath));
}

QString accountArtworkUrl(const QString& artworkUrl, const QString& mediaOrigin)
{
    if (!net::isHttpUrl(artworkUrl) || !foldCaseAscii(artworkUrl).contains(QLatin1StringView("/v1/artwork"))) {
        return artworkUrl;
    }
    if (!isCurrentProviderPath(artworkUrl, mediaOrigin, u"/v1/artwork")) {
        return {};
    }
    return withoutFragment(net::removeLegacyProviderUrlCredential(artworkUrl));
}

QString trustedAccountStreamUrl(const QString& filePath, const QString& mediaOrigin, qint64 nowMsSinceEpoch)
{
    const QString stream = accountStreamUrl(filePath, mediaOrigin);
    if (stream.isEmpty()
        || !account::isTrustedAccountMediaUrl(stream, mediaOrigin, account::AccountMediaScope::Stream,
                                              nowMsSinceEpoch)) {
        return {};
    }
    return stream;
}

ResolvedStream parseResolvedStream(const QByteArray& body)
{
    QJsonParseError error;
    const QJsonDocument document = QJsonDocument::fromJson(body, &error);
    if (error.error != QJsonParseError::NoError || !document.isObject()) {
        return {};
    }
    const QJsonValue result = document.object().value(QLatin1StringView("result"));
    if (!result.isObject()) {
        return {};
    }
    const QJsonObject object = result.toObject();
    const auto text = [&object](QLatin1StringView key) {
        const QJsonValue value = object.value(key);
        return value.isString() ? value.toString() : QString();
    };
    return {text(QLatin1StringView("streamUrl")), text(QLatin1StringView("artworkUrl"))};
}

namespace {

[[nodiscard]] bool startsWithHttpScheme(const QString& url)
{
    const QString lowered = url.toLower();
    return lowered.startsWith(QLatin1StringView("http://")) || lowered.startsWith(QLatin1StringView("https://"));
}

[[nodiscard]] QString appendAccessToken(const QString& url, const QString& apiKey)
{
    if (apiKey.isEmpty()) {
        return url;
    }
    QString text = url;
    QString fragment;
    if (const qsizetype fragmentStart = text.indexOf(QLatin1Char('#')); fragmentStart >= 0) {
        fragment = text.mid(fragmentStart);
        text.truncate(fragmentStart);
    }
    text += text.contains(QLatin1Char('?')) ? QLatin1Char('&') : QLatin1Char('?');
    text += QStringLiteral("access_token=") + QString::fromLatin1(QUrl::toPercentEncoding(apiKey));
    return text + fragment;
}

[[nodiscard]] QString withoutMediaCredentials(const QString& url)
{
    return net::stripQueryFields(url, {QLatin1StringView("media_token"), QLatin1StringView("access_token")});
}

// A saved provider stream address moved to the current provider.
[[nodiscard]] QString rebasedStreamUrl(const QString& streamUrl, const QString& baseUrl, const QString& apiKey)
{
    if (!startsWithHttpScheme(streamUrl)) {
        return {};
    }
    QString text = streamUrl;
    if (const qsizetype fragmentStart = text.indexOf(QLatin1StringView("#lmp=")); fragmentStart >= 0) {
        text.truncate(fragmentStart);
    }
    const qsizetype pathIndex = text.toLower().indexOf(QLatin1StringView("/v1/stream/"));
    if (pathIndex < 0) {
        return {};
    }
    return appendAccessToken(withoutMediaCredentials(provider::normalizeProviderBaseUrl(baseUrl) + text.mid(pathIndex)),
                             apiKey);
}

} // namespace

QString stableStreamId(const QString& kind, const QString& sourceUrl)
{
    // 64-bit FNV-1a over each UTF-16 unit, low byte first. The offset basis
    // is the earlier versions' 1469598103934665603 (one digit short of the
    // usual value); it stays, so a song keeps the stream id it had.
    quint64 hash = 1469598103934665603ULL;
    for (const QChar character : sourceUrl) {
        const char16_t unit = character.unicode();
        for (int shift = 0; shift < 16; shift += 8) {
            hash ^= static_cast<quint8>((unit >> shift) & 0xFF);
            hash *= 1099511628211ULL;
        }
    }
    return kind + QLatin1Char(':') + QStringLiteral("%1").arg(hash, 16, 16, QLatin1Char('0'));
}

QString providerStreamUrl(const QString& filePath, const QString& sourceUrl, const QString& sourceTag,
                          const QString& baseUrl, const QString& apiKey)
{
    if (const QString rebased = rebasedStreamUrl(filePath, baseUrl, apiKey); !rebased.isEmpty()) {
        return rebased;
    }
    if (startsWithHttpScheme(sourceUrl)) {
        const QString tag = sourceTag.toLower();
        const QString kind = tag.isEmpty() || tag == QLatin1StringView("direct") ? QStringLiteral("direct")
                                                                                 : QStringLiteral("remote");
        const QString url = provider::normalizeProviderBaseUrl(baseUrl) + QStringLiteral("/v1/stream/")
            + QString::fromLatin1(QUrl::toPercentEncoding(stableStreamId(kind, sourceUrl))) + QStringLiteral("?url=")
            + QString::fromLatin1(QUrl::toPercentEncoding(sourceUrl));
        return appendAccessToken(url, apiKey);
    }
    if (startsWithHttpScheme(filePath) && filePath.toLower().contains(QLatin1StringView("/v1/stream/"))) {
        return appendAccessToken(withoutMediaCredentials(filePath), apiKey);
    }
    return {};
}

QString providerArtworkUrl(const QString& artworkUrl, const QString& baseUrl, const QString& apiKey)
{
    if (!startsWithHttpScheme(artworkUrl)) {
        return artworkUrl;
    }
    const qsizetype pathIndex = artworkUrl.toLower().indexOf(QLatin1StringView("/v1/artwork"));
    if (pathIndex < 0) {
        return artworkUrl;
    }
    return appendAccessToken(
        withoutMediaCredentials(provider::normalizeProviderBaseUrl(baseUrl) + artworkUrl.mid(pathIndex)), apiKey);
}

} // namespace lmp::core::remote
