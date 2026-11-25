#include "core/net/Json.h"

#include <QJsonDocument>
#include <QJsonParseError>
#include <QJsonValue>

#include <cmath>
#include <limits>
#include <utility>

namespace lmp::core::json {

namespace {

[[nodiscard]] Failure invalid(QString detail)
{
    return Failure::of(FailureKind::InvalidResponse, std::move(detail));
}

[[nodiscard]] Failure missing(QLatin1StringView key)
{
    return invalid(QStringLiteral("key \"%1\" is missing").arg(key));
}

[[nodiscard]] Failure wrongType(QLatin1StringView key, QLatin1StringView expected)
{
    return invalid(QStringLiteral("key \"%1\" is not %2").arg(key, expected));
}

[[nodiscard]] bool isAbsent(const QJsonValue& value)
{
    return value.isUndefined() || value.isNull();
}

// Reads `key` with `convert`, failing when it is absent.
template <typename T, typename Convert>
[[nodiscard]] Result<T> readRequired(const QJsonObject& object, QLatin1StringView key, Convert convert)
{
    const QJsonValue value = object.value(key);
    if (isAbsent(value)) {
        return missing(key);
    }
    return convert(value, key);
}

// Reads `key` with `convert`, giving nothing when it is absent.
template <typename T, typename Convert>
[[nodiscard]] Result<std::optional<T>> readOptional(const QJsonObject& object, QLatin1StringView key,
                                                    Convert convert)
{
    const QJsonValue value = object.value(key);
    if (isAbsent(value)) {
        return std::optional<T>{};
    }
    Result<T> converted = convert(value, key);
    if (!converted) {
        return converted.failure();
    }
    return std::optional<T>(std::move(converted).value());
}

[[nodiscard]] Result<QString> toString(const QJsonValue& value, QLatin1StringView key)
{
    if (!value.isString()) {
        return wrongType(key, QLatin1StringView("a string"));
    }
    return value.toString();
}

[[nodiscard]] Result<qint64> toInt64(const QJsonValue& value, QLatin1StringView key)
{
    if (!value.isDouble()) {
        return wrongType(key, QLatin1StringView("a number"));
    }
    // toInteger gives its fallback for anything that is not a whole number
    // representable as qint64, so two different fallbacks expose that case.
    const qint64 first = value.toInteger(0);
    const qint64 second = value.toInteger(1);
    if (first != second) {
        return wrongType(key, QLatin1StringView("a whole number in range"));
    }
    return first;
}

[[nodiscard]] Result<int> toInt(const QJsonValue& value, QLatin1StringView key)
{
    const Result<qint64> wide = toInt64(value, key);
    if (!wide) {
        return wide.failure();
    }
    if (wide.value() < std::numeric_limits<int>::min() || wide.value() > std::numeric_limits<int>::max()) {
        return wrongType(key, QLatin1StringView("a whole number in range"));
    }
    return static_cast<int>(wide.value());
}

[[nodiscard]] Result<double> toDouble(const QJsonValue& value, QLatin1StringView key)
{
    if (!value.isDouble() || !std::isfinite(value.toDouble())) {
        return wrongType(key, QLatin1StringView("a number"));
    }
    return value.toDouble();
}

[[nodiscard]] Result<bool> toBool(const QJsonValue& value, QLatin1StringView key)
{
    if (!value.isBool()) {
        return wrongType(key, QLatin1StringView("a boolean"));
    }
    return value.toBool();
}

[[nodiscard]] Result<QJsonObject> toObject(const QJsonValue& value, QLatin1StringView key)
{
    if (!value.isObject()) {
        return wrongType(key, QLatin1StringView("an object"));
    }
    return value.toObject();
}

[[nodiscard]] Result<QJsonArray> toArray(const QJsonValue& value, QLatin1StringView key)
{
    if (!value.isArray()) {
        return wrongType(key, QLatin1StringView("an array"));
    }
    return value.toArray();
}

[[nodiscard]] Result<QJsonDocument> parseDocument(const QByteArray& bytes)
{
    QJsonParseError error{};
    QJsonDocument document = QJsonDocument::fromJson(bytes, &error);
    if (error.error != QJsonParseError::NoError) {
        return invalid(QStringLiteral("not valid JSON (%1 at offset %2)")
                           .arg(error.errorString())
                           .arg(error.offset));
    }
    return document;
}

} // namespace

Result<QJsonObject> parseObject(const QByteArray& bytes)
{
    const Result<QJsonDocument> document = parseDocument(bytes);
    if (!document) {
        return document.failure();
    }
    if (!document.value().isObject()) {
        return invalid(QStringLiteral("the top level is not an object"));
    }
    return document.value().object();
}

Result<QJsonArray> parseArray(const QByteArray& bytes)
{
    const Result<QJsonDocument> document = parseDocument(bytes);
    if (!document) {
        return document.failure();
    }
    if (!document.value().isArray()) {
        return invalid(QStringLiteral("the top level is not an array"));
    }
    return document.value().array();
}

Result<QString> requireString(const QJsonObject& object, QLatin1StringView key)
{
    return readRequired<QString>(object, key, toString);
}

Result<std::optional<QString>> optionalString(const QJsonObject& object, QLatin1StringView key)
{
    return readOptional<QString>(object, key, toString);
}

Result<int> requireInt(const QJsonObject& object, QLatin1StringView key)
{
    return readRequired<int>(object, key, toInt);
}

Result<std::optional<int>> optionalInt(const QJsonObject& object, QLatin1StringView key)
{
    return readOptional<int>(object, key, toInt);
}

Result<qint64> requireInt64(const QJsonObject& object, QLatin1StringView key)
{
    return readRequired<qint64>(object, key, toInt64);
}

Result<std::optional<qint64>> optionalInt64(const QJsonObject& object, QLatin1StringView key)
{
    return readOptional<qint64>(object, key, toInt64);
}

Result<double> requireDouble(const QJsonObject& object, QLatin1StringView key)
{
    return readRequired<double>(object, key, toDouble);
}

Result<std::optional<double>> optionalDouble(const QJsonObject& object, QLatin1StringView key)
{
    return readOptional<double>(object, key, toDouble);
}

Result<bool> requireBool(const QJsonObject& object, QLatin1StringView key)
{
    return readRequired<bool>(object, key, toBool);
}

Result<std::optional<bool>> optionalBool(const QJsonObject& object, QLatin1StringView key)
{
    return readOptional<bool>(object, key, toBool);
}

Result<QJsonObject> requireObject(const QJsonObject& object, QLatin1StringView key)
{
    return readRequired<QJsonObject>(object, key, toObject);
}

Result<std::optional<QJsonObject>> optionalObject(const QJsonObject& object, QLatin1StringView key)
{
    return readOptional<QJsonObject>(object, key, toObject);
}

Result<QJsonArray> requireArray(const QJsonObject& object, QLatin1StringView key)
{
    return readRequired<QJsonArray>(object, key, toArray);
}

Result<std::optional<QJsonArray>> optionalArray(const QJsonObject& object, QLatin1StringView key)
{
    return readOptional<QJsonArray>(object, key, toArray);
}

qint64 wholeNumber(double value, qint64 low, qint64 high, qint64 fallback) noexcept
{
    if (!std::isfinite(value)) {
        return fallback;
    }
    if (value <= static_cast<double>(low)) {
        return low;
    }
    if (value >= static_cast<double>(high)) {
        return high;
    }
    return static_cast<qint64>(value);
}

} // namespace lmp::core::json
