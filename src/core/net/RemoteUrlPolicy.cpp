#include "core/net/RemoteUrlPolicy.h"

#include "core/text/CaseFold.h"

#include <QLatin1StringView>
#include <QStringList>
#include <QUrl>

#include <array>

namespace lmp::core::net {

namespace {

constexpr std::array kCredentialNames{
    QLatin1StringView("access_token"), QLatin1StringView("api_key"),       QLatin1StringView("apikey"),
    QLatin1StringView("authorization"), QLatin1StringView("bearer"),       QLatin1StringView("password"),
    QLatin1StringView("refresh_token"), QLatin1StringView("session"),      QLatin1StringView("jwt"),
};

constexpr std::array kSignedDeliveryNames{
    QLatin1StringView("token"),      QLatin1StringView("media_token"), QLatin1StringView("signature"),
    QLatin1StringView("sig"),        QLatin1StringView("credential"),  QLatin1StringView("expires"),
    QLatin1StringView("auth"),       QLatin1StringView("key"),
};

constexpr std::array kSignedDeliveryPrefixes{
    QLatin1StringView("x-amz-"),
    QLatin1StringView("x-goog-"),
};

// Whether a query or fragment has a field of the given kind. Fields are split
// on "&"; a field's name is what comes before its first "=".
bool containsFieldOfKind(const QString& component, QueryFieldKind kind)
{
    QString text = component.toLower();
    while (!text.isEmpty() && (text.front() == QLatin1Char('?') || text.front() == QLatin1Char('#'))) {
        text.remove(0, 1);
    }
    const QStringList fields = text.split(QLatin1Char('&'));
    for (const QString& field : fields) {
        if (classifyQueryFieldName(field.section(QLatin1Char('='), 0, 0)) == kind) {
            return true;
        }
    }
    return false;
}

} // namespace

QueryFieldKind classifyQueryFieldName(QStringView loweredName)
{
    for (const QLatin1StringView name : kCredentialNames) {
        if (loweredName == name) {
            return QueryFieldKind::Credential;
        }
    }
    for (const QLatin1StringView name : kSignedDeliveryNames) {
        if (loweredName == name) {
            return QueryFieldKind::SignedDelivery;
        }
    }
    for (const QLatin1StringView prefix : kSignedDeliveryPrefixes) {
        if (loweredName.startsWith(prefix)) {
            return QueryFieldKind::SignedDelivery;
        }
    }
    return QueryFieldKind::Ordinary;
}

bool isLoopbackHost(QStringView host)
{
    const QString lowered = foldCaseAscii(host);
    return lowered == QLatin1StringView("127.0.0.1") || lowered == QLatin1StringView("::1")
        || lowered == QLatin1StringView("localhost");
}

bool isSafeRemoteUrl(const QString& url, RemoteUrlUse use)
{
    if (url.isEmpty()) {
        return false;
    }
    const QUrl parsed(url, QUrl::StrictMode);
    // Only absolute addresses with a host parse, as with the earlier parser.
    if (!parsed.isValid() || parsed.isRelative() || parsed.host().isEmpty()) {
        return false;
    }
    const QString scheme = parsed.scheme().toLower();
    if (scheme != QLatin1StringView("https")
        && !(scheme == QLatin1StringView("http") && isLoopbackHost(parsed.host()))) {
        return false;
    }
    if (!parsed.userName().isEmpty() || !parsed.password().isEmpty()) {
        return false;
    }
    // Compared in the form they were written in, without decoding.
    const QString query = parsed.query(QUrl::FullyEncoded);
    const QString fragment = parsed.fragment(QUrl::FullyEncoded);
    if (containsFieldOfKind(query, QueryFieldKind::Credential)
        || containsFieldOfKind(fragment, QueryFieldKind::Credential)) {
        return false;
    }
    if (use == RemoteUrlUse::Durable
        && (containsFieldOfKind(query, QueryFieldKind::SignedDelivery)
            || containsFieldOfKind(fragment, QueryFieldKind::SignedDelivery))) {
        return false;
    }
    return true;
}

bool isHttpUrl(QStringView text)
{
    return text.startsWith(QLatin1StringView("http://")) || text.startsWith(QLatin1StringView("https://"));
}

} // namespace lmp::core::net
