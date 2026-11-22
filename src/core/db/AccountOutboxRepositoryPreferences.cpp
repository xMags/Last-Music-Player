#include "core/db/AccountOutboxRepository.h"

#include "core/db/AccountRows.h"
#include "core/db/SqlTransaction.h"

#include <algorithm>
#include <initializer_list>
#include <utility>

// Preference values, the pending counts and the retry schedule.
namespace lmp::core::db {

namespace {

namespace tx = sql_transaction;

constexpr char kSavePendingPreferenceSql[] =
    "INSERT INTO AccountPreferences (AccountId, PreferenceKey, ValueJson, Revision, Pending, UpdatedAtUtc) "
    "VALUES (?, ?, ?, 1, 1, datetime('now')) ON CONFLICT(AccountId, PreferenceKey) DO UPDATE SET "
    "ValueJson=excluded.ValueJson, Revision=AccountPreferences.Revision+1, Pending=1, AttemptCount=0, "
    "NextAttemptAtUnix=0, LastFailureCategory='', IsBlocked=0, UpdatedAtUtc=datetime('now') "
    "RETURNING Revision;";

constexpr char kMergeRemotePreferenceSql[] =
    "INSERT INTO AccountPreferences (AccountId, PreferenceKey, ValueJson, Revision, Pending, UpdatedAtUtc) "
    "VALUES (?, ?, ?, 0, 0, datetime('now')) ON CONFLICT(AccountId, PreferenceKey) DO UPDATE SET "
    "ValueJson=excluded.ValueJson, UpdatedAtUtc=datetime('now') "
    "WHERE AccountPreferences.Revision=? AND AccountPreferences.Pending=0;";

constexpr char kAcknowledgePreferenceSql[] =
    "UPDATE AccountPreferences SET Pending=0, AttemptCount=0, NextAttemptAtUnix=0, "
    "LastFailureCategory='', IsBlocked=0 WHERE AccountId=? AND PreferenceKey=? AND Revision=?;";

constexpr char kMarkPreferencesFailedSql[] =
    "UPDATE AccountPreferences SET AttemptCount=AttemptCount+1, "
    "NextAttemptAtUnix=CASE WHEN ? THEN 0 ELSE unixepoch()+? END, "
    "LastFailureCategory=?, IsBlocked=? WHERE AccountId=? AND PreferenceKey=? AND Pending=1;";

constexpr char kPendingCountsSql[] =
    "SELECT "
    "(SELECT count(*) FROM PlaybackEvents WHERE AccountId=? AND IsBlocked=0),"
    "(SELECT count(*) FROM PlaybackEvents WHERE AccountId=? AND IsBlocked=1),"
    "(SELECT count(*) FROM PendingLikes WHERE AccountId=? AND IsBlocked=0),"
    "(SELECT count(*) FROM PendingLikes WHERE AccountId=? AND IsBlocked=1),"
    "(SELECT count(*) FROM AccountPreferences WHERE AccountId=? AND Pending=1 AND IsBlocked=0),"
    "(SELECT count(*) FROM AccountPreferences WHERE AccountId=? AND Pending=1 AND IsBlocked=1);";

constexpr char kRecoverableCountSql[] =
    "SELECT "
    "(SELECT count(*) FROM PlaybackEvents WHERE AccountId=? AND IsBlocked=0) + "
    "(SELECT count(*) FROM PendingLikes WHERE AccountId=? AND IsBlocked=0) + "
    "(SELECT count(*) FROM AccountPreferences "
    "WHERE AccountId=? AND Pending=1 AND IsBlocked=0);";

const char* nextRetrySql(AccountRetryQueue queue)
{
    switch (queue) {
    case AccountRetryQueue::History:
        return "SELECT MIN(NextAttemptAtUnix)-unixepoch() FROM PlaybackEvents "
               "WHERE AccountId=? AND IsBlocked=0 AND NextAttemptAtUnix>unixepoch();";
    case AccountRetryQueue::Likes:
        return "SELECT MIN(NextAttemptAtUnix)-unixepoch() FROM PendingLikes "
               "WHERE AccountId=? AND IsBlocked=0 AND NextAttemptAtUnix>unixepoch();";
    case AccountRetryQueue::Preferences:
        return "SELECT MIN(NextAttemptAtUnix)-unixepoch() FROM AccountPreferences "
               "WHERE AccountId=? AND Pending=1 AND IsBlocked=0 AND NextAttemptAtUnix>unixepoch();";
    }
    return nullptr;
}

} // namespace

QList<AccountPreferenceRecord> AccountOutboxRepository::loadAccountPreferences(const QString& accountId,
                                                                               OutboxRows rows) const
{
    QList<AccountPreferenceRecord> result;
    if (accountId.isEmpty()) {
        return result;
    }
    const char* const sql = rows == OutboxRows::DueOnly
        ? "SELECT AccountId, PreferenceKey, ValueJson, Revision, Pending, AttemptCount, NextAttemptAtUnix, "
          "COALESCE(LastFailureCategory,''), IsBlocked FROM AccountPreferences WHERE AccountId=? AND Pending=1 "
          "AND IsBlocked=0 AND NextAttemptAtUnix<=unixepoch() ORDER BY PreferenceKey;"
        : "SELECT AccountId, PreferenceKey, ValueJson, Revision, Pending, AttemptCount, NextAttemptAtUnix, "
          "COALESCE(LastFailureCategory,''), IsBlocked FROM AccountPreferences WHERE AccountId=? ORDER BY PreferenceKey;";
    auto prepared = m_connection.prepare(sql);
    if (!prepared) {
        return result;
    }
    SqliteStatement& statement = prepared.value();
    statement.bindText(1, accountId);
    while (statement.step().valueOr(false)) {
        AccountPreferenceRecord record;
        record.accountId = statement.columnText(0);
        record.key = statement.columnText(1);
        record.valueJson = statement.columnText(2);
        record.revision = statement.columnInt64(3);
        record.pending = account_rows::columnInt(statement, 4) != 0;
        record.attemptCount = account_rows::columnInt(statement, 5);
        record.nextAttemptAtUnix = statement.columnInt64(6);
        record.lastFailureCategory = statement.columnText(7);
        record.blocked = account_rows::columnInt(statement, 8) != 0;
        result.append(std::move(record));
    }
    return result;
}

Result<qint64> AccountOutboxRepository::savePendingAccountPreference(const QString& accountId,
                                                                     const QString& key,
                                                                     const QString& valueJson)
{
    if (accountId.isEmpty() || key.isEmpty() || valueJson.isEmpty()) {
        return tx::notFound(QStringLiteral("empty account, key or value"));
    }
    if (const Result<void> profile = account_rows::ensureAccountProfile(m_connection, accountId); !profile) {
        return profile.failure();
    }
    auto prepared = m_connection.prepare(kSavePendingPreferenceSql);
    if (!prepared) {
        return prepared.failure();
    }
    SqliteStatement& statement = prepared.value();
    statement.bindText(1, accountId);
    statement.bindText(2, key);
    statement.bindText(3, valueJson);
    const Result<bool> stepped = statement.step();
    if (!stepped) {
        return stepped.failure();
    }
    if (!stepped.value()) {
        return tx::notFound(QStringLiteral("no revision returned"));
    }
    return statement.columnInt64(0);
}

Result<void> AccountOutboxRepository::mergeRemoteAccountPreference(const QString& accountId,
                                                                   const QString& key,
                                                                   const QString& valueJson,
                                                                   qint64 observedRevision)
{
    if (accountId.isEmpty() || key.isEmpty() || valueJson.isEmpty()) {
        return tx::notFound(QStringLiteral("empty account, key or value"));
    }
    if (const Result<void> profile = account_rows::ensureAccountProfile(m_connection, accountId); !profile) {
        return profile;
    }
    auto prepared = m_connection.prepare(kMergeRemotePreferenceSql);
    if (!prepared) {
        return prepared.failure();
    }
    SqliteStatement& statement = prepared.value();
    statement.bindText(1, accountId);
    statement.bindText(2, key);
    statement.bindText(3, valueJson);
    statement.bindInt64(4, observedRevision);
    return statement.run();
}

Result<void> AccountOutboxRepository::acknowledgeAccountPreference(const QString& accountId,
                                                                   const QString& key,
                                                                   qint64 revision)
{
    if (accountId.isEmpty() || key.isEmpty() || revision <= 0) {
        return tx::notFound(QStringLiteral("empty account or key, or no revision"));
    }
    auto prepared = m_connection.prepare(kAcknowledgePreferenceSql);
    if (!prepared) {
        return prepared.failure();
    }
    prepared.value().bindText(1, accountId);
    prepared.value().bindText(2, key);
    prepared.value().bindInt64(3, revision);
    return prepared.value().run();
}

Result<void> AccountOutboxRepository::markAccountPreferencesFailed(const QString& accountId,
                                                                   const QStringList& keys,
                                                                   const OutboxFailureMark& failure)
{
    return markEachFailed(kMarkPreferencesFailedSql, accountId, keys, failure);
}

AccountPendingCounts AccountOutboxRepository::loadAccountPendingCounts(const QString& accountId) const
{
    AccountPendingCounts counts;
    if (accountId.isEmpty()) {
        return counts;
    }
    auto prepared = m_connection.prepare(kPendingCountsSql);
    if (!prepared) {
        return counts;
    }
    SqliteStatement& statement = prepared.value();
    for (int index = 1; index <= 6; ++index) {
        statement.bindText(index, accountId);
    }
    if (statement.step().valueOr(false)) {
        counts.pendingHistory = account_rows::columnInt(statement, 0);
        counts.blockedHistory = account_rows::columnInt(statement, 1);
        counts.pendingLikes = account_rows::columnInt(statement, 2);
        counts.blockedLikes = account_rows::columnInt(statement, 3);
        counts.pendingPreferences = account_rows::columnInt(statement, 4);
        counts.blockedPreferences = account_rows::columnInt(statement, 5);
    }
    return counts;
}

std::optional<int> AccountOutboxRepository::secondsUntilNextAccountRetry(const QString& accountId,
                                                                         AccountRetryQueue queue) const
{
    const char* const sql = nextRetrySql(queue);
    if (accountId.isEmpty() || sql == nullptr) {
        return std::nullopt;
    }
    auto prepared = m_connection.prepare(sql);
    if (!prepared) {
        return std::nullopt;
    }
    SqliteStatement& statement = prepared.value();
    statement.bindText(1, accountId);
    if (!statement.step().valueOr(false) || statement.columnIsNull(0)) {
        return std::nullopt;
    }
    return std::max(1, account_rows::columnInt(statement, 0));
}

Result<void> AccountOutboxRepository::resetSchedule(const QString& accountId,
                                                    const char* historySql,
                                                    const char* likesSql,
                                                    const char* preferencesSql)
{
    if (accountId.isEmpty()) {
        return tx::notFound(QStringLiteral("empty account id"));
    }
    if (const Result<void> begun = m_connection.exec("BEGIN IMMEDIATE;"); !begun) {
        return begun;
    }
    // All three are prepared before any runs, as before.
    auto history = m_connection.prepare(historySql);
    auto likes = m_connection.prepare(likesSql);
    auto preferences = m_connection.prepare(preferencesSql);
    for (const Result<SqliteStatement>* prepared : {&history, &likes, &preferences}) {
        if (!*prepared) {
            return tx::rollbackWith(m_connection, prepared->failure());
        }
    }
    for (Result<SqliteStatement>* prepared : {&history, &likes, &preferences}) {
        SqliteStatement& statement = prepared->value();
        statement.bindText(1, accountId);
        if (const Result<void> ran = statement.run(); !ran) {
            return tx::rollbackWith(m_connection, ran.failure());
        }
    }
    return account_rows::commitOrRollback(m_connection);
}

Result<void> AccountOutboxRepository::resetAccountRetrySchedule(const QString& accountId)
{
    return resetSchedule(
        accountId,
        "UPDATE PlaybackEvents SET NextAttemptAtUnix=0, LastFailureCategory='', IsBlocked=0 WHERE AccountId=?;",
        "UPDATE PendingLikes SET NextAttemptAtUnix=0, LastFailureCategory='', IsBlocked=0 WHERE AccountId=?;",
        "UPDATE AccountPreferences SET NextAttemptAtUnix=0, LastFailureCategory='', IsBlocked=0 "
        "WHERE AccountId=? AND Pending=1;");
}

Result<void> AccountOutboxRepository::resetRecoverableAccountRetrySchedule(const QString& accountId)
{
    return resetSchedule(accountId,
                         "UPDATE PlaybackEvents SET NextAttemptAtUnix=0, LastFailureCategory='' "
                         "WHERE AccountId=? AND IsBlocked=0;",
                         "UPDATE PendingLikes SET NextAttemptAtUnix=0, LastFailureCategory='' "
                         "WHERE AccountId=? AND IsBlocked=0;",
                         "UPDATE AccountPreferences SET NextAttemptAtUnix=0, LastFailureCategory='' "
                         "WHERE AccountId=? AND Pending=1 AND IsBlocked=0;");
}

std::optional<std::size_t> AccountOutboxRepository::recoverablePendingChangeCount(const QString& accountId) const
{
    if (accountId.isEmpty()) {
        return std::nullopt;
    }
    auto prepared = m_connection.prepare(kRecoverableCountSql);
    if (!prepared) {
        return std::nullopt;
    }
    SqliteStatement& statement = prepared.value();
    statement.bindText(1, accountId);
    statement.bindText(2, accountId);
    statement.bindText(3, accountId);
    if (!statement.step().valueOr(false)) {
        return std::nullopt;
    }
    return static_cast<std::size_t>(std::max(qint64{0}, statement.columnInt64(0)));
}

} // namespace lmp::core::db
