#pragma once

#include "core/async/Result.h"
#include "core/db/SqliteConnection.h"

#include <QList>
#include <QString>
#include <QtGlobal>

namespace lmp::core::db {

// Appends `trackIds` to the end of the playlist with this key, in one
// transaction: all of them or none. Ids already in the playlist are skipped
// without leaving a gap in the order. Fails before writing anything when the
// key is empty, the list is empty or an id is not positive; fails and rolls
// back when the playlist or a track does not exist.
[[nodiscard]] Result<void> addTracksToPlaylistTransactional(SqliteConnection& connection,
                                                            const QString& playlistKey,
                                                            const QList<qint64>& trackIds);

} // namespace lmp::core::db
