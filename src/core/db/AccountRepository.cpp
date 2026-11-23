#include "core/db/AccountRepository.h"

#include "core/db/AccountOutboxRepository.h"
#include "core/db/AccountRows.h"
#include "core/db/SqlTransaction.h"

#include <QLatin1StringView>

// The active context, sync state, the discovery cache, clearing data and the
// sign-out storage contract. The cached library is in
// AccountRepositoryLibrary.cpp.
namespace lmp::core::db {

namespace {

namespace tx = sql_transaction;

constexpr QLatin1StringView kAccountMode{"Account"};

// The service is trusted, but it is still the far side of a network, and
// this row is written on every successful load for the life of the install.
// A payload past this size is not a catalogue page any more, so it is
// refused rather than let one grow the database without bound. Losing the
// cache only costs a cold start.
constexpr qsizetype kMaxDiscoveryPayloadCharacters = qsizetype{4} * 1024 * 1024;

bool isKnownRemoteMode(const QString& mode)
{
    return mode == QLatin1StringView("LocalOnly") || mode == QLatin1StringView("ApiKey") || mode == kAccountMode;
}

} // namespace

AccountRepository::AccountRepository(Database& database)
    : m_database(database)
    , m_connection(database.connection())
{
}

Result<void> AccountRepository::setRemoteLibraryContext(const QString& mode, const QString& accountId)
{
    if (!isKnownRemoteMode(mode)) {
        return tx::notFound(QStringLiteral("unknown remote mode"));
    }

    const QString ownerId = mode == kAccountMode ? accountId : QString();
    if (mode == kAccountMode) {
        if (ownerId.isEmpty()) {
            return tx::notFound(QStringLiteral("account mode needs an account id"));
        }
        if (const Result<void> profile = account_rows::ensureAccountProfile(m_connection, ownerId); !profile) {
            return profile;
        }
    }

    auto prepared = m_connection.prepare(
        "INSERT INTO ActiveAccountContext (SingletonId, RemoteMode, AccountId) VALUES (1, ?, ?) "
        "ON CONFLICT(SingletonId) DO UPDATE SET RemoteMode=excluded.RemoteMode, AccountId=excluded.AccountId;");
    if (!prepared) {
        return prepared.failure();
    }
    prepared.value().bindText(1, mode);
    prepared.value().bindText(2, ownerId);
    return prepared.value().run();
}

QString AccountRepository::activeAccountId() const
{
    auto prepared =
        m_connection.prepare("SELECT AccountId FROM ActiveAccountContext WHERE SingletonId=1 AND RemoteMode='Account';");
    if (prepared && prepared.value().step().valueOr(false)) {
        return prepared.value().columnText(0);
    }
    return {};
}

Result<void> AccountRepository::updateAccountTrackLike(const QString& accountId,
                                                       const QString& remoteTrackId,
                                                       bool liked)
{
    if (accountId.isEmpty() || remoteTrackId.isEmpty()) {
        return tx::notFound(QStringLiteral("empty account or track id"));
    }
    auto prepared = m_connection.prepare(
        "UPDATE AccountTracks SET IsLiked=?, UpdatedAtUtc=datetime('now') WHERE AccountId=? AND RemoteId=?;");
    if (!prepared) {
        return prepared.failure();
    }
    prepared.value().bindInt64(1, liked ? 1 : 0);
    prepared.value().bindText(2, accountId);
    prepared.value().bindText(3, remoteTrackId);
    return prepared.value().run();
}

std::optional<bool> AccountRepository::isAccountTrackLiked(const QString& accountId, const QString& remoteTrackId) const
{
    if (accountId.isEmpty() || remoteTrackId.isEmpty()) {
        return std::nullopt;
    }
    auto prepared = m_connection.prepare("SELECT IsLiked FROM AccountTracks WHERE AccountId=? AND RemoteId=?;");
    if (!prepared) {
        return std::nullopt;
    }
    prepared.value().bindText(1, accountId);
    prepared.value().bindText(2, remoteTrackId);
    if (!prepared.value().step().valueOr(false)) {
        return std::nullopt;
    }
    return account_rows::columnInt(prepared.value(), 0) != 0;
}

QList<Track> AccountRepository::loadAccountLikedTracks(const QString& accountId) const
{
    QList<Track> liked;
    if (accountId.isEmpty()) {
        return liked;
    }
    auto prepared = m_connection.prepare(
        "SELECT RemoteId, Title, Artist FROM AccountTracks WHERE AccountId=? AND IsLiked=1 ORDER BY rowid;");
    if (!prepared) {
        return liked;
    }
    prepared.value().bindText(1, accountId);
    while (prepared.value().step().valueOr(false)) {
        Track track;
        track.remoteId = prepared.value().columnText(0);
        track.title = prepared.value().columnText(1);
        track.artist = prepared.value().columnText(2);
        track.liked = true;
        liked.append(std::move(track));
    }
    return liked;
}

AccountSyncState AccountRepository::loadAccountSyncState(const QString& accountId) const
{
    AccountSyncState state;
    state.accountId = accountId;
    if (accountId.isEmpty()) {
        return state;
    }

    auto prepared = m_connection.prepare(
        "SELECT LastSuccessfulSyncUtc, LastSafeErrorCode, LegacyHistoryImportState FROM AccountSyncState WHERE AccountId=?;");
    if (!prepared) {
        return state;
    }
    SqliteStatement& statement = prepared.value();
    statement.bindText(1, accountId);
    if (statement.step().valueOr(false)) {
        state.lastSuccessfulSyncUtc = statement.columnText(0);
        state.lastSafeErrorCode = statement.columnText(1);
        state.legacyHistoryImportState = statement.columnText(2);
    }
    return state;
}

Result<void> AccountRepository::saveAccountSyncState(const AccountSyncState& state)
{
    if (state.accountId.isEmpty()) {
        return tx::notFound(QStringLiteral("empty account id"));
    }
    if (const Result<void> profile = account_rows::ensureAccountProfile(m_connection, state.accountId); !profile) {
        return profile;
    }

    auto prepared = m_connection.prepare(
        "INSERT INTO AccountSyncState (AccountId, LastSuccessfulSyncUtc, LastSafeErrorCode, LegacyHistoryImportState) "
        "VALUES (?, ?, ?, ?) ON CONFLICT(AccountId) DO UPDATE SET "
        "LastSuccessfulSyncUtc=excluded.LastSuccessfulSyncUtc, LastSafeErrorCode=excluded.LastSafeErrorCode, "
        "LegacyHistoryImportState=excluded.LegacyHistoryImportState;");
    if (!prepared) {
        return prepared.failure();
    }
    SqliteStatement& statement = prepared.value();
    statement.bindText(1, state.accountId);
    statement.bindText(2, state.lastSuccessfulSyncUtc);
    statement.bindText(3, state.lastSafeErrorCode);
    statement.bindText(4, state.legacyHistoryImportState);
    return statement.run();
}

QString AccountRepository::loadCatalogDiscoveryPayload(const QString& accountId, const QString& storefront) const
{
    if (accountId.isEmpty() || storefront.isEmpty()) {
        return {};
    }
    auto prepared = m_connection.prepare("SELECT Payload FROM CatalogDiscoveryCache WHERE AccountId=? AND Storefront=?;");
    if (!prepared) {
        return {};
    }
    prepared.value().bindText(1, accountId);
    prepared.value().bindText(2, storefront);
    if (!prepared.value().step().valueOr(false)) {
        return {};
    }
    return prepared.value().columnText(0);
}

Result<void> AccountRepository::saveCatalogDiscoveryPayload(const QString& accountId,
                                                            const QString& storefront,
                                                            const QString& payload)
{
    if (accountId.isEmpty() || storefront.isEmpty() || payload.isEmpty()) {
        return tx::notFound(QStringLiteral("empty account, region or payload"));
    }
    if (payload.size() > kMaxDiscoveryPayloadCharacters) {
        return tx::notFound(QStringLiteral("discovery payload is too large"));
    }
    if (const Result<void> profile = account_rows::ensureAccountProfile(m_connection, accountId); !profile) {
        return profile;
    }

    auto prepared = m_connection.prepare(
        "INSERT INTO CatalogDiscoveryCache (AccountId, Storefront, Payload, FetchedAtUtc) "
        "VALUES (?, ?, ?, datetime('now')) ON CONFLICT(AccountId, Storefront) DO UPDATE SET "
        "Payload=excluded.Payload, FetchedAtUtc=excluded.FetchedAtUtc;");
    if (!prepared) {
        return prepared.failure();
    }
    prepared.value().bindText(1, accountId);
    prepared.value().bindText(2, storefront);
    prepared.value().bindText(3, payload);
    return prepared.value().run();
}

Result<void> AccountRepository::clearAccountData(const QString& accountId, AccountDataClearMode mode)
{
    if (accountId.isEmpty()) {
        return tx::notFound(QStringLiteral("empty account id"));
    }
    if (const Result<void> begun = m_connection.exec("BEGIN IMMEDIATE;"); !begun) {
        return begun;
    }

    auto clearActive = m_connection.prepare(
        "UPDATE ActiveAccountContext SET RemoteMode='LocalOnly', AccountId='' WHERE SingletonId=1 AND AccountId=?;");
    if (!clearActive) {
        return tx::rollbackWith(m_connection, clearActive.failure());
    }
    auto clearProfile = m_connection.prepare("DELETE FROM AccountProfiles WHERE AccountId=?;");
    if (!clearProfile) {
        return tx::rollbackWith(m_connection, clearProfile.failure());
    }
    clearActive.value().bindText(1, accountId);
    clearProfile.value().bindText(1, accountId);
    if (const Result<void> ran = clearActive.value().run(); !ran) {
        return tx::rollbackWith(m_connection, ran.failure());
    }
    if (const Result<void> ran = clearProfile.value().run(); !ran) {
        return tx::rollbackWith(m_connection, ran.failure());
    }

    if (mode == AccountDataClearMode::RestoreActiveContext) {
        if (const Result<void> profile = account_rows::ensureAccountProfile(m_connection, accountId); !profile) {
            return tx::rollbackWith(m_connection, profile.failure());
        }
        auto restoreActive = m_connection.prepare(
            "INSERT INTO ActiveAccountContext (SingletonId, RemoteMode, AccountId) VALUES (1, 'Account', ?) "
            "ON CONFLICT(SingletonId) DO UPDATE SET RemoteMode='Account', AccountId=excluded.AccountId;");
        if (!restoreActive) {
            return tx::rollbackWith(m_connection, restoreActive.failure());
        }
        restoreActive.value().bindText(1, accountId);
        if (const Result<void> ran = restoreActive.value().run(); !ran) {
            return tx::rollbackWith(m_connection, ran.failure());
        }
    }

    return account_rows::commitOrRollback(m_connection);
}

Result<void> AccountRepository::clearAllAccountData()
{
    if (const Result<void> begun = m_connection.exec("BEGIN IMMEDIATE;"); !begun) {
        return begun;
    }
    if (const Result<void> cleared = m_connection.exec(
            "UPDATE ActiveAccountContext SET RemoteMode='LocalOnly', AccountId='' WHERE SingletonId=1; DELETE FROM AccountProfiles;");
        !cleared) {
        return tx::rollbackWith(m_connection, cleared.failure());
    }
    return account_rows::commitOrRollback(m_connection);
}

bool AccountRepository::isAccountDataStoreReady() const noexcept
{
    return m_database.isOpen();
}

std::optional<std::size_t> AccountRepository::recoverablePendingChangeCount(const QString& accountId) const
{
    return AccountOutboxRepository(m_database).recoverablePendingChangeCount(accountId);
}

Result<void> AccountRepository::removeSignedOutAccountData(const QString& accountId)
{
    return clearAccountData(accountId, AccountDataClearMode::LeaveInactive);
}

} // namespace lmp::core::db
