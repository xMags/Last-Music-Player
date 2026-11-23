#pragma once

#include "core/catalog/CatalogModels.h"

#include <QJsonObject>
#include <QJsonValue>
#include <QString>
#include <QStringList>

// Small readers the catalogue parsers share. Internal to core/catalog.
namespace lmp::core::catalog::catalog_values {

// The largest offset a page may name (well inside int).
inline constexpr double MaxSafeOffset = 1'000'000'000.0;

// A field as text, trimmed: a string as it is, a non-zero finite number in
// its shortest form, anything else empty.
[[nodiscard]] QString text(const QJsonObject& object, const char* key);
// text(), or `fallback` when that is empty.
[[nodiscard]] QString textOr(const QJsonObject& object, const char* key, const QString& fallback);
// The non-empty strings of an array, in order.
[[nodiscard]] QStringList strings(const QJsonValue& value);
// A whole number of at least 0 that fits an int; 0 otherwise.
[[nodiscard]] int nonNegativeInteger(const QJsonValue& value);
// An item's type, which is singular ("song"); Unknown for anything else.
[[nodiscard]] ResourceType itemType(const QJsonObject& object);

} // namespace lmp::core::catalog::catalog_values
