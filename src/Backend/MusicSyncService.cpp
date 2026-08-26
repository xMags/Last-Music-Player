#include "pch.h"
#include "Backend/MusicSyncService.h"

#include "Backend/AccountClient.h"
#include "Backend/AutoSyncPolicy.h"
#include "Backend/AccountUrlPolicy.h"
#include "Backend/ProviderHelpers.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cwctype>
#include <optional>
#include <unordered_map>
#include <utility>

namespace LastMusicPlayer::Backend
{
    namespace
    {
        [[nodiscard]] std::uint32_t Bits(AccountSyncLane lanes) noexcept
        {
            return static_cast<std::uint32_t>(lanes);
        }

        [[nodiscard]] bool HasLane(std::uint32_t lanes, AccountSyncLane lane) noexcept
        {
            return (lanes & Bits(lane)) != 0;
        }

        [[nodiscard]] std::size_t LaneIndex(AccountSyncLane lane) noexcept
        {
            switch (lane)
            {
            case AccountSyncLane::History: return 0;
            case AccountSyncLane::Likes: return 1;
            case AccountSyncLane::PreferencePush: return 2;
            case AccountSyncLane::PreferencePull: return 3;
            case AccountSyncLane::Library: return 4;
            default: return 0;
            }
        }

        winrt::fire_and_forget StartQueuedSync(MusicSyncService* service, AccountSyncLane lanes)
        {
            try
            {
                co_await service->RequestSyncAsync(lanes);
            }
            catch (...)
            {
            }
        }

        std::wstring UtcNow()
        {
            SYSTEMTIME now{};
            ::GetSystemTime(&now);
            wchar_t value[40]{};
            swprintf_s(
                value,
                L"%04u-%02u-%02uT%02u:%02u:%02uZ",
                now.wYear,
                now.wMonth,
                now.wDay,
                now.wHour,
                now.wMinute,
                now.wSecond);
            return value;
        }

        winrt::hstring FormatDuration(double seconds)
        {
            if (seconds <= 0.0)
            {
                return {};
            }
            auto const whole = static_cast<long long>(seconds);
            wchar_t value[32]{};
            swprintf_s(value, L"%lld:%02lld", whole / 60, whole % 60);
            return value;
        }

        PortableAccountTrack PortableTrackFrom(TrackInfo const& track)
        {
            PortableAccountTrack result;
            if (!track)
            {
                return result;
            }
            result.Id = track.RemoteId().c_str();
            result.Title = track.Title().c_str();
            result.Artist = track.Artist().c_str();
            result.Album = track.Album().c_str();
            result.Genre = track.Genre().c_str();
            result.ArtworkUrl = DurableArtworkUrl(track.ArtworkUrl()).c_str();
            result.SourceKind = track.SourceKind().empty()
                ? std::wstring{ L"remote" }
                : std::wstring(track.SourceKind().c_str());
            result.Provider = track.RemoteProvider().empty()
                ? (track.Provider() == L"account" ? std::wstring{} : std::wstring(track.Provider().c_str()))
                : std::wstring(track.RemoteProvider().c_str());
            result.SourceUrl = IsSafeRemoteUrl(track.SourceUrl(), RemoteUrlUse::Durable)
                ? std::wstring(track.SourceUrl().c_str())
                : std::wstring{};
            result.CatalogId = track.RemoteCatalogId().c_str();
            result.AlbumCatalogId = track.AlbumCatalogId().c_str();
            result.ArtistCatalogId = track.ArtistCatalogId().c_str();
            result.DurationSec = (std::max)(0.0, track.DurationSeconds());
            return result;
        }

        std::optional<PortableAccountTrack> PortableTrackFrom(PlaybackEventRecord const& event)
        {
            auto parsed = ParsePortableTrack(winrt::hstring(event.TrackJson));
            if (parsed)
            {
                return parsed;
            }
            auto fallback = PortableTrackFrom(event.Track);
            return IsValidPortableTrack(fallback)
                ? std::optional<PortableAccountTrack>{ std::move(fallback) }
                : std::nullopt;
        }

        std::optional<PortableAccountTrack> PortableTrackFrom(PendingLikeRecord const& like)
        {
            auto parsed = ParsePortableTrack(winrt::hstring(like.TrackJson));
            if (parsed)
            {
                return parsed;
            }
            auto fallback = PortableTrackFrom(like.Track);
            return IsValidPortableTrack(fallback)
                ? std::optional<PortableAccountTrack>{ std::move(fallback) }
                : std::nullopt;
        }

        TrackInfo TrackFrom(PortableAccountTrack const& value)
        {
            TrackInfo track;
            track.RemoteId(winrt::hstring(value.Id));
            track.Title(winrt::hstring(value.Title));
            track.Artist(winrt::hstring(value.Artist));
            track.Album(winrt::hstring(value.Album));
            track.Genre(winrt::hstring(value.Genre));
            track.ArtworkUrl(winrt::hstring(value.ArtworkUrl));
            track.SourceKind(winrt::hstring(value.SourceKind));
            track.Provider(L"account");
            track.RemoteProvider(winrt::hstring(value.Provider));
            track.SourceUrl(winrt::hstring(value.SourceUrl));
            track.RemoteCatalogId(winrt::hstring(value.CatalogId));
            track.AlbumCatalogId(winrt::hstring(value.AlbumCatalogId));
            track.ArtistCatalogId(winrt::hstring(value.ArtistCatalogId));
            track.DurationSeconds(value.DurationSec);
            track.Duration(FormatDuration(value.DurationSec));
            track.SourceLabel(L"Account");
            track.FilePath(L"");
            return track;
        }

        [[nodiscard]] bool IsTransient(std::uint32_t status) noexcept
        {
            return status == 408 || status == 429 || status >= 500;
        }

        [[nodiscard]] std::chrono::seconds FailureDelay(
            int attemptCount,
            std::uint32_t retryAfterSeconds = 0) noexcept
        {
            return AccountRetryDelay(
                attemptCount + 1,
                retryAfterSeconds);
        }

        std::wstring LocalPreferenceValue(
            SettingsManager& settings,
            PortablePreferenceKey key)
        {
            switch (key)
            {
            case PortablePreferenceKey::Theme:
            {
                auto value = std::wstring(settings.GetString(L"Theme", L"System").c_str());
                std::transform(value.begin(), value.end(), value.begin(), ::towlower);
                return L"\"" + value + L"\"";
            }
            case PortablePreferenceKey::Accent:
            {
                auto value = std::wstring(settings.GetString(L"AccentColor", L"#FF0097B2").c_str());
                if (value.size() == 9)
                {
                    value = L"#" + value.substr(3);
                }
                return L"\"" + value + L"\"";
            }
            case PortablePreferenceKey::GaplessPlayback:
                return settings.GetBool(L"Gapless", true) ? L"true" : L"false";
            case PortablePreferenceKey::DataSaver:
                return settings.GetBool(L"DataSaver", false) ? L"true" : L"false";
            case PortablePreferenceKey::Storefront:
                return L"\"" + std::wstring(settings.GetString(
                    L"CatalogStorefront",
                    DefaultCatalogStorefront).c_str()) + L"\"";
            }
            return {};
        }

        void ApplyPreference(
            SettingsManager& settings,
            PortablePreferenceKey key,
            std::wstring const& valueJson)
        {
            auto const validated = ValidatePortablePreferenceValue(key, valueJson);
            if (!validated)
            {
                return;
            }
            auto values = PortablePreferencesFromValues({ { key, *validated } });
            switch (key)
            {
            case PortablePreferenceKey::Theme:
                if (values.Theme)
                {
                    auto value = *values.Theme;
                    value.front() = static_cast<wchar_t>(std::towupper(value.front()));
                    settings.SetString(L"Theme", winrt::hstring(value));
                }
                break;
            case PortablePreferenceKey::Accent:
                if (values.Accent)
                {
                    settings.SetString(L"AccentColor", winrt::hstring(L"#FF" + values.Accent->substr(1)));
                }
                break;
            case PortablePreferenceKey::GaplessPlayback:
                if (values.GaplessPlayback) settings.SetBool(L"Gapless", *values.GaplessPlayback);
                break;
            case PortablePreferenceKey::DataSaver:
                if (values.DataSaver) settings.SetBool(L"DataSaver", *values.DataSaver);
                break;
            case PortablePreferenceKey::Storefront:
                if (values.Storefront) settings.SetString(L"CatalogStorefront", winrt::hstring(*values.Storefront));
                break;
            }
        }
    }

    MusicSyncService::MusicSyncService(
        RemoteMusicService& remoteMusic,
        DatabaseEngine& database,
        UserDataOperationGate& operationGate,
        SettingsManager& settings)
        : m_remoteMusic(remoteMusic),
          m_database(database),
          m_operationGate(operationGate),
          m_settings(settings)
    {
    }

    bool MusicSyncService::IsSyncing() const noexcept
    {
        std::lock_guard guard{ m_mutex };
        return m_syncing;
    }

    winrt::hstring MusicSyncService::LastSafeError() const
    {
        std::lock_guard guard{ m_mutex };
        return m_lastSafeError;
    }

    AccountPendingCounts MusicSyncService::PendingCounts() const
    {
        try
        {
            auto context = m_remoteMusic.CaptureAccountSyncContext();
            return m_database.LoadAccountPendingCounts(context.OwnerId().c_str());
        }
        catch (...)
        {
            return {};
        }
    }

    std::uint64_t MusicSyncService::LibraryRevision() const noexcept
    {
        std::lock_guard guard{ m_mutex };
        return m_libraryRevision;
    }

    std::uint64_t MusicSyncService::PreferenceRevision() const noexcept
    {
        std::lock_guard guard{ m_mutex };
        return m_preferenceRevision;
    }

    void MusicSyncService::SetSafeError(winrt::hstring const& message)
    {
        std::lock_guard guard{ m_mutex };
        m_lastSafeError = message;
    }

    void MusicSyncService::SaveSyncError(
        AccountSyncContext const& context,
        std::wstring const& code)
    {
        if (!m_remoteMusic.IsCurrent(context))
        {
            return;
        }
        auto state = m_database.LoadAccountSyncState(context.OwnerId().c_str());
        if (!m_remoteMusic.IsCurrent(context))
        {
            return;
        }
        state.AccountId = context.OwnerId().c_str();
        state.LastSafeErrorCode = code;
        (void)m_database.SaveAccountSyncState(state);
    }

    void MusicSyncService::Requeue(AccountSyncLane lanes) noexcept
    {
        std::lock_guard guard{ m_mutex };
        m_pendingLanes |= Bits(lanes);
    }

    void MusicSyncService::Finish(bool succeeded) noexcept
    {
        std::lock_guard guard{ m_mutex };
        if (m_syncCompletion)
        {
            m_syncCompletion->Succeeded.store(succeeded, std::memory_order_release);
        }
        m_syncing = false;
        if (m_syncCompletion)
        {
            ::SetEvent(m_syncCompletion->Event.get());
        }
    }

    void MusicSyncService::ScheduleMutation(AccountSyncLane lane)
    {
        std::lock_guard guard{ m_mutex };
        m_debouncedLanes |= Bits(lane);
        if (m_mutationTimer)
        {
            return;
        }
        m_mutationTimer = winrt::Windows::System::Threading::ThreadPoolTimer::CreateTimer(
            [this](winrt::Windows::System::Threading::ThreadPoolTimer const&)
            {
                std::uint32_t lanes{};
                {
                    std::lock_guard timerGuard{ m_mutex };
                    lanes = std::exchange(m_debouncedLanes, 0u);
                    m_mutationTimer = nullptr;
                }
                if (lanes != 0)
                {
                    StartQueuedSync(this, static_cast<AccountSyncLane>(lanes));
                }
            },
            std::chrono::seconds(1));
    }

    void MusicSyncService::ScheduleRetry(AccountSyncLane lane, std::chrono::seconds delay)
    {
        auto const index = LaneIndex(lane);
        std::lock_guard guard{ m_mutex };
        auto const due = std::chrono::steady_clock::now() + delay;
        if (m_retryTimers[index] && due >= m_retryDue[index])
        {
            return;
        }
        if (m_retryTimers[index])
        {
            m_retryTimers[index].Cancel();
        }
        m_retryDue[index] = due;
        m_retryTimers[index] = winrt::Windows::System::Threading::ThreadPoolTimer::CreateTimer(
            [this, lane, index](winrt::Windows::System::Threading::ThreadPoolTimer const&)
            {
                {
                    std::lock_guard timerGuard{ m_mutex };
                    m_retryTimers[index] = nullptr;
                    m_retryDue[index] = {};
                }
                StartQueuedSync(this, lane);
            },
            delay);
    }

    void MusicSyncService::ScheduleNextDurableRetry(
        AccountSyncContext const& context,
        AccountSyncLane lane,
        AccountRetryQueue queue)
    {
        if (!m_remoteMusic.IsCurrent(context))
        {
            return;
        }
        auto const delay = m_database.SecondsUntilNextAccountRetry(
            context.OwnerId().c_str(),
            queue);
        if (delay)
        {
            ScheduleRetry(lane, std::chrono::seconds(*delay));
        }
    }

    int MusicSyncService::RecordLaneFailure(AccountSyncLane lane) noexcept
    {
        std::lock_guard guard{ m_mutex };
        return ++m_laneFailureAttempts[LaneIndex(lane)];
    }

    void MusicSyncService::ResetLaneFailure(AccountSyncLane lane) noexcept
    {
        std::lock_guard guard{ m_mutex };
        auto const index = LaneIndex(lane);
        m_laneFailureAttempts[index] = 0;
        if (m_retryTimers[index])
        {
            m_retryTimers[index].Cancel();
            m_retryTimers[index] = nullptr;
            m_retryDue[index] = {};
        }
    }

    bool MusicSyncService::SavePortablePreference(
        PortablePreferenceKey key,
        std::wstring const& valueJson)
    {
        auto operationLease = m_operationGate.TryEnter();
        if (!operationLease)
        {
            return false;
        }
        auto const validated = ValidatePortablePreferenceValue(key, valueJson);
        if (!validated)
        {
            return false;
        }
        try
        {
            auto context = m_remoteMusic.CaptureAccountSyncContext();
            auto const status = m_remoteMusic.StatusFor(context);
            if (status != AccountSessionStatus::Validated
                && status != AccountSessionStatus::Offline)
            {
                return false;
            }
            auto const revision = m_database.SavePendingAccountPreference(
                context.OwnerId().c_str(),
                PortablePreferenceName(key),
                *validated);
            if (revision <= 0 || !m_remoteMusic.IsCurrent(context))
            {
                return false;
            }
            ScheduleMutation(AccountSyncLane::PreferencePush);
            return true;
        }
        catch (...)
        {
            return false;
        }
    }

    winrt::Windows::Foundation::IAsyncOperation<bool> MusicSyncService::SyncAsync()
    {
        {
            std::lock_guard guard{ m_mutex };
            for (std::size_t index = 0; index < m_retryTimers.size(); ++index)
            {
                if (m_retryTimers[index])
                {
                    m_retryTimers[index].Cancel();
                    m_retryTimers[index] = nullptr;
                    m_retryDue[index] = {};
                }
                m_laneFailureAttempts[index] = 0;
            }
        }
        try
        {
            auto context = m_remoteMusic.CaptureAccountSyncContext();
            (void)m_database.ResetAccountRetrySchedule(context.OwnerId().c_str());
        }
        catch (...)
        {
        }
        co_return co_await RequestSyncAsync(AccountSyncLane::All);
    }

    winrt::Windows::Foundation::IAsyncOperation<bool>
        MusicSyncService::RequestOutboundSyncAsync()
    {
        constexpr auto outboundLanes = AccountSyncLane::History
            | AccountSyncLane::Likes
            | AccountSyncLane::PreferencePush;
        {
            std::lock_guard guard{ m_mutex };
            if (m_mutationTimer)
            {
                m_mutationTimer.Cancel();
                m_mutationTimer = nullptr;
            }
            m_debouncedLanes &= ~Bits(outboundLanes);
        }
        for (auto lane : { AccountSyncLane::History,
                           AccountSyncLane::Likes,
                           AccountSyncLane::PreferencePush })
        {
            ResetLaneFailure(lane);
        }
        try
        {
            auto const context = m_remoteMusic.CaptureAccountSyncContext();
            if (!m_database.ResetRecoverableAccountRetrySchedule(context.OwnerId().c_str()))
            {
                co_return false;
            }
        }
        catch (...)
        {
            co_return false;
        }
        co_return co_await RequestSyncAsync(outboundLanes);
    }

    winrt::Windows::Foundation::IAsyncOperation<bool> MusicSyncService::RequestSyncAsync(
        AccountSyncLane requestedLanes)
    {
        std::shared_ptr<SyncCompletionState> waitState;
        {
            std::lock_guard guard{ m_mutex };
            m_pendingLanes |= Bits(requestedLanes);
            if (m_syncing)
            {
                waitState = m_syncCompletion;
            }
            else
            {
                auto const completion = ::CreateEventW(nullptr, TRUE, FALSE, nullptr);
                winrt::check_pointer(completion);
                m_syncCompletion = std::make_shared<SyncCompletionState>();
                m_syncCompletion->Event = winrt::handle{ completion };
                m_syncing = true;
            }
        }

        if (waitState)
        {
            co_await winrt::resume_on_signal(waitState->Event.get());
            co_return waitState->Succeeded.load(std::memory_order_acquire);
        }

        auto operationLease = m_operationGate.TryEnter();
        if (!operationLease)
        {
            Finish(false);
            co_return false;
        }

        bool allSucceeded = true;
        bool refreshedAuthentication{};
        for (;;)
        {
            std::uint32_t lanes{};
            bool finished{};
            {
                std::lock_guard guard{ m_mutex };
                lanes = std::exchange(m_pendingLanes, 0u);
                if (lanes == 0)
                {
                    if (allSucceeded)
                    {
                        m_lastSafeError = {};
                    }
                    if (m_syncCompletion)
                    {
                        m_syncCompletion->Succeeded.store(allSucceeded, std::memory_order_release);
                    }
                    m_syncing = false;
                    if (m_syncCompletion)
                    {
                        ::SetEvent(m_syncCompletion->Event.get());
                    }
                    finished = true;
                }
            }
            if (finished)
            {
                co_return allSucceeded;
            }

            std::optional<AccountSyncContext> context;
            try
            {
                context.emplace(m_remoteMusic.CaptureAccountSyncContext());
                auto const status = m_remoteMusic.StatusFor(*context);
                if (status != AccountSessionStatus::Validated)
                {
                    SetSafeError(status == AccountSessionStatus::Offline
                        ? winrt::hstring{ L"Offline. Pending account changes are saved." }
                        : winrt::hstring{ L"Sign in to synchronize the account library." });
                    SaveSyncError(*context, status == AccountSessionStatus::Offline ? L"offline" : L"signed-out");
                    Finish(false);
                    co_return false;
                }

                auto runLane = [&](AccountSyncLane lane)
                    -> winrt::Windows::Foundation::IAsyncOperation<int>
                {
                    switch (lane)
                    {
                    case AccountSyncLane::History: co_return co_await RunHistoryLaneAsync(*context);
                    case AccountSyncLane::Likes: co_return co_await RunLikesLaneAsync(*context);
                    case AccountSyncLane::PreferencePush: co_return co_await RunPreferencePushLaneAsync(*context);
                    case AccountSyncLane::PreferencePull: co_return co_await RunPreferencePullLaneAsync(*context);
                    case AccountSyncLane::Library: co_return co_await RunLibraryLaneAsync(*context);
                    default: co_return LaneOutcome::Succeeded;
                    }
                };

                bool restart{};
                for (auto lane : { AccountSyncLane::History, AccountSyncLane::Likes,
                    AccountSyncLane::PreferencePush, AccountSyncLane::PreferencePull,
                    AccountSyncLane::Library })
                {
                    if (!HasLane(lanes, lane))
                    {
                        continue;
                    }
                    auto const outcome = co_await runLane(lane);
                    if (outcome == LaneOutcome::Failed)
                    {
                        allSucceeded = false;
                        continue;
                    }
                    if (outcome != LaneOutcome::Unauthorized)
                    {
                        continue;
                    }

                    if (refreshedAuthentication)
                    {
                        m_remoteMusic.HandleAccountSyncUnauthorized(*context);
                        SetSafeError(L"Your account session has expired. Sign in again to resume synchronization.");
                        SaveSyncError(*context, L"unauthorized");
                        Finish(false);
                        co_return false;
                    }

                    refreshedAuthentication = true;
                    Requeue(static_cast<AccountSyncLane>(lanes));
                    if (!co_await m_remoteMusic.RefreshAccountSessionAsync())
                    {
                        SetSafeError(L"Your account session has expired. Sign in again to resume synchronization.");
                        Finish(false);
                        co_return false;
                    }
                    restart = true;
                    break;
                }
                if (restart)
                {
                    continue;
                }
            }
            catch (winrt::hresult_canceled const&)
            {
                Finish(false);
                co_return false;
            }
            catch (...)
            {
                if (context && m_remoteMusic.IsCurrent(*context))
                {
                    SetSafeError(L"Account synchronization failed.");
                    SaveSyncError(*context, L"unexpected");
                }
                Finish(false);
                co_return false;
            }
        }
    }

    winrt::Windows::Foundation::IAsyncOperation<int>
        MusicSyncService::RunHistoryLaneAsync(AccountSyncContext const& context)
    {
        bool failed{};
        for (;;)
        {
            if (!m_remoteMusic.IsCurrent(context))
            {
                co_return LaneOutcome::Failed;
            }
            auto events = m_database.LoadPendingPlaybackEvents(context.OwnerId().c_str(), 100);
            if (events.empty())
            {
                ScheduleNextDurableRetry(
                    context,
                    AccountSyncLane::History,
                    AccountRetryQueue::History);
                co_return failed ? LaneOutcome::Failed : LaneOutcome::Succeeded;
            }
            auto const outcome = co_await UploadHistoryBatchAsync(context, std::move(events));
            if (outcome == LaneOutcome::Unauthorized)
            {
                co_return outcome;
            }
            if (outcome == LaneOutcome::Failed)
            {
                failed = true;
            }
        }
    }

    winrt::Windows::Foundation::IAsyncOperation<int>
        MusicSyncService::UploadHistoryBatchAsync(
            AccountSyncContext const& context,
            std::vector<PlaybackEventRecord> events)
    {
        auto const accountId = std::wstring(context.OwnerId().c_str());
        std::vector<AccountHistoryEventDto> payloadEvents;
        std::vector<std::wstring> submitted;
        payloadEvents.reserve(events.size());
        submitted.reserve(events.size());
        for (auto const& event : events)
        {
            auto track = PortableTrackFrom(event);
            if (!track || event.EventId.empty() || event.PlayedAtUtc.empty())
            {
                if (m_remoteMusic.IsCurrent(context))
                {
                    (void)m_database.MarkPlaybackEventsFailed(
                        accountId, { event.EventId }, L"invalid-payload", 0, true);
                }
                continue;
            }
            payloadEvents.push_back(AccountHistoryEventDto{
                event.EventId,
                std::move(*track),
                event.PlayedAtUtc,
                event.PositionSeconds });
            submitted.push_back(event.EventId);
        }
        if (submitted.empty())
        {
            SetSafeError(L"One or more pending history items need attention.");
            co_return LaneOutcome::Failed;
        }

        bool permanentRequestFailure{};
        try
        {
            auto const response = co_await m_remoteMusic.PostAccountHistoryBatchAsync(
                context,
                SerializeHistoryBatch(payloadEvents));
            if (!m_remoteMusic.IsCurrent(context))
            {
                co_return LaneOutcome::Failed;
            }
            auto const acknowledged = ParseAcknowledgedHistoryEvents(response, submitted);
            if (!acknowledged.empty())
            {
                (void)m_database.AcknowledgePlaybackEvents(accountId, acknowledged);
            }
            std::vector<PlaybackEventRecord> rejected;
            for (auto& event : events)
            {
                if (std::find(acknowledged.begin(), acknowledged.end(), event.EventId) == acknowledged.end())
                {
                    rejected.push_back(std::move(event));
                }
            }
            if (rejected.empty())
            {
                co_return LaneOutcome::Succeeded;
            }
            if (rejected.size() == 1)
            {
                (void)m_database.MarkPlaybackEventsFailed(
                    accountId, { rejected.front().EventId }, L"rejected", 0, true);
                SetSafeError(L"One pending history item was rejected. Other account data will keep synchronizing.");
                co_return LaneOutcome::Failed;
            }
            auto middle = rejected.begin() + static_cast<std::ptrdiff_t>(rejected.size() / 2);
            std::vector<PlaybackEventRecord> left(rejected.begin(), middle);
            std::vector<PlaybackEventRecord> right(middle, rejected.end());
            auto const leftResult = co_await UploadHistoryBatchAsync(context, std::move(left));
            if (leftResult == LaneOutcome::Unauthorized) co_return leftResult;
            auto const rightResult = co_await UploadHistoryBatchAsync(context, std::move(right));
            if (rightResult == LaneOutcome::Unauthorized) co_return rightResult;
            co_return leftResult == LaneOutcome::Succeeded && rightResult == LaneOutcome::Succeeded
                ? LaneOutcome::Succeeded
                : LaneOutcome::Failed;
        }
        catch (winrt::hresult_canceled const&)
        {
            throw;
        }
        catch (winrt::hresult_error const& error)
        {
            auto const status = AccountHttpStatus(error);
            if (status == 401)
            {
                co_return LaneOutcome::Unauthorized;
            }
            if (status != 0 && !IsTransient(status))
            {
                permanentRequestFailure = true;
            }
            else
            {
            auto const maxAttempt = std::max_element(events.begin(), events.end(), [](auto const& left, auto const& right)
            {
                return left.AttemptCount < right.AttemptCount;
            })->AttemptCount;
            auto const delay = FailureDelay(maxAttempt, AccountRetryAfterSeconds(error));
            if (m_remoteMusic.IsCurrent(context))
            {
                (void)m_database.MarkPlaybackEventsFailed(
                    accountId,
                    submitted,
                    status == 0 ? L"network" : L"transient",
                    static_cast<int>(delay.count()),
                    false);
                ScheduleRetry(AccountSyncLane::History, delay);
            }
            SetSafeError(L"History upload will retry automatically.");
            co_return LaneOutcome::Failed;
            }
        }

        if (permanentRequestFailure && events.size() > 1)
        {
            auto middle = events.begin() + static_cast<std::ptrdiff_t>(events.size() / 2);
            std::vector<PlaybackEventRecord> left(events.begin(), middle);
            std::vector<PlaybackEventRecord> right(middle, events.end());
            auto const leftResult = co_await UploadHistoryBatchAsync(context, std::move(left));
            if (leftResult == LaneOutcome::Unauthorized) co_return leftResult;
            auto const rightResult = co_await UploadHistoryBatchAsync(context, std::move(right));
            if (rightResult == LaneOutcome::Unauthorized) co_return rightResult;
            co_return leftResult == LaneOutcome::Succeeded && rightResult == LaneOutcome::Succeeded
                ? LaneOutcome::Succeeded
                : LaneOutcome::Failed;
        }
        if (m_remoteMusic.IsCurrent(context))
        {
            (void)m_database.MarkPlaybackEventsFailed(
                accountId, submitted, L"rejected", 0, true);
        }
        SetSafeError(L"One pending history item was rejected. Other account data will keep synchronizing.");
        co_return LaneOutcome::Failed;
    }

    winrt::Windows::Foundation::IAsyncOperation<int>
        MusicSyncService::RunLikesLaneAsync(AccountSyncContext const& context)
    {
        bool failed{};
        auto const accountId = std::wstring(context.OwnerId().c_str());
        for (;;)
        {
            if (!m_remoteMusic.IsCurrent(context))
            {
                co_return LaneOutcome::Failed;
            }
            auto likes = m_database.LoadPendingLikes(accountId, 100);
            if (likes.empty())
            {
                ScheduleNextDurableRetry(
                    context,
                    AccountSyncLane::Likes,
                    AccountRetryQueue::Likes);
                if (failed)
                {
                    SetSafeError(L"Some likes are pending and will retry automatically.");
                }
                co_return failed ? LaneOutcome::Failed : LaneOutcome::Succeeded;
            }

            for (auto const& like : likes)
            {
                auto track = PortableTrackFrom(like);
                if (!track)
                {
                    if (m_remoteMusic.IsCurrent(context))
                    {
                        (void)m_database.MarkPendingLikeFailed(
                            accountId, like.RemoteTrackId, L"invalid-payload", 0, true);
                    }
                    failed = true;
                    continue;
                }
                try
                {
                    co_await m_remoteMusic.SetAccountLikedAsync(
                        context,
                        SerializePortableTrack(*track),
                        like.DesiredState,
                        winrt::hstring(like.RemoteTrackId));
                    if (!m_remoteMusic.IsCurrent(context))
                    {
                        co_return LaneOutcome::Failed;
                    }
                    (void)m_database.AcknowledgePendingLike(
                        accountId, like.RemoteTrackId, like.DesiredState);
                }
                catch (winrt::hresult_canceled const&)
                {
                    throw;
                }
                catch (winrt::hresult_error const& error)
                {
                    auto const status = AccountHttpStatus(error);
                    if (status == 401)
                    {
                        co_return LaneOutcome::Unauthorized;
                    }
                    auto const transient = status == 0 || IsTransient(status);
                    auto const delay = FailureDelay(
                        like.AttemptCount,
                        AccountRetryAfterSeconds(error));
                    if (m_remoteMusic.IsCurrent(context))
                    {
                        (void)m_database.MarkPendingLikeFailed(
                            accountId,
                            like.RemoteTrackId,
                            status == 0 ? L"network" : (transient ? L"transient" : L"rejected"),
                            transient ? static_cast<int>(delay.count()) : 0,
                            !transient);
                        if (transient) ScheduleRetry(AccountSyncLane::Likes, delay);
                    }
                    failed = true;
                }
            }
        }
    }

    winrt::Windows::Foundation::IAsyncOperation<int>
        MusicSyncService::RunPreferencePushLaneAsync(AccountSyncContext const& context)
    {
        auto const accountId = std::wstring(context.OwnerId().c_str());
        auto records = m_database.LoadAccountPreferences(accountId, true);
        if (records.empty())
        {
            co_return LaneOutcome::Succeeded;
        }
        std::unordered_map<PortablePreferenceKey, std::wstring> values;
        std::vector<std::pair<std::wstring, std::int64_t>> submitted;
        std::vector<std::wstring> invalidKeys;
        int maxAttempt{};
        for (auto const& record : records)
        {
            auto const key = ParsePortablePreferenceName(record.Key);
            auto const value = key
                ? ValidatePortablePreferenceValue(*key, record.ValueJson)
                : std::nullopt;
            if (!key || !value)
            {
                invalidKeys.push_back(record.Key);
                continue;
            }
            values[*key] = *value;
            submitted.emplace_back(record.Key, record.Revision);
            maxAttempt = (std::max)(maxAttempt, record.AttemptCount);
        }
        bool invalidFailed = !invalidKeys.empty();
        if (invalidFailed && m_remoteMusic.IsCurrent(context))
        {
            (void)m_database.MarkAccountPreferencesFailed(
                accountId,
                invalidKeys,
                L"invalid-payload",
                0,
                true);
        }
        if (values.empty())
        {
            SetSafeError(L"One or more preference changes need attention.");
            co_return LaneOutcome::Failed;
        }
        try
        {
            co_await m_remoteMusic.PutAccountPreferencesAsync(
                context,
                SerializePortablePreferences(PortablePreferencesFromValues(values)));
            if (!m_remoteMusic.IsCurrent(context))
            {
                co_return LaneOutcome::Failed;
            }
            for (auto const& [key, revision] : submitted)
            {
                if (!m_remoteMusic.IsCurrent(context))
                {
                    co_return LaneOutcome::Failed;
                }
                (void)m_database.AcknowledgeAccountPreference(
                    accountId, key, revision);
            }
            ScheduleNextDurableRetry(
                context,
                AccountSyncLane::PreferencePush,
                AccountRetryQueue::Preferences);
            if (invalidFailed)
            {
                SetSafeError(L"One or more preference changes need attention.");
                co_return LaneOutcome::Failed;
            }
            co_return LaneOutcome::Succeeded;
        }
        catch (winrt::hresult_canceled const&)
        {
            throw;
        }
        catch (winrt::hresult_error const& error)
        {
            auto const status = AccountHttpStatus(error);
            if (status == 401) co_return LaneOutcome::Unauthorized;
            auto const transient = status == 0 || IsTransient(status);
            auto const delay = FailureDelay(
                maxAttempt,
                AccountRetryAfterSeconds(error));
            if (m_remoteMusic.IsCurrent(context))
            {
                std::vector<std::wstring> keys;
                keys.reserve(submitted.size());
                for (auto const& entry : submitted) keys.push_back(entry.first);
                (void)m_database.MarkAccountPreferencesFailed(
                    accountId,
                    keys,
                    status == 0 ? L"network" : (transient ? L"transient" : L"rejected"),
                    transient ? static_cast<int>(delay.count()) : 0,
                    !transient);
                if (transient) ScheduleRetry(AccountSyncLane::PreferencePush, delay);
            }
        }
        ScheduleNextDurableRetry(
            context,
            AccountSyncLane::PreferencePush,
            AccountRetryQueue::Preferences);
        SetSafeError(L"Some preference changes are still pending.");
        co_return LaneOutcome::Failed;
    }

    winrt::Windows::Foundation::IAsyncOperation<int>
        MusicSyncService::RunPreferencePullLaneAsync(AccountSyncContext const& context)
    {
        auto const accountId = std::wstring(context.OwnerId().c_str());
        auto before = m_database.LoadAccountPreferences(accountId, false);
        std::unordered_map<std::wstring, AccountPreferenceRecord> observed;
        for (auto const& record : before) observed.emplace(record.Key, record);
        try
        {
            auto const payload = co_await m_remoteMusic.GetAccountPreferencesAsync(context);
            auto const remote = PortablePreferenceValues(ParsePortablePreferences(payload));
            for (auto key : { PortablePreferenceKey::Theme, PortablePreferenceKey::Accent,
                PortablePreferenceKey::GaplessPlayback, PortablePreferenceKey::DataSaver,
                PortablePreferenceKey::Storefront })
            {
                auto const name = std::wstring(PortablePreferenceName(key));
                auto const foundRemote = remote.find(key);
                auto const foundLocal = observed.find(name);
                if (foundRemote == remote.end())
                {
                    // A cached row is also the durable marker that this key was
                    // already backfilled. Re-enqueuing every absent field on
                    // every GET would turn a one-time compatibility write into
                    // a permanent upload loop.
                    if (foundLocal == observed.end())
                    {
                        auto const localValue = ValidatePortablePreferenceValue(
                            key,
                            LocalPreferenceValue(m_settings, key));
                        if (localValue && m_remoteMusic.IsCurrent(context))
                        {
                            (void)m_database.SavePendingAccountPreference(
                                accountId, name, *localValue);
                            ScheduleMutation(AccountSyncLane::PreferencePush);
                        }
                    }
                    continue;
                }
                auto const revision = foundLocal == observed.end() ? 0 : foundLocal->second.Revision;
                if (foundLocal != observed.end() && foundLocal->second.Pending)
                {
                    continue;
                }
                if (!m_remoteMusic.IsCurrent(context))
                {
                    co_return LaneOutcome::Failed;
                }
                (void)m_database.MergeRemoteAccountPreference(
                    accountId, name, foundRemote->second, revision);
            }
            if (!m_remoteMusic.IsCurrent(context))
            {
                co_return LaneOutcome::Failed;
            }
            auto current = m_database.LoadAccountPreferences(accountId, false);
            for (auto const& record : current)
            {
                if (!record.Pending)
                {
                    auto const key = ParsePortablePreferenceName(record.Key);
                    if (!m_remoteMusic.IsCurrent(context))
                    {
                        co_return LaneOutcome::Failed;
                    }
                    if (key) ApplyPreference(m_settings, *key, record.ValueJson);
                }
            }
            ResetLaneFailure(AccountSyncLane::PreferencePull);
            {
                std::lock_guard guard{ m_mutex };
                ++m_preferenceRevision;
            }
            co_return LaneOutcome::Succeeded;
        }
        catch (winrt::hresult_canceled const&)
        {
            throw;
        }
        catch (winrt::hresult_error const& error)
        {
            auto const status = AccountHttpStatus(error);
            if (status == 401) co_return LaneOutcome::Unauthorized;
            if (status == 0 || IsTransient(status))
            {
                auto const delay = AccountRetryDelay(
                    RecordLaneFailure(AccountSyncLane::PreferencePull),
                    AccountRetryAfterSeconds(error));
                ScheduleRetry(AccountSyncLane::PreferencePull, delay);
            }
        }
        SetSafeError(L"Cloud preferences could not be refreshed. Cached values remain active.");
        co_return LaneOutcome::Failed;
    }

    winrt::Windows::Foundation::IAsyncOperation<int>
        MusicSyncService::RunLibraryLaneAsync(AccountSyncContext const& context)
    {
        try
        {
            auto const payload = co_await m_remoteMusic.GetAccountLibraryAsync(context);
            auto parsed = ParseAccountLibrary(payload);
            if (!parsed.Valid || !m_remoteMusic.IsCurrent(context))
            {
                SetSafeError(L"The account library response was not accepted.");
                SaveSyncError(context, L"library-response");
                co_return LaneOutcome::Failed;
            }

            auto const accountId = std::wstring(context.OwnerId().c_str());
            auto const profile = m_remoteMusic.ProfileFor(context);
            AccountLibrarySnapshot snapshot;
            snapshot.Profile.AccountId = accountId;
            snapshot.Profile.DisplayName = profile.DisplayName.c_str();
            snapshot.Profile.Username = profile.Username.c_str();
            snapshot.Profile.AvatarUrl = profile.AvatarUrl.c_str();
            snapshot.Profile.PlanLabel = profile.PlanLabel.c_str();
            snapshot.Profile.UpdatedAtUtc = UtcNow();
            snapshot.SynchronizedAtUtc = snapshot.Profile.UpdatedAtUtc;

            std::unordered_map<std::wstring, std::size_t> indexes;
            std::size_t sequence{ 1 };
            for (auto const& remote : parsed.Tracks)
            {
                AccountTrackSnapshot entry;
                entry.Track = TrackFrom(remote.Track);
                entry.Track.IsLiked(remote.Liked);
                entry.Track.DateAdded(winrt::hstring(remote.AddedAt));
                entry.Track.DateAddedSortKey(remote.LegacyAddedSortKey > 0.0
                    ? remote.LegacyAddedSortKey
                    : (remote.AddedAt.empty() ? -static_cast<double>(sequence++) : 0.0));
                entry.PlayCount = remote.PlayCount;
                entry.FirstPlayedAtUtc = remote.FirstPlayedAt;
                entry.LastPlayedAtUtc = remote.LastPlayedAt;
                entry.LastPositionSeconds = remote.LastPositionSec;
                indexes.emplace(remote.Track.Id, snapshot.Tracks.size());
                snapshot.Tracks.push_back(std::move(entry));
            }

            auto existing = m_database.LoadAccountLibrary(accountId);
            std::unordered_map<std::wstring, AccountTrackSnapshot> existingTracks;
            if (existing)
            {
                for (auto const& track : existing->Tracks)
                {
                    existingTracks.emplace(track.Track.RemoteId().c_str(), track);
                }
            }

            auto ensureTrack = [&](std::wstring const& remoteId, TrackInfo const& fallback) -> AccountTrackSnapshot*
            {
                auto found = indexes.find(remoteId);
                if (found != indexes.end())
                {
                    return &snapshot.Tracks[found->second];
                }
                if (!fallback)
                {
                    return nullptr;
                }
                AccountTrackSnapshot entry;
                entry.Track = fallback;
                entry.Track.CatalogId(0);
                indexes.emplace(remoteId, snapshot.Tracks.size());
                snapshot.Tracks.push_back(std::move(entry));
                return &snapshot.Tracks.back();
            };

            for (auto const& like : m_database.LoadPendingLikes(accountId, 10000, false))
            {
                if (auto entry = ensureTrack(like.RemoteTrackId, like.Track))
                {
                    entry->Track.IsLiked(like.DesiredState);
                }
            }
            for (auto const& event : m_database.LoadPendingPlaybackEvents(accountId, 10000, false))
            {
                if (auto entry = ensureTrack(event.RemoteTrackId, event.Track))
                {
                    auto const found = existingTracks.find(event.RemoteTrackId);
                    if (found != existingTracks.end())
                    {
                        entry->PlayCount = (std::max)(entry->PlayCount, found->second.PlayCount);
                        if (found->second.LastPlayedAtUtc > entry->LastPlayedAtUtc)
                        {
                            entry->LastPlayedAtUtc = found->second.LastPlayedAtUtc;
                            entry->LastPositionSeconds = found->second.LastPositionSeconds;
                        }
                    }
                    if (event.PlayedAtUtc > entry->LastPlayedAtUtc)
                    {
                        entry->LastPlayedAtUtc = event.PlayedAtUtc;
                        entry->LastPositionSeconds = event.PositionSeconds;
                    }
                }
            }

            for (auto const& remote : parsed.Playlists)
            {
                AccountPlaylistSnapshot playlist;
                playlist.PlaylistId = remote.Id;
                playlist.Name = remote.Name;
                playlist.Description = remote.Description;
                playlist.SourceUrl = remote.SourceUrl;
                playlist.UpdatedAtUtc = remote.UpdatedAt;
                playlist.RemoteTrackIds = remote.TrackIds;
                snapshot.Playlists.push_back(std::move(playlist));
            }
            if (!m_remoteMusic.IsCurrent(context)
                || !m_database.ReplaceAccountLibrary(accountId, snapshot))
            {
                SetSafeError(L"Could not save the synchronized account library.");
                SaveSyncError(context, L"cache-write");
                co_return LaneOutcome::Failed;
            }
            ResetLaneFailure(AccountSyncLane::Library);
            {
                std::lock_guard guard{ m_mutex };
                ++m_libraryRevision;
            }
            co_return LaneOutcome::Succeeded;
        }
        catch (winrt::hresult_canceled const&)
        {
            throw;
        }
        catch (winrt::hresult_error const& error)
        {
            auto const status = AccountHttpStatus(error);
            if (status == 401) co_return LaneOutcome::Unauthorized;
            if (status == 0 || IsTransient(status))
            {
                auto const delay = AccountRetryDelay(
                    RecordLaneFailure(AccountSyncLane::Library),
                    AccountRetryAfterSeconds(error));
                ScheduleRetry(AccountSyncLane::Library, delay);
            }
        }
        SetSafeError(L"The account library could not be refreshed. Cached data remains available.");
        SaveSyncError(context, L"library-request");
        co_return LaneOutcome::Failed;
    }
}
