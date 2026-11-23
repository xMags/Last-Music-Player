#include "core/library/LikedIndex.h"

namespace lmp::core {

namespace {

void appendUnique(QList<SongIdentity>& list, const SongIdentity& song)
{
    for (const SongIdentity& existing : list) {
        if (existing.id == song.id && existing.title == song.title && existing.artist == song.artist) {
            return;
        }
    }
    list.append(song);
}

} // namespace

LikedIndex::LikedIndex(const QList<SongIdentity>& liked)
{
    for (const SongIdentity& song : liked) {
        if (!m_byId.contains(song.id)) {
            m_byId.insert(song.id, song);
        }
        const SongKey key = songKey(song.title, song.artist);
        if (key.name.isEmpty()) {
            continue;
        }
        m_byName[key.name].append({song, key});
    }
}

bool LikedIndex::has(const QString& id) const
{
    return m_byId.contains(id);
}

bool LikedIndex::hasTrack(const SongIdentity& song) const
{
    return copyOf(song).has_value();
}

std::optional<SongIdentity> LikedIndex::copyOf(const SongIdentity& song) const
{
    if (const auto own = m_byId.constFind(song.id); own != m_byId.cend()) {
        return *own;
    }
    const SongKey key = songKey(song.title, song.artist);
    for (const Entry& entry : m_byName.value(key.name)) {
        if (sameSongKeys(key, entry.key)) {
            return entry.song;
        }
    }
    return std::nullopt;
}

QList<SongIdentity> LikedIndex::copiesOf(const SongIdentity& song) const
{
    QList<SongIdentity> copies;
    const auto own = m_byId.constFind(song.id);
    if (own != m_byId.cend()) {
        copies.append(*own);
    }
    const SongKey key = songKey(song.title, song.artist);
    for (const Entry& entry : m_byName.value(key.name)) {
        if (sameSongKeys(key, entry.key)) {
            appendUnique(copies, entry.song);
        }
    }
    if (own != m_byId.cend()) {
        const SongKey ownKey = songKey(own->title, own->artist);
        for (const Entry& entry : m_byName.value(ownKey.name)) {
            if (sameSongKeys(ownKey, entry.key)) {
                appendUnique(copies, entry.song);
            }
        }
    }
    return copies;
}

} // namespace lmp::core
