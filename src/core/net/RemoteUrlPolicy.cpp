#include "core/net/RemoteUrlPolicy.h"

#include <QLatin1StringView>
#include <QStringList>
#include <QUrl>

#include <initializer_list>

namespace lmp::core::net {

namespace {

bool isLoopbackHost(const QString& host)
{
    const QString lowered = host.toLower();
    return lowered == QLatin1StringView("127.0.0.1") || lowered == QLatin1StringView("::1")
        || lowered == QLatin1StringView("localhost");
}

// Whether a query or fragment has a field whose name is one of `exactNames`
// or starts with one of `namePrefixes`. Fields are split on "&"; a field's
// name is what comes before its first "=".
bool containsNamedField(const QString& component,
                        std::initializer_list<QLatin1StringView> exactNames,
                        std::initializer_list<QLatin1StringView> namePrefixes = {})
{
    QString text = component.toLower();
    while (!text.isEmpty() && (text.front() == QLatin1Char('?') || text.front() == QLatin1Char('#'))) {
        text.remove(0, 1);
    }
    const QStringList fields = text.split(QLatin1Char('&'));
    for (const QString& field : fields) {
        const QString name = field.section(QLatin1Char('='), 0, 0);
        for (const QLatin1StringView expected : exactNames) {
            if (name == expected) {
                return true;
            }
        }
        for (const QLatin1StringView prefix : namePrefixes) {
            if (name.startsWith(prefix)) {
                return true;
            }
        }
    }
    return false;
}

bool carriesCredential(const QString& component)
{
    return containsNamedField(component,
                              {QLatin1StringView("access_token"), QLatin1StringView("api_key"),
                               QLatin1StringView("apikey"), QLatin1StringView("authorization"),
                               QLatin1StringView("bearer"), QLatin1StringView("password"),
                               QLatin1StringView("refresh_token"), QLatin1StringView("session"),
                               QLatin1StringView("jwt")});
}

bool carriesSignedDelivery(const QString& component)
{
    return containsNamedField(component,
                              {QLatin1StringView("token"), QLatin1StringView("media_token"),
                               QLatin1StringView("signature"), QLatin1StringView("sig"),
                               QLatin1StringView("credential"), QLatin1StringView("expires"),
                               QLatin1StringView("auth"), QLatin1StringView("key")},
                              {QLatin1StringView("x-amz-"), QLatin1StringView("x-goog-")});
}

} // namespace

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
    if (carriesCredential(query) || carriesCredential(fragment)) {
        return false;
    }
    if (use == RemoteUrlUse::Durable && (carriesSignedDelivery(query) || carriesSignedDelivery(fragment))) {
        return false;
    }
    return true;
}

bool isHttpUrl(QStringView text)
{
    return text.startsWith(QLatin1StringView("http://")) || text.startsWith(QLatin1StringView("https://"));
}

} // namespace lmp::core::net
