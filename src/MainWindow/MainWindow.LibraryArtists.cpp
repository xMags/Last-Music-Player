#include "pch.h"
#include "MainWindow.xaml.h"
#include "MainWindow.Internal.h"

#include "Backend/ArtistLibraryPolicy.h"
#include "Backend/ProviderHelpers.h"

#include <winrt/Microsoft.UI.Xaml.Controls.h>
#include <winrt/Microsoft.UI.Xaml.Controls.Primitives.h>

#include <algorithm>
#include <chrono>
#include <limits>
#include <string>
#include <unordered_set>
#include <utility>
#include <vector>

namespace winrt::Last_Music_Player::implementation
{
    namespace
    {
        LastMusicPlayer::Backend::LibraryArtistView ArtistViewFromFilter(
            std::wstring const& filter) noexcept
        {
            if (filter == L"Followed")
            {
                return LastMusicPlayer::Backend::LibraryArtistView::Followed;
            }
            if (filter == L"All")
            {
                return LastMusicPlayer::Backend::LibraryArtistView::All;
            }
            return LastMusicPlayer::Backend::LibraryArtistView::Default;
        }

        int32_t BoundedCardCount(std::int64_t value) noexcept
        {
            return static_cast<int32_t>((std::clamp)(
                value,
                std::int64_t{ 0 },
                static_cast<std::int64_t>((std::numeric_limits<int32_t>::max)())));
        }
    }

    std::wstring MainWindow::ArtistFollowScopeKey()
    {
        auto& remote = detail::RemoteMusicServiceService();
        auto const scope = remote.CaptureScope();
        if (scope.Mode == LastMusicPlayer::Backend::RemoteAccessMode::Account)
        {
            return {};
        }
        if (scope.Mode == LastMusicPlayer::Backend::RemoteAccessMode::ApiKey)
        {
            return scope.CachePartition.empty() ? std::wstring{} : L"api-key|" + scope.CachePartition;
        }
        return L"local";
    }

    winrt::Last_Music_Player::TrackInfo MainWindow::ArtistCardFromContract(
        LastMusicPlayer::Backend::LibraryArtist const& artist)
    {
        winrt::Last_Music_Player::TrackInfo card;
        card.RemoteId(winrt::hstring(artist.Id));
        card.Title(winrt::hstring(artist.Name));
        auto caption = std::to_wstring(artist.TrackCount)
            + (artist.TrackCount == 1 ? L" song" : L" songs")
            + L" - "
            + std::to_wstring(artist.AlbumCount)
            + (artist.AlbumCount == 1 ? L" album" : L" albums");
        if (artist.Resolving)
        {
            caption += L" - Resolving";
        }
        card.Artist(winrt::hstring(caption));
        card.TrackCount(BoundedCardCount(artist.TrackCount));
        card.IsInLibrary(artist.TrackCount > 0);
        card.IsFollowed(artist.Followed);
        card.SourceKind(L"artist");
        card.SourceUrl(winrt::hstring(artist.DetailIdentity.SourceUrl));
        card.ArtistCatalogId(winrt::hstring(artist.DetailIdentity.CatalogId));
        card.RemoteCatalogId(winrt::hstring(artist.AppleCatalogId));
        card.Provider(L"account");
        card.RemoteProvider(winrt::hstring(artist.DetailIdentity.Provider));
        card.SourceLabel(L"Account");
        card.ArtworkUrl(winrt::hstring(artist.ArtworkUrl));
        detail::ResolveArtworkPresentation(card, L"artist");
        return card;
    }

    winrt::Windows::Foundation::IAsyncAction MainWindow::LoadAccountLibraryArtistsAsync(bool append)
    {
        auto lifetime = get_strong();
        auto& remote = detail::RemoteMusicServiceService();
        auto const scope = remote.CaptureScope();
        if (scope.Mode != LastMusicPlayer::Backend::RemoteAccessMode::Account
            || !remote.IsModeAvailable(scope.Mode))
        {
            co_return;
        }
        if (append && (m_artistPageLoading || m_artistNextCursor.empty()))
        {
            co_return;
        }

        auto const epoch = append ? m_artistQueryEpoch : ++m_artistQueryEpoch;
        auto const filter = m_artistLibraryFilter;
        auto const query = m_artistLibrarySearch;
        auto const cursor = append ? m_artistNextCursor : std::wstring{};
        m_artistPageLoading = true;
        if (ArtistLibraryLoadMoreButton())
        {
            ArtistLibraryLoadMoreButton().IsEnabled(false);
        }
        if (ArtistLibraryStatusText())
        {
            ArtistLibraryStatusText().Text(append ? L"Loading more artists..." : L"Loading artists...");
            ArtistLibraryStatusText().Visibility(winrt::Microsoft::UI::Xaml::Visibility::Visible);
        }

        LastMusicPlayer::Backend::ParsedLibraryArtistPage page;
        try
        {
            LastMusicPlayer::Backend::LibraryArtistListRequest request;
            request.View = ArtistViewFromFilter(filter);
            request.Query = query;
            request.Limit = LastMusicPlayer::Backend::DefaultLibraryArtistPageSize;
            request.Cursor = cursor;
            auto payload = co_await remote.GetLibraryArtistsAsync(scope, request);
            if (!remote.IsCurrent(scope))
            {
                co_return;
            }
            auto dispatcher = DispatcherQueue();
            co_await winrt::resume_background();
            page = LastMusicPlayer::Backend::ParseLibraryArtistPage(payload);
            co_await wil::resume_foreground(dispatcher);
        }
        catch (winrt::hresult_canceled const&)
        {
            co_return;
        }
        catch (winrt::hresult_error const& error)
        {
            if (epoch != m_artistQueryEpoch || !remote.IsCurrent(scope))
            {
                co_return;
            }
            m_artistPageLoading = false;
            if (ArtistLibraryLoadMoreButton())
            {
                ArtistLibraryLoadMoreButton().IsEnabled(true);
            }
            if (ArtistLibraryStatusText())
            {
                auto const status = LastMusicPlayer::Backend::AccountHttpStatus(error);
                ArtistLibraryStatusText().Text(status == 0
                    ? winrt::hstring{ L"Artists are temporarily unavailable." }
                    : LastMusicPlayer::Backend::SafeAccountErrorMessage(status));
                ArtistLibraryStatusText().Visibility(winrt::Microsoft::UI::Xaml::Visibility::Visible);
            }
            co_return;
        }
        catch (...)
        {
            if (epoch == m_artistQueryEpoch && remote.IsCurrent(scope))
            {
                m_artistPageLoading = false;
                if (ArtistLibraryLoadMoreButton())
                {
                    ArtistLibraryLoadMoreButton().IsEnabled(true);
                }
                if (ArtistLibraryStatusText())
                {
                    ArtistLibraryStatusText().Text(L"Artists are temporarily unavailable.");
                    ArtistLibraryStatusText().Visibility(winrt::Microsoft::UI::Xaml::Visibility::Visible);
                }
            }
            co_return;
        }

        if (epoch != m_artistQueryEpoch
            || filter != m_artistLibraryFilter
            || query != m_artistLibrarySearch
            || !remote.IsCurrent(scope))
        {
            co_return;
        }
        m_artistPageLoading = false;
        if (!page.Valid)
        {
            if (ArtistLibraryLoadMoreButton())
            {
                ArtistLibraryLoadMoreButton().IsEnabled(true);
            }
            if (ArtistLibraryStatusText())
            {
                ArtistLibraryStatusText().Text(L"The artist response was not accepted.");
                ArtistLibraryStatusText().Visibility(winrt::Microsoft::UI::Xaml::Visibility::Visible);
            }
            co_return;
        }

        if (!append)
        {
            m_artistAllResults.clear();
        }
        std::unordered_set<std::wstring> publicIds;
        for (auto const& existing : m_artistAllResults)
        {
            if (!existing.RemoteId().empty())
            {
                publicIds.emplace(existing.RemoteId().c_str());
            }
        }
        for (auto const& artist : page.Artists)
        {
            if (publicIds.emplace(artist.Id).second)
            {
                m_artistAllResults.push_back(ArtistCardFromContract(artist));
            }
        }
        m_artistNextCursor = std::move(page.NextCursor);
        if (page.TotalCount || !append)
        {
            m_artistTotalCount = page.TotalCount;
        }
        if (filter == L"All" && query.empty() && page.TotalCount)
        {
            m_artistCanonicalTotalCount = page.TotalCount;
        }
        ApplyArtistLibraryView();
        UpdateLibraryHeaderMetrics();
        if (ArtistLibraryStatusText())
        {
            ArtistLibraryStatusText().Text(L"");
            ArtistLibraryStatusText().Visibility(winrt::Microsoft::UI::Xaml::Visibility::Collapsed);
        }
        co_return;
    }

    LastMusicPlayer::Backend::ArtistIdentity MainWindow::ArtistIdentityForTrack(
        winrt::Last_Music_Player::TrackInfo const& artist) const
    {
        if (!artist)
        {
            return {};
        }
        auto sourceUrl = std::wstring(artist.SourceUrl().c_str());
        if (!LastMusicPlayer::Backend::IsSafeRemoteUrl(
                artist.SourceUrl(),
                LastMusicPlayer::Backend::RemoteUrlUse::Durable))
        {
            sourceUrl.clear();
        }
        auto provider = artist.RemoteProvider().empty() ? artist.Provider() : artist.RemoteProvider();
        return {
            std::wstring(artist.ArtistCatalogId().c_str()),
            std::move(sourceUrl),
            std::wstring(artist.Title().c_str()),
            std::wstring(provider.c_str()) };
    }

    bool MainWindow::SetArtistFollowed(
        winrt::Last_Music_Player::TrackInfo const& artist,
        bool followed)
    {
        auto scopeKey = ArtistFollowScopeKey();
        if (scopeKey.empty())
        {
            if (ArtistLibraryStatusText())
            {
                ArtistLibraryStatusText().Text(
                    L"Account artist following needs the matching MagnetoFX sync update.");
                ArtistLibraryStatusText().Visibility(winrt::Microsoft::UI::Xaml::Visibility::Visible);
            }
            return false;
        }

        auto identity = ArtistIdentityForTrack(artist);
        auto identityKey = LastMusicPlayer::Backend::ArtistIdentityKey(identity);
        if (identityKey.empty() || !detail::DatabaseService().IsInitialized())
        {
            return false;
        }

        bool saved = false;
        if (followed)
        {
            LastMusicPlayer::Backend::FollowedArtistSnapshot snapshot;
            snapshot.ScopeKey = scopeKey;
            snapshot.IdentityKey = identityKey;
            snapshot.CatalogId = identity.CatalogId;
            snapshot.SourceUrl = identity.SourceUrl;
            snapshot.Name = identity.Name;
            snapshot.Provider = identity.Provider;
            saved = detail::DatabaseService().FollowArtist(snapshot);
        }
        else
        {
            saved = detail::DatabaseService().UnfollowArtist(scopeKey, identityKey);
        }
        if (!saved)
        {
            return false;
        }

        m_artistAllResults = detail::DatabaseService().LoadArtists(scopeKey);
        for (auto& item : m_artistAllResults)
        {
            auto itemIdentity = ArtistIdentityForTrack(item);
            if (followed
                && LastMusicPlayer::Backend::SameArtistIdentity(itemIdentity, identity)
                && item.ArtworkUrl().empty())
            {
                item.ArtworkUrl(artist.ArtworkUrl());
                item.AlbumArt(artist.AlbumArt());
            }
            detail::ResolveArtworkPresentation(item, L"artist");
        }
        ApplyArtistLibraryView();
        if (ArtistLibraryStatusText())
        {
            ArtistLibraryStatusText().Text(followed ? L"Artist followed" : L"Artist unfollowed");
            ArtistLibraryStatusText().Visibility(winrt::Microsoft::UI::Xaml::Visibility::Visible);
        }
        return true;
    }

    winrt::Windows::Foundation::IAsyncAction MainWindow::SetAccountArtistFollowedAsync(
        winrt::Last_Music_Player::TrackInfo artist,
        bool followed)
    {
        auto lifetime = get_strong();
        auto publicId = std::wstring(artist ? artist.RemoteId().c_str() : L"");
        if (!LastMusicPlayer::Backend::IsValidLibraryArtistPublicId(publicId)
            || !m_artistFollowMutations.emplace(publicId).second)
        {
            co_return;
        }
        if (m_currentLibraryArtistId == publicId)
        {
            UpdateArtistDetailFollowButton();
        }

        auto& remote = detail::RemoteMusicServiceService();
        auto const scope = remote.CaptureScope();
        if (scope.Mode != LastMusicPlayer::Backend::RemoteAccessMode::Account
            || !remote.IsModeAvailable(scope.Mode))
        {
            m_artistFollowMutations.erase(publicId);
            if (m_currentLibraryArtistId == publicId)
            {
                UpdateArtistDetailFollowButton();
            }
            co_return;
        }
        if (ArtistLibraryStatusText())
        {
            ArtistLibraryStatusText().Text(followed ? L"Following artist..." : L"Unfollowing artist...");
            ArtistLibraryStatusText().Visibility(winrt::Microsoft::UI::Xaml::Visibility::Visible);
        }

        try
        {
            (void)co_await remote.SetLibraryArtistFollowedAsync(
                scope,
                winrt::hstring(publicId),
                followed);
        }
        catch (winrt::hresult_canceled const&)
        {
            m_artistFollowMutations.erase(publicId);
            if (m_currentLibraryArtistId == publicId)
            {
                UpdateArtistDetailFollowButton();
            }
            co_return;
        }
        catch (winrt::hresult_error const& error)
        {
            m_artistFollowMutations.erase(publicId);
            if (m_currentLibraryArtistId == publicId)
            {
                UpdateArtistDetailFollowButton();
            }
            if (remote.IsCurrent(scope) && ArtistLibraryStatusText())
            {
                auto const status = LastMusicPlayer::Backend::AccountHttpStatus(error);
                ArtistLibraryStatusText().Text(status == 0
                    ? winrt::hstring{ L"Could not update artist follow state." }
                    : LastMusicPlayer::Backend::SafeAccountErrorMessage(status));
                ArtistLibraryStatusText().Visibility(winrt::Microsoft::UI::Xaml::Visibility::Visible);
            }
            co_return;
        }
        catch (...)
        {
            m_artistFollowMutations.erase(publicId);
            if (m_currentLibraryArtistId == publicId)
            {
                UpdateArtistDetailFollowButton();
            }
            if (remote.IsCurrent(scope) && ArtistLibraryStatusText())
            {
                ArtistLibraryStatusText().Text(L"Could not update artist follow state.");
                ArtistLibraryStatusText().Visibility(winrt::Microsoft::UI::Xaml::Visibility::Visible);
            }
            co_return;
        }

        m_artistFollowMutations.erase(publicId);
        if (!remote.IsCurrent(scope))
        {
            co_return;
        }
        if (m_currentLibraryArtistId == publicId)
        {
            m_currentLibraryArtistFollowed = followed;
            UpdateArtistDetailFollowButton();
        }
        co_await LoadAccountLibraryArtistsAsync(false);
    }

    void MainWindow::ApplyArtistLibraryView()
    {
        auto visible = m_artistAllResults;
        auto const accountMode = detail::RemoteMusicServiceService().Mode()
            == LastMusicPlayer::Backend::RemoteAccessMode::Account;
        if (!accountMode)
        {
            auto const query = LastMusicPlayer::Backend::NormalizeArtistName(m_artistLibrarySearch);
            std::erase_if(visible, [&](auto const& artist)
            {
                if (query.empty() && m_artistLibraryFilter == L"Followed" && !artist.IsFollowed())
                {
                    return true;
                }
                return !query.empty()
                    && LastMusicPlayer::Backend::NormalizeArtistName(artist.Title().c_str()).find(query)
                        == std::wstring::npos;
            });

            auto byName = [](auto const& left, auto const& right)
            {
                return LastMusicPlayer::Backend::NormalizeArtistName(left.Title().c_str())
                    < LastMusicPlayer::Backend::NormalizeArtistName(right.Title().c_str());
            };
            if (!query.empty() || m_artistLibraryFilter == L"All")
            {
                std::stable_sort(visible.begin(), visible.end(), byName);
            }
            else if (m_artistLibraryFilter == L"Followed")
            {
                std::stable_sort(visible.begin(), visible.end(), [&](auto const& left, auto const& right)
                {
                    return left.DateAddedSortKey() == right.DateAddedSortKey()
                        ? byName(left, right)
                        : left.DateAddedSortKey() > right.DateAddedSortKey();
                });
            }
            else
            {
                std::stable_sort(visible.begin(), visible.end(), [&](auto const& left, auto const& right)
                {
                    if (left.IsFollowed() != right.IsFollowed())
                    {
                        return left.IsFollowed();
                    }
                    if (left.IsFollowed() && left.DateAddedSortKey() != right.DateAddedSortKey())
                    {
                        return left.DateAddedSortKey() > right.DateAddedSortKey();
                    }
                    return left.TrackCount() == right.TrackCount()
                        ? byName(left, right)
                        : left.TrackCount() > right.TrackCount();
                });
                auto const followedCount = static_cast<std::size_t>(std::count_if(
                    visible.begin(), visible.end(), [](auto const& artist) { return artist.IsFollowed(); }));
                auto const maximum = followedCount + LastMusicPlayer::Backend::DefaultTopArtistLimit;
                if (visible.size() > maximum)
                {
                    visible.resize(maximum);
                }
            }
        }

        m_artists.Clear();
        for (auto const& artist : visible)
        {
            m_artists.Append(artist);
        }
        if (ArtistLibraryResultCount())
        {
            auto text = std::to_wstring(visible.size());
            if (accountMode && m_artistTotalCount && *m_artistTotalCount > static_cast<std::int64_t>(visible.size()))
            {
                text += L" of " + std::to_wstring(*m_artistTotalCount);
            }
            auto const grammaticalCount = m_artistTotalCount.value_or(
                static_cast<std::int64_t>(visible.size()));
            text += grammaticalCount == 1 ? L" artist" : L" artists";
            ArtistLibraryResultCount().Text(winrt::hstring(text));
        }
        if (ArtistLibraryEmptyText())
        {
            ArtistLibraryEmptyText().Visibility(visible.empty()
                ? winrt::Microsoft::UI::Xaml::Visibility::Visible
                : winrt::Microsoft::UI::Xaml::Visibility::Collapsed);
        }
        if (ArtistLibraryLoadMoreButton())
        {
            ArtistLibraryLoadMoreButton().Visibility(accountMode && !m_artistNextCursor.empty()
                ? winrt::Microsoft::UI::Xaml::Visibility::Visible
                : winrt::Microsoft::UI::Xaml::Visibility::Collapsed);
            ArtistLibraryLoadMoreButton().IsEnabled(!m_artistPageLoading);
        }
    }

    void MainWindow::LibraryArtistSearch_TextChanged(
        winrt::Windows::Foundation::IInspectable const& sender,
        winrt::Microsoft::UI::Xaml::Controls::TextChangedEventArgs const& args)
    {
        (void)args;
        auto box = sender.try_as<winrt::Microsoft::UI::Xaml::Controls::TextBox>();
        auto const typed = box
            ? std::wstring(detail::TrimQuery(box.Text()).c_str())
            : std::wstring{};
        auto const debounceId = ++m_artistSearchDebounceId;
        [](winrt::weak_ref<MainWindow> weakThis,
           winrt::Microsoft::UI::Dispatching::DispatcherQueue dispatcher,
           uint64_t debounceId,
           std::wstring typed) -> winrt::fire_and_forget
        {
            co_await winrt::resume_after(std::chrono::milliseconds(250));
            dispatcher.TryEnqueue([weakThis, debounceId, typed = std::move(typed)]()
            {
                auto self = weakThis.get();
                if (!self || debounceId != self->m_artistSearchDebounceId
                    || self->m_artistLibrarySearch == typed)
                {
                    return;
                }
                self->m_artistLibrarySearch = typed;
                if (detail::RemoteMusicServiceService().Mode()
                    == LastMusicPlayer::Backend::RemoteAccessMode::Account)
                {
                    detail::RunDetached(self->LoadAccountLibraryArtistsAsync(false));
                }
                else
                {
                    self->ApplyArtistLibraryView();
                }
            });
        }(get_weak(), DispatcherQueue(), debounceId, typed);
    }

    void MainWindow::LibraryArtistFilter_Checked(
        winrt::Windows::Foundation::IInspectable const& sender,
        winrt::Microsoft::UI::Xaml::RoutedEventArgs const& args)
    {
        (void)args;
        auto button = sender.try_as<winrt::Microsoft::UI::Xaml::Controls::Primitives::ToggleButton>();
        if (!button || !m_xamlReadyForEvents)
        {
            return;
        }
        winrt::Microsoft::UI::Xaml::Controls::Primitives::ToggleButton filters[] = {
            ArtistFilterTop(), ArtistFilterFollowed(), ArtistFilterAll()
        };
        for (auto const& filter : filters)
        {
            if (filter && filter != button)
            {
                filter.IsChecked(false);
            }
        }
        m_artistLibraryFilter = detail::ReadTagString(button.Tag());
        if (detail::RemoteMusicServiceService().Mode()
            == LastMusicPlayer::Backend::RemoteAccessMode::Account)
        {
            detail::RunDetached(LoadAccountLibraryArtistsAsync(false));
        }
        else
        {
            ApplyArtistLibraryView();
        }
    }

    void MainWindow::LibraryArtistFollow_Click(
        winrt::Windows::Foundation::IInspectable const& sender,
        winrt::Microsoft::UI::Xaml::RoutedEventArgs const& args)
    {
        (void)args;
        auto artist = TrackFromActionSender(sender);
        if (artist)
        {
            if (detail::RemoteMusicServiceService().Mode()
                == LastMusicPlayer::Backend::RemoteAccessMode::Account)
            {
                detail::RunDetached(SetAccountArtistFollowedAsync(artist, !artist.IsFollowed()));
            }
            else
            {
                (void)SetArtistFollowed(artist, !artist.IsFollowed());
            }
        }
    }

    void MainWindow::LibraryArtistLoadMore_Click(
        winrt::Windows::Foundation::IInspectable const& sender,
        winrt::Microsoft::UI::Xaml::RoutedEventArgs const& args)
    {
        (void)sender;
        (void)args;
        detail::RunDetached(LoadAccountLibraryArtistsAsync(true));
    }

}
