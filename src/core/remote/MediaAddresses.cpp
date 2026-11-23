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

} // namespace lmp::core::remote
