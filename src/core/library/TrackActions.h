#pragma once

#include <QList>
#include <QObject>
#include <QString>

#include "core/async/Result.h"
#include "core/model/Track.h"

namespace lmp::core {
class PlayerController;
class UserDataOperationGate;
} // namespace lmp::core

namespace lmp::core::db {
class CollectionRepository;
class GroupQueries;
class TrackRepository;
} // namespace lmp::core::db

namespace lmp::core::mode {
class ModeService;
}

namespace lmp::core::account {
class AccountLibraryEditor;
}

namespace lmp::core {

// What a song's menu, row buttons and keys can do with it: play it (in its
// list, next, at the end of the queue), like it, add it to or remove it from
// a playlist, and open its album or artist. Each action first asks the
// mode's Capabilities whether it applies to this song (a song with no file
// on this PC plays only where remote songs can stream), as the menu does.
//
// Likes and playlists of songs on this PC are the local library's. In
// Account mode an account song's like goes to the account (through the
// outbox), and account playlists ("account-playlist|<id>") are edited on the
// service; those edits finish later and their own change signals follow.
// Opening a page is reported through signals for the navigation to follow.
//
// GUI thread only (the database's owner).
class TrackActions : public QObject {
    Q_OBJECT

public:
    struct Dependencies {
        PlayerController& player;
        db::TrackRepository& tracks;
        db::CollectionRepository& collections;
        db::GroupQueries& groups;
        const mode::ModeService& mode;
        UserDataOperationGate& gate;
        // Account songs and playlists in Account mode; without it they
        // cannot be kept here.
        account::AccountLibraryEditor* account = nullptr;
    };

    // Every dependency must outlive this object.
    explicit TrackActions(const Dependencies& dependencies, QObject* parent = nullptr);

    // Whether the song can play in the current mode: songs on this PC
    // always, others where remote songs stream.
    [[nodiscard]] bool canPlay(const Track& track) const;
    // Whether the song can be liked or put in a playlist here.
    [[nodiscard]] bool canKeep(const Track& track) const;
    // Whether a mix can start from the song: a remote song where mixes play.
    [[nodiscard]] bool canStartMix(const Track& track) const;
    // Whether a link to the song can be made: Account mode, a song the
    // account can hold.
    [[nodiscard]] bool canShare(const Track& track) const;

    // Plays the song with `context` as the list around it (the song alone
    // when the context is empty).
    bool play(const Track& track, const QList<Track>& context = {});
    bool playNext(const Track& track);
    bool addToQueue(const Track& track);
    // A whole list (a collection page's Shuffle and selection bar). Songs
    // that cannot play here are left out; false when none can.
    bool shufflePlay(const QList<Track>& tracks);
    bool addAllToQueue(const QList<Track>& tracks);

    // Likes or unlikes the song; the new state, or nothing when refused.
    std::optional<bool> toggleLike(const Track& track);

    // The user's own playlists, newest first (title and key in sourceUrl).
    [[nodiscard]] QList<Track> playlists();
    Result<void> addToPlaylist(const Track& track, const QString& playlistKey);
    // A new playlist with the song in it; its key.
    Result<QString> addToNewPlaylist(const Track& track, const QString& name);
    Result<void> removeFromPlaylist(const Track& track, const QString& playlistKey);
    // Several songs of a playlist page at once; all or nothing.
    Result<void> removeAllFromPlaylist(const QList<Track>& tracks, const QString& playlistKey);
    Result<void> moveToPlaylist(const QList<Track>& tracks, const QString& fromKey, const QString& toKey);
    // `tracks` is the playlist's whole new order.
    Result<void> reorderPlaylist(const QString& playlistKey, const QList<Track>& tracks);

    void openAlbum(const Track& track);
    void openArtist(const Track& track);
    // Plays the song alone and fills the queue with songs like it (the
    // autoplay service follows mixRequested).
    void startMix(const Track& track);
    // Makes and copies a link to the song (the share controller follows
    // shareRequested).
    void share(const Track& track);

signals:
    void likedChanged(const QString& sourceKey, bool liked);
    void playlistsChanged();
    void playlistChanged(const QString& playlistKey);
    void albumRequested(const lmp::core::Track& track);
    void artistRequested(const lmp::core::Track& track);
    void mixRequested(const lmp::core::Track& track);
    void shareRequested(const lmp::core::Track& track);

private:
    // The song's library row, written first when it has none.
    [[nodiscard]] Result<qint64> persist(const Track& track);
    // The rows of songs read from the library (rowId set); empty when any lacks one.
    [[nodiscard]] static QList<qint64> rowIds(const QList<Track>& tracks);
    [[nodiscard]] QList<Track> playable(const QList<Track>& tracks) const;

    PlayerController& m_player;
    db::TrackRepository& m_tracks;
    db::CollectionRepository& m_collections;
    db::GroupQueries& m_groups;
    const mode::ModeService& m_mode;
    UserDataOperationGate& m_gate;
    account::AccountLibraryEditor* m_account = nullptr;
};

} // namespace lmp::core
