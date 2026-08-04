#pragma once

#include <string>
#include <vector>
#include <mutex>
#include <optional>
#include <winrt/Windows.Storage.h>
#include "TrackInfo.h"
#include "ThirdParty/sqlite/sqlite3.h"

namespace LastMusicPlayer::Backend
{
    struct LibraryStats
    {
        int SongCount{ 0 };
        int AlbumCount{ 0 };
        int ArtistCount{ 0 };
        int GenreCount{ 0 };
        double TotalSeconds{ 0.0 };
    };

    enum class AccountDataClearMode
    {
        LeaveInactive,
        RestoreActiveContext
    };

    struct AccountProfileSnapshot
    {
        std::wstring AccountId;
        std::wstring DisplayName;
        std::wstring Username;
        std::wstring AvatarUrl;
        std::wstring PlanLabel;
        std::wstring UpdatedAtUtc;
    };

    struct AccountTrackSnapshot
    {
        winrt::Last_Music_Player::TrackInfo Track{ nullptr };
        int64_t PlayCount{};
        std::wstring FirstPlayedAtUtc;
        std::wstring LastPlayedAtUtc;
        double LastPositionSeconds{};
    };

    struct AccountPlaylistSnapshot
    {
        std::wstring PlaylistId;
        std::wstring Name;
        std::wstring Description;
        std::wstring SourceUrl;
        std::wstring UpdatedAtUtc;
        std::vector<std::wstring> RemoteTrackIds;
    };

    struct AccountLibrarySnapshot
    {
        AccountProfileSnapshot Profile;
        std::vector<AccountTrackSnapshot> Tracks;
        std::vector<AccountPlaylistSnapshot> Playlists;
        std::wstring SynchronizedAtUtc;
    };

    struct PlaybackEventRecord
    {
        std::wstring EventId;
        std::wstring AccountId;
        std::wstring RemoteTrackId;
        winrt::Last_Music_Player::TrackInfo Track{ nullptr };
        std::wstring PlayedAtUtc;
        double PositionSeconds{};
        int AttemptCount{};
    };

    struct PendingLikeRecord
    {
        std::wstring AccountId;
        std::wstring RemoteTrackId;
        winrt::Last_Music_Player::TrackInfo Track{ nullptr };
        bool DesiredState{};
        std::wstring UpdatedAtUtc;
        int AttemptCount{};
    };

    struct AccountSyncState
    {
        std::wstring AccountId;
        std::wstring LastSuccessfulSyncUtc;
        std::wstring LastSafeErrorCode;
        std::wstring LegacyHistoryImportState;
    };

    struct PlaybackStatSnapshot
    {
        winrt::Last_Music_Player::TrackInfo Track{ nullptr };
        std::wstring SourceKey;
        uint32_t PlayCount{};
        uint64_t LastPlayedOrder{};
    };
    struct LegacyHistoryImportCandidate
    {
        TrackInfo Track{ nullptr };
        std::wstring SourceKey;
        std::wstring LastPlayedUtc;
    };


    struct TrackQuery
    {
        std::wstring Filter{ L"All" };
        std::wstring Sort{ L"DateAdded" };
        std::wstring Scope{ L"All" };
        std::wstring GroupKind;
        std::wstring GroupKey;
        // Optional case-insensitive substring match across title, artist and
        // album. LIKE wildcards are escaped by DatabaseEngine, so callers pass
        // the listener's text unchanged rather than building SQL patterns.
        std::wstring SearchText;
        std::wstring AccountOwnerId;
        bool IncludeRemote{ true };
        bool ActiveOnly{ true };
        int Offset{ 0 };
        int Limit{ 0 };
    };

    struct TrackPage
    {
        std::vector<TrackInfo> Tracks;
        int TotalCount{ 0 };
        double TotalSeconds{ 0.0 };
    };

    class DatabaseEngine
    {
    public:
        DatabaseEngine() = default;
        ~DatabaseEngine();

        bool Initialize();
        void Close();
        bool IsInitialized() const { return m_db != nullptr; }
        bool ClearAllUserData();

        void BeginLocalScan();
        void CompleteLocalScan(std::vector<std::wstring> const& activeSourceKeys);
        int64_t UpsertLocalTrack(TrackInfo const& track, std::wstring const& sourceKey);
        int64_t UpsertRemoteTrack(TrackInfo const& track, std::wstring const& sourceKey);
        void RecordPlayback(std::wstring const& sourceKey, uint64_t playOrder);
        void MergePlaybackStats(std::wstring const& sourceKey, uint32_t playCount, uint64_t lastPlayedOrder);
        uint64_t MaxLastPlayedOrder() const;
        void SetLiked(std::wstring const& sourceKey, bool liked);
        bool IsLiked(std::wstring const& sourceKey) const;
        int64_t CreateAlbumCollection(std::wstring const& title);
        int64_t UpsertAlbumCollection(TrackInfo const& album, std::wstring const& albumKey);
        void ReplaceAlbumCollectionTracks(int64_t albumId, std::vector<int64_t> const& trackIds);
        bool DeleteAlbumCollection(std::wstring const& albumKey);
        int64_t CreatePlaylist(std::wstring const& title);
        int64_t UpsertPlaylist(TrackInfo const& playlist, std::wstring const& playlistKey);
        void ReplacePlaylistTracks(int64_t playlistId, std::vector<int64_t> const& trackIds);
        bool ReorderPlaylistTracks(std::wstring const& playlistKey, std::vector<int64_t> const& trackIds);
        bool AddTrackToPlaylist(std::wstring const& playlistKey, int64_t trackId);
        bool RemoveTrackFromPlaylist(std::wstring const& playlistKey, int64_t trackId);
        bool RenamePlaylist(std::wstring const& playlistKey, std::wstring const& title);
        bool DeletePlaylist(std::wstring const& playlistKey);

        std::vector<TrackInfo> LoadTracks(bool includeRemote = true, bool activeOnly = true) const;
        std::vector<TrackInfo> LoadHistoryTracks(bool includeRemote = true, bool activeOnly = true) const;
        std::vector<TrackInfo> LoadMostPlayedTracks(bool includeRemote = true, bool activeOnly = true) const;
        std::vector<LegacyHistoryImportCandidate> LoadLegacyRemoteHistoryImportCandidates() const;
        TrackPage LoadTrackPage(TrackQuery const& query) const;
        std::vector<TrackInfo> LoadTracksForQuery(TrackQuery const& query) const;
        std::vector<TrackInfo> LoadRecentlyAddedTracks(int limit) const;
        std::vector<TrackInfo> LoadAlbums() const;
        std::vector<TrackInfo> LoadPlaylists() const;
        std::vector<TrackInfo> LoadRecentPlaylists(int limit) const;
        std::vector<TrackInfo> LoadArtists() const;
        std::vector<TrackInfo> LoadGenres() const;
        // Genre names ranked by total listening (SUM(PlayCount)), falling back
        // to genre size when the user has no play history yet. Drives the
        // genre-clustered "Made for you" Daily Mixes. Excludes untagged tracks.
        std::vector<std::wstring> LoadTopGenres(int limit) const;
        std::vector<TrackInfo> LoadTracksForGroup(std::wstring const& groupKind, std::wstring const& groupKey) const;
        LibraryStats GetLibraryStats() const;
        std::vector<PlaybackStatSnapshot> LoadPlaybackStats() const;

        bool SetRemoteLibraryContext(std::wstring const& mode, std::wstring const& accountId = {});
        std::wstring ActiveAccountId() const;
        bool ReplaceAccountLibrary(
            std::wstring const& expectedAccountId,
            AccountLibrarySnapshot const& snapshot);
        std::optional<AccountLibrarySnapshot> LoadAccountLibrary(std::wstring const& accountId) const;
        std::vector<TrackInfo> LoadAccountPlaylistTracks(std::wstring const& accountId, std::wstring const& playlistId) const;
        bool EnsureAccountTrack(std::wstring const& accountId, TrackInfo const& track);
        bool RecordAccountPlayback(
            std::wstring const& accountId,
            TrackInfo const& track,
            double positionSeconds);


        bool EnqueuePlaybackEvent(PlaybackEventRecord const& event);
        std::vector<PlaybackEventRecord> LoadPendingPlaybackEvents(std::wstring const& accountId, int limit = 100) const;
        bool MarkPlaybackEventsAttempted(std::wstring const& accountId, std::vector<std::wstring> const& eventIds);
        bool AcknowledgePlaybackEvents(std::wstring const& accountId, std::vector<std::wstring> const& eventIds);

        bool EnqueuePendingLike(PendingLikeRecord const& like);
        std::vector<PendingLikeRecord> LoadPendingLikes(std::wstring const& accountId, int limit = 100) const;
        bool MarkPendingLikeAttempted(std::wstring const& accountId, std::wstring const& remoteTrackId);
        bool AcknowledgePendingLike(std::wstring const& accountId, std::wstring const& remoteTrackId, bool desiredState);
        bool UpdateAccountTrackLike(std::wstring const& accountId, std::wstring const& remoteTrackId, bool liked);

        AccountSyncState LoadAccountSyncState(std::wstring const& accountId) const;
        bool SaveAccountSyncState(AccountSyncState const& state);

        // Last catalog discovery payload for an account and region. Returns an
        // empty string when nothing is stored, which callers treat the same as a
        // cold start. Both are cheap but touch disk, so call them off the UI
        // thread; the payload can run to a few hundred kilobytes.
        std::wstring LoadCatalogDiscoveryPayload(
            std::wstring const& accountId,
            std::wstring const& storefront) const;
        bool SaveCatalogDiscoveryPayload(
            std::wstring const& accountId,
            std::wstring const& storefront,
            std::wstring const& payload);
        bool ClearAccountData(
            std::wstring const& accountId,
            AccountDataClearMode mode = AccountDataClearMode::LeaveInactive);
        bool ClearAllAccountData();

    private:
        bool EnsureSchema();
        bool MigrateImportedAlbumCollectionsToPlaylists();
        int64_t UpsertTrack(TrackInfo const& track, std::wstring const& sourceKind, std::wstring const& provider, std::wstring const& sourceKey, bool active);
        sqlite3* m_db = nullptr;
        mutable std::recursive_mutex m_mutex;
    };
}
