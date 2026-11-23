#pragma once

#include <QObject>
#include <QString>

#include "core/async/Result.h"

namespace lmp::core::db {
class CollectionRepository;
}

namespace lmp::core {

class UserDataOperationGate;

// Making and changing the user's own collections on this PC: playlists
// (create, rename, delete) and albums made by hand ("Create album", delete),
// as the current app's Library offers them. Every change waits for the
// user-data gate, like the current app (refused while a cleanup runs).
// A blank name becomes "Untitled Playlist" or "Untitled Album".
//
// GUI thread only (the database's owner).
class CollectionActions : public QObject {
    Q_OBJECT

public:
    // Both outlive this object.
    CollectionActions(db::CollectionRepository& collections, UserDataOperationGate& gate, QObject* parent = nullptr);

    // The new playlist's key.
    Result<QString> createPlaylist(const QString& name);
    Result<void> renamePlaylist(const QString& playlistKey, const QString& name);
    Result<void> deletePlaylist(const QString& playlistKey);
    // The new album's key.
    Result<QString> createAlbum(const QString& name);
    Result<void> deleteAlbum(const QString& albumKey);

    // Longest name kept (the reference's prompt limit).
    static constexpr int MaximumNameLength = 80;
    [[nodiscard]] static QString cleanName(const QString& name, const QString& fallback);

signals:
    void playlistsChanged();
    void albumsChanged();
    // The playlist's new (cleaned) name.
    void playlistRenamed(const QString& playlistKey, const QString& name);
    // A playlist or album went away (its page closes).
    void collectionDeleted(const QString& kind, const QString& key);

private:
    db::CollectionRepository& m_collections;
    UserDataOperationGate& m_gate;
};

} // namespace lmp::core
