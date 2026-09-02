#include "pch.h"
#include "Backend/CastDiscovery.h"

#include <set>

namespace LastMusicPlayer::Backend
{
    namespace
    {
        constexpr std::uint32_t kResolveTimeoutMs = 2000;
    }

    std::vector<CastDevice> DiscoverCastDevices(
        IDnsSdAdapter& adapter,
        std::uint32_t timeoutMs)
    {
        auto browse = adapter.BeginBrowse();
        if (!browse)
        {
            return {};
        }

        (void)browse->Wait(timeoutMs);
        browse->CancelAndWait();
        auto const instances = browse->Instances();

        std::vector<CastDevice> devices;
        std::set<std::wstring> seenDeviceIds;
        for (auto const& instance : instances)
        {
            auto resolve = adapter.BeginResolve(instance);
            if (!resolve)
            {
                continue;
            }

            auto const completed = resolve->Wait(kResolveTimeoutMs);
            resolve->CancelAndWait();
            if (!completed)
            {
                continue;
            }

            auto const device = resolve->Device();
            if (!device || device->host.empty() || device->id.empty())
            {
                continue;
            }
            if (seenDeviceIds.insert(device->id).second)
            {
                devices.push_back(*device);
            }
        }
        return devices;
    }
}
