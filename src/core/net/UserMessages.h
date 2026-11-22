#pragma once

#include "core/async/Result.h"

#include <QJsonValue>
#include <QString>

#include <optional>

// The only error text the app shows. A server's own message, a network error
// string or a Failure's detail never reaches the screen: they can name hosts
// and internal services, or read as developer output. The one exception is
// planRefusalText.
namespace lmp::core::net {

enum class UserMessage {
    Connection,
    Timeout,
    Unavailable,
    SessionExpired,
    Forbidden,
    NotFound,
    Conflict,
    RateLimited,
    Generic,
    SignInFailed,
};

// The translated sentence for a message.
[[nodiscard]] QString userMessageText(UserMessage message);

// 401 session expired, 403 forbidden, 404 and 410 not found, 408 timeout,
// 409 conflict, 429 rate limited, 500 and above unavailable, anything else
// generic.
[[nodiscard]] UserMessage userMessageForStatus(int status);

// No connection is Connection and a timeout is Timeout; any other failure
// that carries an HTTP status follows userMessageForStatus; the rest follow
// their kind (NotAvailable and Server read as Unavailable; Storage,
// InvalidResponse, Cancelled and Unknown as Generic).
[[nodiscard]] UserMessage userMessageForFailure(const Failure& failure);

// What a failed browser sign-in shows: Connection when the service could not
// be reached, nothing when it was cancelled, otherwise SignInFailed.
[[nodiscard]] std::optional<UserMessage> userMessageForSignInFailure(const Failure& failure);

// The server's own sentence when an account's plan refuses something. Unlike
// other server text it is written for members and names no service, and only
// the codes "plan_required" and "plan_limit_reached" mark it. Nothing for
// any other payload or an empty sentence.
[[nodiscard]] std::optional<QString> planRefusalText(const QJsonValue& payload);

} // namespace lmp::core::net
