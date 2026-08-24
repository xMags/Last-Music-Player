#include "pch.h"
#include "MainWindow.xaml.h"
#include "MainWindow.Internal.h"

#include "Backend/ProviderHelpers.h"
#include "Frontend/UIHelpers.h"

#include <winrt/Windows.System.Threading.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <memory>
#include <random>
#include <unordered_map>

namespace winrt::Last_Music_Player::implementation
{
    namespace
    {
        std::wstring DetailCacheKey(
            LastMusicPlayer::Backend::RemoteScopeSnapshot const& scope,
            LastMusicPlayer::Backend::RemoteDetailIdentity const& identity)
        {
            auto query = LastMusicPlayer::Backend::BuildRemoteDetailQuery(identity);
            if (!query)
            {
                return {};
            }
            auto key = LastMusicPlayer::Backend::RemoteScopeCacheKey(scope);
            key += L"|";
            key += query->c_str();
            return key;
        }

        winrt::hstring JoinGenres(std::vector<winrt::hstring> const& genres)
        {
            std::wstring text;
            for (auto const& genre : genres)
            {
                if (!text.empty())
                {
                    text += L"  ·  ";
                }
                text += genre.c_str();
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

        bool SameArtist(winrt::hstring const& left, winrt::hstring const& right)
        {
            return detail::ToLowerCopy(left) == detail::ToLowerCopy(right);
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
        auto identity = ArtistIdentityForTrack(artist);
        LastMusicPlayer::Backend::RemoteDetailIdentity requestIdentity{
            winrt::hstring(identity.CatalogId),
            winrt::hstring(identity.SourceUrl),
            winrt::hstring(identity.Name) };
        auto requestKey = DetailCacheKey(scope, requestIdentity);
        if (requestKey.empty() || !m_artistArtworkRequests.emplace(requestKey).second)
        {
            return;
        }
        detail::RunDetached(HydrateArtistLibraryArtworkAsync(
            border,
            artist,
            std::move(scope),
            std::move(requestKey)));
    }

    winrt::Windows::Foundation::IAsyncAction MainWindow::HydrateArtistLibraryArtworkAsync(
        winrt::Microsoft::UI::Xaml::Controls::Border border,
        winrt::Last_Music_Player::TrackInfo artist,
        LastMusicPlayer::Backend::RemoteScopeSnapshot scope,
        std::wstring requestKey)
    {
        auto lifetime = get_strong();
        auto dispatcher = DispatcherQueue();
        std::optional<LastMusicPlayer::Backend::RemoteArtistDetail> artistDetail;
        bool failed = false;
        try
        {
            auto cached = m_artistDetailCache.find(requestKey);
            if (cached != m_artistDetailCache.end())
            {
                artistDetail = cached->second;
            }
            else
            {
                auto identity = ArtistIdentityForTrack(artist);
                auto payload = co_await WithDetailTimeout(
                    detail::RemoteMusicServiceService().GetArtistDetailAsync(
                        scope,
                        { winrt::hstring(identity.CatalogId),
                          winrt::hstring(identity.SourceUrl),
                          winrt::hstring(identity.Name) }));
                co_await winrt::resume_background();
                artistDetail = LastMusicPlayer::Backend::ParseRemoteArtistDetail(payload);
                co_await wil::resume_foreground(dispatcher);
            }
        }
        catch (...)
        {
            failed = true;
        }
        if (!dispatcher.HasThreadAccess())
        {
            co_await wil::resume_foreground(dispatcher);
        }

        m_artistArtworkRequests.erase(requestKey);
        auto& remote = detail::RemoteMusicServiceService();
        if (failed || !artistDetail || !remote.IsCurrent(scope) || artistDetail->Artist.ArtworkUrl.empty()
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
        ProviderAlbumPanel().Visibility(winrt::Microsoft::UI::Xaml::Visibility::Collapsed);
        ArtistDetailSkeleton().Visibility(winrt::Microsoft::UI::Xaml::Visibility::Visible);
        m_artistAlbumOpen = false;
        m_artistAboutExpanded = false;
        m_currentArtistIdentity = { catalogId, sourceUrl, artistName };
        if (sourceGroup && m_currentArtistIdentity.CatalogId.empty())
        {
            m_currentArtistIdentity.CatalogId = sourceGroup.ArtistCatalogId();
        }
        m_currentLibraryArtistId.clear();
        m_currentLibraryArtistFollowed = false;
        if (detail::RemoteMusicServiceService().Mode()
            == LastMusicPlayer::Backend::RemoteAccessMode::Account)
        {
            if (sourceGroup
                && sourceGroup.Provider() == L"account"
                && LastMusicPlayer::Backend::IsValidLibraryArtistPublicId(
                    std::wstring(sourceGroup.RemoteId().c_str())))
            {
                m_currentLibraryArtistId = sourceGroup.RemoteId().c_str();
                m_currentLibraryArtistFollowed = sourceGroup.IsFollowed();
            }
            else
            {
                LastMusicPlayer::Backend::ArtistIdentity requested{
                    std::wstring(m_currentArtistIdentity.CatalogId.c_str()),
                    std::wstring(m_currentArtistIdentity.SourceUrl.c_str()),
                    std::wstring(m_currentArtistIdentity.Name.c_str()),
                    sourceGroup ? std::wstring(sourceGroup.RemoteProvider().c_str()) : std::wstring{} };
                auto const found = std::find_if(
                    m_artistAllResults.begin(),
                    m_artistAllResults.end(),
                    [&](auto const& candidate)
                    {
                        return candidate.Provider() == L"account"
                            && LastMusicPlayer::Backend::SameArtistIdentity(
                                ArtistIdentityForTrack(candidate),
                                requested);
                    });
                if (found != m_artistAllResults.end())
                {
                    m_currentLibraryArtistId = found->RemoteId().c_str();
                    m_currentLibraryArtistFollowed = found->IsFollowed();
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
        auto& remoteMusic = detail::RemoteMusicServiceService();
        auto scope = remoteMusic.CaptureScope();
        auto cacheKey = DetailCacheKey(scope, m_currentArtistIdentity);
        auto cached = m_artistDetailCache.find(cacheKey);
        if (!cacheKey.empty() && cached != m_artistDetailCache.end() && remoteMusic.IsCurrent(scope))
        {
            RenderArtistDetail(cached->second, false);
            return;
        }
        m_artistLoadOperation = LoadArtistDetailAsync(
            m_currentArtistIdentity,
            artworkUrl.empty() && sourceGroup ? sourceGroup.ArtworkUrl() : artworkUrl,
            requestEpoch,
            false);
        detail::RunDetached(m_artistLoadOperation);
    }

    winrt::Windows::Foundation::IAsyncAction MainWindow::LoadArtistDetailAsync(
        LastMusicPlayer::Backend::RemoteDetailIdentity identity,
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

        if (scope.Mode != LastMusicPlayer::Backend::RemoteAccessMode::LocalOnly
            && remoteMusic.IsModeAvailable(scope.Mode))
        {
            try
            {
                auto payload = co_await WithDetailTimeout(
                    remoteMusic.GetArtistDetailAsync(scope, identity));
                if (!remoteMusic.IsCurrent(scope))
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
            catch (...)
            {
                remoteDetail.reset();
            }
        }

        if (requestEpoch != m_artistRequestEpoch
            || m_artistAlbumOpen
            || !ArtistDetailContent()
            || ArtistDetailContent().Visibility() != winrt::Microsoft::UI::Xaml::Visibility::Visible
            || !remoteMusic.IsCurrent(scope))
        {
            co_return;
        }

        if (remoteDetail)
        {
            auto cacheKey = DetailCacheKey(scope, identity);
            if (!cacheKey.empty())
            {
                if (m_artistDetailCache.size() >= 32)
                {
                    m_artistDetailCache.erase(m_artistDetailCache.begin());
                }
                m_artistDetailCache[cacheKey] = *remoteDetail;
            }
            RenderArtistDetail(*remoteDetail, false);
            co_return;
        }

        co_await winrt::resume_background();
        auto local = BuildLocalArtistDetail(identity.Name, artworkUrl);
        co_await wil::resume_foreground(dispatcher);
        if (requestEpoch == m_artistRequestEpoch
            && !m_artistAlbumOpen
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
        m_artistAlbumOpen = false;
        m_artistAboutExpanded = false;
        m_artistTopSongs.clear();
        m_artistTopSongsLeft.Clear();
        m_artistTopSongsRight.Clear();
        m_artistAlbums.Clear();
        m_artistSingles.Clear();
        m_artistRelated.Clear();
        m_providerAlbumTracks.Clear();

        ArtistDetailName().Text(artistDetail.Artist.Name.empty()
            ? m_currentArtistIdentity.Name
            : artistDetail.Artist.Name);
        ArtistDetailGenres().Text(JoinGenres(artistDetail.GenreNames));
        ArtistBiographyText().Text(artistDetail.Biography);
        ArtistBiographyText().MaxLines(5);
        ArtistAboutToggle().Content(winrt::box_value(L"More"));
        ArtistAboutSection().Visibility(artistDetail.Biography.empty()
            ? winrt::Microsoft::UI::Xaml::Visibility::Collapsed
            : winrt::Microsoft::UI::Xaml::Visibility::Visible);
        ArtistAboutToggle().Visibility(artistDetail.Biography.size() > 420
            ? winrt::Microsoft::UI::Xaml::Visibility::Visible
            : winrt::Microsoft::UI::Xaml::Visibility::Collapsed);

        if (localFallback)
        {
            auto count = (std::min<std::size_t>)(10, m_artistLocalAllTracks.size());
            for (std::size_t index = 0; index < count; ++index)
            {
                auto track = m_artistLocalAllTracks[index];
                track.Index(static_cast<int32_t>(index + 1));
                detail::ResolveArtworkPresentation(track, L"track");
                m_artistTopSongs.push_back(track);
            }
        }
        else
        {
            m_artistLocalAllTracks.clear();
            for (std::size_t index = 0; index < artistDetail.TopSongs.size(); ++index)
            {
                m_artistTopSongs.push_back(ArtistTrackFromCatalogItem(
                    artistDetail.TopSongs[index],
                    static_cast<int32_t>(index + 1)));
            }
        }
        ReflowArtistTopSongs(ArtistDetailContent().ActualWidth());
        ArtistTopSongsSection().Visibility(m_artistTopSongs.empty()
            ? winrt::Microsoft::UI::Xaml::Visibility::Collapsed
            : winrt::Microsoft::UI::Xaml::Visibility::Visible);
        ArtistPlayButton().IsEnabled(!m_artistTopSongs.empty());
        ArtistShuffleButton().IsEnabled(!m_artistTopSongs.empty());
        UpdateArtistDetailFollowButton();

        for (auto const& summary : artistDetail.Releases)
        {
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
            if (summary.Kind == LastMusicPlayer::Backend::RemoteReleaseKind::Album)
            {
                m_artistAlbums.Append(release);
            }
            else
            {
                m_artistSingles.Append(release);
            }
        }
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
        ArtistRelatedSection().Visibility(m_artistRelated.Size() == 0
            ? winrt::Microsoft::UI::Xaml::Visibility::Collapsed
            : winrt::Microsoft::UI::Xaml::Visibility::Visible);

        ArtistLibraryStatus().Visibility(localFallback
            ? winrt::Microsoft::UI::Xaml::Visibility::Visible
            : winrt::Microsoft::UI::Xaml::Visibility::Collapsed);
        auto heroArtwork = artistDetail.HeroArtworkUrl.empty()
            ? artistDetail.Artist.ArtworkUrl
            : artistDetail.HeroArtworkUrl;
        ApplyArtistHeroArtwork(heroArtwork);
        ArtistDetailSkeleton().Visibility(winrt::Microsoft::UI::Xaml::Visibility::Collapsed);
        ProviderAlbumPanel().Visibility(winrt::Microsoft::UI::Xaml::Visibility::Collapsed);
        ArtistDetailScroll().Visibility(winrt::Microsoft::UI::Xaml::Visibility::Visible);
        ArtistDetailScroll().ChangeView(nullptr, 0.0, nullptr, true);
    }

    void MainWindow::ReflowArtistTopSongs(double width)
    {
        m_artistTopSongsLeft.Clear();
        m_artistTopSongsRight.Clear();
        auto const wide = width >= 860.0;
        for (std::size_t index = 0; index < m_artistTopSongs.size(); ++index)
        {
            if (wide && index >= 5)
            {
                m_artistTopSongsRight.Append(m_artistTopSongs[index]);
            }
            else
            {
                m_artistTopSongsLeft.Append(m_artistTopSongs[index]);
            }
        }
        ArtistTopSongsRight().Visibility(wide && m_artistTopSongsRight.Size() > 0
            ? winrt::Microsoft::UI::Xaml::Visibility::Visible
            : winrt::Microsoft::UI::Xaml::Visibility::Collapsed);
        ArtistTopSongsGrid().ColumnDefinitions().GetAt(1).Width(
            wide
                ? winrt::Microsoft::UI::Xaml::GridLengthHelper::FromValueAndType(1.0, winrt::Microsoft::UI::Xaml::GridUnitType::Star)
                : winrt::Microsoft::UI::Xaml::GridLengthHelper::FromPixels(0.0));
    }

    void MainWindow::ApplyArtistHeroArtwork(winrt::hstring const& artworkUrl)
    {
        ArtistHeroImage().Tag(nullptr);
        ArtistHeroImage().Source(nullptr);
        ArtistHeroImage().Opacity(0.0);
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
        ArtistHeroImage().DataContext(artworkTrack);
        if (detail::RemoteMusicServiceService().Mode() == LastMusicPlayer::Backend::RemoteAccessMode::Account)
        {
            QueueAccountArtworkImage(
                ArtistHeroImage(),
                artworkUrl,
                detail::ArtworkDetail::Hero,
                artworkTrack,
                AccountArtworkPriority::Visible);
            return;
        }
        auto bitmap = detail::CreateMusicArtworkBitmap(artworkUrl, detail::ArtworkDetail::Hero);
        if (bitmap)
        {
            ArtistHeroImage().Source(bitmap);
            ArtistHeroImage().Opacity(0.72);
        }
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
        if (m_artistAlbumOpen)
        {
            m_artistAlbumOpen = false;
            auto restoreOffset = m_artistScrollOffset;
            RenderArtistDetail(m_currentArtistDetail, m_artistUsingLocalFallback);
            ArtistDetailScroll().ChangeView(nullptr, restoreOffset, nullptr, true);
            return;
        }
        if (!m_artistBackStack.empty())
        {
            auto previous = std::move(m_artistBackStack.back());
            m_artistBackStack.pop_back();
            m_currentArtistIdentity = std::move(previous.Identity);
            m_currentLibraryArtistId = std::move(previous.LibraryArtistId);
            m_currentLibraryArtistFollowed = previous.Followed;
            RenderArtistDetail(previous.Detail, previous.LocalFallback);
            return;
        }
        HideArtistDetail(true);
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
        m_artistAlbumOpen = false;
        m_artistBackStack.clear();
        m_currentLibraryArtistId.clear();
        m_currentLibraryArtistFollowed = false;
        m_artistTopSongs.clear();
        m_artistLocalAllTracks.clear();
        m_artistTopSongsLeft.Clear();
        m_artistTopSongsRight.Clear();
        m_artistAlbums.Clear();
        m_artistSingles.Clear();
        m_artistRelated.Clear();
        m_providerAlbumTracks.Clear();
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
            m_currentArtistDetail.Artist.ArtworkUrl,
            requestEpoch,
            true);
        detail::RunDetached(m_artistLoadOperation);
    }

    void MainWindow::ArtistDetailPlay_Click(
        winrt::Windows::Foundation::IInspectable const& sender,
        winrt::Microsoft::UI::Xaml::RoutedEventArgs const& args)
    {
        (void)sender;
        (void)args;
        if (!m_artistTopSongs.empty())
        {
            QueueAndPlayVisible(m_artistTopSongs, m_artistTopSongs.front());
        }
    }

    void MainWindow::ArtistDetailShuffle_Click(
        winrt::Windows::Foundation::IInspectable const& sender,
        winrt::Microsoft::UI::Xaml::RoutedEventArgs const& args)
    {
        (void)sender;
        (void)args;
        if (m_artistTopSongs.empty())
        {
            return;
        }
        auto shuffled = m_artistTopSongs;
        std::random_device seed;
        std::mt19937 generator{ seed() };
        std::shuffle(shuffled.begin(), shuffled.end(), generator);
        QueueAndPlayVisible(shuffled, shuffled.front());
    }

    void MainWindow::UpdateArtistDetailFollowButton()
    {
        if (!ArtistFollowButton())
        {
            return;
        }
        auto const accountMode = detail::RemoteMusicServiceService().Mode()
            == LastMusicPlayer::Backend::RemoteAccessMode::Account;
        if (accountMode)
        {
            auto const canFollow = LastMusicPlayer::Backend::IsValidLibraryArtistPublicId(
                m_currentLibraryArtistId);
            ArtistFollowButton().Content(winrt::box_value(
                m_currentLibraryArtistFollowed ? L"Following" : L"Follow"));
            ArtistFollowButton().IsEnabled(canFollow
                && !m_artistFollowMutations.contains(m_currentLibraryArtistId));
            winrt::Microsoft::UI::Xaml::Controls::ToolTipService::SetToolTip(
                ArtistFollowButton(),
                canFollow
                    ? nullptr
                    : winrt::box_value(L"Follow is available for artists in your Account library."));
            return;
        }

        auto scopeKey = ArtistFollowScopeKey();
        auto provider = m_currentArtistDetail.Artist.Provider;
        LastMusicPlayer::Backend::ArtistIdentity identity{
            std::wstring((m_currentArtistDetail.Artist.CatalogId.empty()
                ? m_currentArtistIdentity.CatalogId
                : m_currentArtistDetail.Artist.CatalogId).c_str()),
            std::wstring((m_currentArtistDetail.Artist.SourceUrl.empty()
                ? m_currentArtistIdentity.SourceUrl
                : m_currentArtistDetail.Artist.SourceUrl).c_str()),
            std::wstring(ArtistDetailName().Text().c_str()),
            std::wstring(provider.c_str()) };
        auto const identityKey = LastMusicPlayer::Backend::ArtistIdentityKey(identity);
        auto followed = !scopeKey.empty()
            && !identityKey.empty()
            && detail::DatabaseService().IsInitialized()
            && detail::DatabaseService().IsArtistFollowed(scopeKey, identityKey);
        ArtistFollowButton().Content(winrt::box_value(followed ? L"Following" : L"Follow"));
        ArtistFollowButton().IsEnabled(!scopeKey.empty() && !identityKey.empty());
        winrt::Microsoft::UI::Xaml::Controls::ToolTipService::SetToolTip(
            ArtistFollowButton(),
            scopeKey.empty()
                ? winrt::box_value(L"Account artist following needs the matching MagnetoFX sync update.")
                : nullptr);
    }

    void MainWindow::ArtistDetailFollow_Click(
        winrt::Windows::Foundation::IInspectable const& sender,
        winrt::Microsoft::UI::Xaml::RoutedEventArgs const& args)
    {
        (void)sender;
        (void)args;
        winrt::Last_Music_Player::TrackInfo artist;
        artist.Title(ArtistDetailName().Text());
        artist.ArtistCatalogId(m_currentArtistDetail.Artist.CatalogId.empty()
            ? m_currentArtistIdentity.CatalogId
            : m_currentArtistDetail.Artist.CatalogId);
        artist.SourceUrl(m_currentArtistDetail.Artist.SourceUrl.empty()
            ? m_currentArtistIdentity.SourceUrl
            : m_currentArtistDetail.Artist.SourceUrl);
        artist.RemoteProvider(m_currentArtistDetail.Artist.Provider);
        artist.ArtworkUrl(m_currentArtistDetail.Artist.ArtworkUrl);

        if (detail::RemoteMusicServiceService().Mode()
            == LastMusicPlayer::Backend::RemoteAccessMode::Account)
        {
            artist.Provider(L"account");
            artist.RemoteId(winrt::hstring(m_currentLibraryArtistId));
            detail::RunDetached(SetAccountArtistFollowedAsync(
                artist,
                !m_currentLibraryArtistFollowed));
            return;
        }

        auto scopeKey = ArtistFollowScopeKey();
        auto identityKey = LastMusicPlayer::Backend::ArtistIdentityKey(ArtistIdentityForTrack(artist));
        auto followed = !scopeKey.empty()
            && detail::DatabaseService().IsArtistFollowed(scopeKey, identityKey);
        if (SetArtistFollowed(artist, !followed))
        {
            UpdateArtistDetailFollowButton();
        }
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

    void MainWindow::ProviderAlbumTrack_ItemClick(
        winrt::Windows::Foundation::IInspectable const& sender,
        winrt::Microsoft::UI::Xaml::Controls::ItemClickEventArgs const& args)
    {
        ArtistTopSong_ItemClick(sender, args);
    }

    void MainWindow::ArtistRelease_ItemClick(
        winrt::Windows::Foundation::IInspectable const& sender,
        winrt::Microsoft::UI::Xaml::Controls::ItemClickEventArgs const& args)
    {
        (void)sender;
        auto release = args.ClickedItem().try_as<winrt::Last_Music_Player::TrackInfo>();
        if (release)
        {
            OpenProviderAlbum(release);
        }
    }

    void MainWindow::OpenProviderAlbum(winrt::Last_Music_Player::TrackInfo const& release)
    {
        if (!release)
        {
            return;
        }
        m_artistAlbumOpen = true;
        m_artistScrollOffset = ArtistDetailScroll().VerticalOffset();
        auto requestEpoch = ++m_artistRequestEpoch;
        if (m_artistLoadOperation)
        {
            m_artistLoadOperation.Cancel();
        }
        if (m_albumLoadOperation)
        {
            m_albumLoadOperation.Cancel();
        }
        ArtistDetailScroll().Visibility(winrt::Microsoft::UI::Xaml::Visibility::Collapsed);
        ProviderAlbumPanel().Visibility(winrt::Microsoft::UI::Xaml::Visibility::Collapsed);
        ArtistDetailSkeleton().Visibility(winrt::Microsoft::UI::Xaml::Visibility::Visible);

        m_albumLoadOperation = LoadProviderAlbumAsync(release, requestEpoch);
        detail::RunDetached(m_albumLoadOperation);
    }

    winrt::Windows::Foundation::IAsyncAction MainWindow::LoadProviderAlbumAsync(
        winrt::Last_Music_Player::TrackInfo release,
        uint64_t requestEpoch)
    {
        auto lifetime = get_strong();
        auto cancellation = co_await winrt::get_cancellation_token();
        cancellation.enable_propagation();
        auto dispatcher = DispatcherQueue();
        auto& remoteMusic = detail::RemoteMusicServiceService();
        auto scope = remoteMusic.CaptureScope();
        if (release.SourceKind() == L"local-album")
        {
            LastMusicPlayer::Backend::TrackQuery query;
            query.GroupKind = L"album";
            query.GroupKey = std::wstring{ release.Title().c_str() };
            query.Sort = L"DateAdded";
            query.IncludeRemote = true;
            query.ActiveOnly = true;
            co_await winrt::resume_background();
            auto tracks = detail::DatabaseService().LoadTracksForQuery(query);
            std::erase_if(tracks, [&](auto const& track)
            {
                return !SameArtist(track.Artist(), release.Artist());
            });
            co_await wil::resume_foreground(dispatcher);
            if (requestEpoch != m_artistRequestEpoch
                || !m_artistAlbumOpen
                || !remoteMusic.IsCurrent(scope))
            {
                co_return;
            }
            m_artistTopSongs = std::move(tracks);
            LastMusicPlayer::Backend::RemoteAlbumDetail local;
            local.Release.Kind = LastMusicPlayer::Backend::RemoteReleaseKind::Album;
            local.Release.Release.Title = release.Title();
            local.Release.Release.Subtitle = release.Artist();
            local.Release.Release.ArtworkUrl = release.ArtworkUrl();
            RenderProviderAlbum(local, true);
            co_return;
        }
        LastMusicPlayer::Backend::RemoteDetailIdentity identity{
            release.RemoteCatalogId(), release.SourceUrl(), release.Title()
        };
        auto cacheKey = DetailCacheKey(scope, identity);
        auto cached = m_albumDetailCache.find(cacheKey);
        if (!cacheKey.empty() && cached != m_albumDetailCache.end() && remoteMusic.IsCurrent(scope))
        {
            RenderProviderAlbum(cached->second, false);
            co_return;
        }

        std::optional<LastMusicPlayer::Backend::RemoteAlbumDetail> album;
        try
        {
            auto payload = co_await WithDetailTimeout(
                remoteMusic.GetAlbumDetailAsync(scope, identity));
            if (remoteMusic.IsCurrent(scope))
            {
                co_await winrt::resume_background();
                album = LastMusicPlayer::Backend::ParseRemoteAlbumDetail(payload);
                co_await wil::resume_foreground(dispatcher);
            }
        }
        catch (winrt::hresult_canceled const&)
        {
            co_return;
        }
        catch (...)
        {
            album.reset();
        }

        if (requestEpoch != m_artistRequestEpoch || !m_artistAlbumOpen || !remoteMusic.IsCurrent(scope))
        {
            co_return;
        }
        if (!album)
        {
            m_artistAlbumOpen = false;
            auto restoreOffset = m_artistScrollOffset;
            RenderArtistDetail(m_currentArtistDetail, m_artistUsingLocalFallback);
            ArtistDetailScroll().ChangeView(nullptr, restoreOffset, nullptr, true);
            ShowPlaybackNotice(L"This album is unavailable. Try again from the artist page.");
            co_return;
        }
        if (!cacheKey.empty())
        {
            if (m_albumDetailCache.size() >= 32)
            {
                m_albumDetailCache.erase(m_albumDetailCache.begin());
            }
            m_albumDetailCache[cacheKey] = *album;
        }
        RenderProviderAlbum(*album, false);
    }

    void MainWindow::RenderProviderAlbum(
        LastMusicPlayer::Backend::RemoteAlbumDetail const& album,
        bool localFallback)
    {
        m_providerAlbumTracks.Clear();
        if (!localFallback)
        {
            m_artistTopSongs.clear();
            for (std::size_t index = 0; index < album.Tracks.size(); ++index)
            {
                m_artistTopSongs.push_back(ArtistTrackFromCatalogItem(
                    album.Tracks[index],
                    static_cast<int32_t>(index + 1)));
            }
        }
        else
        {
            for (std::size_t index = 0; index < m_artistTopSongs.size(); ++index)
            {
                m_artistTopSongs[index].Index(static_cast<int32_t>(index + 1));
                detail::ResolveArtworkPresentation(m_artistTopSongs[index], L"track");
            }
        }
        for (auto const& track : m_artistTopSongs)
        {
            m_providerAlbumTracks.Append(track);
        }

        ProviderAlbumTitle().Text(album.Release.Release.Title);
        ProviderAlbumArtist().Text(album.Release.Release.Subtitle);
        ProviderAlbumKind().Text(ReleaseKindText(album.Release.Kind).empty()
            ? winrt::hstring{ L"ALBUM" }
            : detail::UpperArtworkText(ReleaseKindText(album.Release.Kind), L"ALBUM"));
        ProviderAlbumArtwork().Source(nullptr);
        LastMusicPlayer::Backend::TrackInfo artworkTrack;
        artworkTrack.Title(album.Release.Release.Title);
        artworkTrack.Artist(album.Release.Release.Subtitle);
        artworkTrack.ArtworkUrl(album.Release.Release.ArtworkUrl);
        artworkTrack.SourceUrl(album.Release.Release.SourceUrl);
        artworkTrack.SourceKind(localFallback ? L"local" : L"remote");
        ProviderAlbumArtwork().DataContext(artworkTrack);
        if (detail::RemoteMusicServiceService().Mode() == LastMusicPlayer::Backend::RemoteAccessMode::Account
            && !localFallback)
        {
            QueueAccountArtworkImage(
                ProviderAlbumArtwork(),
                artworkTrack.ArtworkUrl(),
                detail::ArtworkDetail::Hero,
                artworkTrack,
                AccountArtworkPriority::Visible);
        }
        else
        {
            ProviderAlbumArtwork().Source(detail::CreateMusicArtworkBitmap(
                artworkTrack.ArtworkUrl(),
                detail::ArtworkDetail::Hero));
        }
        ArtistDetailSkeleton().Visibility(winrt::Microsoft::UI::Xaml::Visibility::Collapsed);
        ArtistDetailScroll().Visibility(winrt::Microsoft::UI::Xaml::Visibility::Collapsed);
        ProviderAlbumPanel().Visibility(winrt::Microsoft::UI::Xaml::Visibility::Visible);
        ProviderAlbumPanel().ChangeView(nullptr, 0.0, nullptr, true);
    }

    void MainWindow::RelatedArtist_ItemClick(
        winrt::Windows::Foundation::IInspectable const& sender,
        winrt::Microsoft::UI::Xaml::Controls::ItemClickEventArgs const& args)
    {
        (void)sender;
        auto artist = args.ClickedItem().try_as<winrt::Last_Music_Player::TrackInfo>();
        if (!artist)
        {
            return;
        }
        m_artistBackStack.push_back({
            m_currentArtistDetail,
            m_currentArtistIdentity,
            m_currentLibraryArtistId,
            m_currentLibraryArtistFollowed,
            m_artistUsingLocalFallback });
        OpenArtistDetail(
            artist.Title(),
            artist.RemoteCatalogId(),
            artist.SourceUrl(),
            artist.ArtworkUrl(),
            artist);
    }

    void MainWindow::ArtistAboutToggle_Click(
        winrt::Windows::Foundation::IInspectable const& sender,
        winrt::Microsoft::UI::Xaml::RoutedEventArgs const& args)
    {
        (void)sender;
        (void)args;
        m_artistAboutExpanded = !m_artistAboutExpanded;
        ArtistBiographyText().MaxLines(m_artistAboutExpanded ? 0 : 5);
        ArtistBiographyText().TextTrimming(m_artistAboutExpanded
            ? winrt::Microsoft::UI::Xaml::TextTrimming::None
            : winrt::Microsoft::UI::Xaml::TextTrimming::CharacterEllipsis);
        ArtistAboutToggle().Content(winrt::box_value(m_artistAboutExpanded ? L"Less" : L"More"));
    }

    void MainWindow::ArtistDetail_SizeChanged(
        winrt::Windows::Foundation::IInspectable const& sender,
        winrt::Microsoft::UI::Xaml::SizeChangedEventArgs const& args)
    {
        (void)sender;
        ReflowArtistTopSongs(args.NewSize().Width);
        auto const narrow = args.NewSize().Width < 700.0;
        ArtistDetailName().FontSize(narrow ? 34.0 : 44.0);
        ArtistHero().Height(narrow ? 270.0 : 330.0);
    }
}
