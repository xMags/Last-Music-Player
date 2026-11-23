#pragma once

#include <QHash>
#include <QList>
#include <QString>

#include <optional>

#include "core/library/SameSong.h"

namespace lmp::core {

// The liked songs indexed by id and by song name (the reference client's
// liked-index.ts), so the heart on any version of a liked song shows filled
// without comparing it against every like.
class LikedIndex {
public:
    LikedIndex() = default;
    explicit LikedIndex(const QList<SongIdentity>& liked);

    // An exact id check.
    [[nodiscard]] bool has(const QString& id) const;
    // This song or another version of it is liked.
    [[nodiscard]] bool hasTrack(const SongIdentity& song) const;
    // The stored like for this song: its own id first, else another version.
    [[nodiscard]] std::optional<SongIdentity> copyOf(const SongIdentity& song) const;
    // Every stored like that is this song, its own first, then in liked order.
    [[nodiscard]] QList<SongIdentity> copiesOf(const SongIdentity& song) const;

private:
    struct Entry {
        SongIdentity song;
        SongKey key;
    };

    QHash<QString, SongIdentity> m_byId;
    QHash<QString, QList<Entry>> m_byName;
};

} // namespace lmp::core
