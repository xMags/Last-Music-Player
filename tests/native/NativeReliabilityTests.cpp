#include "pch.h"

#include "Backend/CastDiscovery.h"
#include "Backend/UtfCodec.h"

#include <cstdint>
#include <exception>
#include <iostream>
#include <map>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace
{
    void Expect(bool condition, char const* message)
    {
        if (!condition)
        {
            throw std::runtime_error(message);
        }
    }

    void TestUtfCodec()
    {
        using LastMusicPlayer::Backend::Utf::FromUtf8;
        using LastMusicPlayer::Backend::Utf::ToUtf8;

        auto const ascii = ToUtf8(L"Last Music");
        Expect(ascii && *ascii == "Last Music", "ASCII UTF-8 encoding changed");
        Expect(FromUtf8(*ascii) == std::optional<std::wstring>{ L"Last Music" },
            "ASCII UTF-8 round-trip failed");

        std::wstring const bengali = L"শেষ গান";
        auto const bengaliUtf8 = ToUtf8(bengali);
        Expect(bengaliUtf8 && FromUtf8(*bengaliUtf8) == bengali,
            "Bengali UTF-8 round-trip failed");

        std::wstring const emoji = L"Now playing \U0001F3B5";
        auto const emojiUtf8 = ToUtf8(emoji);
        Expect(emojiUtf8 && FromUtf8(*emojiUtf8) == emoji,
            "emoji UTF-8 round-trip failed");

        auto const emptyUtf8 = ToUtf8(L"");
        auto const emptyWide = FromUtf8("");
        Expect(emptyUtf8 && emptyUtf8->empty(), "empty UTF-16 input was treated as failure");
        Expect(emptyWide && emptyWide->empty(), "empty UTF-8 input was treated as failure");

        std::wstring const embeddedNull{ L'A', L'\0', L'B' };
        auto const embeddedUtf8 = ToUtf8(embeddedNull);
        Expect(embeddedUtf8 && embeddedUtf8->size() == 3 && (*embeddedUtf8)[1] == '\0',
            "embedded null was truncated during UTF-8 encoding");
        auto const embeddedWide = FromUtf8(*embeddedUtf8);
        Expect(embeddedWide && *embeddedWide == embeddedNull,
            "embedded null was truncated during UTF-8 decoding");

        std::string const malformed{ '\xF0', '\x28', '\x8C', '\x28' };
        Expect(!FromUtf8(malformed), "malformed UTF-8 was accepted");

        std::wstring const unpairedSurrogate(1, static_cast<wchar_t>(0xD800));
        Expect(!ToUtf8(unpairedSurrogate), "unpaired UTF-16 surrogate was accepted");
    }

    struct FakeDnsState final
    {
        int ActiveCallbacks{};
        int CancelCount{};
    };

    struct ResolvePlan final
    {
        bool Starts{ true };
        bool Completes{ true };
        bool DeliversLateAfterCancellation{};
        std::optional<LastMusicPlayer::Backend::CastDevice> Device;
    };

    class FakeBrowseOperation final : public LastMusicPlayer::Backend::IDnsSdBrowseOperation
    {
    public:
        FakeBrowseOperation(
            std::shared_ptr<FakeDnsState> state,
            std::vector<std::wstring> instances)
            : m_state(std::move(state)), m_instances(std::move(instances))
        {
            ++m_state->ActiveCallbacks;
        }

        ~FakeBrowseOperation() override { CancelAndWait(); }

        bool Wait(std::uint32_t) override { return false; }

        void CancelAndWait() noexcept override
        {
            if (m_finished)
            {
                return;
            }
            m_finished = true;
            ++m_state->CancelCount;
            --m_state->ActiveCallbacks;
        }

        std::vector<std::wstring> Instances() const override { return m_instances; }

    private:
        std::shared_ptr<FakeDnsState> m_state;
        std::vector<std::wstring> m_instances;
        bool m_finished{};
    };

    class FakeResolveOperation final : public LastMusicPlayer::Backend::IDnsSdResolveOperation
    {
    public:
        FakeResolveOperation(std::shared_ptr<FakeDnsState> state, ResolvePlan plan)
            : m_state(std::move(state)), m_plan(std::move(plan))
        {
            ++m_state->ActiveCallbacks;
        }

        ~FakeResolveOperation() override { CancelAndWait(); }

        bool Wait(std::uint32_t) override
        {
            if (m_plan.Completes)
            {
                m_device = m_plan.Device;
                FinishCallback();
                return true;
            }
            return false;
        }

        void CancelAndWait() noexcept override
        {
            if (m_cancelled)
            {
                return;
            }
            m_cancelled = true;
            ++m_state->CancelCount;
            if (m_plan.DeliversLateAfterCancellation)
            {
                m_device = m_plan.Device;
            }
            FinishCallback();
        }

        std::optional<LastMusicPlayer::Backend::CastDevice> Device() const override
        {
            return m_device;
        }

    private:
        void FinishCallback() noexcept
        {
            if (!m_callbackActive)
            {
                return;
            }
            m_callbackActive = false;
            --m_state->ActiveCallbacks;
        }

        std::shared_ptr<FakeDnsState> m_state;
        ResolvePlan m_plan;
        std::optional<LastMusicPlayer::Backend::CastDevice> m_device;
        bool m_cancelled{};
        bool m_callbackActive{ true };
    };

    class FakeDnsAdapter final : public LastMusicPlayer::Backend::IDnsSdAdapter
    {
    public:
        enum class BrowseFailure
        {
            None,
            EventCreation,
            ImmediateApi,
        };

        std::unique_ptr<LastMusicPlayer::Backend::IDnsSdBrowseOperation> BeginBrowse() override
        {
            ++BrowseStarts;
            if (Failure != BrowseFailure::None)
            {
                return nullptr;
            }
            return std::make_unique<FakeBrowseOperation>(State, Instances);
        }

        std::unique_ptr<LastMusicPlayer::Backend::IDnsSdResolveOperation> BeginResolve(
            std::wstring const& instance) override
        {
            ++ResolveStarts;
            auto const found = Plans.find(instance);
            if (found == Plans.end() || !found->second.Starts)
            {
                return nullptr;
            }
            return std::make_unique<FakeResolveOperation>(State, found->second);
        }

        std::shared_ptr<FakeDnsState> State{ std::make_shared<FakeDnsState>() };
        BrowseFailure Failure{};
        std::vector<std::wstring> Instances;
        std::map<std::wstring, ResolvePlan> Plans;
        int BrowseStarts{};
        int ResolveStarts{};
    };

    LastMusicPlayer::Backend::CastDevice Device(
        wchar_t const* id,
        wchar_t const* name,
        wchar_t const* host)
    {
        return { id, name, host, 8009 };
    }

    void TestCastDiscoveryFailures()
    {
        FakeDnsAdapter eventFailure;
        eventFailure.Failure = FakeDnsAdapter::BrowseFailure::EventCreation;
        Expect(LastMusicPlayer::Backend::DiscoverCastDevices(eventFailure, 10).empty(),
            "event creation failure returned devices");
        Expect(eventFailure.ResolveStarts == 0 && eventFailure.State->ActiveCallbacks == 0,
            "event creation failure started or retained callbacks");

        FakeDnsAdapter apiFailure;
        apiFailure.Failure = FakeDnsAdapter::BrowseFailure::ImmediateApi;
        Expect(LastMusicPlayer::Backend::DiscoverCastDevices(apiFailure, 10).empty(),
            "immediate browse API failure returned devices");
        Expect(apiFailure.State->ActiveCallbacks == 0,
            "immediate browse API failure retained callbacks");

        FakeDnsAdapter resolveFailure;
        resolveFailure.Instances = { L"Living Room._googlecast._tcp.local" };
        resolveFailure.Plans.emplace(resolveFailure.Instances.front(), ResolvePlan{ false });
        Expect(LastMusicPlayer::Backend::DiscoverCastDevices(resolveFailure, 10).empty(),
            "immediate resolve API failure returned devices");
        Expect(resolveFailure.State->ActiveCallbacks == 0,
            "immediate resolve API failure retained callbacks");
    }

    void TestCastDiscoveryCompletionAndDeduplication()
    {
        FakeDnsAdapter adapter;
        adapter.Instances = {
            L"Living Room._googlecast._tcp.local",
            L"Living Room duplicate._googlecast._tcp.local",
            L"Kitchen._googlecast._tcp.local",
        };
        adapter.Plans.emplace(adapter.Instances[0], ResolvePlan{
            true, true, false, Device(L"living", L"Living Room", L"192.168.1.10") });
        adapter.Plans.emplace(adapter.Instances[1], ResolvePlan{
            true, true, false, Device(L"living", L"Living Room", L"192.168.1.10") });
        adapter.Plans.emplace(adapter.Instances[2], ResolvePlan{
            true, true, false, Device(L"kitchen", L"Kitchen", L"192.168.1.11") });

        auto const devices = LastMusicPlayer::Backend::DiscoverCastDevices(adapter, 10);
        Expect(devices.size() == 2, "duplicate Cast device IDs were not removed");
        Expect(devices[0].id == L"living" && devices[1].id == L"kitchen",
            "normal Cast discovery changed device ordering");
        Expect(adapter.State->ActiveCallbacks == 0,
            "completed Cast discovery retained callbacks");
    }

    void TestCastDiscoveryTimeoutAndLateCallback()
    {
        FakeDnsAdapter timeout;
        timeout.Instances = { L"Slow._googlecast._tcp.local" };
        timeout.Plans.emplace(timeout.Instances.front(), ResolvePlan{
            true, false, false, Device(L"slow", L"Slow", L"192.168.1.20") });
        Expect(LastMusicPlayer::Backend::DiscoverCastDevices(timeout, 10).empty(),
            "timed-out resolve returned a device");
        Expect(timeout.State->ActiveCallbacks == 0,
            "timed-out resolve retained its callback");

        FakeDnsAdapter late;
        late.Instances = { L"Late._googlecast._tcp.local" };
        late.Plans.emplace(late.Instances.front(), ResolvePlan{
            true, false, true, Device(L"late", L"Late", L"192.168.1.21") });
        Expect(LastMusicPlayer::Backend::DiscoverCastDevices(late, 10).empty(),
            "late post-cancellation callback published a device");
        Expect(late.State->ActiveCallbacks == 0,
            "late post-cancellation callback retained its state");
    }
}

int wmain()
{
    try
    {
        TestUtfCodec();
        TestCastDiscoveryFailures();
        TestCastDiscoveryCompletionAndDeduplication();
        TestCastDiscoveryTimeoutAndLateCallback();
        std::wcout << L"Native reliability tests passed.\n";
        return 0;
    }
    catch (std::exception const& error)
    {
        std::cerr << "Native reliability tests failed: " << error.what() << "\n";
        return 1;
    }
}
