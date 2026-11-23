#pragma once

#include "core/account/AccountMusicContract.h"
#include "core/async/Result.h"

#include <QByteArray>
#include <QFuture>
#include <QString>
#include <QStringList>

namespace lmp::core::account {

// The account calls library sync makes, kept narrow so the sync service can
// be tested with a fake. AccountClient implements it. Futures may finish on
// another thread; the caller decides where to continue.
class AccountSyncTransport {
public:
    virtual ~AccountSyncTransport() = default;

    // GET /v1/music/library. The parsed answer; `valid` is false when the
    // body was not a library.
    [[nodiscard]] virtual QFuture<Result<ParsedAccountLibrary>> fetchLibrary(const QString& bearerSession) = 0;

    // POST /v1/music/liked {"track": ...} when liking; DELETE
    // /v1/music/liked?id=<remote id> when not.
    [[nodiscard]] virtual QFuture<Result<void>> setLiked(const QString& bearerSession, const QByteArray& trackJson,
                                                         bool liked, const QString& remoteId) = 0;

    // POST /v1/music/history/batch. The submitted event ids the service
    // accepted or already had.
    [[nodiscard]] virtual QFuture<Result<QStringList>> postHistoryBatch(const QString& bearerSession,
                                                                        const QByteArray& batchJson,
                                                                        const QStringList& submittedEventIds) = 0;

    // GET and PUT /v1/music/preferences. The preferences in the answer.
    [[nodiscard]] virtual QFuture<Result<PortablePreferences>> fetchPreferences(const QString& bearerSession) = 0;
    [[nodiscard]] virtual QFuture<Result<PortablePreferences>> putPreferences(const QString& bearerSession,
                                                                              const QByteArray& preferencesJson) = 0;

protected:
    AccountSyncTransport() = default;
    AccountSyncTransport(const AccountSyncTransport&) = default;
    AccountSyncTransport& operator=(const AccountSyncTransport&) = default;
};

} // namespace lmp::core::account
