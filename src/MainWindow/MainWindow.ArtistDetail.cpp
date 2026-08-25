#include "pch.h"
#include "MainWindow.xaml.h"
#include "MainWindow.Internal.h"

#include "Backend/AccountClient.h"
#include "Backend/AccountArtistDetailPolicy.h"
#include "Backend/ProviderHelpers.h"
#include "Frontend/ArtistPagePolicy.h"
#include "Frontend/RoundedCornerClip.h"
#include "Frontend/UIHelpers.h"

#include <winrt/Windows.System.Threading.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <memory>
#include <string_view>
#include <unordered_map>
#include <utility>

namespace winrt::Last_Music_Player::implementation
{
    namespace
    {
        std::wstring DetailCacheKey(
            LastMusicPlayer::Backend::RemoteScopeSnapshot const& scope,
            LastMusicPlayer::Backend::RemoteDetailIdentity const& identity,
            std::wstring_view discriminator = {})
        {
            auto query = LastMusicPlayer::Backend::BuildRemoteDetailQuery(identity);
            if (!query)
            {
                return {};
            }
            auto key = LastMusicPlayer::Backend::RemoteScopeCacheKey(scope);
            key += L"|";
            if (!discriminator.empty())
            {
                key += discriminator;
                key += L"|";
            }
            key += query->c_str();
            return key;
        }

        std::wstring DetailCacheKey(
            LastMusicPlayer::Backend::RemoteScopeSnapshot const& scope,
            LastMusicPlayer::Backend::AccountArtistDetailTarget const& target)
        {
            return DetailCacheKey(
                scope,
                LastMusicPlayer::Backend::RemoteIdentityFor(target),
                LastMusicPlayer::Backend::AccountArtistDetailCacheDiscriminator(target));
        }

        winrt::hstring ArtistMetadata(std::vector<winrt::hstring> const& genres)
        {
            std::wstring text{ L"artist" };
            for (auto const& genre : genres)
            {
                if (!genre.empty())
                {
                    text += L" · ";
                    text += genre.c_str();
                }
            }
            return winrt::hstring{ text };
        }

        winrt::hstring ReleaseKindText(LastMusicPlayer::Backend::RemoteReleaseKind kind)
        {
            switch (kind)
            {
            case LastMusicPlayer::Backend::RemoteReleaseKind::Single: return L"Single";
            case LastMusicPlayer::Backend::RemoteReleaseKind::Ep: return L"EP";
            default: return L"Album";
            }
        }

        winrt::Windows::Foundation::IAsyncOperation<winrt::hstring> WithDetailTimeout(
            winrt::Windows::Foundation::IAsyncOperation<winrt::hstring> operation)
        {
            auto cancellation = co_await winrt::get_cancellation_token();
            cancellation.enable_propagation();
            auto timedOut = std::make_shared<std::atomic_bool>(false);
            auto timer = winrt::Windows::System::Threading::ThreadPoolTimer::CreateTimer(
                [operation, timedOut](winrt::Windows::System::Threading::ThreadPoolTimer const&)
                {
                    timedOut->store(true, std::memory_order_release);
                    try
                    {
                        operation.Cancel();
                    }
                    catch (...)
                    {
                    }
                },
                std::chrono::seconds(5));
            try
            {
                auto payload = co_await operation;
                timer.Cancel();
                co_return payload;
            }
            catch (winrt::hresult_canceled const&)
            {
                timer.Cancel();
                if (timedOut->load(std::memory_order_acquire))
                {
                    throw winrt::hresult_error(
                        HRESULT_FROM_WIN32(ERROR_TIMEOUT),
                        L"The music detail request timed out.");
                }
                throw;
            }
            catch (...)
            {
                timer.Cancel();
                throw;
            }
        }

        LastMusicPlayer::Backend::AccountArtistDetailAttemptOutcome DetailFailureOutcome(
            winrt::hresult_error const& error) noexcept
        {
            if (error.code() == HRESULT_FROM_WIN32(ERROR_TIMEOUT))
            {
                return LastMusicPlayer::Backend::AccountArtistDetailAttemptOutcome::Timeout;
            }
            auto const status = LastMusicPlayer::Backend::AccountHttpStatus(error);
            if (status == 404)
            {
                return LastMusicPlayer::Backend::AccountArtistDetailAttemptOutcome::HttpNotFound;
            }
            return status == 0
                ? LastMusicPlayer::Backend::AccountArtistDetailAttemptOutcome::TransportFailure
                : LastMusicPlayer::Backend::AccountArtistDetailAttemptOutcome::HttpFailure;
        }
    }

    winrt::Last_Music_Player::TrackInfo MainWindow::ArtistTrackFromCatalogItem(
        LastMusicPlayer::Backend::CatalogItem const& item,
        int32_t index)
    {
        auto track = CatalogItemToTrack(item, index);
        track.Genre(L"song");
        detail::ApplyMusicArtwork(track, item.ArtworkUrl, L"track");
        return track;
    }

    void MainWindow::ArtistLibraryArtworkBorder_Loaded(
        winrt::Windows::Foundation::IInspectable const& sender,
        winrt::Microsoft::UI::Xaml::RoutedEventArgs const& args)
    {
        LibraryArtworkBorder_Loaded(sender, args);
        auto border = sender.try_as<winrt::Microsoft::UI::Xaml::Controls::Border>();
        auto artist = border ? border.Tag().try_as<winrt::Last_Music_Player::TrackInfo>() : nullptr;
        if (!artist || !artist.IsFollowed() || !artist.ArtworkUrl().empty())
        {
            return;
        }
        auto& remote = detail::RemoteMusicServiceService();
        auto scope = remote.CaptureScope();
        if (scope.Mode == LastMusicPlayer::Backend::RemoteAccessMode::LocalOnly
            || !remote.IsModeAvailable(scope.Mode))
        {
            return;
        }
        std::optional<LastMusicPlayer::Backend::AccountArtistDetailPlan> accountPlan;
        LastMusicPlayer::Backend::RemoteDetailIdentity requestIdentity;
        std::wstring requestKey;
        if (scope.Mode == LastMusicPlayer::Backend::RemoteAccessMode::Account)
        {
            auto const found = m_accountArtistDetailPlans.find(
                std::wstring(artist.RemoteId().c_str()));
            if (found == m_accountArtistDetailPlans.end())
            {
                return;
            }
            accountPlan = found->second;
            auto const targets = LastMusicPlayer::Backend::OrderedAccountArtistDetailTargets(
                *accountPlan);
            if (targets.empty())
            {
                return;
            }
            requestIdentity = LastMusicPlayer::Backend::RemoteIdentityFor(targets.front());
            requestKey = DetailCacheKey(scope, targets.front());
        }
        else
        {
            auto identity = ArtistIdentityForTrack(artist);
            requestIdentity = {
                winrt::hstring(identity.CatalogId),
                winrt::hstring(identity.SourceUrl),
                winrt::hstring(identity.Name)
            };
            requestKey = DetailCacheKey(scope, requestIdentity);
        }
        if (requestKey.empty() || !m_artistArtworkRequests.emplace(requestKey).second)
        {
            return;
        }
        detail::RunDetached(HydrateArtistLibraryArtworkAsync(
            border,
            artist,
            std::move(scope),
            std::move(requestIdentity),
            std::move(accountPlan),
            std::move(requestKey)));
    }

    winrt::Windows::Foundation::IAsyncAction MainWindow::HydrateArtistLibraryArtworkAsync(
        winrt::Microsoft::UI::Xaml::Controls::Border border,
        winrt::Last_Music_Player::TrackInfo artist,
        LastMusicPlayer::Backend::RemoteScopeSnapshot scope,
        LastMusicPlayer::Backend::RemoteDetailIdentity requestIdentity,
        std::optional<LastMusicPlayer::Backend::AccountArtistDetailPlan> accountPlan,
        std::wstring requestKey)
    {
        auto lifetime = get_strong();
        auto dispatcher = DispatcherQueue();
        std::optional<LastMusicPlayer::Backend::RemoteArtistDetail> artistDetail;
        bool cancelled = false;
        std::vector<std::pair<LastMusicPlayer::Backend::RemoteDetailIdentity, std::wstring>> requests;
        if (accountPlan && scope.Mode == LastMusicPlayer::Backend::RemoteAccessMode::Account)
        {
            for (auto const& target : LastMusicPlayer::Backend::OrderedAccountArtistDetailTargets(
                *accountPlan))
            {
                requests.emplace_back(
                    LastMusicPlayer::Backend::RemoteIdentityFor(target),
                    LastMusicPlayer::Backend::AccountArtistDetailCacheDiscriminator(target));
            }
        }
        else
        {
            requests.emplace_back(std::move(requestIdentity), std::wstring{});
        }

        auto& remote = detail::RemoteMusicServiceService();
        for (std::size_t index = 0; index < requests.size(); ++index)
        {
            if (!remote.IsCurrent(scope))
            {
                cancelled = true;
                break;
            }

            auto const cacheKey = DetailCacheKey(
                scope,
                requests[index].first,
                requests[index].second);
            auto const cached = m_artistDetailCache.find(cacheKey);
            if (!cacheKey.empty() && cached != m_artistDetailCache.end())
            {
                artistDetail = cached->second;
                break;
            }

            try
            {
                auto payload = co_await WithDetailTimeout(
                    remote.GetArtistDetailAsync(scope, requests[index].first));
                if (!remote.IsCurrent(scope))
                {
                    cancelled = true;
                    break;
                }
                co_await winrt::resume_background();
                artistDetail = LastMusicPlayer::Backend::ParseRemoteArtistDetail(payload);
                co_await wil::resume_foreground(dispatcher);
            }
            catch (winrt::hresult_canceled const&)
            {
                cancelled = true;
                break;
            }
            catch (winrt::hresult_error const& error)
            {
                auto const canFallback = index + 1 < requests.size()
                    && LastMusicPlayer::Backend::ShouldTryNextAccountArtistDetailTarget(
                        DetailFailureOutcome(error));
                if (canFallback)
                {
                    continue;
                }
                break;
            }
            catch (...)
            {
                auto const canFallback = index + 1 < requests.size()
                    && LastMusicPlayer::Backend::ShouldTryNextAccountArtistDetailTarget(
                        LastMusicPlayer::Backend::AccountArtistDetailAttemptOutcome::TransportFailure);
                if (canFallback)
                {
                    continue;
                }
                break;
            }

            if (!artistDetail)
            {
                auto const canFallback = index + 1 < requests.size()
                    && LastMusicPlayer::Backend::ShouldTryNextAccountArtistDetailTarget(
                        LastMusicPlayer::Backend::AccountArtistDetailAttemptOutcome::InvalidPayload);
                if (canFallback)
                {
                    continue;
                }
                break;
            }
            if (!remote.IsCurrent(scope))
            {
                cancelled = true;
                break;
            }
            if (!cacheKey.empty())
            {
                if (m_artistDetailCache.size() >= 32)
                {
                    m_artistDetailCache.erase(m_artistDetailCache.begin());
                }
                m_artistDetailCache[cacheKey] = *artistDetail;
            }
            break;
        }
        if (!dispatcher.HasThreadAccess())
        {
            co_await wil::resume_foreground(dispatcher);
        }

        m_artistArtworkRequests.erase(requestKey);
        if (cancelled || !artistDetail || !remote.IsCurrent(scope) || artistDetail->Artist.ArtworkUrl.empty()
            || !border || border.Tag() != artist)
        {
            co_return;
        }
        artist.ArtworkUrl(artistDetail->Artist.ArtworkUrl);
        auto grid = border.Child().try_as<winrt::Microsoft::UI::Xaml::Controls::Grid>();
        if (!grid)
        {
            co_return;
        }
        for (auto const& child : grid.Children())
        {
            auto image = child.try_as<winrt::Microsoft::UI::Xaml::Controls::Image>();
            if (image && image.Name() == L"ArtistCardArtwork")
            {
                if (scope.Mode == LastMusicPlayer::Backend::RemoteAccessMode::Account)
                {
                    QueueAccountArtworkImage(
                        image,
                        artist.ArtworkUrl(),
                        detail::ArtworkDetail::Tile,
                        artist,
                        AccountArtworkPriority::Visible);
                    break;
                }
                auto bitmap = detail::CreateMusicArtworkBitmap(
                    artist.ArtworkUrl(),
                    detail::ArtworkDetail::Tile);
                if (bitmap)
                {
                    artist.AlbumArt(bitmap);
                    artist.ImageArtworkOpacity(1.0);
                    artist.GeneratedArtworkOpacity(0.0);
                    image.Source(bitmap);
                }
                break;
            }
        }
    }

    void MainWindow::OpenArtistDetail(
        winrt::hstring const& name,
        winrt::hstring const& catalogId,
        winrt::hstring const& sourceUrl,
        winrt::hstring const& artworkUrl,
        winrt::Last_Music_Player::TrackInfo const& sourceGroup)
    {
        auto artistName = detail::TrimQuery(name);
        if (artistName.empty())
        {
            return;
        }

        auto const alreadyOpen = ArtistDetailContent()
            && ArtistDetailContent().Visibility() == winrt::Microsoft::UI::Xaml::Visibility::Visible;
        if (!alreadyOpen)
        {
            m_artistOriginNav = std::wstring{ m_currentNav.c_str() };
            m_artistBackStack.clear();
        }

        ShowPrimaryView(L"Library");
        HideLibraryDetail();
        LibraryHeaderBar().Visibility(winrt::Microsoft::UI::Xaml::Visibility::Collapsed);
        LibraryNavigationBar().Visibility(winrt::Microsoft::UI::Xaml::Visibility::Collapsed);
        LibraryTabsContent().Visibility(winrt::Microsoft::UI::Xaml::Visibility::Collapsed);
        ArtistDetailContent().Visibility(winrt::Microsoft::UI::Xaml::Visibility::Visible);
        ArtistDetailScroll().Visibility(winrt::Microsoft::UI::Xaml::Visibility::Collapsed);
        ArtistDetailSkeleton().Visibility(winrt::Microsoft::UI::Xaml::Visibility::Visible);
        m_pendingArtistScrollState.reset();
        ResetArtistShelfOffsets();
        m_currentArtistIdentity = { catalogId, sourceUrl, artistName };
        if (sourceGroup && m_currentArtistIdentity.CatalogId.empty())
        {
            m_currentArtistIdentity.CatalogId = sourceGroup.ArtistCatalogId();
        }
        m_currentAccountArtistPlan.reset();
        if (detail::RemoteMusicServiceService().Mode()
            == LastMusicPlayer::Backend::RemoteAccessMode::Account)
        {
            if (sourceGroup && sourceGroup.Provider() == L"account")
            {
                auto const found = m_accountArtistDetailPlans.find(
                    std::wstring(sourceGroup.RemoteId().c_str()));
                if (found != m_accountArtistDetailPlans.end())
                {
                    m_currentAccountArtistPlan = found->second;
                }
            }
            if (!m_currentAccountArtistPlan)
            {
                auto provider = sourceGroup
                    ? (sourceGroup.RemoteProvider().empty()
                        ? sourceGroup.Provider()
                        : sourceGroup.RemoteProvider())
                    : winrt::hstring{};
                LastMusicPlayer::Backend::ArtistIdentity requested{
                    std::wstring(m_currentArtistIdentity.CatalogId.c_str()),
                    std::wstring(m_currentArtistIdentity.SourceUrl.c_str()),
                    std::wstring(m_currentArtistIdentity.Name.c_str()),
                    std::wstring(provider.c_str())
                };
                LastMusicPlayer::Backend::AccountArtistDetailPlan const* match = nullptr;
                bool ambiguous = false;
                for (auto const& [publicId, plan] : m_accountArtistDetailPlans)
                {
                    (void)publicId;
                    if (!LastMusicPlayer::Backend::AccountArtistDetailPlanMatches(plan, requested))
                    {
                        continue;
                    }
                    if (match)
                    {
                        ambiguous = true;
                        break;
                    }
                    match = &plan;
                }
                if (match && !ambiguous)
                {
                    m_currentAccountArtistPlan = *match;
                }
            }
            if (m_currentAccountArtistPlan)
            {
                auto const targets = LastMusicPlayer::Backend::OrderedAccountArtistDetailTargets(
                    *m_currentAccountArtistPlan);
                if (!targets.empty())
                {
                    m_currentArtistIdentity = LastMusicPlayer::Backend::RemoteIdentityFor(
                        targets.front());
                }
            }
        }

        auto requestEpoch = ++m_artistRequestEpoch;
        if (m_artistLoadOperation)
        {
            m_artistLoadOperation.Cancel();
        }
        if (m_albumLoadOperation)
        {
            m_albumLoadOperation.Cancel();
        }
        m_artistLoadOperation = LoadArtistDetailAsync(
            m_currentArtistIdentity,
            m_currentAccountArtistPlan,
            artworkUrl.empty() && sourceGroup ? sourceGroup.ArtworkUrl() : artworkUrl,
            requestEpoch,
            false);
        detail::RunDetached(m_artistLoadOperation);
    }

    winrt::Windows::Foundation::IAsyncAction MainWindow::LoadArtistDetailAsync(
        LastMusicPlayer::Backend::RemoteDetailIdentity identity,
        std::optional<LastMusicPlayer::Backend::AccountArtistDetailPlan> accountPlan,
        winrt::hstring artworkUrl,
        uint64_t requestEpoch,
        bool manualRetry)
    {
        auto lifetime = get_strong();
        auto cancellation = co_await winrt::get_cancellation_token();
        cancellation.enable_propagation();
        (void)manualRetry;
        auto dispatcher = DispatcherQueue();
        auto& remoteMusic = detail::RemoteMusicServiceService();
        auto scope = remoteMusic.CaptureScope();
        std::optional<LastMusicPlayer::Backend::RemoteArtistDetail> remoteDetail;
        LastMusicPlayer::Backend::RemoteDetailIdentity selectedIdentity = identity;

        std::vector<std::pair<LastMusicPlayer::Backend::RemoteDetailIdentity, std::wstring>> requests;
        if (accountPlan && scope.Mode == LastMusicPlayer::Backend::RemoteAccessMode::Account)
        {
            for (auto const& target : LastMusicPlayer::Backend::OrderedAccountArtistDetailTargets(
                *accountPlan))
            {
                requests.emplace_back(
                    LastMusicPlayer::Backend::RemoteIdentityFor(target),
                    LastMusicPlayer::Backend::AccountArtistDetailCacheDiscriminator(target));
            }
        }
        else
        {
            requests.emplace_back(identity, std::wstring{});
        }

        if (scope.Mode != LastMusicPlayer::Backend::RemoteAccessMode::LocalOnly
            && remoteMusic.IsModeAvailable(scope.Mode))
        {
            for (std::size_t index = 0; index < requests.size(); ++index)
            {
                if (requestEpoch != m_artistRequestEpoch || !remoteMusic.IsCurrent(scope))
                {
                    co_return;
                }

                auto const cacheKey = DetailCacheKey(
                    scope,
                    requests[index].first,
                    requests[index].second);
                auto const cached = m_artistDetailCache.find(cacheKey);
                if (!cacheKey.empty() && cached != m_artistDetailCache.end())
                {
                    remoteDetail = cached->second;
                    selectedIdentity = requests[index].first;
                    break;
                }

                try
                {
                    auto payload = co_await WithDetailTimeout(
                        remoteMusic.GetArtistDetailAsync(scope, requests[index].first));
                    if (requestEpoch != m_artistRequestEpoch || !remoteMusic.IsCurrent(scope))
                    {
                        co_return;
                    }
                    co_await winrt::resume_background();
                    remoteDetail = LastMusicPlayer::Backend::ParseRemoteArtistDetail(payload);
                    co_await wil::resume_foreground(dispatcher);
                }
                catch (winrt::hresult_canceled const&)
                {
                    co_return;
                }
                catch (winrt::hresult_error const& error)
                {
                    remoteDetail.reset();
                    auto const canFallback = index + 1 < requests.size()
                        && LastMusicPlayer::Backend::ShouldTryNextAccountArtistDetailTarget(
                            DetailFailureOutcome(error));
                    if (canFallback)
                    {
                        continue;
                    }
                    break;
                }
                catch (...)
                {
                    remoteDetail.reset();
                    auto const canFallback = index + 1 < requests.size()
                        && LastMusicPlayer::Backend::ShouldTryNextAccountArtistDetailTarget(
                            LastMusicPlayer::Backend::AccountArtistDetailAttemptOutcome::TransportFailure);
                    if (canFallback)
                    {
                        continue;
                    }
                    break;
                }

                if (requestEpoch != m_artistRequestEpoch || !remoteMusic.IsCurrent(scope))
                {
                    co_return;
                }
                if (!remoteDetail)
                {
                    auto const canFallback = index + 1 < requests.size()
                        && LastMusicPlayer::Backend::ShouldTryNextAccountArtistDetailTarget(
                            LastMusicPlayer::Backend::AccountArtistDetailAttemptOutcome::InvalidPayload);
                    if (canFallback)
                    {
                        continue;
                    }
                    break;
                }

                selectedIdentity = requests[index].first;
                if (!cacheKey.empty())
                {
                    if (m_artistDetailCache.size() >= 32)
                    {
                        m_artistDetailCache.erase(m_artistDetailCache.begin());
                    }
                    m_artistDetailCache[cacheKey] = *remoteDetail;
                }
                break;
            }
        }

        if (requestEpoch != m_artistRequestEpoch
            || !ArtistDetailContent()
            || ArtistDetailContent().Visibility() != winrt::Microsoft::UI::Xaml::Visibility::Visible
            || !remoteMusic.IsCurrent(scope))
        {
            co_return;
        }

        if (remoteDetail)
        {
            m_currentArtistIdentity = std::move(selectedIdentity);
            RenderArtistDetail(*remoteDetail, false);
            co_return;
        }

        co_await winrt::resume_background();
        auto local = BuildLocalArtistDetail(identity.Name, artworkUrl);
        co_await wil::resume_foreground(dispatcher);
        if (requestEpoch == m_artistRequestEpoch
            && remoteMusic.IsCurrent(scope))
        {
            m_artistLocalAllTracks = std::move(local.Tracks);
            RenderArtistDetail(local.Detail, true);
        }
    }

    MainWindow::LocalArtistDetailResult MainWindow::BuildLocalArtistDetail(
        winrt::hstring const& artistName,
        winrt::hstring const& artworkUrl)
    {
        LocalArtistDetailResult result;
        auto& detailResult = result.Detail;
        detailResult.Artist.Name = artistName;
        detailResult.Artist.ArtworkUrl = artworkUrl;

        if (!detail::DatabaseService().IsInitialized())
        {
            return result;
        }

        LastMusicPlayer::Backend::TrackQuery query;
        query.GroupKind = L"artist";
        query.GroupKey = std::wstring{ artistName.c_str() };
        query.Sort = L"MostPlayed";
        query.IncludeRemote = true;
        query.ActiveOnly = true;
        result.Tracks = detail::DatabaseService().LoadTracksForQuery(query);
        if (detailResult.Artist.ArtworkUrl.empty())
        {
            auto artwork = std::find_if(result.Tracks.begin(), result.Tracks.end(), [](auto const& track)
            {
                return track && !track.ArtworkUrl().empty();
            });
            if (artwork != result.Tracks.end())
            {
                detailResult.Artist.ArtworkUrl = artwork->ArtworkUrl();
            }
        }

        std::unordered_map<std::wstring, std::size_t> releaseIndexes;
        for (auto const& track : result.Tracks)
        {
            if (!track || track.Album().empty() || detail::ToLowerCopy(track.Album()) == L"unknown album")
            {
                continue;
            }
            auto key = detail::ToLowerCopy(track.Album());
            auto found = releaseIndexes.find(key);
            if (found == releaseIndexes.end())
            {
                LastMusicPlayer::Backend::RemoteReleaseSummary summary;
                summary.Kind = LastMusicPlayer::Backend::RemoteReleaseKind::Album;
                summary.Release.Type = LastMusicPlayer::Backend::CatalogResourceType::Album;
                summary.Release.Title = track.Album();
                summary.Release.Subtitle = artistName;
                summary.Release.ArtworkUrl = track.ArtworkUrl();
                summary.Release.CatalogId = track.AlbumCatalogId();
                summary.Release.Provider = track.RemoteProvider().empty() ? track.Provider() : track.RemoteProvider();
                summary.Release.TrackCount = 1;
                releaseIndexes.emplace(std::move(key), detailResult.Releases.size());
                detailResult.Releases.push_back(std::move(summary));
            }
            else
            {
                auto& existing = detailResult.Releases[found->second].Release;
                if (!existing.CatalogId.empty()
                    && !track.AlbumCatalogId().empty()
                    && existing.CatalogId != track.AlbumCatalogId())
                {
                    existing.CatalogId.clear();
                }
                ++detailResult.Releases[found->second].Release.TrackCount;
            }
        }
        return result;
    }

    void MainWindow::RenderArtistDetail(
        LastMusicPlayer::Backend::RemoteArtistDetail const& artistDetail,
        bool localFallback)
    {
        m_currentArtistDetail = artistDetail;
        m_artistUsingLocalFallback = localFallback;
        m_artistTopSongs.clear();
        m_artistLatestRelease.Clear();
        m_artistTopSongsShelf.Clear();
        m_artistAlbums.Clear();
        m_artistSingles.Clear();
        m_artistRelated.Clear();

        ArtistDetailName().Text(artistDetail.Artist.Name.empty()
            ? m_currentArtistIdentity.Name
            : artistDetail.Artist.Name);
        ArtistDetailGenres().Text(ArtistMetadata(artistDetail.GenreNames));

        if (localFallback)
        {
            auto count = (std::min<std::size_t>)(10, m_artistLocalAllTracks.size());
            for (std::size_t index = 0; index < count; ++index)
            {
                auto track = m_artistLocalAllTracks[index];
                track.Index(static_cast<int32_t>(index + 1));
                detail::ResolveArtworkPresentation(track, L"track");
                m_artistTopSongs.push_back(track);
                m_artistTopSongsShelf.Append(track);
            }
        }
        else
        {
            m_artistLocalAllTracks.clear();
            for (std::size_t index = 0; index < artistDetail.TopSongs.size(); ++index)
            {
                auto track = ArtistTrackFromCatalogItem(
                    artistDetail.TopSongs[index],
                    static_cast<int32_t>(index + 1));
                m_artistTopSongs.push_back(track);
                m_artistTopSongsShelf.Append(track);
            }
        }
        ArtistTopSongsSection().Visibility(m_artistTopSongs.empty()
            ? winrt::Microsoft::UI::Xaml::Visibility::Collapsed
            : winrt::Microsoft::UI::Xaml::Visibility::Visible);

        auto const latestReleaseIndex = LastMusicPlayer::Frontend::LatestArtistReleaseIndex(
            artistDetail.Releases.size(),
            localFallback);
        for (std::size_t index = 0; index < artistDetail.Releases.size(); ++index)
        {
            auto const& summary = artistDetail.Releases[index];
            winrt::Last_Music_Player::TrackInfo release;
            release.Title(summary.Release.Title);
            release.Artist(summary.Release.Subtitle.empty() ? ArtistDetailName().Text() : summary.Release.Subtitle);
            release.ArtworkUrl(summary.Release.ArtworkUrl);
            release.SourceUrl(summary.Release.SourceUrl);
            release.RemoteCatalogId(summary.Release.CatalogId);
            release.Provider(summary.Release.Provider);
            release.SourceKind(localFallback ? L"local-album" : L"provider-album");
            release.Genre(ReleaseKindText(summary.Kind));
            release.TrackCount(summary.Release.TrackCount);
            auto meta = std::wstring{ summary.Release.ReleaseDate.c_str() };
            if (meta.size() >= 4)
            {
                meta.resize(4);
                meta += L"  ·  ";
            }
            meta += ReleaseKindText(summary.Kind).c_str();
            release.DateAdded(winrt::hstring{ meta });
            detail::ApplyMusicArtwork(release, release.ArtworkUrl(), L"album");
            if (latestReleaseIndex && *latestReleaseIndex == index)
            {
                m_artistLatestRelease.Append(release);
            }
            if (summary.Kind == LastMusicPlayer::Backend::RemoteReleaseKind::Album)
            {
                m_artistAlbums.Append(release);
            }
            else
            {
                m_artistSingles.Append(release);
            }
        }
        ArtistLatestReleaseSection().Visibility(m_artistLatestRelease.Size() == 0
            ? winrt::Microsoft::UI::Xaml::Visibility::Collapsed
            : winrt::Microsoft::UI::Xaml::Visibility::Visible);
        ArtistAlbumsSection().Visibility(m_artistAlbums.Size() == 0
            ? winrt::Microsoft::UI::Xaml::Visibility::Collapsed
            : winrt::Microsoft::UI::Xaml::Visibility::Visible);
        ArtistSinglesSection().Visibility(m_artistSingles.Size() == 0
            ? winrt::Microsoft::UI::Xaml::Visibility::Collapsed
            : winrt::Microsoft::UI::Xaml::Visibility::Visible);

        for (auto const& relatedArtist : artistDetail.RelatedArtists)
        {
            winrt::Last_Music_Player::TrackInfo related;
            related.Title(relatedArtist.Name);
            related.ArtworkUrl(relatedArtist.ArtworkUrl);
            related.SourceUrl(relatedArtist.SourceUrl);
            related.RemoteCatalogId(relatedArtist.CatalogId);
            related.ArtistCatalogId(relatedArtist.CatalogId);
            related.Provider(relatedArtist.Provider);
            related.SourceKind(L"artist");
            detail::ApplyMusicArtwork(related, related.ArtworkUrl(), L"artist");
            m_artistRelated.Append(related);
        }
        ArtistSimilarSection().Visibility(m_artistRelated.Size() == 0
            ? winrt::Microsoft::UI::Xaml::Visibility::Collapsed
            : winrt::Microsoft::UI::Xaml::Visibility::Visible);

        ArtistLibraryStatus().Visibility(localFallback
            ? winrt::Microsoft::UI::Xaml::Visibility::Visible
            : winrt::Microsoft::UI::Xaml::Visibility::Collapsed);
        auto portraitArtwork = artistDetail.Artist.ArtworkUrl.empty()
            ? artistDetail.HeroArtworkUrl
            : artistDetail.Artist.ArtworkUrl;
        ApplyArtistPortraitArtwork(portraitArtwork);
        ArtistDetailSkeleton().Visibility(winrt::Microsoft::UI::Xaml::Visibility::Collapsed);
        ArtistDetailScroll().Visibility(winrt::Microsoft::UI::Xaml::Visibility::Visible);
        ArtistDetailScroll().ChangeView(nullptr, 0.0, nullptr, true);
        ResetArtistShelfOffsets();
        auto weak = get_weak();
        DispatcherQueue().TryEnqueue([weak]()
        {
            if (auto self = weak.get())
            {
                self->UpdateArtistShelfButtons();
                self->ApplyPendingArtistPageScrollState();
            }
        });
    }

    void MainWindow::ApplyArtistPortraitArtwork(winrt::hstring const& artworkUrl)
    {
        ArtistPortraitImage().Tag(nullptr);
        ArtistPortraitImage().Source(nullptr);
        ArtistPortraitImage().Opacity(1.0);
        if (artworkUrl.empty())
        {
            return;
        }

        LastMusicPlayer::Backend::TrackInfo artworkTrack;
        artworkTrack.Title(ArtistDetailName().Text());
        artworkTrack.Artist(ArtistDetailName().Text());
        artworkTrack.ArtworkUrl(artworkUrl);
        artworkTrack.SourceUrl(m_currentArtistIdentity.SourceUrl);
        artworkTrack.SourceKind(L"remote");
        ArtistPortraitImage().DataContext(artworkTrack);
        if (detail::RemoteMusicServiceService().Mode() == LastMusicPlayer::Backend::RemoteAccessMode::Account)
        {
            QueueAccountArtworkImage(
                ArtistPortraitImage(),
                artworkUrl,
                detail::ArtworkDetail::Hero,
                artworkTrack,
                AccountArtworkPriority::Visible);
            return;
        }
        auto bitmap = detail::CreateMusicArtworkBitmap(artworkUrl, detail::ArtworkDetail::Hero);
        if (bitmap)
        {
            ArtistPortraitImage().Source(bitmap);
        }
    }

    void MainWindow::ApplyArtistHeaderLayout(double availableWidth)
    {
        auto const layout = LastMusicPlayer::Frontend::CalculateArtistHeaderLayout(
            availableWidth);
        if (layout.PortraitSize <= 0.0)
        {
            return;
        }

        ArtistPortraitBorder().Width(layout.PortraitSize);
        ArtistPortraitBorder().Height(layout.PortraitSize);
        LastMusicPlayer::Frontend::ApplyRoundedCornerClip(
            ArtistPortraitImage(),
            static_cast<float>(layout.PortraitSize / 2.0));
        ArtistSkeletonPortrait().Width(layout.PortraitSize);
        ArtistSkeletonPortrait().Height(layout.PortraitSize);

        auto apply = [&](winrt::Microsoft::UI::Xaml::Controls::Grid const& header,
                         winrt::Microsoft::UI::Xaml::FrameworkElement const& portrait,
                         winrt::Microsoft::UI::Xaml::FrameworkElement const& copy)
        {
            header.ColumnSpacing(layout.Stacked ? 0.0 : 34.0);
            header.RowSpacing(layout.Stacked ? 20.0 : 0.0);
            header.ColumnDefinitions().GetAt(0).Width(layout.Stacked
                ? winrt::Microsoft::UI::Xaml::GridLengthHelper::FromValueAndType(
                    1.0, winrt::Microsoft::UI::Xaml::GridUnitType::Star)
                : winrt::Microsoft::UI::Xaml::GridLengthHelper::FromPixels(
                    layout.PortraitSize));
            header.ColumnDefinitions().GetAt(1).Width(layout.Stacked
                ? winrt::Microsoft::UI::Xaml::GridLengthHelper::FromPixels(0.0)
                : winrt::Microsoft::UI::Xaml::GridLengthHelper::FromValueAndType(
                    1.0, winrt::Microsoft::UI::Xaml::GridUnitType::Star));

            winrt::Microsoft::UI::Xaml::Controls::Grid::SetRow(portrait, 0);
            winrt::Microsoft::UI::Xaml::Controls::Grid::SetColumn(portrait, 0);
            winrt::Microsoft::UI::Xaml::Controls::Grid::SetColumnSpan(
                portrait, layout.Stacked ? 2 : 1);
            portrait.HorizontalAlignment(layout.Stacked
                ? winrt::Microsoft::UI::Xaml::HorizontalAlignment::Center
                : winrt::Microsoft::UI::Xaml::HorizontalAlignment::Left);

            winrt::Microsoft::UI::Xaml::Controls::Grid::SetRow(copy, layout.Stacked ? 1 : 0);
            winrt::Microsoft::UI::Xaml::Controls::Grid::SetColumn(copy, layout.Stacked ? 0 : 1);
            winrt::Microsoft::UI::Xaml::Controls::Grid::SetColumnSpan(copy, layout.Stacked ? 2 : 1);
            copy.HorizontalAlignment(layout.Stacked
                ? winrt::Microsoft::UI::Xaml::HorizontalAlignment::Center
                : winrt::Microsoft::UI::Xaml::HorizontalAlignment::Stretch);
        };

        apply(ArtistHeader(), ArtistPortraitBorder(), ArtistHeaderCopy());
        apply(ArtistSkeletonHeader(), ArtistSkeletonPortrait(), ArtistSkeletonCopy());
        auto const alignment = layout.Stacked
            ? winrt::Microsoft::UI::Xaml::TextAlignment::Center
            : winrt::Microsoft::UI::Xaml::TextAlignment::Left;
        ArtistEyebrow().TextAlignment(alignment);
        ArtistDetailName().TextAlignment(alignment);
        ArtistDetailGenres().TextAlignment(alignment);
    }

    LastMusicPlayer::Frontend::ArtistPageScrollState MainWindow::CaptureArtistPageScrollState()
    {
        return {
            ArtistDetailScroll().VerticalOffset(),
            ArtistLatestReleaseScroller().HorizontalOffset(),
            ArtistTopSongsScroller().HorizontalOffset(),
            ArtistAlbumsScroller().HorizontalOffset(),
            ArtistSinglesScroller().HorizontalOffset(),
            ArtistSimilarScroller().HorizontalOffset(),
        };
    }

    void MainWindow::RestoreArtistPageScrollState(
        LastMusicPlayer::Frontend::ArtistPageScrollState const& state)
    {
        m_pendingArtistScrollState = state;
        // Observable-vector changes are applied before this call, but the shelf
        // extents can still describe the artist we just left. Force one layout
        // pass so a restored offset is clamped against the page being shown.
        ArtistPagePanel().UpdateLayout();
        ApplyPendingArtistPageScrollState();
        auto weak = get_weak();
        DispatcherQueue().TryEnqueue([weak]()
        {
            if (auto self = weak.get())
            {
                self->ApplyPendingArtistPageScrollState();
            }
        });
    }

    void MainWindow::ApplyPendingArtistPageScrollState()
    {
        if (!m_pendingArtistScrollState)
        {
            return;
        }

        auto const& requested = *m_pendingArtistScrollState;
        auto const ready = [](double offset, double scrollableExtent)
        {
            return offset <= LastMusicPlayer::Frontend::kArtistShelfEdgeEpsilon
                || scrollableExtent > LastMusicPlayer::Frontend::kArtistShelfEdgeEpsilon;
        };
        if (!ready(requested.Vertical, ArtistDetailScroll().ScrollableHeight())
            || !ready(requested.LatestRelease, ArtistLatestReleaseScroller().ScrollableWidth())
            || !ready(requested.TopSongs, ArtistTopSongsScroller().ScrollableWidth())
            || !ready(requested.Albums, ArtistAlbumsScroller().ScrollableWidth())
            || !ready(requested.Singles, ArtistSinglesScroller().ScrollableWidth())
            || !ready(requested.SimilarArtists, ArtistSimilarScroller().ScrollableWidth()))
        {
            return;
        }

        LastMusicPlayer::Frontend::ArtistPageScrollState const extents{
            ArtistDetailScroll().ScrollableHeight(),
            ArtistLatestReleaseScroller().ScrollableWidth(),
            ArtistTopSongsScroller().ScrollableWidth(),
            ArtistAlbumsScroller().ScrollableWidth(),
            ArtistSinglesScroller().ScrollableWidth(),
            ArtistSimilarScroller().ScrollableWidth(),
        };
        auto const restored = LastMusicPlayer::Frontend::ClampArtistPageScrollState(
            requested,
            extents);
        ArtistDetailScroll().ChangeView(nullptr, restored.Vertical, nullptr, true);
        ArtistLatestReleaseScroller().ChangeView(restored.LatestRelease, nullptr, nullptr, true);
        ArtistTopSongsScroller().ChangeView(restored.TopSongs, nullptr, nullptr, true);
        ArtistAlbumsScroller().ChangeView(restored.Albums, nullptr, nullptr, true);
        ArtistSinglesScroller().ChangeView(restored.Singles, nullptr, nullptr, true);
        ArtistSimilarScroller().ChangeView(restored.SimilarArtists, nullptr, nullptr, true);
        m_pendingArtistScrollState.reset();
        UpdateArtistShelfButtons();
    }

    void MainWindow::ResetArtistShelfOffsets()
    {
        ArtistLatestReleaseScroller().ChangeView(0.0, nullptr, nullptr, true);
        ArtistTopSongsScroller().ChangeView(0.0, nullptr, nullptr, true);
        ArtistAlbumsScroller().ChangeView(0.0, nullptr, nullptr, true);
        ArtistSinglesScroller().ChangeView(0.0, nullptr, nullptr, true);
        ArtistSimilarScroller().ChangeView(0.0, nullptr, nullptr, true);
        UpdateArtistShelfButtons();
    }

    void MainWindow::UpdateArtistShelfButtons()
    {
        auto apply = [](winrt::Microsoft::UI::Xaml::Controls::Button const& button,
                        winrt::Microsoft::UI::Xaml::Controls::ScrollViewer const& scroller)
        {
            auto const canAdvance = LastMusicPlayer::Frontend::CanAdvanceArtistShelf(
                scroller.HorizontalOffset(),
                scroller.ScrollableWidth());
            button.Visibility(canAdvance
                ? winrt::Microsoft::UI::Xaml::Visibility::Visible
                : winrt::Microsoft::UI::Xaml::Visibility::Collapsed);
        };
        apply(ArtistTopSongsViewMore(), ArtistTopSongsScroller());
        apply(ArtistAlbumsViewMore(), ArtistAlbumsScroller());
        apply(ArtistSinglesViewMore(), ArtistSinglesScroller());
        apply(ArtistSimilarViewMore(), ArtistSimilarScroller());
    }

    void MainWindow::ArtistDetailBack_Click(
        winrt::Windows::Foundation::IInspectable const& sender,
        winrt::Microsoft::UI::Xaml::RoutedEventArgs const& args)
    {
        (void)sender;
        (void)args;
        ++m_artistRequestEpoch;
        if (m_artistLoadOperation)
        {
            m_artistLoadOperation.Cancel();
        }
        if (m_albumLoadOperation)
        {
            m_albumLoadOperation.Cancel();
        }
        if (!m_artistBackStack.empty())
        {
            auto previous = std::move(m_artistBackStack.back());
            m_artistBackStack.pop_back();
            RestoreArtistNavigationEntry(std::move(previous));
            return;
        }
        HideArtistDetail(true);
    }

    LastMusicPlayer::Frontend::ArtistNavigationEntry
        MainWindow::CaptureCurrentArtistNavigationEntry()
    {
        return {
            m_currentArtistDetail,
            m_currentArtistIdentity,
            m_currentAccountArtistPlan,
            m_artistLocalAllTracks,
            m_artistUsingLocalFallback,
            CaptureArtistPageScrollState(),
        };
    }

    LastMusicPlayer::Frontend::ArtistNavigationSnapshot
        MainWindow::CaptureArtistNavigationSnapshot()
    {
        return {
            CaptureCurrentArtistNavigationEntry(),
            m_artistBackStack,
            m_artistOriginNav,
        };
    }

    void MainWindow::RestoreArtistNavigationEntry(
        LastMusicPlayer::Frontend::ArtistNavigationEntry entry)
    {
        m_currentArtistIdentity = std::move(entry.Identity);
        m_currentAccountArtistPlan = std::move(entry.AccountPlan);
        m_artistLocalAllTracks = std::move(entry.LocalTracks);
        RenderArtistDetail(entry.Detail, entry.LocalFallback);
        RestoreArtistPageScrollState(entry.ScrollState);
    }

    void MainWindow::RestoreArtistNavigationSnapshot(
        LastMusicPlayer::Frontend::ArtistNavigationSnapshot snapshot)
    {
        m_artistOriginNav = std::move(snapshot.OriginalDestination);
        m_artistBackStack = std::move(snapshot.BackStack);

        ShowPrimaryView(L"Library");
        HideLibraryDetail();
        LibraryHeaderBar().Visibility(winrt::Microsoft::UI::Xaml::Visibility::Collapsed);
        LibraryNavigationBar().Visibility(winrt::Microsoft::UI::Xaml::Visibility::Collapsed);
        LibraryTabsContent().Visibility(winrt::Microsoft::UI::Xaml::Visibility::Collapsed);
        ArtistDetailContent().Visibility(winrt::Microsoft::UI::Xaml::Visibility::Visible);
        RestoreArtistNavigationEntry(std::move(snapshot.Current));
    }

    void MainWindow::HideArtistDetail(bool restoreOrigin)
    {
        ++m_artistRequestEpoch;
        if (m_artistLoadOperation)
        {
            m_artistLoadOperation.Cancel();
        }
        if (m_albumLoadOperation)
        {
            m_albumLoadOperation.Cancel();
        }
        m_artistBackStack.clear();
        m_currentAccountArtistPlan.reset();
        m_pendingArtistScrollState.reset();
        m_artistTopSongs.clear();
        m_artistLocalAllTracks.clear();
        m_artistLatestRelease.Clear();
        m_artistTopSongsShelf.Clear();
        m_artistAlbums.Clear();
        m_artistSingles.Clear();
        m_artistRelated.Clear();
        if (ArtistDetailContent())
        {
            ArtistDetailContent().Visibility(winrt::Microsoft::UI::Xaml::Visibility::Collapsed);
        }
        LibraryHeaderBar().Visibility(winrt::Microsoft::UI::Xaml::Visibility::Visible);
        LibraryNavigationBar().Visibility(winrt::Microsoft::UI::Xaml::Visibility::Visible);
        LibraryTabsContent().Visibility(winrt::Microsoft::UI::Xaml::Visibility::Visible);
        if (restoreOrigin)
        {
            auto origin = m_artistOriginNav.empty() ? winrt::hstring{ L"Library" } : winrt::hstring{ m_artistOriginNav };
            m_artistOriginNav.clear();
            ShowPrimaryView(origin);
        }
    }

    void MainWindow::ArtistDetailRetry_Click(
        winrt::Windows::Foundation::IInspectable const& sender,
        winrt::Microsoft::UI::Xaml::RoutedEventArgs const& args)
    {
        (void)sender;
        (void)args;
        auto requestEpoch = ++m_artistRequestEpoch;
        if (m_artistLoadOperation)
        {
            m_artistLoadOperation.Cancel();
        }
        ArtistDetailSkeleton().Visibility(winrt::Microsoft::UI::Xaml::Visibility::Visible);
        ArtistDetailScroll().Visibility(winrt::Microsoft::UI::Xaml::Visibility::Collapsed);
        m_artistLoadOperation = LoadArtistDetailAsync(
            m_currentArtistIdentity,
            m_currentAccountArtistPlan,
            m_currentArtistDetail.Artist.ArtworkUrl,
            requestEpoch,
            true);
        detail::RunDetached(m_artistLoadOperation);
    }

    void MainWindow::ArtistTopSongCard_Click(
        winrt::Windows::Foundation::IInspectable const& sender,
        winrt::Microsoft::UI::Xaml::RoutedEventArgs const& args)
    {
        (void)args;
        auto button = sender.try_as<winrt::Microsoft::UI::Xaml::Controls::Button>();
        auto track = button
            ? button.Tag().try_as<winrt::Last_Music_Player::TrackInfo>()
            : nullptr;
        if (track)
        {
            QueueAndPlayVisible(m_artistTopSongs, track);
        }
    }

    void MainWindow::ArtistShelfViewMore_Click(
        winrt::Windows::Foundation::IInspectable const& sender,
        winrt::Microsoft::UI::Xaml::RoutedEventArgs const& args)
    {
        (void)args;
        auto button = sender.try_as<winrt::Microsoft::UI::Xaml::Controls::Button>();
        auto const tag = button ? winrt::unbox_value_or<winrt::hstring>(button.Tag(), L"") : L"";
        winrt::Microsoft::UI::Xaml::Controls::ScrollViewer scroller{ nullptr };
        if (tag == L"top")
        {
            scroller = ArtistTopSongsScroller();
        }
        else if (tag == L"albums")
        {
            scroller = ArtistAlbumsScroller();
        }
        else if (tag == L"singles")
        {
            scroller = ArtistSinglesScroller();
        }
        else if (tag == L"similar")
        {
            scroller = ArtistSimilarScroller();
        }
        if (!scroller)
        {
            return;
        }

        auto const target = LastMusicPlayer::Frontend::NextArtistShelfOffset(
            scroller.HorizontalOffset(),
            scroller.ViewportWidth(),
            scroller.ScrollableWidth());
        scroller.ChangeView(target, nullptr, nullptr);
    }

    void MainWindow::ArtistShelf_ViewChanged(
        winrt::Windows::Foundation::IInspectable const& sender,
        winrt::Microsoft::UI::Xaml::Controls::ScrollViewerViewChangedEventArgs const& args)
    {
        (void)sender;
        (void)args;
        UpdateArtistShelfButtons();
        ApplyPendingArtistPageScrollState();
    }

    void MainWindow::ArtistShelf_SizeChanged(
        winrt::Windows::Foundation::IInspectable const& sender,
        winrt::Microsoft::UI::Xaml::SizeChangedEventArgs const& args)
    {
        (void)sender;
        (void)args;
        UpdateArtistShelfButtons();
        ApplyPendingArtistPageScrollState();
    }

    void MainWindow::ArtistTopSong_ItemClick(
        winrt::Windows::Foundation::IInspectable const& sender,
        winrt::Microsoft::UI::Xaml::Controls::ItemClickEventArgs const& args)
    {
        (void)sender;
        auto track = args.ClickedItem().try_as<winrt::Last_Music_Player::TrackInfo>();
        if (track)
        {
            QueueAndPlayVisible(m_artistTopSongs, track);
        }
    }

    void MainWindow::ArtistReleaseCard_Click(
        winrt::Windows::Foundation::IInspectable const& sender,
        winrt::Microsoft::UI::Xaml::RoutedEventArgs const& args)
    {
        (void)args;
        auto button = sender.try_as<winrt::Microsoft::UI::Xaml::Controls::Button>();
        auto release = button
            ? button.Tag().try_as<winrt::Last_Music_Player::TrackInfo>()
            : nullptr;
        if (release)
        {
            if (m_albumLoadOperation)
            {
                m_albumLoadOperation.Cancel();
            }
            m_albumLoadOperation = OpenArtistAlbumAsync(release);
            detail::RunDetached(m_albumLoadOperation);
        }
    }

    winrt::Windows::Foundation::IAsyncAction MainWindow::OpenArtistAlbumAsync(
        winrt::Last_Music_Player::TrackInfo release)
    {
        auto lifetime = get_strong();
        if (!release)
        {
            co_return;
        }

        m_artistAlbumReturnState = CaptureArtistNavigationSnapshot();
        ++m_artistRequestEpoch;
        if (m_artistLoadOperation)
        {
            m_artistLoadOperation.Cancel();
        }
        LastMusicPlayer::Backend::AlbumLoadRequest request;
        request.Identity = {
            release.RemoteCatalogId(),
            release.SourceUrl(),
            release.Title(),
        };
        request.Storefront = CurrentDiscoverStorefront();
        request.Release.Type = LastMusicPlayer::Backend::CatalogResourceType::Album;
        request.Release.CatalogId = release.RemoteCatalogId();
        request.Release.Title = release.Title();
        request.Release.Subtitle = release.Artist();
        request.Release.ArtworkUrl = release.ArtworkUrl();
        request.Release.SourceUrl = release.SourceUrl();
        request.Release.Provider = release.Provider();
        request.Release.TrackCount = release.TrackCount();
        request.Route = LastMusicPlayer::Backend::SelectArtistAlbumLoadRoute(
            release.SourceKind() == L"local-album",
            detail::RemoteMusicServiceService().Mode()
                == LastMusicPlayer::Backend::RemoteAccessMode::Account,
            release.Provider(),
            release.RemoteCatalogId());

        co_await OpenAlbumDetailAsync(std::move(request), release.Title(), true);
    }

    void MainWindow::SimilarArtistCard_Click(
        winrt::Windows::Foundation::IInspectable const& sender,
        winrt::Microsoft::UI::Xaml::RoutedEventArgs const& args)
    {
        (void)args;
        auto button = sender.try_as<winrt::Microsoft::UI::Xaml::Controls::Button>();
        auto artist = button
            ? button.Tag().try_as<winrt::Last_Music_Player::TrackInfo>()
            : nullptr;
        if (!artist)
        {
            return;
        }
        m_artistBackStack.push_back(CaptureCurrentArtistNavigationEntry());
        OpenArtistDetail(
            artist.Title(),
            artist.RemoteCatalogId(),
            artist.SourceUrl(),
            artist.ArtworkUrl(),
            artist);
    }

    void MainWindow::ArtistDetail_SizeChanged(
        winrt::Windows::Foundation::IInspectable const& sender,
        winrt::Microsoft::UI::Xaml::SizeChangedEventArgs const& args)
    {
        (void)sender;
        ApplyArtistHeaderLayout(args.NewSize().Width);
        UpdateArtistShelfButtons();
        ApplyPendingArtistPageScrollState();
    }
}
