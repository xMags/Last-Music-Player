#include "core/model/TrackKeys.h"

#include "core/playback/LastTrackIdentity.h"
#include "core/text/CaseFold.h"

#include <QLatin1Char>
#include <QLatin1StringView>
#include <QStringList>

#include <algorithm>
#include <optional>

namespace lmp::core::track_keys {

QString remoteScopeCacheKey(const RemoteScopeKeyParts& scope)
{
    QString key = mode::sourceModeName(scope.mode);
    if (scope.mode == mode::SourceMode::ApiKey) {
        key += QLatin1Char(':') + scope.cachePartition;
    }
    key += QLatin1Char(':') + QString::number(scope.generation);
    if (scope.mode == mode::SourceMode::Account) {
        key += QLatin1Char(':') + QString::number(scope.accountGeneration);
    }
    return key;
}

QString durableScopeKey(const RemoteScopeKeyParts& scope)
{
    QString key = mode::sourceModeName(scope.mode);
    if (scope.mode == mode::SourceMode::ApiKey) {
        key += QLatin1Char(':') + scope.cachePartition;
    } else if (scope.mode == mode::SourceMode::Account) {
        if (scope.ownerId.isEmpty()) {
            return {};
        }
        key += QLatin1Char(':') + scope.ownerId;
    }
    return key;
}

namespace {

constexpr QLatin1StringView kDownloadPrefix{"download|"};

struct DownloadScope {
    QString mode;
    // ApiKey mode only.
    QString partition;
    // Written with run counters by earlier versions (no account named).
    bool counted = false;
};

[[nodiscard]] bool allDigits(const QString& text)
{
    return !text.isEmpty() && std::all_of(text.cbegin(), text.cend(), [](QChar c) { return c.isDigit(); });
}

// The scope part of a download key ("download|<scope>\n...").
[[nodiscard]] std::optional<DownloadScope> downloadScopeOf(const QString& stableKey)
{
    if (!stableKey.startsWith(kDownloadPrefix)) {
        return std::nullopt;
    }
    const qsizetype end = stableKey.indexOf(QLatin1Char('\n'));
    if (end < 0) {
        return std::nullopt;
    }
    const QStringList parts = stableKey.mid(kDownloadPrefix.size(), end - kDownloadPrefix.size()).split(QLatin1Char(':'));
    DownloadScope scope;
    scope.mode = parts.value(0);
    if (scope.mode == mode::sourceModeName(mode::SourceMode::ApiKey)) {
        scope.partition = parts.value(1);
        // "ApiKey:<partition>:<generation>"
        scope.counted = parts.size() == 3 && allDigits(parts.at(2));
    } else {
        // "Account:<generation>:<account generation>"
        scope.counted = parts.size() == 3 && allDigits(parts.at(1)) && allDigits(parts.at(2));
    }
    return scope;
}

} // namespace

QString downloadMatchKey(const QString& stableKey)
{
    const std::optional<DownloadScope> scope = downloadScopeOf(stableKey);
    if (!scope) {
        return stableKey;
    }
    QString key = kDownloadPrefix + scope->mode;
    if (scope->mode == mode::sourceModeName(mode::SourceMode::ApiKey)) {
        key += QLatin1Char(':') + scope->partition;
    }
    return key + stableKey.mid(stableKey.indexOf(QLatin1Char('\n')));
}

bool downloadInScope(const QString& stableKey, const QString& scopePrefix)
{
    if (scopePrefix.isEmpty()) {
        return false;
    }
    if (stableKey.startsWith(scopePrefix)) {
        return true;
    }
    const std::optional<DownloadScope> stored = downloadScopeOf(stableKey);
    const std::optional<DownloadScope> current = downloadScopeOf(scopePrefix);
    return stored && current && stored->counted && stored->mode == current->mode
           && stored->partition == current->partition;
}

QString streamCacheKey(const RemoteScopeKeyParts& scope, const Track& track)
{
    if (scope.mode != mode::SourceMode::ApiKey || track.sourceUrl.isEmpty()) {
        return {};
    }
    return remoteScopeCacheKey(scope) + QLatin1Char('\n') + track.sourceUrl;
}

QList<RemoteScopeKeyParts> localOnlyDownloadScopes(const QString& ownerId, const QString& providerPartition)
{
    QList<RemoteScopeKeyParts> scopes;
    if (!ownerId.isEmpty()) {
        RemoteScopeKeyParts account;
        account.mode = mode::SourceMode::Account;
        account.ownerId = ownerId;
        scopes.append(account);
    }
    if (!providerPartition.isEmpty()) {
        RemoteScopeKeyParts provider;
        provider.mode = mode::SourceMode::ApiKey;
        provider.cachePartition = providerPartition;
        scopes.append(provider);
    }
    return scopes;
}

QString downloadStableKey(const RemoteScopeKeyParts& scope, const Track& track)
{
    const QString trackKey = legacyCatalogSourceKey(track);
    if (trackKey.isEmpty()) {
        return {};
    }
    // Only the stored kind counts here, as before: a song with an http(s)
    // path but kind "local" keeps its plain key.
    if (foldCaseAscii(track.sourceKind) != QLatin1StringView("remote")) {
        return trackKey;
    }
    if (scope.mode == mode::SourceMode::LocalOnly) {
        return {};
    }
    const QString durable = durableScopeKey(scope);
    if (durable.isEmpty()) {
        return {};
    }
    return QLatin1StringView("download|") + durable + QLatin1Char('\n') + trackKey;
}

} // namespace lmp::core::track_keys
