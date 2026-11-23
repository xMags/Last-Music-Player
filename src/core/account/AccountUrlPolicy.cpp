#include "core/account/AccountUrlPolicy.h"

#include "core/net/RemoteUrlPolicy.h"
#include "core/text/CaseFold.h"

#include <QLatin1StringView>
#include <QStringList>
#include <QStringView>
#include <QUrl>

#include <limits>
#include <optional>
#include <utility>

namespace lmp::core::account {

namespace {

constexpr qsizetype kMaxProfileUrlCharacters = 2048;

// A media token is refreshed this long before it expires, so playback that
// starts now does not run into the expiry.
constexpr qint64 kMediaTokenRefreshMarginMs = 30'000;
// "v2.<scope>.<expiry ms>.<signature>": the second form of media token.
constexpr QLatin1StringView kMediaTokenVersion2Prefix{"v2."};

struct TrustedOrigin {
    QString normalized;
    QString scheme;
    QString host;
    int port = -1;
};

std::optional<QUrl> parseAbsolute(const QString& text)
{
    QUrl url(text, QUrl::StrictMode);
    if (!url.isValid() || url.isRelative()) {
        return std::nullopt;
    }
    return url;
}

int effectivePort(const QUrl& url)
{
    const int port = url.port();
    if (port > 0) {
        return port;
    }
    const QString scheme = foldCaseAscii(url.scheme());
    if (scheme == QLatin1StringView("https")) {
        return 443;
    }
    if (scheme == QLatin1StringView("http")) {
        return 80;
    }
    return port;
}

bool hasAllowedScheme(const QUrl& url, LoopbackHttp loopback)
{
    const QString scheme = foldCaseAscii(url.scheme());
    if (scheme == QLatin1StringView("https")) {
        return true;
    }
    return loopback == LoopbackHttp::Allowed && scheme == QLatin1StringView("http")
        && net::isLoopbackHost(url.host());
}

bool hasUserInfo(const QUrl& url)
{
    return !url.userName().isEmpty() || !url.password().isEmpty();
}

// Field names are percent-decoded and A to Z folded before they are compared.
bool hasSensitiveQueryField(const QString& query)
{
    QStringView text(query);
    while (!text.isEmpty() && text.front() == QLatin1Char('?')) {
        text = text.sliced(1);
    }
    const QStringList fields = text.toString().split(QLatin1Char('&'));
    for (const QString& field : fields) {
        const QString encodedName = field.section(QLatin1Char('='), 0, 0);
        const QString name = foldCaseAscii(QUrl::fromPercentEncoding(encodedName.toUtf8()));
        if (net::classifyQueryFieldName(name) != net::QueryFieldKind::Ordinary) {
            return true;
        }
    }
    return false;
}

std::optional<TrustedOrigin> parseTrustedOrigin(const QString& origin, LoopbackHttp loopback)
{
    if (origin.isEmpty()) {
        return std::nullopt;
    }
    const std::optional<QUrl> url = parseAbsolute(origin);
    if (!url || url->hasQuery() || url->hasFragment() || hasUserInfo(*url)) {
        return std::nullopt;
    }
    const QString path = url->path(QUrl::FullyEncoded);
    if (!path.isEmpty() && path != QLatin1StringView("/")) {
        return std::nullopt;
    }
    QString host = foldCaseAscii(url->host());
    if (host.isEmpty() || !hasAllowedScheme(*url, loopback)) {
        return std::nullopt;
    }

    QString normalized = origin;
    while (normalized.endsWith(QLatin1Char('/'))) {
        normalized.chop(1);
    }
    if (normalized.isEmpty()) {
        return std::nullopt;
    }
    return TrustedOrigin{
        .normalized = std::move(normalized),
        .scheme = foldCaseAscii(url->scheme()),
        .host = std::move(host),
        .port = effectivePort(*url),
    };
}

QLatin1StringView scopeName(AccountMediaScope scope)
{
    return scope == AccountMediaScope::Stream ? QLatin1StringView("stream") : QLatin1StringView("artwork");
}

QLatin1StringView scopePath(AccountMediaScope scope)
{
    return scope == AccountMediaScope::Stream ? QLatin1StringView("/v1/stream/")
                                              : QLatin1StringView("/v1/artwork");
}

// A trailing "/" means "a path below this one"; otherwise the path must match
// whole. Dot segments are resolved first, and A to Z are folded.
bool hasScopePath(const QUrl& url, AccountMediaScope scope)
{
    const QString path = foldCaseAscii(url.adjusted(QUrl::NormalizePathSegments).path(QUrl::FullyEncoded));
    const QLatin1StringView expected = scopePath(scope);
    if (expected.endsWith(QLatin1Char('/'))) {
        return path.size() > expected.size() && path.startsWith(expected);
    }
    return path == expected;
}

// The raw (still percent-encoded) value of the first query field with this
// name, compared with A to Z folded; empty when there is none. Read from the
// text so the value is exactly what the service signed.
QString rawQueryValue(const QString& url, QLatin1StringView name)
{
    const qsizetype queryStart = url.indexOf(QLatin1Char('?'));
    if (queryStart < 0) {
        return {};
    }
    const qsizetype fragmentStart = url.indexOf(QLatin1Char('#'), queryStart + 1);
    const qsizetype queryEnd = fragmentStart < 0 ? url.size() : fragmentStart;
    const QStringList fields = url.mid(queryStart + 1, queryEnd - queryStart - 1).split(QLatin1Char('&'));
    for (const QString& field : fields) {
        const qsizetype equals = field.indexOf(QLatin1Char('='));
        if (foldCaseAscii(QStringView(field).left(equals < 0 ? field.size() : equals)) == name) {
            return equals < 0 ? QString() : field.mid(equals + 1);
        }
    }
    return {};
}

bool isAsciiSpace(QChar character)
{
    const char16_t unit = character.unicode();
    return unit == u' ' || (unit >= u'\t' && unit <= u'\r');
}

// Reads a base-10 integer the way std::stoll does: leading white space, an
// optional sign, at least one digit, and anything after the digits ignored.
// Empty when there is no digit or the value does not fit.
std::optional<qint64> parseLeadingInteger(QStringView text)
{
    qsizetype index = 0;
    while (index < text.size() && isAsciiSpace(text.at(index))) {
        ++index;
    }
    bool negative = false;
    if (index < text.size() && (text.at(index) == QLatin1Char('+') || text.at(index) == QLatin1Char('-'))) {
        negative = text.at(index) == QLatin1Char('-');
        ++index;
    }
    constexpr quint64 kMaxPositive = static_cast<quint64>(std::numeric_limits<qint64>::max());
    const quint64 limit = negative ? kMaxPositive + 1 : kMaxPositive;
    quint64 magnitude = 0;
    qsizetype digits = 0;
    while (index < text.size() && text.at(index) >= QLatin1Char('0') && text.at(index) <= QLatin1Char('9')) {
        const auto digit = static_cast<quint64>(text.at(index).unicode() - u'0');
        if (magnitude > (limit - digit) / 10) {
            return std::nullopt;
        }
        magnitude = magnitude * 10 + digit;
        ++digits;
        ++index;
    }
    if (digits == 0) {
        return std::nullopt;
    }
    if (!negative) {
        return static_cast<qint64>(magnitude);
    }
    return magnitude == kMaxPositive + 1 ? std::numeric_limits<qint64>::min() : -static_cast<qint64>(magnitude);
}

} // namespace

bool isTrustedAccountOrigin(const QString& origin, LoopbackHttp loopback)
{
    return parseTrustedOrigin(origin, loopback).has_value();
}

QString normalizeTrustedAccountOrigin(const QString& origin, LoopbackHttp loopback)
{
    std::optional<TrustedOrigin> parsed = parseTrustedOrigin(origin, loopback);
    return parsed ? std::move(parsed->normalized) : QString();
}

bool isUrlFromTrustedAccountOrigin(const QString& url, const QString& origin, LoopbackHttp loopback)
{
    const std::optional<TrustedOrigin> trusted = parseTrustedOrigin(origin, loopback);
    if (!trusted || url.isEmpty()) {
        return false;
    }
    const std::optional<QUrl> candidate = parseAbsolute(url);
    if (!candidate || hasUserInfo(*candidate)) {
        return false;
    }
    return foldCaseAscii(candidate->scheme()) == trusted->scheme && foldCaseAscii(candidate->host()) == trusted->host
        && effectivePort(*candidate) == trusted->port;
}

bool isSafeAccountProfileUrl(const QString& url, LoopbackHttp loopback)
{
    if (url.isEmpty() || url.size() > kMaxProfileUrlCharacters) {
        return false;
    }
    const std::optional<QUrl> parsed = parseAbsolute(url);
    return parsed && hasAllowedScheme(*parsed, loopback) && !parsed->host().isEmpty() && !hasUserInfo(*parsed)
        && !parsed->hasFragment() && !hasSensitiveQueryField(parsed->query(QUrl::FullyEncoded));
}

bool mediaTokenNeedsRefresh(const QString& mediaUrl, AccountMediaScope scope, qint64 nowMsSinceEpoch)
{
    if (mediaUrl.isEmpty() || !foldCaseAscii(mediaUrl).contains(scopePath(scope))) {
        return false;
    }

    const QString rawToken = rawQueryValue(mediaUrl, QLatin1StringView("media_token"));
    QStringView token = rawToken;
    // The service's second token form puts "v2." in front of the first one.
    if (token.startsWith(kMediaTokenVersion2Prefix)) {
        token = token.sliced(kMediaTokenVersion2Prefix.size());
    }
    const qsizetype firstDot = token.indexOf(QLatin1Char('.'));
    const qsizetype secondDot = firstDot < 0 ? -1 : token.indexOf(QLatin1Char('.'), firstDot + 1);
    // The scope part is compared exactly, without folding.
    if (firstDot < 0 || secondDot < 0 || secondDot + 1 >= token.size() || token.left(firstDot) != scopeName(scope)) {
        return true;
    }
    const std::optional<qint64> expiresAtMs = parseLeadingInteger(token.sliced(firstDot + 1, secondDot - firstDot - 1));
    if (!expiresAtMs) {
        return true;
    }
    return *expiresAtMs <= nowMsSinceEpoch + kMediaTokenRefreshMarginMs;
}

bool isTrustedAccountMediaUrl(const QString& mediaUrl, const QString& mediaOrigin, AccountMediaScope scope,
                              qint64 nowMsSinceEpoch, LoopbackHttp loopback)
{
    if (mediaOrigin.isEmpty() || !net::isSafeRemoteUrl(mediaUrl, net::RemoteUrlUse::EphemeralMedia)) {
        return false;
    }
    const std::optional<QUrl> parsed = parseAbsolute(mediaUrl);
    return parsed && isUrlFromTrustedAccountOrigin(mediaUrl, mediaOrigin, loopback) && hasScopePath(*parsed, scope)
        && !mediaTokenNeedsRefresh(mediaUrl, scope, nowMsSinceEpoch);
}

} // namespace lmp::core::account
