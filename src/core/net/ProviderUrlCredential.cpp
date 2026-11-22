#include "core/net/ProviderUrlCredential.h"

#include <QLatin1StringView>
#include <QStringList>

namespace lmp::core::net {

namespace {

// Unlike isHttpUrl in RemoteUrlPolicy, the scheme is compared without regard
// to case here, as the server address helpers always did.
bool startsWithHttpScheme(const QString& url)
{
    const QString lowered = url.toLower();
    return lowered.startsWith(QLatin1StringView("http://")) || lowered.startsWith(QLatin1StringView("https://"));
}

// Drops every query field named `loweredName` (compared without regard to
// case) and every empty field, keeping the order of the rest and any fragment.
QString stripQueryField(const QString& url, QLatin1StringView loweredName)
{
    const qsizetype queryStart = url.indexOf(QLatin1Char('?'));
    if (queryStart < 0) {
        return url;
    }

    const qsizetype fragmentStart = url.indexOf(QLatin1Char('#'), queryStart + 1);
    const qsizetype queryEnd = fragmentStart < 0 ? url.size() : fragmentStart;
    const QString query = url.mid(queryStart + 1, queryEnd - queryStart - 1);

    QStringList kept;
    qsizetype start = 0;
    while (start <= query.size()) {
        const qsizetype ampersand = query.indexOf(QLatin1Char('&'), start);
        const QString part = ampersand < 0 ? query.mid(start) : query.mid(start, ampersand - start);
        const qsizetype equals = part.indexOf(QLatin1Char('='));
        const QString fieldName = (equals < 0 ? part : part.left(equals)).toLower();
        if (fieldName != loweredName && !part.isEmpty()) {
            kept.append(part);
        }
        if (ampersand < 0) {
            break;
        }
        start = ampersand + 1;
    }

    QString rebuilt = url.left(queryStart);
    if (!kept.isEmpty()) {
        rebuilt += QLatin1Char('?');
        rebuilt += kept.join(QLatin1Char('&'));
    }
    if (fragmentStart >= 0) {
        rebuilt += url.mid(fragmentStart);
    }
    return rebuilt;
}

} // namespace

QString removeLegacyProviderUrlCredential(const QString& url)
{
    if (!startsWithHttpScheme(url)) {
        return url;
    }
    const QString lowered = url.toLower();
    if (!lowered.contains(QLatin1StringView("/v1/stream/")) && !lowered.contains(QLatin1StringView("/v1/artwork"))) {
        return url;
    }
    return stripQueryField(url, QLatin1StringView("access_token"));
}

} // namespace lmp::core::net
