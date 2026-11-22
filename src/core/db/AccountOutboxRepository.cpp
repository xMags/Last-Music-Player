#include "core/db/AccountOutboxRepository.h"

#include "core/db/AccountRows.h"
#include "core/db/AccountTrackJson.h"
#include "core/db/SqlTransaction.h"

#include <algorithm>
#include <utility>

// Playback history and like requests, and the shared per-id transaction
// helpers. Preferences, counts and the retry schedule are in
// AccountOutboxRepositoryPreferences.cpp.
namespace lmp::core::db {

namespace {

namespace tx = sql_transaction;

constexpr char kEnqueuePlaybackSql[] =
    "INSERT OR IGNORE INTO PlaybackEvents (EventId, AccountId, RemoteId, TrackJson, PlayedAtUtc, PositionSeconds) "
    "VALUES (?, ?, ?, ?, ?, ?);";

constexpr char kMarkPlaybackAttemptedSql[] =
    "UPDATE PlaybackEvents SET AttemptCount=AttemptCount+1, LastAttemptAtUtc=datetime('now') WHERE AccountId=? AND EventId=?;";

constexpr char kMarkPlaybackFailedSql[] =
    "UPDATE PlaybackEvents SET AttemptCount=AttemptCount+1, LastAttemptAtUtc=datetime('now'), "
    "NextAttemptAtUnix=CASE WHEN ? THEN 0 ELSE unixepoch()+? END, LastFailureCategory=?, IsBlocked=? "
    "WHERE AccountId=? AND EventId=?;";

constexpr char kAcknowledgePlaybackSql[] = "DELETE FROM PlaybackEvents WHERE AccountId=? AND EventId=?;";

constexpr char kEnqueueLikeSql[] =
    "INSERT INTO PendingLikes (AccountId, RemoteId, DesiredState, TrackJson, UpdatedAtUtc, AttemptCount, LastAttemptAtUtc) "
    "VALUES (?, ?, ?, ?, ?, 0, NULL) ON CONFLICT(AccountId, RemoteId) DO UPDATE SET "
    "DesiredState=excluded.DesiredState, TrackJson=excluded.TrackJson, UpdatedAtUtc=excluded.UpdatedAtUtc, "
    "AttemptCount=0, LastAttemptAtUtc=NULL, NextAttemptAtUnix=0, LastFailureCategory='', IsBlocked=0;";

constexpr char kMarkLikeAttemptedSql[] =
    "UPDATE PendingLikes SET AttemptCount=AttemptCount+1, LastAttemptAtUtc=datetime('now') WHERE AccountId=? AND RemoteId=?;";

constexpr char kMarkLikeFailedSql[] =
    "UPDATE PendingLikes SET AttemptCount=AttemptCount+1, LastAttemptAtUtc=datetime('now'), "
    "NextAttemptAtUnix=CASE WHEN ? THEN 0 ELSE unixepoch()+? END, LastFailureCategory=?, IsBlocked=? "
    "WHERE AccountId=? AND RemoteId=?;";

constexpr char kAcknowledgeLikeSql[] = "DELETE FROM PendingLikes WHERE AccountId=? AND RemoteId=? AND DesiredState=?;";

// Binds ?1 to ?4 of a "mark failed" statement; the account and id follow.
void bindFailure(SqliteStatement& statement, const OutboxFailureMark& failure)
{
    const qint64 blocked = failure.blocked ? 1 : 0;
    statement.bindInt64(1, blocked);
    statement.bindInt64(2, std::max(0, failure.retryDelaySeconds));
    statement.bindText(3, failure.category);
    statement.bindInt64(4, blocked);
}

} // namespace

AccountOutboxRepository::AccountOutboxRepository(Database& database)
    : m_connection(database.connection())
{
}

Result<void> AccountOutboxRepository::updateEach(const char* sql, const QString& accountId, const QStringList& ids)
{
    if (accountId.isEmpty()) {
        return tx::notFound(QStringLiteral("empty account id"));
    }
    if (const Result<void> begun = m_connection.exec("BEGIN IMMEDIATE;"); !begun) {
        return begun;
    }
    auto prepared = m_connection.prepare(sql);
    if (!prepared) {
        return tx::rollbackWith(m_connection, prepared.failure());
    }
    SqliteStatement& statement = prepared.value();
    for (const QString& id : ids) {
        statement.reset();
        statement.clearBindings();
        statement.bindText(1, accountId);
        statement.bindText(2, id);
        if (const Result<void> ran = statement.run(); !ran) {
            return tx::rollbackWith(m_connection, ran.failure());
        }
    }
    return account_rows::commitOrRollback(m_connection);
}

Result<void> AccountOutboxRepository::markEachFailed(const char* sql,
                                                     const QString& accountId,
                                                     const QStringList& ids,
                                                     const OutboxFailureMark& failure)
{
    if (accountId.isEmpty()) {
        return tx::notFound(QStringLiteral("empty account id"));
    }
    if (const Result<void> begun = m_connection.exec("BEGIN IMMEDIATE;"); !begun) {
        return begun;
    }
    auto prepared = m_connection.prepare(sql);
    if (!prepared) {
        return tx::rollbackWith(m_connection, prepared.failure());
    }
    SqliteStatement& statement = prepared.value();
    for (const QString& id : ids) {
        statement.reset();
        statement.clearBindings();
        bindFailure(statement, failure);
        statement.bindText(5, accountId);
        statement.bindText(6, id);
        if (const Result<void> ran = statement.run(); !ran) {
            return tx::rollbackWith(m_connection, ran.failure());
        }
    }
    return account_rows::commitOrRollback(m_connection);
}

Result<void> AccountOutboxRepository::enqueuePlaybackEvent(const PlaybackEventRecord& event)
{
    if (event.eventId.isEmpty() || event.accountId.isEmpty() || event.remoteTrackId.isEmpty() || !event.track) {
        return tx::notFound(QStringLiteral("playback event needs an id, an account, a song id and a song"));
    }
    if (const Result<void> profile = account_rows::ensureAccountProfile(m_connection, event.accountId); !profile) {
        return profile;
    }
    auto prepared = m_connection.prepare(kEnqueuePlaybackSql);
    if (!prepared) {
        return prepared.failure();
    }
    SqliteStatement& statement = prepared.value();
    statement.bindText(1, event.eventId);
    statement.bindText(2, event.accountId);
    statement.bindText(3, event.remoteTrackId);
    statement.bindText(4, account_track_json::serialize(*event.track));
    statement.bindText(5, event.playedAtUtc);
    statement.bindDouble(6, event.positionSeconds);
    return statement.run();
}

QList<PlaybackEventRecord> AccountOutboxRepository::loadPendingPlaybackEvents(const QString& accountId,
                                                                              int limit,
                                                                              OutboxRows rows) const
{
    QList<PlaybackEventRecord> result;
    if (accountId.isEmpty() || limit <= 0) {
        return result;
    }

    const char* const sql = rows == OutboxRows::DueOnly
        ? "SELECT EventId, AccountId, RemoteId, TrackJson, PlayedAtUtc, PositionSeconds, AttemptCount, "
          "COALESCE(NextAttemptAtUnix,0), COALESCE(LastFailureCategory,''), COALESCE(IsBlocked,0) "
          "FROM PlaybackEvents WHERE AccountId=? AND COALESCE(IsBlocked,0)=0 "
          "AND COALESCE(NextAttemptAtUnix,0)<=unixepoch() ORDER BY PlayedAtUtc, EventId LIMIT ?;"
        : "SELECT EventId, AccountId, RemoteId, TrackJson, PlayedAtUtc, PositionSeconds, AttemptCount, "
          "COALESCE(NextAttemptAtUnix,0), COALESCE(LastFailureCategory,''), COALESCE(IsBlocked,0) "
          "FROM PlaybackEvents WHERE AccountId=? ORDER BY PlayedAtUtc, EventId LIMIT ?;";
    auto prepared = m_connection.prepare(sql);
    if (!prepared) {
        return result;
    }
    SqliteStatement& statement = prepared.value();
    statement.bindText(1, accountId);
    statement.bindInt64(2, limit);
    while (statement.step().valueOr(false)) {
        PlaybackEventRecord event;
        event.eventId = statement.columnText(0);
        event.accountId = statement.columnText(1);
        event.remoteTrackId = statement.columnText(2);
        event.trackJson = statement.columnText(3);
        event.track = account_track_json::deserialize(event.trackJson);
        event.playedAtUtc = statement.columnText(4);
        event.positionSeconds = statement.columnDouble(5);
        event.attemptCount = account_rows::columnInt(statement, 6);
        event.nextAttemptAtUnix = statement.columnInt64(7);
        event.lastFailureCategory = statement.columnText(8);
        event.blocked = account_rows::columnInt(statement, 9) != 0;
        result.append(std::move(event));
    }
    return result;
}

Result<void> AccountOutboxRepository::markPlaybackEventsAttempted(const QString& accountId, const QStringList& eventIds)
{
    return updateEach(kMarkPlaybackAttemptedSql, accountId, eventIds);
}

Result<void> AccountOutboxRepository::markPlaybackEventsFailed(const QString& accountId,
                                                               const QStringList& eventIds,
                                                               const OutboxFailureMark& failure)
{
    return markEachFailed(kMarkPlaybackFailedSql, accountId, eventIds, failure);
}

Result<void> AccountOutboxRepository::acknowledgePlaybackEvents(const QString& accountId, const QStringList& eventIds)
{
    return updateEach(kAcknowledgePlaybackSql, accountId, eventIds);
}

Result<void> AccountOutboxRepository::enqueuePendingLike(const PendingLikeRecord& like)
{
    if (like.accountId.isEmpty() || like.remoteTrackId.isEmpty() || !like.track) {
        return tx::notFound(QStringLiteral("pending like needs an account, a song id and a song"));
    }
    if (const Result<void> profile = account_rows::ensureAccountProfile(m_connection, like.accountId); !profile) {
        return profile;
    }
    auto prepared = m_connection.prepare(kEnqueueLikeSql);
    if (!prepared) {
        return prepared.failure();
    }
    SqliteStatement& statement = prepared.value();
    statement.bindText(1, like.accountId);
    statement.bindText(2, like.remoteTrackId);
    statement.bindInt64(3, like.desiredState ? 1 : 0);
    statement.bindText(4, account_track_json::serialize(*like.track));
    statement.bindText(5, like.updatedAtUtc);
    return statement.run();
}

QList<PendingLikeRecord> AccountOutboxRepository::loadPendingLikes(const QString& accountId,
                                                                   int limit,
                                                                   OutboxRows rows) const
{
    QList<PendingLikeRecord> result;
    if (accountId.isEmpty() || limit <= 0) {
        return result;
    }

    const char* const sql = rows == OutboxRows::DueOnly
        ? "SELECT AccountId, RemoteId, DesiredState, TrackJson, UpdatedAtUtc, AttemptCount, "
          "COALESCE(NextAttemptAtUnix,0), COALESCE(LastFailureCategory,''), COALESCE(IsBlocked,0) "
          "FROM PendingLikes WHERE AccountId=? AND COALESCE(IsBlocked,0)=0 "
          "AND COALESCE(NextAttemptAtUnix,0)<=unixepoch() ORDER BY UpdatedAtUtc, RemoteId LIMIT ?;"
        : "SELECT AccountId, RemoteId, DesiredState, TrackJson, UpdatedAtUtc, AttemptCount, "
          "COALESCE(NextAttemptAtUnix,0), COALESCE(LastFailureCategory,''), COALESCE(IsBlocked,0) "
          "FROM PendingLikes WHERE AccountId=? ORDER BY UpdatedAtUtc, RemoteId LIMIT ?;";
    auto prepared = m_connection.prepare(sql);
    if (!prepared) {
        return result;
    }
    SqliteStatement& statement = prepared.value();
    statement.bindText(1, accountId);
    statement.bindInt64(2, limit);
    while (statement.step().valueOr(false)) {
        PendingLikeRecord like;
        like.accountId = statement.columnText(0);
        like.remoteTrackId = statement.columnText(1);
        like.desiredState = account_rows::columnInt(statement, 2) != 0;
        like.trackJson = statement.columnText(3);
        like.track = account_track_json::deserialize(like.trackJson);
        like.updatedAtUtc = statement.columnText(4);
        like.attemptCount = account_rows::columnInt(statement, 5);
        like.nextAttemptAtUnix = statement.columnInt64(6);
        like.lastFailureCategory = statement.columnText(7);
        like.blocked = account_rows::columnInt(statement, 8) != 0;
        result.append(std::move(like));
    }
    return result;
}

Result<void> AccountOutboxRepository::markPendingLikeAttempted(const QString& accountId, const QString& remoteTrackId)
{
    if (accountId.isEmpty() || remoteTrackId.isEmpty()) {
        return tx::notFound(QStringLiteral("empty account or song id"));
    }
    auto prepared = m_connection.prepare(kMarkLikeAttemptedSql);
    if (!prepared) {
        return prepared.failure();
    }
    prepared.value().bindText(1, accountId);
    prepared.value().bindText(2, remoteTrackId);
    return prepared.value().run();
}

Result<void> AccountOutboxRepository::markPendingLikeFailed(const QString& accountId,
                                                            const QString& remoteTrackId,
                                                            const OutboxFailureMark& failure)
{
    if (accountId.isEmpty() || remoteTrackId.isEmpty()) {
        return tx::notFound(QStringLiteral("empty account or song id"));
    }
    auto prepared = m_connection.prepare(kMarkLikeFailedSql);
    if (!prepared) {
        return prepared.failure();
    }
    bindFailure(prepared.value(), failure);
    prepared.value().bindText(5, accountId);
    prepared.value().bindText(6, remoteTrackId);
    return prepared.value().run();
}

Result<void> AccountOutboxRepository::acknowledgePendingLike(const QString& accountId,
                                                             const QString& remoteTrackId,
                                                             bool desiredState)
{
    if (accountId.isEmpty() || remoteTrackId.isEmpty()) {
        return tx::notFound(QStringLiteral("empty account or song id"));
    }
    auto prepared = m_connection.prepare(kAcknowledgeLikeSql);
    if (!prepared) {
        return prepared.failure();
    }
    prepared.value().bindText(1, accountId);
    prepared.value().bindText(2, remoteTrackId);
    prepared.value().bindInt64(3, desiredState ? 1 : 0);
    return prepared.value().run();
}

} // namespace lmp::core::db
