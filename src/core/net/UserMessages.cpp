#include "core/net/UserMessages.h"

#include <QCoreApplication>
#include <QJsonObject>
#include <QLatin1StringView>

namespace lmp::core::net {

QString userMessageText(UserMessage message)
{
    switch (message) {
    case UserMessage::Connection:
        return QCoreApplication::translate("UserMessages",
                                           "Could not reach Last Music. Check your connection and try again.");
    case UserMessage::Timeout:
        return QCoreApplication::translate("UserMessages", "The request took too long. Try again.");
    case UserMessage::Unavailable:
        return QCoreApplication::translate("UserMessages", "Last Music is unavailable right now. Try again shortly.");
    case UserMessage::SessionExpired:
        return QCoreApplication::translate("UserMessages", "Your session has expired. Sign in again.");
    case UserMessage::Forbidden:
        return QCoreApplication::translate("UserMessages", "You don't have permission to do that.");
    case UserMessage::NotFound:
        return QCoreApplication::translate("UserMessages", "That is no longer available.");
    case UserMessage::Conflict:
        return QCoreApplication::translate("UserMessages", "That changed in the meantime. Refresh and try again.");
    case UserMessage::RateLimited:
        return QCoreApplication::translate("UserMessages", "Too many requests. Wait a moment and try again.");
    case UserMessage::Generic:
        return QCoreApplication::translate("UserMessages", "Something went wrong. Try again.");
    case UserMessage::SignInFailed:
        return QCoreApplication::translate("UserMessages", "Sign-in could not be completed. Try again.");
    }
    return QCoreApplication::translate("UserMessages", "Something went wrong. Try again.");
}

UserMessage userMessageForStatus(int status)
{
    if (status == 401) {
        return UserMessage::SessionExpired;
    }
    if (status == 403) {
        return UserMessage::Forbidden;
    }
    if (status == 404 || status == 410) {
        return UserMessage::NotFound;
    }
    if (status == 408) {
        return UserMessage::Timeout;
    }
    if (status == 409) {
        return UserMessage::Conflict;
    }
    if (status == 429) {
        return UserMessage::RateLimited;
    }
    if (status >= 500) {
        return UserMessage::Unavailable;
    }
    return UserMessage::Generic;
}

QString userMessageOr(const Failure& failure, const QString& fallback)
{
    const bool fromRequest =
        failure.kind == FailureKind::Network || failure.kind == FailureKind::Timeout || failure.httpStatus != 0;
    return fromRequest ? userMessageText(userMessageForFailure(failure)) : fallback;
}

UserMessage userMessageForFailure(const Failure& failure)
{
    // A transport failure can come with the status of a response whose body
    // never arrived; what the user needs to hear is still the transport.
    if (failure.kind == FailureKind::Network) {
        return UserMessage::Connection;
    }
    if (failure.kind == FailureKind::Timeout) {
        return UserMessage::Timeout;
    }
    if (failure.httpStatus != 0) {
        return userMessageForStatus(failure.httpStatus);
    }
    switch (failure.kind) {
    case FailureKind::Unauthorized:
        return UserMessage::SessionExpired;
    case FailureKind::Forbidden:
        return UserMessage::Forbidden;
    case FailureKind::NotFound:
        return UserMessage::NotFound;
    case FailureKind::RateLimited:
        return UserMessage::RateLimited;
    case FailureKind::Server:
    case FailureKind::NotAvailable:
        return UserMessage::Unavailable;
    case FailureKind::Network:
    case FailureKind::Timeout:
    case FailureKind::InvalidResponse:
    case FailureKind::Cancelled:
    case FailureKind::Storage:
    case FailureKind::Unknown:
        break;
    }
    return UserMessage::Generic;
}

std::optional<UserMessage> userMessageForSignInFailure(const Failure& failure)
{
    if (failure.kind == FailureKind::Cancelled) {
        return std::nullopt;
    }
    if (failure.kind == FailureKind::Network) {
        return UserMessage::Connection;
    }
    return UserMessage::SignInFailed;
}

std::optional<QString> planRefusalText(const QJsonValue& payload)
{
    if (!payload.isObject()) {
        return std::nullopt;
    }
    const QJsonObject object = payload.toObject();
    const QJsonValue code = object.value(QLatin1StringView("code"));
    if (code != QJsonValue(QStringLiteral("plan_required")) && code != QJsonValue(QStringLiteral("plan_limit_reached"))) {
        return std::nullopt;
    }
    const QJsonValue sentence = object.value(QLatin1StringView("error"));
    if (!sentence.isString() || sentence.toString().isEmpty()) {
        return std::nullopt;
    }
    return sentence.toString();
}

} // namespace lmp::core::net
