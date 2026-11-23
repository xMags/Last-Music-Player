#pragma once

#include <QFuture>
#include <QObject>
#include <QString>

#include "core/async/Result.h"

namespace lmp::core::db {
class CollectionRepository;
}

namespace lmp::core {

class UserDataOperationGate;
namespace account {
class AccountLibraryEditor;
}

// Making and changing the user's own collections on this PC: playlists
// (create, rename, delete) and albums made by hand ("Create album", delete),
// as the current app's Library offers them. Every change waits for the
// user-data gate, like the current app (refused while a cleanup runs).
// A blank name becomes "Untitled Playlist" or "Untitled Album".
//
// In Account mode new playlists are made on the account service
// (createsOnAccount, createAccountPlaylist), and account playlists
// ("account-playlist|<id>") are renamed and deleted there; those changes are
// reported when they took (or through the editor's failed signal).
//
// GUI thread only (the database's owner).
class CollectionActions : public QObject {
    Q_OBJECT

public:
    // Both outlive this object.
    CollectionActions(db::CollectionRepository& collections, UserDataOperationGate& gate, QObject* parent = nullptr);

    // Account playlists are edited through `editor` from now on. It must
    // outlive this object.
    void setAccountEditor(account::AccountLibraryEditor* editor);
    // Whether a new playlist is made on the account (Account mode).
    [[nodiscard]] bool createsOnAccount() const;
    // Makes the playlist on the account; its key once it exists.
    [[nodiscard]] QFuture<Result<QString>> createAccountPlaylist(const QString& name);

    // The new playlist's key (on this PC).
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
    account::AccountLibraryEditor* m_account = nullptr;
};

} // namespace lmp::core
