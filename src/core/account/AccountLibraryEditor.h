#pragma once

#include "core/account/AccountSessionState.h"
#include "core/async/Result.h"
#include "core/model/Track.h"
#include "core/playback/AccountPlaybackSink.h"

#include <QFuture>
#include <QLatin1StringView>
#include <QList>
#include <QObject>
#include <QString>

#include <functional>
#include <optional>

namespace lmp::core {
class UserDataOperationGate;
namespace db {
class AccountOutboxRepository;
class AccountRepository;
} // namespace db
namespace mode {
class ModeService;
}
} // namespace lmp::core

namespace lmp::core::account {

class AccountClient;
class AccountSessionService;
class MusicSyncService;

// What the user changes in the account library from this PC, in Account
// mode: likes and plays go through the outbox (stored here at once and sent
// by MusicSyncService, as earlier versions did), playlist edits go to the
// service and the library is synced after each one. Likes are optimistic:
// the stored song changes first and is put back when the change cannot be
// queued. Unliking a song also unlikes every other liked version of it (the
// same song by title and artist), as the reference client does.
//
// Failures of the service calls are reported through `failed` in words a
// user can read; a 401 goes to the session.
//
// Threading: GUI thread only.
class AccountLibraryEditor final : public QObject, public AccountPlaybackSink {
    Q_OBJECT

public:
    struct Dependencies {
        AccountClient& client;
        AccountSessionService& session;
        mode::ModeService& mode;
        db::AccountRepository& accounts;
        db::AccountOutboxRepository& outbox;
        MusicSyncService& sync;
        UserDataOperationGate& gate;
        // ISO 8601 UTC now and a new event id; the system's when empty.
        std::function<QString()> nowUtc;
        std::function<QString()> newEventId;
    };

    // The prefix of an account playlist's key ("account-playlist|<id>").
    static constexpr QLatin1StringView PlaylistKeyPrefix{"account-playlist|"};

    // Every dependency must outlive this object.
    explicit AccountLibraryEditor(Dependencies dependencies, QObject* parent = nullptr);

    // Account mode with a signed-in (or offline) account.
    [[nodiscard]] bool isActive() const;
    // An account song this editor keeps: active, and a remote song with a
    // server id and a source address that may be kept.
    [[nodiscard]] bool handles(const Track& track) const;
    [[nodiscard]] static bool isAccountPlaylistKey(const QString& key);

    // The song's stored liked state (false when it is not cached).
    [[nodiscard]] bool isLiked(const Track& track) const;
    // Likes or unlikes the song. The new state, or nothing when refused.
    std::optional<bool> setLiked(const Track& track, bool liked);

    // Playlist edits on the service, followed by a library sync. A new
    // playlist's key once it exists.
    [[nodiscard]] QFuture<Result<QString>> createPlaylist(const QString& name, const QList<Track>& tracks = {});
    [[nodiscard]] QFuture<Result<void>> renamePlaylist(const QString& playlistKey, const QString& name);
    [[nodiscard]] QFuture<Result<void>> deletePlaylist(const QString& playlistKey);
    [[nodiscard]] QFuture<Result<void>> addToPlaylist(const QString& playlistKey, const Track& track);
    [[nodiscard]] QFuture<Result<void>> removeFromPlaylist(const QString& playlistKey, const QList<Track>& tracks);
    // Save to library for an album or playlist from the catalogue: a playlist
    // of the listener's own, filed under the collection's link (the service
    // tops up an existing copy of the same link). The saved copy's key, if
    // there is one; removing deletes that copy.
    [[nodiscard]] QFuture<Result<QString>> saveCollection(const QString& title, const QString& description,
                                                          const QList<Track>& tracks, const QString& sourceUrl);
    [[nodiscard]] std::optional<QString> savedCollectionKey(const QString& sourceUrl) const;
    [[nodiscard]] QFuture<Result<void>> removeSavedCollection(const QString& sourceUrl);

    // `tracks` is the playlist's whole new order.
    [[nodiscard]] QFuture<Result<void>> replacePlaylist(const QString& playlistKey, const QList<Track>& tracks);

    // AccountPlaybackSink: a play of an account song that reached the history.
    bool recordAccountPlay(const Track& track, double positionSeconds) override;

signals:
    // A change could not be made; `message` says so in words a user can read.
    void failed(const QString& message);
    // The account's playlists, or one playlist's songs, changed.
    void playlistsChanged();
    void playlistChanged(const QString& playlistKey);

private:
    [[nodiscard]] std::optional<AccountOperationContext> writableContext() const;
    [[nodiscard]] bool writeLike(const QString& owner, const Track& track, bool liked);
    // Ends a service call: a failure is reported, a success syncs the library
    // and then announces `playlistKey` (all playlists when empty).
    [[nodiscard]] QFuture<Result<void>> afterEdit(QFuture<Result<void>> call, const AccountOperationContext& context,
                                                  const QString& playlistKey, const QString& failureMessage);

    Dependencies m_deps;
};

} // namespace lmp::core::account
