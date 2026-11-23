#pragma once

#include "core/model/Track.h"

#include <QByteArray>
#include <QString>

#include <optional>

namespace lmp::core::account {

// The pieces of the earlier versions' "import previous history into the
// account" offer that need no service.

// A stable event id for one imported song, so a retry never adds it twice:
// SHA-256 of UTF-8 "accountId\nsourceKey", shaped as a version 5 UUID.
// Empty when either part is empty. The source key does not appear in it.
[[nodiscard]] QString historyImportEventId(const QString& accountId, const QString& sourceKey);

// The earlier builds stored "YYYY-MM-DD HH:MM:SS" (UTC); the service wants
// "YYYY-MM-DDTHH:MM:SSZ". An already converted value passes; anything else
// is empty.
[[nodiscard]] QString normalizeLegacyPlayedAt(const QString& value);

// The account song a resolve answer describes ({"result": {...}} or
// {"result": {"track": {...}}}), filled from `legacy` where the answer is
// silent. Nothing without an id and a source address, or when the result is
// not a song the account can store.
[[nodiscard]] std::optional<Track> resolvedImportTrack(const QByteArray& payload, const Track& legacy);

} // namespace lmp::core::account
