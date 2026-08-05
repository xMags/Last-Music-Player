#include "pch.h"
#include "MainWindow.xaml.h"
#include "MainWindow.Internal.h"

#include "Backend/CatalogParser.h"
#include "Backend/CatalogPresentation.h"
#include "Frontend/RoundedCornerClip.h"
#include "Frontend/CarouselArrows.h"

#include <winrt/Windows.Storage.Streams.h>
#include <winrt/Microsoft.UI.Xaml.Controls.h>
#include <winrt/Microsoft.UI.Xaml.Controls.Primitives.h>
#include <winrt/Microsoft.UI.Xaml.Markup.h>
#include <winrt/Microsoft.UI.Xaml.Media.Imaging.h>
#include <winrt/Microsoft.UI.Text.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace winrt::Last_Music_Player::implementation
{
    using namespace detail;

    namespace
    {
        // Budget for the encoded account artwork held in memory. Covers run
        // around 300-600 KB each, so this holds well over a hundred of them.
        constexpr std::size_t kMaxCachedAccountArtworkBytes = 64u * 1024u * 1024u;

        // Total transport slots, and the share of them backfill may occupy.
        // Backfill is capped below the total so a tile scrolled into view always
        // finds a slot instead of queueing behind off-screen work.
        constexpr std::size_t kMaxConcurrentAccountArtworkRequests = 6;
        constexpr std::size_t kMaxConcurrentAccountArtworkBackfill = 2;

        // Delays between transport attempts within a single run.
        constexpr std::array kAccountArtworkRetryDelays{
            std::chrono::milliseconds{ 500 },
            std::chrono::milliseconds{ 1500 }
        };

        // A run that exhausts those and still failed transiently is re-queued
        // instead of dropped, which is what stopped throttled tiles from ever
        // recovering. The ceiling is on runs, not attempts, so the real limit is
        // this many times the retries above.
        constexpr int kMaxAccountArtworkRuns = 5;

        // Backoff before a re-queued run, indexed by runs already spent. Long
        // enough that a relay refusing a burst has stopped refusing by the time
        // the tail comes back to it.
        constexpr std::array kAccountArtworkRunBackoff{
            std::chrono::milliseconds{ 2000 },
            std::chrono::milliseconds{ 5000 },
            std::chrono::milliseconds{ 12000 },
            std::chrono::milliseconds{ 30000 }
        };

        // How often the deferred list is swept. Coarse on purpose: these are
        // decorative images and a few seconds of imprecision costs nothing.
        constexpr std::chrono::milliseconds kAccountArtworkRetrySweep{ 1000 };

        // How close to the viewport a tile must come before its artwork is
        // promoted, as a multiple of the viewport size. Slightly ahead of the
        // scroll so art is usually there by the time the tile is.
        constexpr double kAccountArtworkViewportMargin = 1.0;

        // First element of type T at or below `root`, breadth-first.
        //
        // A tile template nests its image a couple of levels down, and the
        // container hands back only the template root, so the element has to be
        // found rather than named: the same handler serves several templates.
        template <typename T>
        T FindDescendant(winrt::Microsoft::UI::Xaml::DependencyObject const& root)
        {
            if (!root)
            {
                return nullptr;
            }
            if (auto match = root.try_as<T>())
            {
                return match;
            }
            auto count = winrt::Microsoft::UI::Xaml::Media::VisualTreeHelper::GetChildrenCount(root);
            for (int32_t index = 0; index < count; ++index)
            {
                auto child = winrt::Microsoft::UI::Xaml::Media::VisualTreeHelper::GetChild(root, index);
                if (auto found = FindDescendant<T>(child))
                {
                    return found;
                }
            }
            return nullptr;
        }

        // Whether a parsed payload has anything worth putting on screen. A
        // request can succeed and still yield nothing usable, either because the
        // region genuinely has no charts or because the payload shape moved, and
        // both cases must be kept out of the on-screen state and off disk.
        bool HasCatalogContent(LastMusicPlayer::Backend::CatalogDiscovery const& discovery) noexcept
        {
            return !discovery.Shelves.empty()
                || !discovery.Charts.empty()
                || discovery.MoodActivityShelf.has_value()
                || (discovery.CityChartGroups
                    && (!discovery.CityChartGroups->Indian.Charts.empty()
                        || !discovery.CityChartGroups->International.Charts.empty()));
        }

        // Takes its inputs as parameters rather than captures, and deliberately
        // so: a coroutine copies its parameters into the frame, while a lambda's
        // captures stay in the closure object. A closure created inline at the
        // call site is a temporary, destroyed once the call expression ends,
        // which is before a coroutine that suspends ever reads them again.
        winrt::Windows::Foundation::IAsyncAction WriteCatalogDiscoveryCacheAsync(
            std::wstring storefront,
            std::wstring payload)
        {
            co_await winrt::resume_background();
            auto accountId = DatabaseService().ActiveAccountId();
            if (accountId.empty())
            {
                co_return;
            }
            DatabaseService().SaveCatalogDiscoveryPayload(accountId, storefront, payload);
        }

        bool IsTransientAccountArtworkError(winrt::hresult_error const& error) noexcept
        {
            auto code = error.code();
            return code == HRESULT_FROM_WIN32(ERROR_TIMEOUT)
                || code == HRESULT_FROM_WIN32(ERROR_CONNECTION_UNAVAIL)
                || code == HRESULT_FROM_WIN32(ERROR_RETRY);
        }

        winrt::hstring ResolvedArtworkUrl(winrt::hstring const& payload)
        {
            try
            {
                auto root = winrt::Windows::Data::Json::JsonObject::Parse(payload);
                auto result = root.GetNamedObject(L"result", nullptr);
                if (!result)
                {
                    return {};
                }

                auto track = result.GetNamedObject(L"track", nullptr);
                if (track)
                {
                    auto nestedArtwork = track.GetNamedString(
                        L"artworkUrl",
                        track.GetNamedString(L"imageUrl", L""));
                    if (!nestedArtwork.empty())
                    {
                        return nestedArtwork;
                    }
                }
                return result.GetNamedString(
                    L"artworkUrl",
                    result.GetNamedString(L"imageUrl", L""));
            }
            catch (...)
            {
                return {};
            }
        }
    }

    winrt::fire_and_forget MainWindow::ApplyAccountArtworkTargetAsync(
        AccountArtworkTarget target,
        winrt::Windows::Storage::Streams::IBuffer bytes,
        winrt::hstring requestKey,
        winrt::hstring artworkUrl,
        winrt::hstring sourceUrl)
    {
        auto lifetime = get_strong();

        // Revalidating both before and after the decode: the checks below are
        // what keep a recycled container or a newer now-playing track from being
        // overwritten, and the decode yields the thread in between.
        auto stillWanted = [&target, &requestKey]()
        {
            if (!target.Image)
            {
                return false;
            }

            auto tag = target.Image.Tag();
            auto currentUrl = tag
                ? winrt::unbox_value_or<winrt::hstring>(tag, winrt::hstring{})
                : winrt::hstring{};
            if (currentUrl != requestKey)
            {
                return false;
            }

            // The tag is the whole answer. It is written at registration from
            // whichever track the container is taking on, and any later
            // registration for that same element overwrites it, so a tag that
            // still reads back as this request means no newer item has claimed
            // the element and the bytes are still the ones it wants.
            //
            // This deliberately does not consult the element's DataContext.
            // Artwork is requested from ContainerContentChanging, which runs
            // before the container's DataContext has been repointed at its new
            // item: it still holds the previous one. Comparing against it
            // rejected the very applies that were correct, and because the
            // source is cleared when the request is registered, the tile was
            // left permanently blank rather than merely stale.
            //
            // Nor does it re-derive the URL from the track. The request key was
            // built from those fields at registration, so the tag comparison
            // above has already made that check; repeating it against a
            // possibly different TrackInfo instance for the same song only
            // reintroduces the same false rejection.
            return true;
        };

        if (!bytes || bytes.Length() == 0 || !stillWanted())
        {
            co_return;
        }

        // A bitmap of this target's own, never shared with another Image.
        winrt::Microsoft::UI::Xaml::Media::Imaging::BitmapImage bitmap{ nullptr };
        try
        {
            winrt::Windows::Storage::Streams::InMemoryRandomAccessStream stream;
            co_await stream.WriteAsync(bytes);
            stream.Seek(0);
            bitmap = CreateMusicArtworkBitmap(target.Detail);
            if (bitmap)
            {
                co_await bitmap.SetSourceAsync(stream);
            }
        }
        catch (...)
        {
            co_return;
        }

        if (!bitmap || !stillWanted())
        {
            co_return;
        }

        if (target.Track)
        {
            target.Track.ArtworkUrl(artworkUrl);
            // Only a tile-sized bitmap is published on the TrackInfo. That
            // object outlives this view and is bound into list rows, so parking
            // a full-screen decode on it would both waste memory for the rest of
            // the session and hand rows a bitmap sized for something else.
            if (target.Detail != ArtworkDetail::Hero)
            {
                target.Track.AlbumArt(bitmap);
                target.Track.ImageArtworkOpacity(1.0);
                target.Track.GeneratedArtworkOpacity(0.0);
            }
        }

        target.Image.Source(bitmap);
        target.Image.Opacity(1.0);
    }

    void MainWindow::QueueAccountArtworkImage(
        winrt::Microsoft::UI::Xaml::Controls::Image const& image,
        winrt::hstring const& artworkUrl,
        ArtworkDetail artworkDetail,
        winrt::Last_Music_Player::TrackInfo const& track)
    {
        auto normalized = NormalizeMusicArtworkUrl(artworkUrl);
        auto sourceUrl = track ? track.SourceUrl() : winrt::hstring{};
        auto hasArtworkUrl = !normalized.empty() && IsHttpUrl(normalized);
        auto canResolveSource = track
            && normalized.empty()
            && !sourceUrl.empty()
            && IsHttpUrl(sourceUrl);
        if (!image
            || (!hasArtworkUrl && !canResolveSource)
            || RemoteMusicServiceService().Mode() != LastMusicPlayer::Backend::RemoteAccessMode::Account)
        {
            return;
        }

        // The tag is the target's generation token. A recycled container or a new
        // now-playing track overwrites it before an older request can complete.
        // Surfaces of every size share one key, and so one fetch, because what
        // they must not share is the decoded bitmap rather than the bytes.
        auto requestKey = winrt::hstring{
            hasArtworkUrl
                ? L"artwork|" + std::wstring(normalized.c_str())
                : L"source|" + std::wstring(sourceUrl.c_str()) };
        image.Tag(winrt::box_value(requestKey));
        image.Source(nullptr);

        AccountArtworkTarget target{ image, track, artworkDetail };
        std::wstring key{ requestKey.c_str() };
        auto cached = m_accountArtworkCache.find(key);
        if (cached != m_accountArtworkCache.end())
        {
            ApplyAccountArtworkTargetAsync(
                target,
                cached->second.Bytes,
                requestKey,
                cached->second.ArtworkUrl,
                cached->second.SourceUrl);
            return;
        }

        auto pending = m_accountArtworkRequests.find(key);
        if (pending != m_accountArtworkRequests.end())
        {
            auto existing = std::find_if(
                pending->second.Targets.begin(),
                pending->second.Targets.end(),
                [&](AccountArtworkTarget const& queued)
                {
                    return queued.Image == image;
                });
            if (existing != pending->second.Targets.end())
            {
                existing->Track = track;
                existing->Detail = artworkDetail;
            }
            else
            {
                pending->second.Targets.push_back(std::move(target));
            }
            // Another tile for the same image may already be on screen even
            // though the one that first asked was not.
            ObserveAccountArtworkViewport(image, key);
            return;
        }

        auto requestId = ++m_accountArtworkRequestId;
        AccountArtworkRequest request;
        request.Id = requestId;
        request.ArtworkUrl = hasArtworkUrl ? normalized : winrt::hstring{};
        request.SourceUrl = canResolveSource ? sourceUrl : winrt::hstring{};
        request.Targets.push_back(std::move(target));
        // Everything starts as backfill. A tile that is actually on screen gets
        // promoted by the viewport callback below, which fires as soon as XAML
        // has laid it out, so the visible rows still go first.
        request.Priority = AccountArtworkPriority::Backfill;
        m_accountArtworkRequests.emplace(key, std::move(request));
        m_accountArtworkBackfillQueue.push_back(key);
        ObserveAccountArtworkViewport(image, key);
        StartAccountArtworkRequests();
    }

    void MainWindow::AttachCarouselArrows()
    {
        // The artwork on a card shelf is 156 tall and starts at the top of the
        // row, so its middle is 78 down. The mixes row is a taller card, and
        // its art block is 176, so 88.
        constexpr double kCardArtCenter = 78.0;
        constexpr double kMixArtCenter = 88.0;

        using LastMusicPlayer::Frontend::AttachCarouselArrows;
        AttachCarouselArrows(HomeRecentArrowHost(), HomeRecentGridView(), kCardArtCenter);
        AttachCarouselArrows(HomeMostPlayedArrowHost(), HomeMostPlayedGridView(), kCardArtCenter);
        AttachCarouselArrows(HomeLikedArrowHost(), HomeLikedGridView(), kCardArtCenter);
        AttachCarouselArrows(HomeRecentlyAddedArrowHost(), HomeRecentlyAddedGridView(), kCardArtCenter);
        AttachCarouselArrows(HomeMixArrowHost(), HomeMixScroller(), kMixArtCenter);
    }

    void MainWindow::AttachArtworkGridObservers()
    {
        // Every virtualized surface that shows account or catalog artwork. The
        // markup still carries Loaded on these templates, which is harmless
        // duplication: a second registration for the same image is deduplicated
        // by request key. What it cannot do is fire for a recycled container,
        // which is why each of these grids needs the container hook as well.
        for (auto const& grid : {
            HomeRecentGridView(),
            HomeMostPlayedGridView(),
            HomeLikedGridView(),
            HomeRecentlyAddedGridView(),
            DiscoverChartGridView(),
            SongsGridView(),
            LibAlbumsGrid(),
            LibArtistsGrid(),
            LibGenresGrid(),
            LibManualPlaylistsGrid(),
            LibAutoPlaylistsGrid() })
        {
            ObserveCatalogGridContainers(grid);
        }

        // The row lists need this every bit as much as the tile grids: a list
        // recycles a handful of rows for the whole collection, so without the
        // container hook only the rows realized on the first layout ever ask
        // for artwork and everything scrolled to afterwards stays blank. Listed
        // one by one because these are not all the same control type.
        ObserveCatalogGridContainers(SearchSongsListView());
        ObserveCatalogGridContainers(MusicListView());
        ObserveCatalogGridContainers(DiscoverDetailTracksListView());
        ObserveCatalogGridContainers(LibrarySongsListView());
        ObserveCatalogGridContainers(LibraryDetailTracksListView());
        ObserveCatalogGridContainers(UpNextListView());
        ObserveCatalogGridContainers(FsUpNext());
    }

    void MainWindow::ObserveCatalogGridContainers(
        winrt::Microsoft::UI::Xaml::Controls::ListViewBase const& grid)
    {
        if (!grid)
        {
            return;
        }
        grid.ContainerContentChanging({ this, &MainWindow::CatalogGrid_ContainerContentChanging });
    }

    void MainWindow::CatalogGrid_ContainerContentChanging(
        winrt::Microsoft::UI::Xaml::Controls::ListViewBase const& sender,
        winrt::Microsoft::UI::Xaml::Controls::ContainerContentChangingEventArgs const& args)
    {
        (void)sender;

        auto container = args.ItemContainer();
        if (!container)
        {
            return;
        }
        auto image = FindDescendant<winrt::Microsoft::UI::Xaml::Controls::Image>(
            container.ContentTemplateRoot());
        if (!image)
        {
            return;
        }

        if (args.InRecycleQueue())
        {
            // Deliberately nothing. Clearing the image here looked like the
            // tidy thing to do, but several of these templates bind Source to
            // AlbumArt, and nulling it fights the binding that is the only
            // thing putting their picture back. The registration below already
            // clears the source when the container takes on its next item, so
            // no stale cover survives either way.
            return;
        }

        auto track = args.Item().try_as<winrt::Last_Music_Player::TrackInfo>();
        if (!track)
        {
            return;
        }
        QueueAccountArtworkImage(image, track.ArtworkUrl(), ArtworkDetail::Tile, track);
    }

    void MainWindow::ObserveAccountArtworkViewport(
        winrt::Microsoft::UI::Xaml::Controls::Image const& image,
        std::wstring const& key)
    {
        // Weak, not strong: the subscription is owned by the image, which the
        // window owns in turn, so a strong capture would close a cycle for every
        // tile that never comes into view.
        auto weakSelf = get_weak();
        winrt::hstring requestKey{ key };
        // The token is stored in the closure so the handler can detach itself.
        // A tile only ever needs promoting once, and leaving the subscription
        // attached would keep firing for the rest of the element's life.
        auto token = std::make_shared<winrt::event_token>();
        *token = image.EffectiveViewportChanged(
            [weakSelf, requestKey, token](
                winrt::Microsoft::UI::Xaml::FrameworkElement const& sender,
                winrt::Microsoft::UI::Xaml::EffectiveViewportChangedEventArgs const& args)
            {
                // BringIntoViewDistance is zero once the element is inside the
                // viewport and grows as it moves away, so this admits a tile
                // that is on screen or within a screen of it.
                auto viewport = args.EffectiveViewport();
                auto marginX = viewport.Width * kAccountArtworkViewportMargin;
                auto marginY = viewport.Height * kAccountArtworkViewportMargin;
                if (args.BringIntoViewDistanceX() > marginX
                    || args.BringIntoViewDistanceY() > marginY)
                {
                    return;
                }
                sender.EffectiveViewportChanged(*token);
                if (auto self = weakSelf.get())
                {
                    self->PromoteAccountArtworkRequest(std::wstring{ requestKey.c_str() });
                }
            });
    }

    void MainWindow::PromoteAccountArtworkRequest(std::wstring const& key)
    {
        auto pending = m_accountArtworkRequests.find(key);
        if (pending == m_accountArtworkRequests.end()
            || pending->second.Priority == AccountArtworkPriority::Visible)
        {
            return;
        }
        pending->second.Priority = AccountArtworkPriority::Visible;

        // A deferred request keeps its place in the deferred list: promoting it
        // must not let it skip the backoff it is waiting out, or a scrolling
        // user would hammer a relay that is already refusing.
        if (pending->second.Deferred)
        {
            return;
        }

        auto queued = std::find(
            m_accountArtworkBackfillQueue.begin(),
            m_accountArtworkBackfillQueue.end(),
            key);
        if (queued == m_accountArtworkBackfillQueue.end())
        {
            // Already running. It will finish at whatever concurrency it took.
            return;
        }
        m_accountArtworkBackfillQueue.erase(queued);
        m_accountArtworkVisibleQueue.push_back(key);
        StartAccountArtworkRequests();
    }

    void MainWindow::StartAccountArtworkRequests()
    {
        while (m_activeAccountArtworkRequests < kMaxConcurrentAccountArtworkRequests)
        {
            std::wstring key;
            auto priority = AccountArtworkPriority::Visible;

            if (!m_accountArtworkVisibleQueue.empty())
            {
                key = std::move(m_accountArtworkVisibleQueue.front());
                m_accountArtworkVisibleQueue.pop_front();
            }
            else if (m_activeAccountArtworkBackfill < kMaxConcurrentAccountArtworkBackfill
                && !m_accountArtworkBackfillQueue.empty())
            {
                key = std::move(m_accountArtworkBackfillQueue.front());
                m_accountArtworkBackfillQueue.pop_front();
                priority = AccountArtworkPriority::Backfill;
            }
            else
            {
                break;
            }

            auto pending = m_accountArtworkRequests.find(key);
            if (pending == m_accountArtworkRequests.end())
            {
                continue;
            }

            ++m_activeAccountArtworkRequests;
            if (priority == AccountArtworkPriority::Backfill)
            {
                ++m_activeAccountArtworkBackfill;
            }
            HydrateAccountArtworkAsync(
                winrt::hstring{ key },
                pending->second.Id,
                priority);
        }
    }

    void MainWindow::CompleteAccountArtworkRequest(AccountArtworkPriority priority)
    {
        if (m_activeAccountArtworkRequests > 0)
        {
            --m_activeAccountArtworkRequests;
        }
        // Released against the slot it was started on, not the priority it may
        // have been promoted to since, or the backfill count would drift.
        if (priority == AccountArtworkPriority::Backfill && m_activeAccountArtworkBackfill > 0)
        {
            --m_activeAccountArtworkBackfill;
        }
        StartAccountArtworkRequests();
    }

    void MainWindow::DeferAccountArtworkRequest(std::wstring const& key, int attempts)
    {
        auto pending = m_accountArtworkRequests.find(key);
        if (pending == m_accountArtworkRequests.end())
        {
            return;
        }
        // Parenthesised because windows.h defines min/max as macros.
        auto index = static_cast<std::size_t>((std::max)(0, attempts - 1));
        auto delay = kAccountArtworkRunBackoff[
            (std::min)(index, kAccountArtworkRunBackoff.size() - 1)];

        pending->second.Deferred = true;
        m_deferredAccountArtwork.push_back(
            DeferredAccountArtworkRequest{ key, std::chrono::steady_clock::now() + delay });
        EnsureAccountArtworkRetryTimer();
    }

    void MainWindow::EnsureAccountArtworkRetryTimer()
    {
        if (m_deferredAccountArtwork.empty())
        {
            return;
        }
        if (!m_accountArtworkRetryTimer)
        {
            auto queue = DispatcherQueue();
            if (!queue)
            {
                return;
            }
            m_accountArtworkRetryTimer = queue.CreateTimer();
            m_accountArtworkRetryTimer.Interval(kAccountArtworkRetrySweep);
            // Raw `this` is safe here only because the timer is stopped in the
            // window's teardown alongside the other timers it owns.
            m_accountArtworkRetryTimer.Tick(
                [this](
                    winrt::Microsoft::UI::Dispatching::DispatcherQueueTimer const&,
                    winrt::Windows::Foundation::IInspectable const&)
                {
                    ReleaseDueAccountArtworkRequests();
                });
        }
        if (!m_accountArtworkRetryTimer.IsRunning())
        {
            m_accountArtworkRetryTimer.Start();
        }
    }

    void MainWindow::ReleaseDueAccountArtworkRequests()
    {
        auto now = std::chrono::steady_clock::now();
        auto due = std::stable_partition(
            m_deferredAccountArtwork.begin(),
            m_deferredAccountArtwork.end(),
            [&](DeferredAccountArtworkRequest const& entry)
            {
                return entry.DueAt > now;
            });

        std::vector<DeferredAccountArtworkRequest> ready{
            std::make_move_iterator(due),
            std::make_move_iterator(m_deferredAccountArtwork.end()) };
        m_deferredAccountArtwork.erase(due, m_deferredAccountArtwork.end());

        for (auto const& entry : ready)
        {
            auto pending = m_accountArtworkRequests.find(entry.Key);
            if (pending == m_accountArtworkRequests.end())
            {
                continue;
            }
            pending->second.Deferred = false;
            // Requeued at whatever priority it holds now: a tile the user has
            // scrolled to while it was waiting goes back as visible work.
            if (pending->second.Priority == AccountArtworkPriority::Visible)
            {
                m_accountArtworkVisibleQueue.push_back(entry.Key);
            }
            else
            {
                m_accountArtworkBackfillQueue.push_back(entry.Key);
            }
        }

        if (m_deferredAccountArtwork.empty() && m_accountArtworkRetryTimer)
        {
            m_accountArtworkRetryTimer.Stop();
        }
        if (!ready.empty())
        {
            StartAccountArtworkRequests();
        }
    }

    winrt::fire_and_forget MainWindow::HydrateAccountArtworkAsync(
        winrt::hstring requestKey,
        uint64_t requestId,
        AccountArtworkPriority priority)
    {
        auto lifetime = get_strong();
        auto dispatcher = DispatcherQueue();
        auto& remoteMusic = RemoteMusicServiceService();
        auto scope = remoteMusic.CaptureScope();
        std::wstring key{ requestKey.c_str() };

        auto initial = m_accountArtworkRequests.find(key);
        if (initial == m_accountArtworkRequests.end() || initial->second.Id != requestId)
        {
            CompleteAccountArtworkRequest(priority);
            co_return;
        }
        auto artworkUrl = initial->second.ArtworkUrl;
        auto sourceUrl = initial->second.SourceUrl;
        auto runsSpent = ++initial->second.Attempts;

        if (artworkUrl.empty() && !sourceUrl.empty())
        {
            try
            {
                artworkUrl = NormalizeMusicArtworkUrl(
                    ResolvedArtworkUrl(co_await remoteMusic.ResolveUrlAsync(sourceUrl)));
            }
            catch (...)
            {
            }
        }

        winrt::Windows::Storage::Streams::IBuffer buffer{ nullptr };
        bool fetched = false;
        // Whether the last failure was the kind that another run could survive:
        // a throttle or a server-side wobble, as opposed to a 404 for artwork
        // that genuinely is not there. Only the former earns a re-queue.
        bool transientFailure = false;
        if (!artworkUrl.empty() && IsHttpUrl(artworkUrl))
        {
            for (std::size_t attempt = 0;
                attempt <= kAccountArtworkRetryDelays.size();
                ++attempt)
            {
                bool retry = false;
                try
                {
                    // BitmapImage cannot attach the account session header. The
                    // relay also validates and unwraps durable external/provider
                    // URLs server-side.
                    buffer = co_await remoteMusic.GetAccountArtworkAsync(artworkUrl);
                    fetched = buffer && buffer.Length() > 0;
                    transientFailure = false;
                }
                catch (winrt::hresult_error const& error)
                {
                    retry = IsTransientAccountArtworkError(error);
                    transientFailure = retry;
                }
                catch (...)
                {
                    transientFailure = false;
                }

                if (fetched
                    || !retry
                    || attempt == kAccountArtworkRetryDelays.size()
                    || !remoteMusic.IsCurrent(scope))
                {
                    break;
                }
                co_await winrt::resume_after(kAccountArtworkRetryDelays[attempt]);
            }
        }

        co_await wil::resume_foreground(dispatcher);

        auto pending = m_accountArtworkRequests.find(key);
        if (pending == m_accountArtworkRequests.end() || pending->second.Id != requestId)
        {
            CompleteAccountArtworkRequest(priority);
            co_return;
        }
        if (!fetched)
        {
            // A relay refusing a burst fails a whole tail of tiles at once.
            // Dropping them here is what left those tiles grey for the rest of
            // the session, with nothing to bring them back: the element has
            // already loaded, so its handler never runs again. Give a transient
            // failure another run after a backoff instead, and only give up for
            // good once the runs are spent or the failure was never going to
            // resolve itself.
            if (transientFailure
                && runsSpent < kMaxAccountArtworkRuns
                && remoteMusic.IsCurrent(scope))
            {
                DeferAccountArtworkRequest(key, runsSpent);
            }
            else
            {
                m_accountArtworkRequests.erase(pending);
            }
            CompleteAccountArtworkRequest(priority);
            co_return;
        }
        if (!remoteMusic.IsCurrent(scope))
        {
            // The account moved out from under this fetch, so the bytes belong
            // to a session that is no longer on screen.
            m_accountArtworkRequests.erase(pending);
            CompleteAccountArtworkRequest(priority);
            co_return;
        }

        auto request = std::move(pending->second);
        m_accountArtworkRequests.erase(pending);

        // Once the budget is spent, drop the cached bytes and start over. This
        // keeps the memory bound deterministic without adding an LRU policy for
        // decorative images.
        auto entryBytes = static_cast<std::size_t>(buffer.Length());
        if (m_accountArtworkCacheBytes + entryBytes > kMaxCachedAccountArtworkBytes)
        {
            m_accountArtworkCache.clear();
            m_accountArtworkCacheBytes = 0;
        }
        m_accountArtworkCache.insert_or_assign(
            key,
            AccountArtworkCacheEntry{ buffer, artworkUrl, sourceUrl });
        m_accountArtworkCacheBytes += entryBytes;

        for (auto const& target : request.Targets)
        {
            ApplyAccountArtworkTargetAsync(
                target,
                buffer,
                requestKey,
                artworkUrl,
                sourceUrl);
        }
        CompleteAccountArtworkRequest(priority);
    }

    void MainWindow::ClearAccountArtworkCache()
    {
        m_accountArtworkCache.clear();
        m_accountArtworkCacheBytes = 0;
        m_accountArtworkRequests.clear();
        m_accountArtworkVisibleQueue.clear();
        m_accountArtworkBackfillQueue.clear();
        m_deferredAccountArtwork.clear();
        if (m_accountArtworkRetryTimer)
        {
            m_accountArtworkRetryTimer.Stop();
        }
        // In-flight runs are not cancelled, only orphaned: each still resumes
        // and releases its own slot through the request-not-found path, so the
        // active counters stay balanced without being reset here.
        ++m_accountArtworkRequestId;
    }

    void MainWindow::ArtworkImage_Loaded(
        winrt::Windows::Foundation::IInspectable const& sender,
        winrt::Microsoft::UI::Xaml::RoutedEventArgs const& args)
    {
        (void)args;
        if (auto element = sender.try_as<winrt::Microsoft::UI::Xaml::FrameworkElement>())
        {
            LastMusicPlayer::Frontend::ApplyRoundedCornerClip(element);
        }
    }

    void MainWindow::AccountArtwork_Loaded(
        winrt::Windows::Foundation::IInspectable const& sender,
        winrt::Microsoft::UI::Xaml::RoutedEventArgs const& args)
    {
        (void)args;
        auto image = sender.try_as<winrt::Microsoft::UI::Xaml::Controls::Image>();
        if (!image)
        {
            return;
        }

        // Round the surface whether or not it ends up with artwork to fetch, so
        // a template that only ever binds AlbumArt is still clipped.
        LastMusicPlayer::Frontend::ApplyRoundedCornerClip(image);

        auto track = image.DataContext().try_as<winrt::Last_Music_Player::TrackInfo>();
        if (!track)
        {
            return;
        }
        // Every Image bound through this handler lives in a list row or a grid
        // tile template.
        QueueAccountArtworkImage(image, track.ArtworkUrl(), ArtworkDetail::Tile, track);
    }

    winrt::Last_Music_Player::TrackInfo MainWindow::CatalogItemToTrack(
        LastMusicPlayer::Backend::CatalogItem const& item,
        int32_t index)
    {
        winrt::Last_Music_Player::TrackInfo track;
        track.Title(item.Title);
        track.Artist(item.Subtitle);
        track.Album(item.AlbumName);
        track.Index(index);
        track.ArtworkUrl(item.ArtworkUrl);
        // SourceUrl is the whole point: playing a catalog song posts this URL to
        // the existing resolve path, which mirrors it onto something playable.
        track.SourceUrl(item.SourceUrl);
        track.Provider(item.Provider);
        track.SourceKind(L"remote");
        track.SourceLabel(L"Catalog");
        track.DateAdded(L"Catalog");
        track.RemoteId(item.RemoteId);
        track.RemoteCatalogId(item.CatalogId);
        track.AlbumCatalogId(item.AlbumCatalogId);
        track.ArtistCatalogId(item.ArtistCatalogId);
        track.TrackCount(item.TrackCount);
        // The resource type rides along so a tile click knows whether to play a
        // song or open a detail page.
        track.Genre(LastMusicPlayer::Backend::CatalogResourceTypeName(item.Type));

        auto seconds = item.DurationMs > 0 ? static_cast<double>(item.DurationMs) / 1000.0 : 0.0;
        track.DurationSeconds(seconds);
        track.Duration(seconds > 0.0
            ? winrt::hstring{ LastMusicPlayer::Frontend::UIHelpers::FormatTime(seconds) }
            : winrt::hstring{});

        if (!item.RemoteId.empty())
        {
            auto override = m_catalogLikeOverrides.find(std::wstring{ item.RemoteId.c_str() });
            if (override != m_catalogLikeOverrides.end())
            {
                track.IsLiked(override->second);
                return track;
            }
            auto applyLiked = [&](winrt::Last_Music_Player::TrackInfo const& candidate)
            {
                if (candidate && candidate.RemoteId() == item.RemoteId)
                {
                    track.IsLiked(candidate.IsLiked());
                    return true;
                }
                return false;
            };
            for (auto const& candidate : m_catalogTracks)
            {
                if (applyLiked(candidate)) break;
            }
            if (!track.IsLiked())
            {
                for (uint32_t likedIndex = 0; likedIndex < m_homeLikedTracks.Size(); ++likedIndex)
                {
                    if (applyLiked(m_homeLikedTracks.GetAt(likedIndex))) break;
                }
            }
        }
        return track;
    }

    void MainWindow::ShowCatalogSurface(CatalogSurface surface, bool pushCurrent)
    {
        using winrt::Microsoft::UI::Xaml::Visibility;
        ++m_discoverNavigationEpoch;

        if (pushCurrent && surface != m_catalogSurface)
        {
            m_catalogBackStack.push_back(m_catalogSurface);
        }
        m_catalogSurface = surface;

        if (surface == CatalogSurface::Home)
        {
            HomeButton_Click(nullptr, nullptr);
            return;
        }

        // The catalog is not a destination of its own: it opens in Home's place
        // and leaves Home selected in the rail. So the ordinary page switch runs
        // first, and the catalog then takes the slot Home would have filled.
        ShowPrimaryView(L"Home");
        HomeViewContainer().Visibility(Visibility::Collapsed);
        HomeCatalogViewContainer().Visibility(Visibility::Visible);

        DiscoverChartGalleryPage().Visibility(surface == CatalogSurface::ChartGallery ? Visibility::Visible : Visibility::Collapsed);
        DiscoverChartPage().Visibility(surface == CatalogSurface::Chart ? Visibility::Visible : Visibility::Collapsed);
        DiscoverDetailPage().Visibility(surface == CatalogSurface::Detail ? Visibility::Visible : Visibility::Collapsed);
    }

    void MainWindow::AttachSkeletons()
    {
        using LastMusicPlayer::Frontend::SkeletonShape;

        // Counts are chosen to fill roughly one screen of the surface they
        // cover, so the placeholder neither stops short nor runs on past the
        // content that replaces it.
        m_catalogSkeleton.Attach(HomeCatalogSkeleton(), SkeletonShape::Shelf, 2);
        m_listenAgainSkeleton.Attach(ListenAgainSkeletonHost(), SkeletonShape::TileRow, 6);
        m_recentlyAddedSkeleton.Attach(RecentlyAddedSkeletonHost(), SkeletonShape::TileRow, 6);
        m_searchSkeleton.Attach(SearchSkeletonHost(), SkeletonShape::TileGrid, 8);
        m_songsSkeleton.Attach(SongsSkeletonHost(), SkeletonShape::TrackList, 9);
        m_libraryTabsSkeleton.Attach(LibraryTabsSkeletonHost(), SkeletonShape::TrackList, 9);
        m_libraryDetailSkeleton.Attach(LibraryDetailSkeletonHost(), SkeletonShape::TrackList, 8);
        m_discoverDetailSkeleton.Attach(DiscoverDetailSkeletonHost(), SkeletonShape::TrackList, 8);
        m_discoverChartSkeleton.Attach(DiscoverChartSkeletonHost(), SkeletonShape::TileGrid, 10);
    }

    void MainWindow::BeginLibraryTabSkeleton(LastMusicPlayer::Frontend::SkeletonShape shape)
    {
        // Re-attaching discards the tree built for the previous tab, which is
        // the point: a track list and a card grid look nothing alike, and only
        // one tab is ever on screen.
        if (shape != m_libraryTabSkeletonShape)
        {
            m_libraryTabsSkeleton.Attach(
                LibraryTabsSkeletonHost(),
                shape,
                shape == LastMusicPlayer::Frontend::SkeletonShape::TrackList ? 9 : 10);
            m_libraryTabSkeletonShape = shape;
        }

        // Always treated as a first load: switching tabs replaces the content
        // wholesale, so there is never anything worth preserving underneath.
        m_libraryTabsSkeleton.BeginLoading(true);
    }

    void MainWindow::ShowHomeCatalogStatus(
        winrt::hstring const& message,
        bool loading,
        bool canRetry)
    {
        using winrt::Microsoft::UI::Xaml::Visibility;
        auto accountMode = RemoteMusicServiceService().Mode() == LastMusicPlayer::Backend::RemoteAccessMode::Account;
        auto hasContent = HasCatalogContent(m_catalogDiscovery);

        // The skeleton stands in for shelves that are not there yet, so it only
        // makes sense on a first load. Refreshing over shelves the user is
        // already reading keeps the one-line status instead: swapping real
        // content for placeholders would be a step backwards.
        auto showSkeleton = accountMode && loading && !hasContent;

        HomeCatalogPrimaryContainer().Visibility(accountMode && (hasContent || !message.empty())
            ? Visibility::Visible
            : Visibility::Collapsed);
        if (showSkeleton)
        {
            m_catalogSkeleton.BeginLoading(true);
        }
        else
        {
            m_catalogSkeleton.EndLoading();
        }
        HomeCatalogStatusText().Text(message);
        HomeCatalogStatusPanel().Visibility(message.empty() || showSkeleton
            ? Visibility::Collapsed
            : Visibility::Visible);
        HomeCatalogProgressRing().IsActive(loading && !showSkeleton);
        HomeCatalogProgressRing().Visibility(loading && !showSkeleton ? Visibility::Visible : Visibility::Collapsed);
        HomeCatalogRetryButton().Visibility(canRetry ? Visibility::Visible : Visibility::Collapsed);
    }

    void MainWindow::UpdateCatalogAvailability()
    {
        using winrt::Microsoft::UI::Xaml::Visibility;
        using LastMusicPlayer::Backend::AccountSessionStatus;
        using LastMusicPlayer::Backend::RemoteAccessMode;

        auto status = AccountSessionService().Status();
        bool available = RemoteMusicServiceService().Mode() == RemoteAccessMode::Account
            && (status == AccountSessionStatus::Validated || status == AccountSessionStatus::Offline);

        // The region picker configures the catalog, so it is only shown while
        // there is a catalog to configure.
        if (auto regionCard = SettingsCatalogRegionCard())
        {
            regionCard.Visibility(available ? Visibility::Visible : Visibility::Collapsed);
        }

        if (available)
        {
            if (HomeViewContainer().Visibility() == Visibility::Visible)
            {
                RunDetached(HydrateDiscoverAsync(false));
            }
            return;
        }

        // Leaving Account mode invalidates everything on the page, including the
        // artwork cache, whose entries were fetched with the old session.
        ++m_discoverEpoch;
        m_discoverLoaded = false;
        m_catalogDiscovery = {};
        m_catalogContentStorefront.clear();
        m_catalogBackStack.clear();
        m_catalogGalleryCharts.clear();
        m_catalogLikeOverrides.clear();
        DiscoverChartGalleryPanel().Children().Clear();
        m_discoverChartItems.Clear();
        m_discoverDetailTracks.Clear();
        HomeCatalogPrimaryPanel().Children().Clear();
        HomeCatalogMoodPanel().Children().Clear();
        HomeCatalogMoodPanel().Visibility(Visibility::Collapsed);
        ShowHomeCatalogStatus(L"", false, false);
        ClearAccountArtworkCache();
        if (HomeCatalogViewContainer().Visibility() == Visibility::Visible)
        {
            HomeButton_Click(nullptr, nullptr);
        }
    }

    winrt::hstring MainWindow::CurrentDiscoverStorefront()
    {
        if (m_discoverStorefront.empty())
        {
            m_discoverStorefront = SettingsManagerService().GetString(
                L"CatalogStorefront",
                LastMusicPlayer::Backend::DefaultCatalogStorefront);
        }
        if (m_discoverStorefront.empty())
        {
            m_discoverStorefront = LastMusicPlayer::Backend::DefaultCatalogStorefront;
        }
        return m_discoverStorefront;
    }

    void MainWindow::PopulateDiscoverStorefronts(
        std::vector<LastMusicPlayer::Backend::CatalogStorefront> const& storefronts)
    {
        auto selector = DiscoverStorefrontSelector();
        if (!selector || storefronts.empty())
        {
            return;
        }

        auto current = CurrentDiscoverStorefront();
        m_suppressDiscoverStorefrontChange = true;
        selector.Items().Clear();
        int selectedIndex = -1;
        for (uint32_t index = 0; index < storefronts.size(); ++index)
        {
            auto const& storefront = storefronts[index];
            winrt::Microsoft::UI::Xaml::Controls::ComboBoxItem item;
            item.Content(winrt::box_value(storefront.Name));
            item.Tag(winrt::box_value(storefront.Code));
            selector.Items().Append(item);
            if (storefront.Code == current)
            {
                selectedIndex = static_cast<int>(index);
            }
        }
        selector.SelectedIndex(selectedIndex);
        m_suppressDiscoverStorefrontChange = false;
    }

    winrt::Microsoft::UI::Xaml::Controls::StackPanel MainWindow::BuildCatalogShelfSection(
        LastMusicPlayer::Backend::CatalogShelf const& shelf,
        winrt::hstring const& sectionTitle,
        winrt::hstring const& pillText,
        std::size_t previewLimit,
        bool forceSeeAll)
    {
        using namespace winrt::Microsoft::UI::Xaml;
        using namespace winrt::Microsoft::UI::Xaml::Controls;

        StackPanel section;
        section.Spacing(0);

        Grid header;
        header.Margin(ThicknessHelper::FromLengths(0, 0, 0, 16));
        ColumnDefinition titleColumn;
        titleColumn.Width(GridLengthHelper::FromValueAndType(1, GridUnitType::Star));
        ColumnDefinition actionColumn;
        actionColumn.Width(GridLengthHelper::Auto());
        header.ColumnDefinitions().Append(titleColumn);
        header.ColumnDefinitions().Append(actionColumn);

        StackPanel heading;
        heading.Orientation(Orientation::Horizontal);
        heading.Spacing(12);
        TextBlock title;
        title.Text(sectionTitle);
        title.Style(Application::Current().Resources()
            .Lookup(winrt::box_value(winrt::hstring{ L"TitleH2" }))
            .try_as<Style>());
        title.VerticalAlignment(VerticalAlignment::Center);
        heading.Children().Append(title);
        if (!pillText.empty())
        {
            Border pill;
            pill.Background(Application::Current().Resources()
                .Lookup(winrt::box_value(winrt::hstring{ L"NeutralFillBrush" }))
                .try_as<winrt::Microsoft::UI::Xaml::Media::Brush>());
            pill.BorderBrush(Application::Current().Resources()
                .Lookup(winrt::box_value(winrt::hstring{ L"StrokeBrush" }))
                .try_as<winrt::Microsoft::UI::Xaml::Media::Brush>());
            pill.BorderThickness(ThicknessHelper::FromUniformLength(1));
            pill.CornerRadius(CornerRadiusHelper::FromUniformRadius(999));
            pill.Padding(ThicknessHelper::FromLengths(9, 3, 9, 3));
            pill.VerticalAlignment(VerticalAlignment::Center);
            TextBlock pillLabel;
            pillLabel.Text(pillText);
            pillLabel.FontSize(10);
            pillLabel.FontWeight(winrt::Microsoft::UI::Text::FontWeights::SemiBold());
            pillLabel.CharacterSpacing(40);
            pillLabel.Foreground(Application::Current().Resources()
                .Lookup(winrt::box_value(winrt::hstring{ L"TextSecondaryBrush" }))
                .try_as<winrt::Microsoft::UI::Xaml::Media::Brush>());
            pill.Child(pillLabel);
            heading.Children().Append(pill);
        }
        header.Children().Append(heading);

        if (shelf.Chart || forceSeeAll)
        {
            Button seeAll;
            seeAll.Content(winrt::box_value(winrt::hstring{ L"See all \x203A" }));
            seeAll.Background(m_brushTransparent);
            seeAll.BorderThickness(ThicknessHelper::FromUniformLength(0));
            seeAll.Padding(ThicknessHelper::FromLengths(6, 2, 6, 2));
            seeAll.FontSize(12);
            seeAll.FontWeight(winrt::Microsoft::UI::Text::FontWeights::SemiBold());
            seeAll.Foreground(m_brushAccent);
            seeAll.HorizontalAlignment(HorizontalAlignment::Right);
            seeAll.VerticalAlignment(VerticalAlignment::Center);
            Grid::SetColumn(seeAll, 1);
            auto shelfCopy = shelf;
            seeAll.Click([this, shelfCopy](auto&&, auto&&)
            {
                if (shelfCopy.Chart)
                {
                    RunDetached(OpenDiscoverChartAsync(*shelfCopy.Chart, shelfCopy.Title));
                    return;
                }
                OpenDiscoverShelf(shelfCopy, L"For the moment");
            });
            header.Children().Append(seeAll);
        }
        section.Children().Append(header);

        auto items = winrt::single_threaded_observable_vector<winrt::Last_Music_Player::TrackInfo>();
        int32_t index = 1;
        for (auto const& item : shelf.Items)
        {
            if (previewLimit > 0 && items.Size() >= previewLimit)
            {
                break;
            }
            items.Append(CatalogItemToTrack(item, index++));
        }

        auto tileTemplate = HomeCatalogViewContainer().Resources()
            .Lookup(winrt::box_value(winrt::hstring{ L"CatalogTileTemplate" }))
            .try_as<DataTemplate>();
        auto containerStyle = Application::Current().Resources()
            .Lookup(winrt::box_value(winrt::hstring{ L"CardTileContainerStyle" }))
            .try_as<Style>();

        GridView grid;
        // Exactly one CatalogTileTemplate tall (156 art + 12 gap + 22 title +
        // 18 artist). Any slack here reads as dead space between shelves.
        grid.Height(208);
        grid.IsItemClickEnabled(true);
        grid.SelectionMode(ListViewSelectionMode::None);
        if (containerStyle) grid.ItemContainerStyle(containerStyle);
        if (tileTemplate) grid.ItemTemplate(tileTemplate);
        ObserveCatalogGridContainers(grid);
        ScrollViewer::SetHorizontalScrollBarVisibility(grid, ScrollBarVisibility::Hidden);
        // Disabled, not Enabled: a shelf with no vertical axis of its own has a
        // vertical wheel steered onto its horizontal one by the framework, and
        // the row runs off sideways while the user is moving down the page.
        // Taking the axis away from user input is the only reliable way to stop
        // that; the arrows drive it instead, and ChangeView still works.
        ScrollViewer::SetHorizontalScrollMode(grid, ScrollMode::Disabled);
        ScrollViewer::SetVerticalScrollMode(grid, ScrollMode::Disabled);
        ScrollViewer::SetVerticalScrollBarVisibility(grid, ScrollBarVisibility::Hidden);
        auto panelTemplate = winrt::Microsoft::UI::Xaml::Markup::XamlReader::Load(
            LR"(<ItemsPanelTemplate xmlns="http://schemas.microsoft.com/winfx/2006/xaml/presentation"><ItemsStackPanel Orientation="Horizontal" /></ItemsPanelTemplate>)")
            .try_as<ItemsPanelTemplate>();
        if (panelTemplate) grid.ItemsPanel(panelTemplate);

        grid.ItemsSource(items);
        grid.ItemClick([this, items](auto&&, ItemClickEventArgs const& args)
        {
            auto track = args.ClickedItem().try_as<winrt::Last_Music_Player::TrackInfo>();
            if (!track)
            {
                return;
            }
            auto type = track.Genre();
            if (type == L"album" || type == L"playlist")
            {
                RunDetached(OpenDiscoverResourceAsync(
                    type == L"album" ? winrt::hstring{ L"albums" } : winrt::hstring{ L"playlists" },
                    track.RemoteCatalogId(),
                    track.Title()));
                return;
            }
            QueueAndPlayObservable(items, track);
        });
        // The shelf goes into a Grid of its own so the arrows have something to
        // overlay; a StackPanel would stack them underneath instead.
        Grid arrowHost;
        arrowHost.Children().Append(grid);
        LastMusicPlayer::Frontend::AttachCarouselArrows(arrowHost, grid, 78.0);
        section.Children().Append(arrowHost);
        return section;
    }

    winrt::Microsoft::UI::Xaml::Controls::Button MainWindow::BuildCatalogChartCard(
        LastMusicPlayer::Backend::CatalogChartDescriptor const& chart)
    {
        using namespace winrt::Microsoft::UI::Xaml;
        using namespace winrt::Microsoft::UI::Xaml::Controls;
        using namespace winrt::Microsoft::UI::Xaml::Media;

        Button button;
        button.Width(184);
        button.Height(232);
        button.Padding(ThicknessHelper::FromUniformLength(0));
        button.Background(m_brushTransparent);
        button.BorderThickness(ThicknessHelper::FromUniformLength(0));
        button.HorizontalContentAlignment(HorizontalAlignment::Stretch);
        button.VerticalContentAlignment(VerticalAlignment::Top);

        StackPanel content;
        content.Spacing(7);
        Border artwork;
        artwork.Width(184);
        artwork.Height(184);
        artwork.CornerRadius(CornerRadiusHelper::FromUniformRadius(18));
        artwork.Translation({ 0, 0, 16 });
        LinearGradientBrush gradient;
        gradient.StartPoint({ 0, 0 });
        gradient.EndPoint({ 1, 1 });
        auto identity = std::wstring{ chart.Ref.Storefront.c_str() }
            + L":" + std::wstring{ chart.Ref.Id.c_str() };
        uint32_t hash = 2166136261u;
        for (auto character : identity)
        {
            hash ^= static_cast<uint32_t>(character);
            hash *= 16777619u;
        }
        constexpr std::array<std::pair<winrt::Windows::UI::Color, winrt::Windows::UI::Color>, 6> palettes{
            std::pair{ winrt::Windows::UI::Color{ 255, 20, 168, 188 }, winrt::Windows::UI::Color{ 255, 127, 108, 255 } },
            std::pair{ winrt::Windows::UI::Color{ 255, 46, 107, 230 }, winrt::Windows::UI::Color{ 255, 79, 176, 255 } },
            std::pair{ winrt::Windows::UI::Color{ 255, 165, 66, 219 }, winrt::Windows::UI::Color{ 255, 228, 92, 183 } },
            std::pair{ winrt::Windows::UI::Color{ 255, 27, 154, 105 }, winrt::Windows::UI::Color{ 255, 53, 200, 149 } },
            std::pair{ winrt::Windows::UI::Color{ 255, 228, 87, 87 }, winrt::Windows::UI::Color{ 255, 242, 154, 103 } },
            std::pair{ winrt::Windows::UI::Color{ 255, 106, 61, 224 }, winrt::Windows::UI::Color{ 255, 155, 106, 240 } },
        };
        auto const& palette = palettes[hash % palettes.size()];
        GradientStop first;
        first.Color(palette.first);
        first.Offset(0);
        GradientStop second;
        second.Color(palette.second);
        second.Offset(1);
        gradient.GradientStops().Append(first);
        gradient.GradientStops().Append(second);
        artwork.Background(gradient);

        Grid artContent;
        FontIcon glyph;
        glyph.Glyph(chart.Ref.Kind == LastMusicPlayer::Backend::CatalogChartKind::City
            ? L"\xE909"
            : chart.Ref.Kind == LastMusicPlayer::Backend::CatalogChartKind::Genre
                ? L"\xE8D6"
                : L"\xE774");
        glyph.FontSize(58);
        glyph.Foreground(winrt::Microsoft::UI::Xaml::Media::SolidColorBrush(
            winrt::Windows::UI::Color{ 220, 255, 255, 255 }));
        glyph.HorizontalAlignment(HorizontalAlignment::Center);
        glyph.VerticalAlignment(VerticalAlignment::Center);
        artContent.Children().Append(glyph);

        TextBlock kind;
        kind.Text(chart.Ref.Kind == LastMusicPlayer::Backend::CatalogChartKind::City
            ? L"CITY CHART"
            : chart.Ref.Kind == LastMusicPlayer::Backend::CatalogChartKind::Genre
                ? L"GENRE CHART"
                : L"CURRENT REGION");
        kind.FontSize(10);
        kind.FontWeight(winrt::Microsoft::UI::Text::FontWeights::Bold());
        kind.CharacterSpacing(120);
        kind.Foreground(winrt::Microsoft::UI::Xaml::Media::SolidColorBrush(
            winrt::Windows::UI::Color{ 255, 255, 255, 255 }));
        kind.Margin(ThicknessHelper::FromUniformLength(13));
        kind.VerticalAlignment(VerticalAlignment::Bottom);
        artContent.Children().Append(kind);

        if (!chart.ArtworkUrl.empty())
        {
            Image image;
            image.Stretch(Stretch::UniformToFill);
            artContent.Children().Append(image);
            QueueAccountArtworkImage(image, chart.ArtworkUrl, ArtworkDetail::Tile);
        }
        artwork.Child(artContent);
        content.Children().Append(artwork);

        TextBlock title;
        title.Text(chart.Name);
        title.Style(Application::Current().Resources()
            .Lookup(winrt::box_value(winrt::hstring{ L"BodyStrong" }))
            .try_as<Style>());
        title.TextTrimming(TextTrimming::CharacterEllipsis);
        content.Children().Append(title);
        TextBlock subtitle;
        subtitle.Text(chart.Subtitle.empty() ? chart.Title : chart.Subtitle);
        subtitle.Style(Application::Current().Resources()
            .Lookup(winrt::box_value(winrt::hstring{ L"CaptionText" }))
            .try_as<Style>());
        subtitle.TextTrimming(TextTrimming::CharacterEllipsis);
        content.Children().Append(subtitle);
        button.Content(content);
        button.Click([this, chart](auto&&, auto&&)
        {
            RunDetached(OpenDiscoverChartAsync(chart.Ref, chart.Title));
        });
        return button;
    }

    winrt::Microsoft::UI::Xaml::Controls::StackPanel MainWindow::BuildCatalogChartSection(
        winrt::hstring const& sectionTitle,
        std::vector<LastMusicPlayer::Backend::CatalogChartDescriptor> const& charts,
        bool partial)
    {
        using namespace winrt::Microsoft::UI::Xaml;
        using namespace winrt::Microsoft::UI::Xaml::Controls;

        StackPanel section;
        section.Spacing(0);
        Grid header;
        header.Margin(ThicknessHelper::FromLengths(0, 0, 0, 16));
        ColumnDefinition titleColumn;
        titleColumn.Width(GridLengthHelper::FromValueAndType(1, GridUnitType::Star));
        ColumnDefinition actionColumn;
        actionColumn.Width(GridLengthHelper::Auto());
        header.ColumnDefinitions().Append(titleColumn);
        header.ColumnDefinitions().Append(actionColumn);

        StackPanel heading;
        heading.Orientation(Orientation::Horizontal);
        heading.Spacing(12);
        TextBlock title;
        title.Text(sectionTitle);
        title.Style(Application::Current().Resources()
            .Lookup(winrt::box_value(winrt::hstring{ L"TitleH2" }))
            .try_as<Style>());
        heading.Children().Append(title);
        TextBlock source;
        source.Text(L"APPLE MUSIC");
        source.Style(Application::Current().Resources()
            .Lookup(winrt::box_value(winrt::hstring{ L"EyebrowText" }))
            .try_as<Style>());
        source.VerticalAlignment(VerticalAlignment::Center);
        heading.Children().Append(source);
        if (partial)
        {
            TextBlock partialText;
            partialText.Text(L"Some regions unavailable");
            partialText.Style(Application::Current().Resources()
                .Lookup(winrt::box_value(winrt::hstring{ L"CaptionText" }))
                .try_as<Style>());
            partialText.VerticalAlignment(VerticalAlignment::Center);
            heading.Children().Append(partialText);
        }
        header.Children().Append(heading);

        if (LastMusicPlayer::Backend::CatalogChartSectionShowsSeeAll(charts.size()))
        {
            Button seeAll;
            seeAll.Content(winrt::box_value(winrt::hstring{ L"See all \x203A" }));
            seeAll.Background(m_brushTransparent);
            seeAll.BorderThickness(ThicknessHelper::FromUniformLength(0));
            seeAll.Padding(ThicknessHelper::FromLengths(6, 2, 6, 2));
            seeAll.FontSize(12);
            seeAll.FontWeight(winrt::Microsoft::UI::Text::FontWeights::SemiBold());
            seeAll.Foreground(m_brushAccent);
            Grid::SetColumn(seeAll, 1);
            auto chartsCopy = charts;
            seeAll.Click([this, sectionTitle, chartsCopy, partial](auto&&, auto&&)
            {
                OpenDiscoverChartGallery(sectionTitle, chartsCopy, partial);
            });
            header.Children().Append(seeAll);
        }
        section.Children().Append(header);

        ScrollViewer scroll;
        scroll.HorizontalScrollBarVisibility(ScrollBarVisibility::Hidden);
        // Disabled for the same reason as the shelves above: the wheel would
        // otherwise be steered sideways. The arrows attached below are what
        // moves this row.
        scroll.HorizontalScrollMode(ScrollMode::Disabled);
        scroll.VerticalScrollMode(ScrollMode::Disabled);
        StackPanel row;
        row.Orientation(Orientation::Horizontal);
        row.Spacing(18);
        auto count = LastMusicPlayer::Backend::CatalogChartPreviewCount(charts.size());
        for (std::size_t index = 0; index < count; ++index)
        {
            row.Children().Append(BuildCatalogChartCard(charts[index]));
        }
        scroll.Content(row);
        // A chart card's artwork is the top 184 of the tile, so its middle is 92
        // down; the arrows line up with the picture rather than the caption.
        Grid arrowHost;
        arrowHost.Children().Append(scroll);
        LastMusicPlayer::Frontend::AttachCarouselArrows(arrowHost, scroll, 92.0);
        section.Children().Append(arrowHost);
        return section;
    }

    void MainWindow::BuildHomeCatalog(LastMusicPlayer::Backend::CatalogDiscovery const& discovery)
    {
        using winrt::Microsoft::UI::Xaml::Visibility;
        auto primary = HomeCatalogPrimaryPanel();
        primary.Children().Clear();
        auto regionName = discovery.StorefrontName.empty() ? discovery.Storefront : discovery.StorefrontName;
        for (auto const& shelf : discovery.Shelves)
        {
            auto title = shelf.Id == L"top-songs"
                ? winrt::hstring{ std::wstring{ shelf.Title.c_str() } + L" in " + std::wstring{ regionName.c_str() } }
                : shelf.Title;
            auto pill = shelf.ItemType == LastMusicPlayer::Backend::CatalogResourceType::Song ? winrt::hstring{ L"SONGS" }
                : shelf.ItemType == LastMusicPlayer::Backend::CatalogResourceType::Album ? winrt::hstring{ L"ALBUMS" }
                : winrt::hstring{ L"PLAYLISTS" };
            primary.Children().Append(BuildCatalogShelfSection(shelf, title, pill, 0, false));
        }

        for (auto const& section : LastMusicPlayer::Backend::BuildCatalogChartSections(discovery))
        {
            primary.Children().Append(BuildCatalogChartSection(
                section.Title,
                section.Charts,
                section.Partial));
        }

        HomeCatalogPrimaryContainer().Visibility(Visibility::Visible);
        auto mood = HomeCatalogMoodPanel();
        mood.Children().Clear();
        if (discovery.MoodActivityShelf)
        {
            mood.Children().Append(BuildCatalogShelfSection(
                *discovery.MoodActivityShelf,
                discovery.MoodActivityShelf->Title,
                L"FOR THE MOMENT",
                LastMusicPlayer::Backend::CatalogMoodPreviewCount(discovery.MoodActivityShelf->Items.size()),
                true));
            mood.Visibility(Visibility::Visible);
        }
        else
        {
            mood.Visibility(Visibility::Collapsed);
        }
    }

    void MainWindow::RebuildCatalogSurfaces()
    {
        BuildHomeCatalog(m_catalogDiscovery);
    }

    winrt::Windows::Foundation::IAsyncOperation<bool> MainWindow::RestoreCachedDiscoveryAsync(
        winrt::hstring storefront,
        std::uint64_t epoch,
        LastMusicPlayer::Backend::RemoteScopeSnapshot scope)
    {
        auto lifetime = get_strong();
        auto dispatcher = DispatcherQueue();
        if (!dispatcher || storefront.empty())
        {
            co_return false;
        }

        co_await winrt::resume_background();

        // Reading the row and parsing it are both off the UI thread: the payload
        // runs to hundreds of kilobytes and the parse walks every shelf in it.
        auto accountId = DatabaseService().ActiveAccountId();
        LastMusicPlayer::Backend::CatalogDiscovery restored;
        if (!accountId.empty())
        {
            auto payload = DatabaseService().LoadCatalogDiscoveryPayload(
                accountId,
                std::wstring(storefront.c_str()));
            if (!payload.empty())
            {
                restored = LastMusicPlayer::Backend::ParseCatalogDiscovery(
                    winrt::hstring(payload),
                    storefront);
            }
        }

        co_await wil::resume_foreground(dispatcher);

        // A stored payload that no longer parses into shelves is treated as a
        // cold start rather than an error: the live request is already on its
        // way, and it is the one that decides what the user is told.
        if (!HasCatalogContent(restored))
        {
            co_return false;
        }
        // The read took long enough to suspend twice, so the account or the load
        // that asked for this may both have moved on. Applying either way would
        // put one account's catalog under another's session, or resurrect a
        // region the user has already navigated away from.
        if (epoch != m_discoverEpoch
            || storefront != CurrentDiscoverStorefront()
            || !RemoteMusicServiceService().IsCurrent(scope))
        {
            co_return false;
        }

        m_catalogDiscovery = std::move(restored);
        m_discoverLoaded = true;
        RebuildCatalogSurfaces();
        co_return true;
    }

    void MainWindow::CacheDiscoveryPayload(
        winrt::hstring const& storefront,
        winrt::hstring const& payload)
    {
        if (storefront.empty() || payload.empty())
        {
            return;
        }

        // Detached and free-standing: the write touches no window state, only
        // the database singleton, so it neither holds the window alive nor makes
        // the surface that queued it wait on a disk round trip.
        RunDetached(WriteCatalogDiscoveryCacheAsync(
            std::wstring(storefront.c_str()),
            std::wstring(payload.c_str())));
    }

    winrt::Windows::Foundation::IAsyncAction MainWindow::HydrateDiscoverAsync(bool force)
    {
        auto lifetime = get_strong();
        if (m_discoverInFlight)
        {
            m_discoverPendingRefresh = m_discoverPendingRefresh || force;
            co_return;
        }
        if (m_discoverLoaded && !force)
        {
            RebuildCatalogSurfaces();
            co_return;
        }

        using LastMusicPlayer::Backend::RemoteAccessMode;
        if (RemoteMusicServiceService().Mode() != RemoteAccessMode::Account)
        {
            ShowHomeCatalogStatus(L"", false, false);
            co_return;
        }

        m_discoverInFlight = true;
        m_discoverPendingRefresh = false;
        auto epoch = ++m_discoverEpoch;
        auto storefront = CurrentDiscoverStorefront();
        auto scope = RemoteMusicServiceService().CaptureScope();
        auto hasCachedContent = m_discoverLoaded
            && m_catalogDiscovery.Storefront == storefront;

        auto finish = [this]()
        {
            m_discoverInFlight = false;
            if (m_discoverPendingRefresh)
            {
                m_discoverPendingRefresh = false;
                RunDetached(HydrateDiscoverAsync(true));
            }
        };

        if (!hasCachedContent)
        {
            // Nothing in memory for this region, which on a relaunch means the
            // shelves would otherwise sit blank for a whole round trip. Draw the
            // last payload that worked instead, then let the request below
            // replace it. The refresh is unconditional: this only decides what
            // is on screen while it runs, never whether it runs.
            hasCachedContent = co_await RestoreCachedDiscoveryAsync(storefront, epoch, scope);
            if (epoch != m_discoverEpoch || !RemoteMusicServiceService().IsCurrent(scope))
            {
                finish();
                co_return;
            }
        }

        if (hasCachedContent)
        {
            ShowHomeCatalogStatus(L"Refreshing catalog...", true, false);
        }
        else
        {
            HomeCatalogPrimaryPanel().Children().Clear();
            ShowHomeCatalogStatus(L"Loading catalog...", true, false);
        }

        winrt::hstring discoveryPayload;
        winrt::hstring storefrontsPayload;
        try
        {
            discoveryPayload = co_await RemoteMusicServiceService().GetCatalogDiscoveryAsync(storefront);
        }
        catch (winrt::hresult_error const& error)
        {
            if (epoch != m_discoverEpoch || !RemoteMusicServiceService().IsCurrent(scope))
            {
                finish();
                co_return;
            }
            // Distinct from "no data": the request itself did not complete, so
            // offer a retry rather than implying the region is empty.
            auto unauthorized = LastMusicPlayer::Backend::IsAccountUnauthorized(error);
            auto message = unauthorized
                ? winrt::hstring{ L"Your account session has expired. Sign in again to load the catalog." }
                : hasCachedContent
                    ? winrt::hstring{ L"Showing saved catalog content. Refresh failed." }
                    : winrt::hstring{ L"Could not reach the catalog service." };
            ShowHomeCatalogStatus(message, false, !unauthorized);
            finish();
            co_return;
        }
        catch (...)
        {
            if (epoch != m_discoverEpoch || !RemoteMusicServiceService().IsCurrent(scope))
            {
                finish();
                co_return;
            }
            auto message = hasCachedContent
                ? winrt::hstring{ L"Showing saved catalog content. Refresh failed." }
                : winrt::hstring{ L"Could not reach the catalog service." };
            ShowHomeCatalogStatus(message, false, true);
            finish();
            co_return;
        }
        if (epoch != m_discoverEpoch
            || storefront != CurrentDiscoverStorefront()
            || !RemoteMusicServiceService().IsCurrent(scope))
        {
            finish();
            co_return;
        }

        auto discovery = LastMusicPlayer::Backend::ParseCatalogDiscovery(discoveryPayload, storefront);
        if (!HasCatalogContent(discovery))
        {
            // The request succeeded and produced nothing usable. Either this
            // storefront has no charts, or the payload shape moved; both look the
            // same from here, so the message says what is observable.
            ShowHomeCatalogStatus(L"Nothing to show for this region right now.", false, true);
        }
        else
        {
            m_catalogDiscovery = std::move(discovery);
            m_discoverLoaded = true;
            RebuildCatalogSurfaces();
            auto message = m_catalogDiscovery.Stale
                ? winrt::hstring{ L"Showing saved catalog content while the service refreshes." }
                : winrt::hstring{};
            ShowHomeCatalogStatus(message, false, m_catalogDiscovery.Stale);
            // Only payloads that actually produced shelves are worth keeping, and
            // only the live one: a payload the service already flagged stale would
            // be served back on the next launch as though it were current.
            if (!m_catalogDiscovery.Stale)
            {
                CacheDiscoveryPayload(storefront, discoveryPayload);
            }
        }

        // The storefront list is cosmetic, so a failure here leaves the existing
        // picker contents alone rather than disturbing a page that already loaded.
        try
        {
            storefrontsPayload = co_await RemoteMusicServiceService().GetCatalogStorefrontsAsync();
        }
        catch (...)
        {
            finish();
            co_return;
        }
        if (epoch != m_discoverEpoch
            || storefront != CurrentDiscoverStorefront()
            || !RemoteMusicServiceService().IsCurrent(scope))
        {
            finish();
            co_return;
        }
        auto storefronts = LastMusicPlayer::Backend::ParseCatalogStorefronts(storefrontsPayload);
        PopulateDiscoverStorefronts(storefronts);
        for (auto const& candidate : storefronts)
        {
            if (candidate.Code == storefront && m_catalogDiscovery.StorefrontName.empty())
            {
                m_catalogDiscovery.StorefrontName = candidate.Name;
                RebuildCatalogSurfaces();
                break;
            }
        }
        finish();
    }

    winrt::Windows::Foundation::IAsyncAction MainWindow::OpenDiscoverChartAsync(
        LastMusicPlayer::Backend::CatalogChartRef chart,
        winrt::hstring title)
    {
        auto lifetime = get_strong();
        using winrt::Microsoft::UI::Xaml::Visibility;
        if (!LastMusicPlayer::Backend::IsValidCatalogChartRef(chart))
        {
            ShowPlaybackNotice(L"That chart is not available.");
            co_return;
        }

        m_discoverChartRequest = { chart, 50, 0 };
        m_catalogContentStorefront = chart.Storefront;
        m_discoverChartNextOffset = 0;
        m_discoverChartHasMore = false;
        m_discoverChartItems.Clear();
        DiscoverChartGridView().ItemsSource(m_discoverChartItems);
        DiscoverChartTitleText().Text(title);
        auto kind = LastMusicPlayer::Backend::CatalogChartKindName(chart.Kind);
        DiscoverChartKindText().Text(kind.empty() ? winrt::hstring{ L"Chart" } : kind);
        DiscoverChartMoreButton().Visibility(Visibility::Collapsed);

        ShowCatalogSurface(CatalogSurface::Chart, true);

        m_discoverChartSkeleton.BeginLoading(m_discoverChartItems.Size() == 0);
        co_await LoadDiscoverChartPageAsync(0);
        m_discoverChartSkeleton.EndLoading();
    }

    winrt::Windows::Foundation::IAsyncAction MainWindow::LoadDiscoverChartPageAsync(int32_t offset)
    {
        auto lifetime = get_strong();
        using winrt::Microsoft::UI::Xaml::Visibility;

        auto epoch = m_discoverEpoch;
        auto navigationEpoch = m_discoverNavigationEpoch;
        auto request = m_discoverChartRequest;
        request.Offset = offset;
        DiscoverChartMoreButton().IsEnabled(false);
        winrt::hstring payload;
        try
        {
            payload = co_await RemoteMusicServiceService().GetCatalogChartAsync(request);
        }
        catch (...)
        {
            if (epoch != m_discoverEpoch
                || navigationEpoch != m_discoverNavigationEpoch)
            {
                co_return;
            }
            DiscoverChartMoreButton().IsEnabled(true);
            ShowPlaybackNotice(L"Could not load more of this chart.");
            co_return;
        }
        if (epoch != m_discoverEpoch
            || navigationEpoch != m_discoverNavigationEpoch
            || request.Ref.Storefront != m_discoverChartRequest.Ref.Storefront
            || request.Ref.Kind != m_discoverChartRequest.Ref.Kind
            || request.Ref.Id != m_discoverChartRequest.Ref.Id
            || request.Ref.ResourceType != m_discoverChartRequest.Ref.ResourceType)
        {
            co_return;
        }

        auto page = LastMusicPlayer::Backend::ParseCatalogChartPage(payload, request.Ref);
        if (!page.Descriptor)
        {
            DiscoverChartMoreButton().IsEnabled(true);
            ShowPlaybackNotice(L"The chart response did not match the requested chart.");
            co_return;
        }
        auto index = static_cast<int32_t>(m_discoverChartItems.Size()) + 1;
        for (auto const& item : page.Items)
        {
            m_discoverChartItems.Append(CatalogItemToTrack(item, index++));
        }
        if (!page.Title.empty())
        {
            DiscoverChartTitleText().Text(page.Title);
        }

        m_discoverChartHasMore = page.HasNextOffset && !page.Items.empty();
        m_discoverChartNextOffset = page.NextOffset;
        DiscoverChartMoreButton().IsEnabled(true);
        DiscoverChartMoreButton().Visibility(m_discoverChartHasMore ? Visibility::Visible : Visibility::Collapsed);
        if (page.Stale)
        {
            ShowPlaybackNotice(L"Showing saved chart content while the service refreshes.");
        }
    }

    void MainWindow::OpenDiscoverChartGallery(
        winrt::hstring const& title,
        std::vector<LastMusicPlayer::Backend::CatalogChartDescriptor> const& charts,
        bool partial)
    {
        using winrt::Microsoft::UI::Xaml::Visibility;
        m_catalogGalleryCharts = charts;
        DiscoverChartGalleryTitle().Text(title);
        DiscoverChartGalleryStatus().Text(partial ? L"Some regions are temporarily unavailable." : L"");
        DiscoverChartGalleryStatus().Visibility(partial ? Visibility::Visible : Visibility::Collapsed);
        auto panel = DiscoverChartGalleryPanel();
        panel.Children().Clear();
        for (auto const& chart : charts)
        {
            panel.Children().Append(BuildCatalogChartCard(chart));
        }
        ShowCatalogSurface(CatalogSurface::ChartGallery, true);
    }

    void MainWindow::OpenDiscoverShelf(
        LastMusicPlayer::Backend::CatalogShelf const& shelf,
        winrt::hstring const& eyebrow)
    {
        using winrt::Microsoft::UI::Xaml::Visibility;
        m_discoverChartRequest = {};
        m_catalogContentStorefront = m_catalogDiscovery.Storefront;
        m_discoverChartItems.Clear();
        int32_t index = 1;
        for (auto const& item : shelf.Items)
        {
            m_discoverChartItems.Append(CatalogItemToTrack(item, index++));
        }
        DiscoverChartGridView().ItemsSource(m_discoverChartItems);
        DiscoverChartTitleText().Text(shelf.Title);
        DiscoverChartKindText().Text(eyebrow);
        DiscoverChartMoreButton().Visibility(Visibility::Collapsed);
        m_discoverChartHasMore = false;
        ShowCatalogSurface(CatalogSurface::Chart, true);
    }

    winrt::Windows::Foundation::IAsyncAction MainWindow::OpenDiscoverResourceAsync(
        winrt::hstring kind,
        winrt::hstring catalogId,
        winrt::hstring title)
    {
        auto lifetime = get_strong();
        using winrt::Microsoft::UI::Xaml::Visibility;

        if (catalogId.empty())
        {
            ShowPlaybackNotice(L"That item has no catalog page.");
            co_return;
        }

        auto epoch = m_discoverEpoch;
        auto storefront = LastMusicPlayer::Backend::IsValidCatalogStorefront(m_catalogContentStorefront)
            ? m_catalogContentStorefront
            : CurrentDiscoverStorefront();
        m_discoverDetailTracks.Clear();
        DiscoverDetailTracksListView().ItemsSource(m_discoverDetailTracks);
        DiscoverDetailTitleText().Text(title);
        DiscoverDetailKindText().Text(kind == L"playlists" ? L"Playlist" : L"Album");
        // Cleared, not relabelled: the track placeholder below is the loading
        // signal, and this line goes on to carry the real track count.
        DiscoverDetailSubtitleText().Text(L"");
        DiscoverDetailDescriptionText().Text(L"");
        DiscoverDetailArt().Tag(nullptr);
        DiscoverDetailArt().Source(nullptr);

        ShowCatalogSurface(CatalogSurface::Detail, true);
        // Scoped, because this coroutine leaves by several routes: a stale
        // epoch, a superseded navigation, or a caught transport failure.
        LastMusicPlayer::Frontend::SkeletonLoadScope detailSkeleton{ m_discoverDetailSkeleton, true };
        auto navigationEpoch = m_discoverNavigationEpoch;

        winrt::hstring payload;
        try
        {
            payload = co_await RemoteMusicServiceService().GetCatalogResourceAsync(
                kind,
                catalogId,
                storefront);
        }
        catch (...)
        {
            if (epoch != m_discoverEpoch
                || navigationEpoch != m_discoverNavigationEpoch)
            {
                co_return;
            }
            DiscoverDetailSubtitleText().Text(L"Could not load this item.");
            co_return;
        }
        if (epoch != m_discoverEpoch
            || navigationEpoch != m_discoverNavigationEpoch)
        {
            co_return;
        }

        auto detail = LastMusicPlayer::Backend::ParseCatalogResourceDetail(payload, kind);
        LastMusicPlayer::Backend::ApplyCatalogDetailTrackArtwork(detail);
        if (!detail.Resource.Title.empty())
        {
            DiscoverDetailTitleText().Text(detail.Resource.Title);
        }
        DiscoverDetailDescriptionText().Text(detail.Resource.Description);

        int32_t index = 1;
        for (auto const& item : detail.Tracks)
        {
            m_discoverDetailTracks.Append(CatalogItemToTrack(item, index++));
        }

        std::wstring subtitle;
        if (!detail.Resource.Subtitle.empty())
        {
            subtitle = std::wstring{ detail.Resource.Subtitle.c_str() } + L" \x00B7 ";
        }
        subtitle += std::to_wstring(m_discoverDetailTracks.Size());
        subtitle += m_discoverDetailTracks.Size() == 1 ? L" song" : L" songs";
        DiscoverDetailSubtitleText().Text(winrt::hstring{ subtitle });

        if (!detail.Resource.ArtworkUrl.empty())
        {
            QueueAccountArtworkImage(DiscoverDetailArt(), detail.Resource.ArtworkUrl, ArtworkDetail::Tile);
        }
    }

    void MainWindow::DiscoverBack_Click(
        winrt::Windows::Foundation::IInspectable const& sender,
        winrt::Microsoft::UI::Xaml::RoutedEventArgs const& args)
    {
        (void)sender;
        (void)args;
        if (m_catalogBackStack.empty())
        {
            ShowCatalogSurface(CatalogSurface::Home, false);
            return;
        }
        auto previous = m_catalogBackStack.back();
        m_catalogBackStack.pop_back();
        ShowCatalogSurface(previous, false);
    }

    winrt::Windows::Foundation::IAsyncAction MainWindow::DiscoverStorefront_SelectionChanged(
        winrt::Windows::Foundation::IInspectable const& sender,
        winrt::Microsoft::UI::Xaml::Controls::SelectionChangedEventArgs const& args)
    {
        (void)args;
        auto lifetime = get_strong();
        if (m_suppressDiscoverStorefrontChange)
        {
            co_return;
        }
        auto selector = sender.try_as<winrt::Microsoft::UI::Xaml::Controls::ComboBox>();
        if (!selector)
        {
            co_return;
        }
        auto item = selector.SelectedItem().try_as<winrt::Microsoft::UI::Xaml::Controls::ComboBoxItem>();
        if (!item)
        {
            co_return;
        }
        auto code = winrt::unbox_value_or<winrt::hstring>(item.Tag(), winrt::hstring{});
        if (code.empty() || code == m_discoverStorefront)
        {
            co_return;
        }

        m_discoverStorefront = code;
        SettingsManagerService().SetString(L"CatalogStorefront", code);
        // Artwork is per-storefront content; keeping the old entries would only
        // hold memory for images no longer on screen.
        ClearAccountArtworkCache();
        ++m_discoverEpoch;
        m_discoverLoaded = false;
        m_catalogDiscovery = {};
        m_catalogContentStorefront.clear();
        m_catalogBackStack.clear();
        m_catalogGalleryCharts.clear();
        DiscoverChartGalleryPanel().Children().Clear();
        HomeCatalogPrimaryPanel().Children().Clear();
        HomeCatalogMoodPanel().Children().Clear();
        HomeCatalogMoodPanel().Visibility(winrt::Microsoft::UI::Xaml::Visibility::Collapsed);

        // A chart or detail page open for the previous region no longer has
        // anything behind it, so fall back to the catalog home when one is
        // showing. Otherwise only the navigation epoch advances, so that
        // in-flight work for the old region cannot land while the user stays
        // where they are: this picker lives in Settings, and changing the region
        // there must not throw them back to Home.
        if (m_catalogSurface != CatalogSurface::Home)
        {
            ShowCatalogSurface(CatalogSurface::Home, false);
        }
        else
        {
            ++m_discoverNavigationEpoch;
        }
        co_await HydrateDiscoverAsync(false);
    }

    winrt::Windows::Foundation::IAsyncAction MainWindow::HomeCatalogRetry_Click(
        winrt::Windows::Foundation::IInspectable const& sender,
        winrt::Microsoft::UI::Xaml::RoutedEventArgs const& args)
    {
        (void)sender;
        (void)args;
        co_await HydrateDiscoverAsync(true);
    }

    winrt::Windows::Foundation::IAsyncAction MainWindow::DiscoverItem_ItemClick(
        winrt::Windows::Foundation::IInspectable const& sender,
        winrt::Microsoft::UI::Xaml::Controls::ItemClickEventArgs const& args)
    {
        (void)sender;
        auto lifetime = get_strong();
        auto track = args.ClickedItem().try_as<winrt::Last_Music_Player::TrackInfo>();
        if (!track)
        {
            co_return;
        }

        auto type = track.Genre();
        if (type == L"album")
        {
            co_await OpenDiscoverResourceAsync(L"albums", track.RemoteCatalogId(), track.Title());
            co_return;
        }
        if (type == L"playlist")
        {
            co_await OpenDiscoverResourceAsync(L"playlists", track.RemoteCatalogId(), track.Title());
            co_return;
        }
        // A song tile plays through the ordinary remote path: PlayTrack sees an
        // http source URL with no stream URL and routes it to the resolver.
        PlayTrack(track);
    }

    winrt::Windows::Foundation::IAsyncAction MainWindow::DiscoverChartMore_Click(
        winrt::Windows::Foundation::IInspectable const& sender,
        winrt::Microsoft::UI::Xaml::RoutedEventArgs const& args)
    {
        (void)sender;
        (void)args;
        auto lifetime = get_strong();
        if (!m_discoverChartHasMore)
        {
            co_return;
        }
        co_await LoadDiscoverChartPageAsync(m_discoverChartNextOffset);
    }

    void MainWindow::DiscoverDetailTrack_ItemClick(
        winrt::Windows::Foundation::IInspectable const& sender,
        winrt::Microsoft::UI::Xaml::Controls::ItemClickEventArgs const& args)
    {
        (void)sender;
        auto track = args.ClickedItem().try_as<winrt::Last_Music_Player::TrackInfo>();
        if (!track)
        {
            return;
        }
        // Queue the whole detail list so the rest of the album or playlist plays
        // on, rather than stopping after the one track that was clicked.
        QueueAndPlayObservable(m_discoverDetailTracks, track);
    }

    void MainWindow::DiscoverDetailPlay_Click(
        winrt::Windows::Foundation::IInspectable const& sender,
        winrt::Microsoft::UI::Xaml::RoutedEventArgs const& args)
    {
        (void)sender;
        (void)args;
        if (m_discoverDetailTracks.Size() == 0)
        {
            ShowPlaybackNotice(L"There is nothing to play here.");
            return;
        }
        QueueAndPlayObservable(m_discoverDetailTracks, m_discoverDetailTracks.GetAt(0));
    }

    void MainWindow::DiscoverDetailQueue_Click(
        winrt::Windows::Foundation::IInspectable const& sender,
        winrt::Microsoft::UI::Xaml::RoutedEventArgs const& args)
    {
        (void)sender;
        (void)args;
        if (m_discoverDetailTracks.Size() == 0)
        {
            ShowPlaybackNotice(L"There is nothing to queue here.");
            return;
        }
        for (auto const& track : m_discoverDetailTracks)
        {
            AddSongsTrackToQueue(track);
        }
        ShowPlaybackNotice(m_discoverDetailTracks.Size() == 1
            ? winrt::hstring{ L"Added 1 song to the queue." }
            : winrt::hstring{ L"Added " + std::to_wstring(m_discoverDetailTracks.Size()) + L" songs to the queue." });
    }
}
