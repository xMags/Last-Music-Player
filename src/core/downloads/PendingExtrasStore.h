#pragma once

#include "core/downloads/SongFileExtras.h"

#include <QSet>
#include <QString>

#include <optional>

namespace lmp::core::downloads {

// The cover, lyrics and names fetched for a download whose file could not
// take them yet (it was playing, say), kept on disk until it can: writing
// them later needs no network, so it happens in Offline mode too, and the
// lyrics show before then. One small JSON file per song, named by a hash of
// the song's path.
//
// Thread-safe: it keeps no state of its own, and each file is replaced in
// one step, so readers on other threads see the old or the new one whole.
class PendingExtrasStore {
public:
    explicit PendingExtrasStore(QString folder);

    // False when it could not be written.
    bool save(const QString& audioPath, const SongFileExtras& extras) const;
    [[nodiscard]] std::optional<SongFileExtras> load(const QString& audioPath) const;
    [[nodiscard]] bool contains(const QString& audioPath) const;
    void remove(const QString& audioPath) const;
    // Removes what is kept for any song not in `audioPaths` (deleted since).
    void keepOnly(const QSet<QString>& audioPaths) const;

private:
    [[nodiscard]] QString fileFor(const QString& audioPath) const;

    QString m_folder;
};

} // namespace lmp::core::downloads
