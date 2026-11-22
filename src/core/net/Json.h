#pragma once

#include "core/async/Result.h"

#include <QByteArray>
#include <QJsonArray>
#include <QJsonObject>
#include <QLatin1StringView>
#include <QString>

#include <optional>

// Strict readers for JSON that arrives from the network. Each returns a
// Failure (FailureKind::InvalidResponse) instead of guessing: a value of the
// wrong type is never converted. The failure detail names the key, never the
// value, because values can be tokens or personal data.
//
// "require" readers fail when the key is missing or null. "optional" readers
// give nothing for a missing or null key and fail only for a wrong type.
namespace lmp::core::json {

// The document must be well-formed UTF-8 JSON whose top level is an object
// (or an array, for parseArray).
[[nodiscard]] Result<QJsonObject> parseObject(const QByteArray& bytes);
[[nodiscard]] Result<QJsonArray> parseArray(const QByteArray& bytes);

[[nodiscard]] Result<QString> requireString(const QJsonObject& object, QLatin1StringView key);
[[nodiscard]] Result<std::optional<QString>> optionalString(const QJsonObject& object,
                                                            QLatin1StringView key);

// Integers must be whole numbers within the range of the result type; 2.5 or
// 1e300 is a failure, not a rounded value.
[[nodiscard]] Result<int> requireInt(const QJsonObject& object, QLatin1StringView key);
[[nodiscard]] Result<std::optional<int>> optionalInt(const QJsonObject& object, QLatin1StringView key);
[[nodiscard]] Result<qint64> requireInt64(const QJsonObject& object, QLatin1StringView key);
[[nodiscard]] Result<std::optional<qint64>> optionalInt64(const QJsonObject& object,
                                                          QLatin1StringView key);

[[nodiscard]] Result<double> requireDouble(const QJsonObject& object, QLatin1StringView key);
[[nodiscard]] Result<std::optional<double>> optionalDouble(const QJsonObject& object,
                                                           QLatin1StringView key);

[[nodiscard]] Result<bool> requireBool(const QJsonObject& object, QLatin1StringView key);
[[nodiscard]] Result<std::optional<bool>> optionalBool(const QJsonObject& object, QLatin1StringView key);

[[nodiscard]] Result<QJsonObject> requireObject(const QJsonObject& object, QLatin1StringView key);
[[nodiscard]] Result<std::optional<QJsonObject>> optionalObject(const QJsonObject& object,
                                                                QLatin1StringView key);

[[nodiscard]] Result<QJsonArray> requireArray(const QJsonObject& object, QLatin1StringView key);
[[nodiscard]] Result<std::optional<QJsonArray>> optionalArray(const QJsonObject& object,
                                                              QLatin1StringView key);

} // namespace lmp::core::json
