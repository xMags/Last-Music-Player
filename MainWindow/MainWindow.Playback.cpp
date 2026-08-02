#include "pch.h"
#include "MainWindow.xaml.h"
#include "MainWindow.Internal.h"

#include "Backend/BuildConfig.h"
#include "Backend/ProviderClient.h"
#include "Backend/DiscordPresence.h"

#include <winrt/Windows.Data.Json.h>
#include <winrt/Windows.Devices.Enumeration.h>
#include <winrt/Windows.Media.Core.h>
#include <winrt/Windows.Media.Devices.h>
#include <winrt/Windows.Storage.Pickers.h>
#include <winrt/Windows.System.h>
#include <winrt/Microsoft.UI.Xaml.Controls.h>
#include <winrt/Microsoft.UI.Xaml.Controls.Primitives.h>
#include <winrt/Microsoft.UI.Xaml.Media.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <limits>
#include <random>
#include <string>
#include <vector>
#include <shobjidl.h>

using namespace winrt;
using namespace Microsoft::UI::Xaml;

namespace winrt::Last_Music_Player::implementation
{
    using namespace detail;
    namespace
    {

        std::wstring NewPlaybackEventId()
        {
            GUID id{};
            if (FAILED(::CoCreateGuid(&id)))
            {
                return {};
            }
            wchar_t value[40]{};
            if (::StringFromGUID2(id, value, static_cast<int>(std::size(value))) <= 0)
            {
                return {};
            }
            auto result = std::wstring(value);
            if (result.size() >= 2 && result.front() == L'{' && result.back() == L'}')
            {
                result = result.substr(1, result.size() - 2);
            }
            return result;
        }

        std::wstring PlaybackTimestampNow()
        {
            SYSTEMTIME now{};
            ::GetSystemTime(&now);
            wchar_t value[40]{};
            swprintf_s(value, L"%04u-%02u-%02uT%02u:%02u:%02uZ",
                now.wYear, now.wMonth, now.wDay, now.wHour, now.wMinute, now.wSecond);
            return value;
        }
    }

    void MainWindow::BeginPlaybackQualification(winrt::Last_Music_Player::TrackInfo const& track)
    {
        m_playbackHistoryQualifier.Clear();
        m_pendingPlaybackTrack = nullptr;
        m_pendingPlaybackIdentity.clear();
        m_pendingPlaybackEventId.clear();
        m_pendingPlaybackOwnerId.clear();
        m_pendingPlaybackRemoteId.clear();
        m_pendingPlaybackAccountGeneration = 0;

        auto identity = CatalogSourceKey(track);
        auto eventId = NewPlaybackEventId();
        if (!track || identity.empty() || eventId.empty())
        {
            return;
        }

        m_pendingPlaybackTrack = track;
        m_pendingPlaybackEventId = std::move(eventId);
        m_pendingPlaybackIdentity = m_pendingPlaybackEventId + L"|" + identity;
        m_playbackHistoryQualifier.Reset(m_pendingPlaybackIdentity);

        if (RemoteMusicServiceService().Mode() != LastMusicPlayer::Backend::RemoteAccessMode::Account
            || !IsCompatibleAccountRemoteTrack(track))
        {
            return;
        }

        auto snapshot = AccountSessionService().Snapshot();
        if ((snapshot.Status == LastMusicPlayer::Backend::AccountSessionStatus::Validated
                || snapshot.Status == LastMusicPlayer::Backend::AccountSessionStatus::Offline)
            && !snapshot.Profile.Id.empty())
        {
            m_pendingPlaybackOwnerId = std::wstring(snapshot.Profile.Id.c_str());
            m_pendingPlaybackRemoteId = std::wstring(track.RemoteId().c_str());
            m_pendingPlaybackAccountGeneration = snapshot.Generation;
        }
    }

    void MainWindow::ObservePlaybackQualification(bool playing, double positionSeconds)
    {
        if (!m_pendingPlaybackTrack || m_pendingPlaybackIdentity.empty())
        {
            return;
        }

        auto current = AudioPlayerService().GetCurrentTrack();
        if (!current || CatalogSourceKey(current) != CatalogSourceKey(m_pendingPlaybackTrack))
        {
            m_playbackHistoryQualifier.Clear();
            m_pendingPlaybackTrack = nullptr;
            m_pendingPlaybackIdentity.clear();
            m_pendingPlaybackEventId.clear();
            m_pendingPlaybackOwnerId.clear();
            m_pendingPlaybackRemoteId.clear();
            m_pendingPlaybackAccountGeneration = 0;
            return;
        }

        if (!m_playbackHistoryQualifier.Observe(m_pendingPlaybackIdentity, playing, ::GetTickCount64()))
        {
            return;
        }

        auto operationLease = UserDataOperationGateService().TryEnter();
        if (!operationLease)
        {
            m_playbackHistoryQualifier.MarkCompleted();
            return;
        }

        bool queuedAccountEvent = false;
        std::wstring playbackAccountOwner;
        uint64_t playbackAccountGeneration{};
        if (!m_pendingPlaybackOwnerId.empty() && !m_pendingPlaybackRemoteId.empty())
        {
            auto snapshot = AccountSessionService().Snapshot();
            auto accountStillCurrent = snapshot.Profile.Id == winrt::hstring(m_pendingPlaybackOwnerId)
                && snapshot.Generation == m_pendingPlaybackAccountGeneration
                && (snapshot.Status == LastMusicPlayer::Backend::AccountSessionStatus::Validated
                    || snapshot.Status == LastMusicPlayer::Backend::AccountSessionStatus::Offline)
                && RemoteMusicServiceService().Mode() == LastMusicPlayer::Backend::RemoteAccessMode::Account
                && IsCompatibleAccountRemoteTrack(current);
            if (accountStillCurrent)
            {
                LastMusicPlayer::Backend::PlaybackEventRecord event;
                event.EventId = m_pendingPlaybackEventId;
                event.AccountId = m_pendingPlaybackOwnerId;
                event.RemoteTrackId = m_pendingPlaybackRemoteId;
                event.Track = current;
                event.PlayedAtUtc = PlaybackTimestampNow();
                event.PositionSeconds = (std::max)(0.0, positionSeconds);
                if (!DatabaseService().EnqueuePlaybackEvent(event))
                {
                    return;
                }
                queuedAccountEvent = true;
                playbackAccountOwner = m_pendingPlaybackOwnerId;
                playbackAccountGeneration = m_pendingPlaybackAccountGeneration;
            }
        }

        RecordHomePlayback(current);
        PersistTrackPlayback(
            current,
            positionSeconds,
            playbackAccountOwner,
            playbackAccountGeneration);
        RunDetached(HydrateHomeAsync(false));
        m_songsLoadState = LoadState::Dirty;
        m_librarySongsState = LoadState::Dirty;
        m_libraryPlaylistsState = LoadState::Dirty;
        SaveAppState();
        m_playbackHistoryQualifier.MarkCompleted();
        operationLease.reset();

        auto snapshot = AccountSessionService().Snapshot();
        if (queuedAccountEvent
            && snapshot.Status == LastMusicPlayer::Backend::AccountSessionStatus::Validated
            && snapshot.Generation == playbackAccountGeneration
            && snapshot.Profile.Id == winrt::hstring(playbackAccountOwner))
        {
            RunDetached(SynchronizeAccountLibraryAsync(false));
        }
    }

    winrt::Windows::Media::Core::MediaSource MainWindow::BuildMediaSourceForTrack(winrt::Last_Music_Player::TrackInfo const& track)
    {
        try
        {
            auto file = track.File();
            auto filePath = track.FilePath();
            auto& remoteMusic = RemoteMusicServiceService();
            auto remoteScope = remoteMusic.CaptureScope();
            auto remoteMode = remoteScope.Mode;
            auto remote = !file && (IsHttpUrl(filePath) || IsHttpUrl(track.SourceUrl()));
            if (remote && remoteMode == LastMusicPlayer::Backend::RemoteAccessMode::LocalOnly)
            {
                return nullptr;
            }

            auto providerStreamUrl = ProviderStreamUrlFor(track);
            auto cacheKey = ApiKeyStreamCacheKey(remoteScope, track);
            auto cached = !cacheKey.empty() && remoteMusic.IsCurrent(remoteScope)
                ? StreamCacheService().ReadyPath(cacheKey)
                : std::wstring{};
            if (!cached.empty())
            {
                filePath = winrt::hstring{ cached };
            }
            else if (!providerStreamUrl.empty())
            {
                filePath = providerStreamUrl;
            }
            else if (remote)
            {
                return nullptr;
            }
            // Always prefer a URI-backed MediaSource. CreateFromStorageFile
            // sources fail to play silently when wrapped in MediaPlaybackItem
            // inside our MediaPlaybackList path — file URIs work uniformly for
            // local and remote, so we use them as the single entry point.
            if (!filePath.empty())
            {
                auto remoteUri = IsHttpUrl(filePath);
                auto uri = remoteUri
                    ? winrt::Windows::Foundation::Uri(filePath)
                    : winrt::Windows::Foundation::Uri(winrt::hstring(FilePathToUri(filePath)));
                return winrt::Windows::Media::Core::MediaSource::CreateFromUri(uri);
            }
            if (file)
            {
                return winrt::Windows::Media::Core::MediaSource::CreateFromStorageFile(file);
            }
        }
        catch (...) {}
        return nullptr;
    }

    void MainWindow::RestoreLastPlayedTrack(AppStateSnapshot const& snapshot)
    {
        // Match the saved last-track path against the just-loaded play history
        // (the most recent play is recorded there). We don't preload the audio
        // engine — the now-playing bar shows the track and the first Play press
        // resumes it, which keeps play-counting/Discord/lyrics on the real play.
        if (snapshot.LastTrackPath.empty())
        {
            return;
        }

        winrt::Last_Music_Player::TrackInfo match{ nullptr };
        auto savedIdentity = std::wstring(snapshot.LastTrackPath.c_str());
        for (auto const& candidate : m_homeRecentHistory)
        {
            if (!candidate)
            {
                continue;
            }

            auto matches = candidate.FilePath() == snapshot.LastTrackPath;
            if (!matches && savedIdentity.starts_with(L"remote-id:"))
            {
                matches = std::wstring(candidate.RemoteId().c_str()) == savedIdentity.substr(10);
            }
            if (!matches && savedIdentity.starts_with(L"source:"))
            {
                matches = CatalogSourceKey(candidate) == savedIdentity.substr(7);
            }
            if (matches)
            {
                match = candidate;
                break;
            }
        }

        // Skip when the track is gone (deleted local file) or otherwise can't be
        // played (remote with no rebuildable stream) — pro players don't restore
        // unavailable media.
        if (!match || LocalFileMissing(match) || !IsPlayableHomeTrack(match))
        {
            return;
        }

        ResolveArtworkPresentation(match, L"track");
        m_pendingResumeTrack = match;
        RestoreNowPlayingBar(match);
    }

    void MainWindow::RestoreNowPlayingBar(winrt::Last_Music_Player::TrackInfo const& track)
    {
        // Visual-only restore of the player chrome to a paused/ready state. Keep
        // this a strict subset of PlayTrack's UI updates — no playback, no SMTC/
        // Discord/lyrics/history side effects.
        bool remote = !track.File() && IsHttpUrl(track.FilePath());

        BottomPlayerTitle().Text(track.Title());
        BottomPlayerArtist().Text(track.Artist());
        BottomGeneratedGlyph().Glyph(track.ArtworkGlyph());

        NowPlayingTitle().Text(track.Title());
        NowPlayingArtist().Text(track.Artist());
        RightPanelGeneratedGlyph().Glyph(track.ArtworkGlyph());
        RightPanelGeneratedTitle().Text(track.ArtworkTitle());
        RightPanelGeneratedCaption().Text(track.ArtworkCaption());

        if (FsTitle())
        {
            FsTitle().Text(track.Title());
            FsArtist().Text(track.Artist());
            if (FsGeneratedGlyph()) FsGeneratedGlyph().Glyph(track.ArtworkGlyph());
        }

        NpMetaAlbum().Text(track.Album().empty() ? (remote ? L"Remote music" : L"Local Library") : track.Album());
        NpMetaYear().Text(track.SourceLabel().empty() ? (remote ? L"Remote" : L"Local") : track.SourceLabel());
        NpMetaFormat().Text(remote ? L"Stream" : L"File");

        bool hasImage = track.ImageArtworkOpacity() > 0.0 && track.AlbumArt() != nullptr;
        BottomPlayerArt().Source(hasImage ? track.AlbumArt() : nullptr);
        BottomPlayerArt().Opacity(hasImage ? 1.0 : 0.0);
        BottomGeneratedArtwork().Opacity(hasImage ? 0.0 : 1.0);
        RightPanelArt().Source(hasImage ? track.AlbumArt() : nullptr);
        RightPanelArt().Opacity(hasImage ? 1.0 : 0.0);
        RightPanelGeneratedArtwork().Opacity(hasImage ? 0.0 : 1.0);
        if (FsArt())
        {
            FsArt().Source(hasImage ? track.AlbumArt() : nullptr);
            FsArt().Opacity(hasImage ? 1.0 : 0.0);
            FsGeneratedArtwork().Opacity(hasImage ? 0.0 : 1.0);
        }
        ApplyShowAlbumArt();

        UpdateLikeButton(track);

        // Paused/ready: show the Play glyph.
        PlayPauseIcon().Glyph(L"\xE768");
        if (FsPlayPauseIcon()) FsPlayPauseIcon().Glyph(L"\xE768");
    }

    bool MainWindow::PlayTrack(winrt::Last_Music_Player::TrackInfo const& track, bool gaplessTransitioned)
    {
        // Any explicit play supersedes a pending resume restore.
        m_pendingResumeTrack = nullptr;
        auto resolveEpoch = ++m_remotePlaybackResolveEpoch;
        try
        {
        auto file = track.File();
        auto filePath = track.FilePath();
        auto& remoteMusic = RemoteMusicServiceService();
        auto remoteScope = remoteMusic.CaptureScope();
        auto remoteMode = remoteScope.Mode;
        bool accountMode = remoteMode == LastMusicPlayer::Backend::RemoteAccessMode::Account;
        auto providerStreamUrl = ProviderStreamUrlFor(track);
        auto rawArtworkUrl = track.ArtworkUrl();
        if (!accountMode && ToLowerCopy(rawArtworkUrl).find(L"/v1/artwork") != std::wstring::npos)
        {
            track.ArtworkUrl(ProviderArtworkUrlFor(rawArtworkUrl));
        }
        auto remote = !file && (IsHttpUrl(filePath) || IsHttpUrl(track.SourceUrl()));
        if (remote && remoteMode == LastMusicPlayer::Backend::RemoteAccessMode::LocalOnly)
        {
            ShowPlaybackNotice(L"Select a remote integration to play this track.");
            return false;
        }

        auto cacheKey = ApiKeyStreamCacheKey(remoteScope, track);
        auto cachedFilePath = m_sink == PlaybackSink::Local
            && remote
            && !cacheKey.empty()
            && remoteMusic.IsCurrent(remoteScope)
            ? StreamCacheService().ReadyPath(cacheKey)
            : std::wstring{};
        bool requiresResolve = cachedFilePath.empty() && (accountMode
            ? !LastMusicPlayer::Backend::IsSafeEphemeralMediaUrl(providerStreamUrl)
            : providerStreamUrl.empty());
        if (!gaplessTransitioned && remote && IsHttpUrl(track.SourceUrl()) && requiresResolve)
        {
            RunDetached(ResolveRemotePlaybackAsync(
                track,
                true,
                gaplessTransitioned,
                0.0,
                resolveEpoch));
            return true;
        }
        if (!providerStreamUrl.empty())
        {
            filePath = providerStreamUrl;
            if (track.FilePath() != providerStreamUrl)
            {
                track.FilePath(providerStreamUrl);
            }
        }

        auto apiKeyCastBlocked = m_sink == PlaybackSink::Cast
            && remoteMode == LastMusicPlayer::Backend::RemoteAccessMode::ApiKey;
        if (m_sink == PlaybackSink::Cast
            && (apiKeyCastBlocked || !IsHttpUrl(providerStreamUrl)))
        {
            // A Cast receiver must receive the media URL. API-key URLs carry the
            // user's credential, while local-only sources are not receiver-
            // reachable, so both cases fail closed to local playback.
            m_cast.Stop();
            m_cast.Disconnect();
            m_sink = PlaybackSink::Local;
            m_castSession = {};
            EnsureAccentBrushes();
            if (CastIcon()) CastIcon().Foreground(m_brushGlyphIdle);
            if (apiKeyCastBlocked)
            {
                ShowPlaybackNotice(L"Casting API-key tracks is disabled to keep the API key on this PC.");
            }
        }

        if (!gaplessTransitioned && m_sink == PlaybackSink::Cast && !providerStreamUrl.empty())
        {
            // The receiver fetches the provider-issued signed media URL
            // directly; the app sends only playback controls.
            m_castSession.CurrentSeconds = 0.0;
            m_castSession.DurationSeconds = track.DurationSeconds();
            m_castSession.ProgressStampMs = ::GetTickCount64();
            m_castSession.LastStatusRequestMs = 0;
            ApplyPlaybackProgress(0.0, m_castSession.DurationSeconds);
            m_cast.LoadAsync(
                providerStreamUrl,
                track.Title(),
                track.Artist(),
                accountMode ? winrt::hstring{} : ProviderArtworkUrlFor(track.ArtworkUrl()));
            m_castSession.IsPlaying = true;
            m_cast.RequestStatus();
            AudioPlayerService().GetMediaPlayer().Pause();
            AudioPlayerService().LoadTrack(track);
            PlayPauseIcon().Glyph(L"\xE769");
            if (FsPlayPauseIcon()) FsPlayPauseIcon().Glyph(L"\xE769");
        }
        else
        {
            auto source = BuildMediaSourceForTrack(track);
            if (!source)
            {
                return false;
            }
            AudioPlayerService().PlayGaplessCurrent(source);
            AudioPlayerService().LoadTrack(track);
        }
        (void)gaplessTransitioned;  // Reserved for future MediaPlaybackList revival.

        if (m_sink != PlaybackSink::Cast)
        {
            double base = SettingsManagerService().GetVolume();
            if (base < 0.0) base = 0.0;
            if (base > 1.0) base = 1.0;
            AudioPlayerService().GetMediaPlayer().Volume(base);
        }
        ResolveArtworkPresentation(track, L"track");

        BottomPlayerTitle().Text(track.Title());
        BottomPlayerArtist().Text(track.Artist());
        BottomGeneratedGlyph().Glyph(track.ArtworkGlyph());

        NowPlayingTitle().Text(track.Title());
        NowPlayingArtist().Text(track.Artist());
        RightPanelGeneratedGlyph().Glyph(track.ArtworkGlyph());
        RightPanelGeneratedTitle().Text(track.ArtworkTitle());
        RightPanelGeneratedCaption().Text(track.ArtworkCaption());

        // Debounced — rapid Next skips collapse into one provider
        // request instead of N parallel ones. The Lyrics-pane click
        // path in xaml.cpp still calls HydrateLyricsForCurrentTrack()
        // directly because that's a single user gesture, not a rapid
        // train of state changes.
        QueueLyricsHydration();

        if (FsTitle())
        {
            FsTitle().Text(track.Title());
            FsArtist().Text(track.Artist());
            if (FsGeneratedGlyph()) FsGeneratedGlyph().Glyph(track.ArtworkGlyph());
        }

        auto artworkEpoch = ++m_nowPlayingArtworkEpoch;
        auto showGeneratedArtwork = [this]()
        {
            BottomPlayerArt().Source(nullptr);
            BottomPlayerArt().Opacity(0.0);
            BottomGeneratedArtwork().Opacity(1.0);
            RightPanelArt().Source(nullptr);
            RightPanelArt().Opacity(0.0);
            RightPanelGeneratedArtwork().Opacity(1.0);
            if (FsArt())
            {
                FsArt().Source(nullptr);
                FsArt().Opacity(0.0);
                FsGeneratedArtwork().Opacity(1.0);
            }
        };
        auto showImageArtwork = [this](winrt::Microsoft::UI::Xaml::Media::ImageSource const& source)
        {
            BottomPlayerArt().Source(source);
            BottomPlayerArt().Opacity(1.0);
            BottomGeneratedArtwork().Opacity(0.0);
            RightPanelArt().Source(source);
            RightPanelArt().Opacity(1.0);
            RightPanelGeneratedArtwork().Opacity(0.0);
            if (FsArt())
            {
                FsArt().Source(source);
                FsArt().Opacity(1.0);
                FsGeneratedArtwork().Opacity(0.0);
            }
        };

        showGeneratedArtwork();
        if (track.ImageArtworkOpacity() > 0.0 && track.AlbumArt() != nullptr)
        {
            auto artworkUrl = track.ArtworkUrl();
            auto useFreshRemoteLoad = !artworkUrl.empty() && IsHttpUrl(artworkUrl);
            if (useFreshRemoteLoad)
            {
                try
                {
                    auto bitmap = CreateMusicArtworkBitmap();
                    auto normalizedArtworkUrl = NormalizeMusicArtworkUrl(artworkUrl);
                    // Use only the provider-issued signed artwork URL. External
                    // image hosts pass through unchanged; expired provider art
                    // falls back to the generated placeholder without exposing
                    // any long-lived credential in a URL.
                    auto freshArtworkUrl = ProviderArtworkUrlFor(normalizedArtworkUrl);
                    if (!bitmap || freshArtworkUrl.empty())
                    {
                        showGeneratedArtwork();
                    }
                    else
                    {
                        auto weakThis = get_weak();
                        bitmap.ImageOpened([weakThis, artworkEpoch, bitmap](auto const&, auto const&)
                        {
                            if (auto self = weakThis.get())
                            {
                                if (self->m_nowPlayingArtworkEpoch == artworkEpoch)
                                {
                                    self->BottomPlayerArt().Source(bitmap);
                                    self->BottomPlayerArt().Opacity(1.0);
                                    self->BottomGeneratedArtwork().Opacity(0.0);
                                    self->RightPanelArt().Source(bitmap);
                                    self->RightPanelArt().Opacity(1.0);
                                    self->RightPanelGeneratedArtwork().Opacity(0.0);
                                    if (self->FsArt())
                                    {
                                        self->FsArt().Source(bitmap);
                                        self->FsArt().Opacity(1.0);
                                        self->FsGeneratedArtwork().Opacity(0.0);
                                    }
                                }
                            }
                        });
                        bitmap.ImageFailed([weakThis, artworkEpoch](auto const&, auto const&)
                        {
                            if (auto self = weakThis.get())
                            {
                                if (self->m_nowPlayingArtworkEpoch == artworkEpoch)
                                {
                                    self->BottomPlayerArt().Source(nullptr);
                                    self->BottomPlayerArt().Opacity(0.0);
                                    self->BottomGeneratedArtwork().Opacity(1.0);
                                    self->RightPanelArt().Source(nullptr);
                                    self->RightPanelArt().Opacity(0.0);
                                    self->RightPanelGeneratedArtwork().Opacity(1.0);
                                    if (self->FsArt())
                                    {
                                        self->FsArt().Source(nullptr);
                                        self->FsArt().Opacity(0.0);
                                        self->FsGeneratedArtwork().Opacity(1.0);
                                    }
                                }
                            }
                        });
                        BottomPlayerArt().Source(bitmap);
                        RightPanelArt().Source(bitmap);
                        bitmap.UriSource(winrt::Windows::Foundation::Uri(freshArtworkUrl));
                    }
                }
                catch (...)
                {
                    showGeneratedArtwork();
                }
            }
            else
            {
                showImageArtwork(track.AlbumArt());
            }
        }

        // Honor the "Show album art on player bar" preference.
        ApplyShowAlbumArt();

        NpMetaAlbum().Text(track.Album().empty() ? (remote ? L"Remote music" : L"Local Library") : track.Album());
        NpMetaYear().Text(track.SourceLabel().empty() ? (remote ? L"Remote" : L"Local") : track.SourceLabel());
        NpMetaFormat().Text(remote ? L"Stream" : L"File");

        UpdateSMTCMetadata(track);
        UpdateDiscordNowPlaying(track);
        PlayPauseIcon().Glyph(L"\xE769");
        BeginPlaybackQualification(track);
        UpdateLikeButton(track);

        // A fresh, user-or-queue-initiated play resets the stream auto-recovery
        // budget and position tracking. (The recovery path re-opens the source
        // directly, not through PlayTrack, so it doesn't reset this counter.)
        m_streamRecoverAttempts = 0;
        m_lastPlaybackPositionSeconds = 0.0;
        m_pendingResumeSeekSeconds = -1.0;

        return true;
        }
        catch (...)
        {
            return false;
        }
    }

    winrt::Windows::Foundation::IAsyncAction MainWindow::ResolveRemotePlaybackAsync(
        winrt::Last_Music_Player::TrackInfo track,
        bool startAsNewPlay,
        bool gaplessTransitioned,
        double resumeSeconds,
        uint64_t resolveEpoch)
    {
        auto lifetime = get_strong();
        auto dispatcher = DispatcherQueue();
        if (!track || !IsHttpUrl(track.SourceUrl()))
        {
            co_return;
        }

        auto& remoteMusic = RemoteMusicServiceService();
        auto remoteScope = remoteMusic.CaptureScope();
        auto remoteMode = remoteScope.Mode;
        bool accountMode = remoteMode == LastMusicPlayer::Backend::RemoteAccessMode::Account;
        auto baseUrl = accountMode ? winrt::hstring{} : CurrentProviderBaseUrl();
        auto apiKey = accountMode ? winrt::hstring{} : CredentialStoreService().ReadProviderApiKey();
        winrt::hstring payload;
        try
        {
            payload = co_await remoteMusic.ResolveUrlAsync(track.SourceUrl());
        }
        catch (...)
        {
        }

        winrt::hstring streamUrl;
        winrt::hstring artworkUrl;
        if (!payload.empty())
        {
            try
            {
                auto root = winrt::Windows::Data::Json::JsonObject::Parse(payload);
                auto result = root.GetNamedObject(L"result", nullptr);
                if (result)
                {
                    streamUrl = result.GetNamedString(L"streamUrl", L"");
                    artworkUrl = result.GetNamedString(L"artworkUrl", L"");
                }
            }
            catch (...)
            {
            }
        }

        // Every failure from here on leaves safeStreamUrl empty and is reported
        // once by the common tail below, on the UI thread. Returning silently
        // instead would strand the queue on a track that never starts, which is
        // easy to hit now that catalog-origin tracks reach playback and the
        // mirror occasionally has no match for one.
        winrt::hstring safeStreamUrl;
        winrt::hstring safeArtworkUrl;
        if (accountMode)
        {
            safeStreamUrl = LastMusicPlayer::Backend::IsTrustedAccountMediaUrl(
                streamUrl,
                LastMusicPlayer::Backend::BuildConfig::AccountMediaOrigin,
                L"stream")
                ? streamUrl
                : winrt::hstring{};
        }
        else
        {
            safeStreamUrl = LastMusicPlayer::Backend::BuildProviderStreamUrl(
                streamUrl,
                track.SourceUrl(),
                track.Provider(),
                artworkUrl,
                baseUrl,
                apiKey);
            safeArtworkUrl = LastMusicPlayer::Backend::BuildProviderArtworkUrl(
                artworkUrl,
                baseUrl,
                apiKey);
        }

        co_await wil::resume_foreground(dispatcher);
        if (resolveEpoch != m_remotePlaybackResolveEpoch
            || !remoteMusic.IsCurrent(remoteScope))
        {
            co_return;
        }
        if (remoteMode == LastMusicPlayer::Backend::RemoteAccessMode::ApiKey
            && (CurrentProviderBaseUrl() != baseUrl
                || CredentialStoreService().ReadProviderApiKey() != apiKey))
        {
            co_return;
        }
        if (safeStreamUrl.empty())
        {
            HandleRemoteResolveFailure(track, startAsNewPlay, resolveEpoch);
            co_return;
        }
        m_remoteResolveFailures = 0;

        auto target = track;
        if (!startAsNewPlay)
        {
            auto current = AudioPlayerService().GetCurrentTrack();
            if (!current || CatalogSourceKey(current) != CatalogSourceKey(track))
            {
                co_return;
            }
            target = current;
        }

        target.FilePath(safeStreamUrl);
        if (!safeArtworkUrl.empty())
        {
            ApplyMusicArtwork(target, safeArtworkUrl, L"track");
        }

        auto sourceKey = CatalogSourceKey(target);
        auto cacheKey = ApiKeyStreamCacheKey(remoteScope, track);
        if (!cacheKey.empty() && remoteMusic.IsCurrent(remoteScope))
        {
            StreamCacheService().Prefetch(
                cacheKey,
                std::wstring{ safeStreamUrl.c_str() });
        }
        if (DatabaseService().IsInitialized() && !sourceKey.empty() && !accountMode)
        {
            DatabaseService().UpsertRemoteTrack(target, sourceKey);
        }

        if (startAsNewPlay)
        {
            PlayTrack(target, gaplessTransitioned);
            co_return;
        }

        auto source = BuildMediaSourceForTrack(target);
        if (!source)
        {
            HandleRemoteResolveFailure(track, startAsNewPlay, resolveEpoch);
            co_return;
        }
        m_pendingResumeSeekSeconds = resumeSeconds;
        AudioPlayerService().PlayGaplessCurrent(source);
        AudioPlayerService().LoadTrack(target);

        double baseVolume = SettingsManagerService().GetVolume();
        if (baseVolume < 0.0) baseVolume = 0.0;
        if (baseVolume > 1.0) baseVolume = 1.0;
        AudioPlayerService().GetMediaPlayer().Volume(baseVolume);
        PlayPauseIcon().Glyph(L"\xE769");
        if (FsPlayPauseIcon()) FsPlayPauseIcon().Glyph(L"\xE769");
    }

    void MainWindow::OnMediaFailed(
        winrt::Windows::Media::Playback::MediaPlayer const& sender,
        winrt::Windows::Media::Playback::MediaPlayerFailedEventArgs const& args)
    {
        (void)sender;
        (void)args;
        // MediaFailed fires on the media thread; all state we touch is UI-thread.
        DispatcherQueue().TryEnqueue([this]()
        {
            if (m_sink != PlaybackSink::Local)
            {
                return;
            }

            auto current = AudioPlayerService().GetCurrentTrack();
            if (!current)
            {
                return;
            }

            bool remote = ToLowerCopy(current.SourceKind()) == L"remote"
                || (!current.File() && IsHttpUrl(current.FilePath()));
            if (!remote)
            {
                return;
            }

            // Bounded retry within a rolling window so a genuinely unavailable
            // source cannot trigger an endless resolve/reopen loop.
            auto now = ::GetTickCount64();
            if (m_lastStreamRecoverTickMs == 0 || now - m_lastStreamRecoverTickMs > 20000)
            {
                m_streamRecoverAttempts = 0;
            }
            m_lastStreamRecoverTickMs = now;
            if (m_streamRecoverAttempts >= 3)
            {
                TransportPause();
                return;
            }
            ++m_streamRecoverAttempts;

            // Prefer a complete local prefetch if one became ready while the
            // live stream was playing. It needs no network or credentials.
            auto& remoteMusic = RemoteMusicServiceService();
            auto remoteScope = remoteMusic.CaptureScope();
            auto sourceKey = ApiKeyStreamCacheKey(remoteScope, current);
            if (!sourceKey.empty()
                && remoteMusic.IsCurrent(remoteScope)
                && !StreamCacheService().ReadyPath(sourceKey).empty())
            {
                auto source = BuildMediaSourceForTrack(current);
                if (source)
                {
                    m_pendingResumeSeekSeconds = m_lastPlaybackPositionSeconds;
                    AudioPlayerService().PlayGaplessCurrent(source);
                    return;
                }
            }

            if (!IsHttpUrl(current.SourceUrl()))
            {
                TransportPause();
                return;
            }

            auto resolveEpoch = ++m_remotePlaybackResolveEpoch;
            RunDetached(ResolveRemotePlaybackAsync(
                current,
                false,
                false,
                m_lastPlaybackPositionSeconds,
                resolveEpoch));
        });
    }

    void MainWindow::OnMediaOpened(
        winrt::Windows::Media::Playback::MediaPlayer const& sender,
        winrt::Windows::Foundation::IInspectable const& args)
    {
        (void)sender;
        (void)args;
        // Apply a pending resume-seek (set by OnMediaFailed) once the re-opened
        // media is ready. Marshal to the UI thread so member access is single-
        // threaded.
        DispatcherQueue().TryEnqueue([this]()
        {
            if (m_pendingResumeSeekSeconds < 0.0)
            {
                return;
            }
            double seekSeconds = m_pendingResumeSeekSeconds;
            m_pendingResumeSeekSeconds = -1.0;
            if (m_sink != PlaybackSink::Local)
            {
                return;
            }
            try
            {
                auto session = AudioPlayerService().GetMediaPlayer().PlaybackSession();
                auto dur = static_cast<double>(session.NaturalDuration().count()) / 10000000.0;
                double target = seekSeconds;
                if (dur > 1.0 && target > dur - 1.0)
                {
                    target = dur - 1.0;  // don't seek to/past the very end
                }
                if (target > 0.5 && session.CanSeek())
                {
                    session.Position(winrt::Windows::Foundation::TimeSpan{ static_cast<int64_t>(target * 10000000.0) });
                }
            }
            catch (...) {}
        });
    }

    int MainWindow::ComputeAutoAdvanceIndex(int currentIdx, int queueSize) const
    {
        if (queueSize <= 0) return -1;
        // Repeat-one keeps the same track in the lookahead slot.
        if (m_queue.RepeatMode == 2) return currentIdx;

        if (m_queue.ShuffleEnabled && queueSize > 1)
        {
            // ShuffleOrder may be stale; if so, fall back to next slot.
            if (m_queue.ShuffleOrder.size() != static_cast<size_t>(queueSize))
            {
                int nextIdx = currentIdx + 1;
                if (nextIdx >= queueSize)
                {
                    return (m_queue.RepeatMode == 0) ? -1 : 0;
                }
                return nextIdx;
            }
            auto it = std::find(m_queue.ShuffleOrder.begin(), m_queue.ShuffleOrder.end(), currentIdx);
            if (it == m_queue.ShuffleOrder.end()) return m_queue.ShuffleOrder.front();
            auto pos = std::distance(m_queue.ShuffleOrder.begin(), it) + 1;
            if (pos >= static_cast<long long>(m_queue.ShuffleOrder.size()))
            {
                return (m_queue.RepeatMode == 0) ? -1 : m_queue.ShuffleOrder.front();
            }
            return m_queue.ShuffleOrder[static_cast<size_t>(pos)];
        }

        int nextIdx = currentIdx + 1;
        if (nextIdx >= queueSize)
        {
            return (m_queue.RepeatMode == 0) ? -1 : 0;
        }
        return nextIdx;
    }

    void MainWindow::RefreshGaplessLookahead()
    {
        auto& player = AudioPlayerService();
        if (!player.IsGaplessEnabled() || m_sink == PlaybackSink::Cast)
        {
            player.SetGaplessNext(nullptr);
            return;
        }
        auto const& queue = m_queue.Queue.empty() ? m_queue.CurrentPlaylist : m_queue.Queue;
        if (queue.empty())
        {
            player.SetGaplessNext(nullptr);
            return;
        }
        int currentIdx = m_queue.Queue.empty() ? m_queue.CurrentTrackIndex : m_queue.QueueIndex;
        int size = static_cast<int>(queue.size());
        int nextIdx = ComputeAutoAdvanceIndex(currentIdx, size);
        if (nextIdx < 0 || nextIdx >= size)
        {
            player.SetGaplessNext(nullptr);
            return;
        }
        // Skip lookahead for queues of length one + repeat-one: MediaPlaybackList
        // will not auto-loop to the same Items[0]; the natural MediaEnded path
        // re-issues PlayTrack which handles repeat correctly.
        if (nextIdx == currentIdx && size <= 1)
        {
            player.SetGaplessNext(nullptr);
            return;
        }
        auto source = BuildMediaSourceForTrack(queue[static_cast<size_t>(nextIdx)]);
        player.SetGaplessNext(source);
    }

    void MainWindow::ApplyGaplessSetting()
    {
        bool on = SettingsManagerService().GetBool(L"Gapless", true);
        AudioPlayerService().EnableGapless(on);
        // Note: gapless audio bridging is dormant; the preference is still
        // honored when MediaPlaybackList playback is restored.
    }

    void MainWindow::PersistTrackPlayback(
        winrt::Last_Music_Player::TrackInfo const& track,
        double positionSeconds,
        std::wstring const& accountOwnerId,
        uint64_t accountGeneration)
    {
        if (!DatabaseService().IsInitialized())
        {
            return;
        }

        if (RemoteMusicServiceService().Mode() == LastMusicPlayer::Backend::RemoteAccessMode::Account)
        {
            auto snapshot = AccountSessionService().Snapshot();
            if (!accountOwnerId.empty()
                && snapshot.Generation == accountGeneration
                && snapshot.Profile.Id == winrt::hstring(accountOwnerId)
                && (snapshot.Status == LastMusicPlayer::Backend::AccountSessionStatus::Validated
                    || snapshot.Status == LastMusicPlayer::Backend::AccountSessionStatus::Offline)
                && IsCompatibleAccountRemoteTrack(track))
            {
                DatabaseService().RecordAccountPlayback(accountOwnerId, track, positionSeconds);
            }
            return;
        }

        auto key = CatalogSourceKey(track);
        if (key.empty())
        {
            return;
        }

        auto remote = ToLowerCopy(track.SourceKind()) == L"remote" || (!track.File() && IsHttpUrl(track.FilePath()));
        if (remote)
        {
            DatabaseService().UpsertRemoteTrack(track, key);
        }
        else if (!track.FilePath().empty())
        {
            DatabaseService().UpsertLocalTrack(track, key);
        }

        DatabaseService().RecordPlayback(key, m_homePlaySequence);
    }

    void MainWindow::UpdateLikeButton(winrt::Last_Music_Player::TrackInfo const& track)
    {
        auto liked = track && track.IsLiked();
        EnsureAccentBrushes();
        // Liked -> filled accent heart; not liked -> outline idle heart.
        // In this app's Segoe Fluent Icons, \xE00B renders filled and
        // \xEB51 renders as the outline heart.
        wchar_t const* glyph = liked ? L"\xE00B" : L"\xEB51";
        auto brush = liked ? m_brushAccent : m_brushGlyphIdle;
        if (BottomLikeIcon())
        {
            BottomLikeIcon().Glyph(glyph);
            if (brush) BottomLikeIcon().Foreground(brush);
        }
        if (FsLikeIcon())
        {
            FsLikeIcon().Glyph(glyph);
            if (brush) FsLikeIcon().Foreground(brush);
        }
        BottomLikeButton().Tag(winrt::box_value(winrt::hstring(CatalogSourceKey(track))));
    }

    void MainWindow::PlayPauseButton_Click(winrt::Windows::Foundation::IInspectable const& sender, winrt::Microsoft::UI::Xaml::RoutedEventArgs const& args)
    {
        (void)sender;
        (void)args;

        if (m_sink == PlaybackSink::Cast)
        {
            if (m_castSession.IsPlaying)
            {
                TransportPause();
            }
            else
            {
                TransportPlay();
            }
            return;
        }

        // First Play press after a launch resume: nothing is loaded in the
        // engine yet, so play the restored last track now (this is the real
        // play, so it records history/Discord/lyrics normally via PlayTrack).
        if (m_pendingResumeTrack && !AudioPlayerService().GetCurrentTrack())
        {
            auto resume = m_pendingResumeTrack;
            m_pendingResumeTrack = nullptr;
            std::vector<winrt::Last_Music_Player::TrackInfo> queue{ resume };
            SetPlaybackQueue(queue, 0);
            PlayTrack(resume);
            return;
        }

        auto session = AudioPlayerService().GetMediaPlayer().PlaybackSession();
        auto state = session.PlaybackState();

        if (state == winrt::Windows::Media::Playback::MediaPlaybackState::Playing)
        {
            AudioPlayerService().GetMediaPlayer().Pause();
            PlayPauseIcon().Glyph(L"\xE768"); // Play icon
            if (FsPlayPauseIcon()) FsPlayPauseIcon().Glyph(L"\xE768");
            // A manual pause cancels any pending autoplay auto-resume — a radio
            // fetch that lands later must not restart playback on its own.
            m_autoplay.ResumeWhenReady = false;
        }
        else
        {
            AudioPlayerService().GetMediaPlayer().Play();
            PlayPauseIcon().Glyph(L"\xE769"); // Pause icon
            if (FsPlayPauseIcon()) FsPlayPauseIcon().Glyph(L"\xE769");
        }
    }

    void MainWindow::ShowPlaybackNotice(winrt::hstring const& message)
    {
        if (!PlaybackNoticePanel() || message.empty())
        {
            return;
        }

        PlaybackNoticeText().Text(message);
        PlaybackNoticePanel().Visibility(winrt::Microsoft::UI::Xaml::Visibility::Visible);

        if (!m_playbackNoticeTimer)
        {
            m_playbackNoticeTimer = winrt::Microsoft::UI::Xaml::DispatcherTimer();
            m_playbackNoticeTimer.Interval(std::chrono::seconds(5));
            m_playbackNoticeTimer.Tick([this](winrt::Windows::Foundation::IInspectable const&, winrt::Windows::Foundation::IInspectable const&)
            {
                m_playbackNoticeTimer.Stop();
                if (PlaybackNoticePanel())
                {
                    PlaybackNoticePanel().Visibility(winrt::Microsoft::UI::Xaml::Visibility::Collapsed);
                }
            });
        }

        // Restart so a second notice gets its own full dwell time.
        m_playbackNoticeTimer.Stop();
        m_playbackNoticeTimer.Start();
    }

    void MainWindow::HandleRemoteResolveFailure(
        winrt::Last_Music_Player::TrackInfo const& track,
        bool startAsNewPlay,
        uint64_t resolveEpoch)
    {
        // A newer play superseded this resolve; its own outcome owns the UI.
        if (resolveEpoch != m_remotePlaybackResolveEpoch)
        {
            return;
        }

        auto title = track ? track.Title() : winrt::hstring{};
        ShowPlaybackNotice(title.empty()
            ? winrt::hstring{ L"No playable source for this track." }
            : winrt::hstring{ L"No playable source for \"" + std::wstring{ title.c_str() } + L"\"." });

        if (!startAsNewPlay)
        {
            // Mid-stream recovery: the track was playing a moment ago, so stay on
            // it and let the user retry rather than jumping somewhere else.
            TransportPause();
            return;
        }

        // PlayTrack returns true as soon as it hands a remote track to the
        // resolver, so AdvanceQueue already counted this as a successful play and
        // cleared its own failure counter. Without a separate bound here, a queue
        // of unresolvable tracks would advance forever, one network round trip per
        // step.
        if (++m_remoteResolveFailures >= 3)
        {
            m_remoteResolveFailures = 0;
            ShowPlaybackNotice(L"Could not play the next few tracks. Playback stopped.");
            TransportPause();
            return;
        }
        AdvanceQueue(+1, true);
    }

    void MainWindow::QueueVolumePersist(double volume)
    {
        m_pendingPersistedVolume = std::clamp(volume, 0.0, 1.0);
        m_volumePersistQueued = true;

        if (!m_volumePersistTimer)
        {
            m_volumePersistTimer = winrt::Microsoft::UI::Xaml::DispatcherTimer();
            m_volumePersistTimer.Interval(std::chrono::milliseconds(350));
            m_volumePersistTimer.Tick([this](winrt::Windows::Foundation::IInspectable const&, winrt::Windows::Foundation::IInspectable const&)
            {
                m_volumePersistTimer.Stop();
                FlushPendingVolumePersist();
            });
        }

        m_volumePersistTimer.Stop();
        m_volumePersistTimer.Start();
    }

    void MainWindow::FlushPendingVolumePersist()
    {
        if (!m_volumePersistQueued)
        {
            return;
        }

        m_volumePersistQueued = false;
        if (m_volumePersistTimer)
        {
            m_volumePersistTimer.Stop();
        }
        SettingsManagerService().SetVolume(m_pendingPersistedVolume);
    }

    void MainWindow::VolumeSlider_ValueChanged(winrt::Windows::Foundation::IInspectable const& sender, winrt::Microsoft::UI::Xaml::Controls::Primitives::RangeBaseValueChangedEventArgs const& args)
    {
        (void)sender;
        // Slider operates on a 0-100 scale; the audio domain is 0.0-1.0.
        double volume = args.NewValue() / 100.0;
        if (volume < 0.0) volume = 0.0;
        if (volume > 1.0) volume = 1.0;
        if (volume > 0.0)
        {
            m_isMuted = false;
        }
        TransportSetVolume(volume);
        AudioPlayerService().SetVolume(volume);
        UpdateVolumeIcon(volume);
        if (VolumeFillScale())
        {
            VolumeFillScale().ScaleX(volume); // fill bar uses the 0.0-1.0 scale
        }
        // Only a genuine user change persists. Mute (slider -> 0) and the
        // startup restore set the slider programmatically and must NOT
        // overwrite the saved level (that is what zeroed it before).
        if (!m_suppressVolumePersist)
        {
            QueueVolumePersist(volume);
        }
    }

    void MainWindow::EqualizerButton_Click(winrt::Windows::Foundation::IInspectable const& sender, winrt::Microsoft::UI::Xaml::RoutedEventArgs const& args)
    {
        (void)sender;
        (void)args;
        // Feature is intentionally disabled until the audio-processing issues
        // are addressed. The button is disabled in XAML; keep this handler a
        // no-op as a second guard for programmatic invocation.
    }

    void MainWindow::MuteButton_Click(winrt::Windows::Foundation::IInspectable const& sender, winrt::Microsoft::UI::Xaml::RoutedEventArgs const& args)
    {
        (void)sender;
        (void)args;
        if (!m_isMuted)
        {
            m_volumeBeforeMute = VolumeSlider().Value();
            m_isMuted = true;
            // Mute is transient: drop to 0 audibly/visually but do NOT
            // persist 0, so the saved level survives a close-while-muted.
            bool prevSuppress = m_suppressVolumePersist;
            m_suppressVolumePersist = true;
            VolumeSlider().Value(0.0); // fires VolumeSlider_ValueChanged
            m_suppressVolumePersist = prevSuppress;
        }
        else
        {
            m_isMuted = false;
            double restore = m_volumeBeforeMute > 0.0 ? m_volumeBeforeMute : 50.0;
            VolumeSlider().Value(restore); // genuine change -> persists
        }
    }

    void MainWindow::UpdateVolumeIcon(double volume)
    {
        if (!VolumeIcon()) return;
        wchar_t const* glyph;
        if (volume <= 0.0001)        glyph = L"\xE74F"; // muted
        else if (volume < 0.34)      glyph = L"\xE993"; // volume low (bar 1)
        else if (volume < 0.67)      glyph = L"\xE994"; // volume medium (bar 2)
        else                         glyph = L"\xE767"; // volume high (bar 3)
        VolumeIcon().Glyph(glyph);
    }

    void MainWindow::TransportPlay()
    {
        if (m_sink == PlaybackSink::Cast)
        {
            m_cast.Play();
            m_castSession.IsPlaying = true;
            m_castSession.ProgressStampMs = ::GetTickCount64();
            m_cast.RequestStatus();
            PlayPauseIcon().Glyph(L"\xE769");
            if (FsPlayPauseIcon()) FsPlayPauseIcon().Glyph(L"\xE769");
            UpdateDiscordPlaybackState(true, m_castSession.CurrentSeconds, m_castSession.DurationSeconds);
            return;
        }
        AudioPlayerService().GetMediaPlayer().Play();
    }

    void MainWindow::TransportPause()
    {
        if (m_sink == PlaybackSink::Cast)
        {
            auto now = ::GetTickCount64();
            if (m_castSession.IsPlaying && m_castSession.ProgressStampMs > 0)
            {
                m_castSession.CurrentSeconds += static_cast<double>(now - m_castSession.ProgressStampMs) / 1000.0;
            }
            m_castSession.ProgressStampMs = now;
            m_cast.Pause();
            m_castSession.IsPlaying = false;
            ApplyPlaybackProgress(m_castSession.CurrentSeconds, m_castSession.DurationSeconds);
            m_cast.RequestStatus();
            UpdateDiscordPlaybackState(false, m_castSession.CurrentSeconds, m_castSession.DurationSeconds);
        }
        else
        {
            AudioPlayerService().GetMediaPlayer().Pause();
        }
        PlayPauseIcon().Glyph(L"\xE768");
        if (FsPlayPauseIcon()) FsPlayPauseIcon().Glyph(L"\xE768");
    }

    void MainWindow::TransportSeekSeconds(double seconds)
    {
        if (m_sink == PlaybackSink::Cast)
        {
            m_castSession.CurrentSeconds = seconds;
            m_castSession.ProgressStampMs = ::GetTickCount64();
            ApplyPlaybackProgress(m_castSession.CurrentSeconds, m_castSession.DurationSeconds);
            m_cast.Seek(seconds);
            m_cast.RequestStatus();
            UpdateDiscordPlaybackState(m_castSession.IsPlaying, seconds, m_castSession.DurationSeconds);
            return;
        }
        auto newPosition = std::chrono::seconds(static_cast<long long>(seconds));
        AudioPlayerService().GetMediaPlayer().PlaybackSession().Position(newPosition);

        bool playing = true;
        double position = seconds;
        double duration = 0.0;
        SampleDiscordPlaybackSnapshot(playing, position, duration);
        UpdateDiscordPlaybackState(playing, seconds, duration);
    }

    void MainWindow::TransportSetVolume(double volume)
    {
        if (m_sink == PlaybackSink::Cast)
        {
            m_cast.SetVolume(volume);
            return;
        }
        AudioPlayerService().GetMediaPlayer().Volume(volume);
    }

    void MainWindow::ExpandButton_Click(winrt::Windows::Foundation::IInspectable const& sender, winrt::Microsoft::UI::Xaml::RoutedEventArgs const& args)
    {
        (void)sender;
        (void)args;
        m_fullScreenOpen = true;
        if (auto current = AudioPlayerService().GetCurrentTrack())
        {
            UpdateFullScreenNowPlaying(current);
        }
        NowPlayingFullScreen().Visibility(winrt::Microsoft::UI::Xaml::Visibility::Visible);
    }

    void MainWindow::FsCloseButton_Click(winrt::Windows::Foundation::IInspectable const& sender, winrt::Microsoft::UI::Xaml::RoutedEventArgs const& args)
    {
        (void)sender;
        (void)args;
        m_fullScreenOpen = false;
        NowPlayingFullScreen().Visibility(winrt::Microsoft::UI::Xaml::Visibility::Collapsed);
    }

    void MainWindow::UpdateFullScreenNowPlaying(winrt::Last_Music_Player::TrackInfo const& track)
    {
        if (!track) return;
        FsTitle().Text(track.Title());
        FsArtist().Text(track.Artist());
        if (FsGeneratedGlyph())
        {
            FsGeneratedGlyph().Glyph(track.ArtworkGlyph());
        }
        auto art = track.AlbumArt();
        if (art)
        {
            FsArt().Source(art);
            FsArt().Opacity(1.0);
            FsGeneratedArtwork().Opacity(0.0);
        }
        else
        {
            FsArt().Source(nullptr);
            FsArt().Opacity(0.0);
            FsGeneratedArtwork().Opacity(1.0);
        }
    }

    void MainWindow::TimelineSlider_ValueChanged(winrt::Windows::Foundation::IInspectable const& sender, winrt::Microsoft::UI::Xaml::Controls::Primitives::RangeBaseValueChangedEventArgs const& e)
    {
        (void)sender;
        if (!m_isUpdatingSlider) {
            TransportSeekSeconds(e.NewValue());
        }
    }

    void MainWindow::ApplyPlaybackProgress(double currentSeconds, double totalSeconds)
    {
        if (!std::isfinite(currentSeconds) || currentSeconds < 0.0)
        {
            currentSeconds = 0.0;
        }
        if (!std::isfinite(totalSeconds) || totalSeconds < 0.0)
        {
            totalSeconds = 0.0;
        }

        auto currentTrack = AudioPlayerService().GetCurrentTrack();
        if (totalSeconds <= 0.0 && currentTrack && currentTrack.DurationSeconds() > 0.0)
        {
            totalSeconds = currentTrack.DurationSeconds();
        }

        auto maxVal = totalSeconds > 0.0 ? totalSeconds : 100.0;
        currentSeconds = std::clamp(currentSeconds, 0.0, maxVal);
        auto curText = LastMusicPlayer::Frontend::UIHelpers::FormatTime(currentSeconds);
        auto durText = LastMusicPlayer::Frontend::UIHelpers::FormatTime(totalSeconds);

        m_isUpdatingSlider = true;
        if (auto slider = PlaybackSlider())
        {
            slider.Maximum(maxVal);
            slider.Value(currentSeconds);
        }
        // Drive the visible fill bar (Border + ScaleTransform layered behind
        // the invisible Slider). WinUI Slider's HorizontalDecreaseRect can't
        // reach the full track edge at Value == Maximum due to thumb-layout
        // reservations even when the thumb is sized 0; the ScaleTransform
        // ratio is deterministic.
        double ratio = (maxVal > 0.0) ? (currentSeconds / maxVal) : 0.0;
        if (ratio < 0.0) ratio = 0.0;
        else if (ratio > 1.0) ratio = 1.0;
        if (auto scale = PlaybackProgressFillScale())
        {
            scale.ScaleX(ratio);
        }
        if (auto scale = FsProgressFillScale())
        {
            scale.ScaleX(ratio);
        }
        if (auto fsSlider = FsSlider())
        {
            fsSlider.Maximum(maxVal);
            fsSlider.Value(currentSeconds);
        }
        m_isUpdatingSlider = false;

        if (auto text = CurrentTimeText())
        {
            text.Text(curText);
        }
        if (auto text = TotalTimeText())
        {
            text.Text(durText);
        }
        if (auto text = FsCurrentTime())
        {
            text.Text(curText);
        }
        if (auto text = FsTotalTime())
        {
            text.Text(durText);
        }
    }

    void MainWindow::RefreshPlaybackProgress()
    {
        if (m_sink == PlaybackSink::Cast)
        {
            auto now = ::GetTickCount64();
            if (m_castSession.LastStatusRequestMs == 0 || now - m_castSession.LastStatusRequestMs >= 2000)
            {
                m_castSession.LastStatusRequestMs = now;
                m_cast.RequestStatus();
            }

            auto currentSeconds = m_castSession.CurrentSeconds;
            if (m_castSession.IsPlaying && m_castSession.ProgressStampMs > 0)
            {
                currentSeconds += static_cast<double>(now - m_castSession.ProgressStampMs) / 1000.0;
            }
            ApplyPlaybackProgress(currentSeconds, m_castSession.DurationSeconds);
            ObservePlaybackQualification(m_castSession.IsPlaying, currentSeconds);
            RefreshDiscordPresenceIfNeeded(
                m_castSession.IsPlaying,
                currentSeconds,
                m_castSession.DurationSeconds);

            auto glyph = m_castSession.IsPlaying ? L"\xE769" : L"\xE768";
            if (auto icon = PlayPauseIcon())
            {
                icon.Glyph(glyph);
            }
            if (auto icon = FsPlayPauseIcon())
            {
                icon.Glyph(glyph);
            }
            return;
        }

        if (m_sink != PlaybackSink::Local)
        {
            return;
        }

        auto mediaPlayer = AudioPlayerService().GetMediaPlayer();
        if (!mediaPlayer)
        {
            return;
        }

        auto session = mediaPlayer.PlaybackSession();
        auto currentSeconds = static_cast<double>(session.Position().count()) / 10000000.0;
        auto totalSeconds = static_cast<double>(session.NaturalDuration().count()) / 10000000.0;
        // Remember where we are so OnMediaFailed can resume near this point if
        // the live stream drops mid-track.
        if (currentSeconds > 0.0)
        {
            m_lastPlaybackPositionSeconds = currentSeconds;
        }
        ApplyPlaybackProgress(currentSeconds, totalSeconds);
        if (m_railOnLyrics && !m_currentLyricsSynced.empty())
        {
            UpdateActiveLyricLine(static_cast<int64_t>(currentSeconds * 1000.0));
        }

        auto state = session.PlaybackState();
        ObservePlaybackQualification(
            state == winrt::Windows::Media::Playback::MediaPlaybackState::Playing,
            currentSeconds);
        RefreshDiscordPresenceIfNeeded(
            IsDiscordPlaybackActive(state),
            currentSeconds,
            totalSeconds);
        auto glyph = (state == winrt::Windows::Media::Playback::MediaPlaybackState::Playing)
            ? L"\xE769"
            : L"\xE768";
        if (auto icon = PlayPauseIcon())
        {
            icon.Glyph(glyph);
        }
        if (auto icon = FsPlayPauseIcon())
        {
            icon.Glyph(glyph);
        }
    }

    winrt::Windows::Foundation::IAsyncAction MainWindow::UpdateSMTCMetadata(winrt::Last_Music_Player::TrackInfo track)
    {
        // Prefer the window-bound SMTC (created via GetForWindow during
        // MainWindow init) — the player-internal one MediaPlayer exposes
        // doesn't reach the OS global media-session list for unpackaged
        // Win32 apps, so its DisplayUpdater changes never surface in the
        // Win+V quick-settings UI. Fall back to the player SMTC when the
        // interop call hasn't run yet (e.g. very early startup).
        auto smtc = m_windowSmtc
            ? m_windowSmtc
            : AudioPlayerService().GetMediaPlayer().SystemMediaTransportControls();
        auto updater = smtc.DisplayUpdater();
        updater.Type(winrt::Windows::Media::MediaPlaybackType::Music);
        updater.MusicProperties().Title(track.Title());
        updater.MusicProperties().Artist(track.Artist());

        try
        {
            auto file = track.File();
            if (!file)
            {
                updater.Thumbnail(nullptr);
                updater.Update();
                co_return;
            }

            auto thumb = co_await file.GetThumbnailAsync(
                winrt::Windows::Storage::FileProperties::ThumbnailMode::MusicView,
                1024,
                winrt::Windows::Storage::FileProperties::ThumbnailOptions::ResizeThumbnail);
            if (thumb && thumb.Type() == winrt::Windows::Storage::FileProperties::ThumbnailType::Image)
            {
                updater.Thumbnail(winrt::Windows::Storage::Streams::RandomAccessStreamReference::CreateFromStream(thumb.as<winrt::Windows::Storage::Streams::IRandomAccessStream>()));
            }
            else
            {
                updater.Thumbnail(nullptr);
            }
        }
        catch(...) { updater.Thumbnail(nullptr); }

        updater.Update();
    }

    winrt::Windows::Foundation::IAsyncAction MainWindow::PlayProviderTest_Click(winrt::Windows::Foundation::IInspectable const& sender, winrt::Microsoft::UI::Xaml::RoutedEventArgs const& args)
    {
        (void)sender;
        (void)args;

        auto baseUrl = ProviderBaseUrlBox().Text();
        auto apiKey = ProviderApiKeyBox().Password();
        if (baseUrl.empty())
        {
            ProviderTestStatusText().Text(L"Missing provider URL");
            co_return;
        }
        if (apiKey.empty())
        {
            ProviderTestStatusText().Text(L"Enter a new API key");
            co_return;
        }

        ProviderTestStatusText().Text(L"Connecting...");

        try
        {
            auto status = co_await RemoteMusicServiceService().TestApiKeyAsync(baseUrl, apiKey);
            if (status == 200)
            {
                auto operationLease = UserDataOperationGateService().TryEnter();
                if (!operationLease)
                {
                    ProviderTestStatusText().Text(L"Cleanup is in progress");
                    co_return;
                }
                if (ProviderBaseUrlBox().Text() != baseUrl
                    || ProviderApiKeyBox().Password() != apiKey)
                {
                    ProviderTestStatusText().Text(L"Provider settings changed. Try again.");
                    co_return;
                }

                if (!CredentialStoreService().WriteProviderApiKey(apiKey)
                    || CredentialStoreService().ReadProviderApiKey() != apiKey)
                {
                    ProviderTestStatusText().Text(L"Could not store API key");
                    co_return;
                }
                WriteAppSettingString(L"ProviderBaseUrl", baseUrl);
                RemoteMusicServiceService().SetMode(LastMusicPlayer::Backend::RemoteAccessMode::ApiKey);
                InvalidateRemoteScopeWork();
                DatabaseService().SetRemoteLibraryContext(L"ApiKey");
                ProviderApiKeyBox().Password(L"");
                m_remoteSearchCache.clear();
                ProviderTestStatusText().Text(L"Configured");
                UpdateSongsScopeLabel();
                RefreshAccountSettingsUi();
                operationLease.reset();
                co_await HydrateHomeAsync(true);
            }
            else if (status == 401)
            {
                ProviderTestStatusText().Text(L"Unauthorized");
            }
            else
            {
                ProviderTestStatusText().Text(L"Provider unavailable");
            }
        }
        catch (winrt::hresult_error const&)
        {
            ProviderTestStatusText().Text(L"Provider unavailable");
        }
        catch (...)
        {
            ProviderTestStatusText().Text(L"Provider unavailable");
        }
    }
    void MainWindow::DisconnectProvider_Click(
        winrt::Windows::Foundation::IInspectable const& sender,
        winrt::Microsoft::UI::Xaml::RoutedEventArgs const& args)
    {
        (void)sender;
        (void)args;

        auto operationLease = UserDataOperationGateService().TryEnter();
        if (!operationLease)
        {
            ProviderTestStatusText().Text(L"Cleanup is in progress");
            return;
        }

        if (!CredentialStoreService().DeleteProviderApiKey())
        {
            ProviderTestStatusText().Text(L"Could not remove API key");
            return;
        }

        auto urlRemoved = SettingsManagerService().Remove(L"ProviderBaseUrl");
        if (RemoteMusicServiceService().Mode() == LastMusicPlayer::Backend::RemoteAccessMode::ApiKey)
        {
            RemoteMusicServiceService().SetMode(LastMusicPlayer::Backend::RemoteAccessMode::LocalOnly);
            DatabaseService().SetRemoteLibraryContext(L"LocalOnly");
        }
        InvalidateRemoteScopeWork();
        ProviderBaseUrlBox().Text(L"");
        ProviderApiKeyBox().Password(L"");
        m_remoteSearchCache.clear();
        ProviderTestStatusText().Text(urlRemoved ? L"Not configured" : L"API key removed; provider URL cleanup failed");
        RefreshAccountSettingsUi();
        UpdateSongsScopeLabel();
    }


}
