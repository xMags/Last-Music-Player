#pragma once

#include <QByteArray>
#include <QByteArrayView>
#include <QString>
#include <QStringView>

#include <optional>

namespace lmp::core::text {

// UTF-8 conversion that refuses bad input instead of repairing it.
// QString::fromUtf8 and toUtf8 swap invalid sequences and unpaired surrogates
// for replacement characters; the data the earlier versions wrote was read and
// written with conversions that failed instead, and their callers treat a
// failure as "no value" (an unreadable settings file reads as empty, a bad
// database text column as an empty string, a bad text parameter binds NULL).
// These keep that behaviour. Empty input is a valid, empty result, and
// embedded NUL characters are kept.

// Nothing when `utf8` is not well-formed UTF-8.
[[nodiscard]] std::optional<QString> decodeUtf8Strict(QByteArrayView utf8);

// Nothing when `text` holds an unpaired surrogate.
[[nodiscard]] std::optional<QByteArray> encodeUtf8Strict(QStringView text);

} // namespace lmp::core::text
