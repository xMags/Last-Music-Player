#pragma once

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace LastMusicPlayer::Backend
{
    struct CastDevice
    {
        std::wstring id;
        std::wstring name;
        std::wstring host;
        std::uint16_t port{ 8009 };
    };

    class IDnsSdBrowseOperation
    {
    public:
        virtual ~IDnsSdBrowseOperation() = default;

        [[nodiscard]] virtual bool Wait(std::uint32_t timeoutMs) = 0;
        virtual void CancelAndWait() noexcept = 0;
        [[nodiscard]] virtual std::vector<std::wstring> Instances() const = 0;
    };

    class IDnsSdResolveOperation
    {
    public:
        virtual ~IDnsSdResolveOperation() = default;

        [[nodiscard]] virtual bool Wait(std::uint32_t timeoutMs) = 0;
        virtual void CancelAndWait() noexcept = 0;
        [[nodiscard]] virtual std::optional<CastDevice> Device() const = 0;
    };

    class IDnsSdAdapter
    {
    public:
        virtual ~IDnsSdAdapter() = default;

        [[nodiscard]] virtual std::unique_ptr<IDnsSdBrowseOperation> BeginBrowse() = 0;
        [[nodiscard]] virtual std::unique_ptr<IDnsSdResolveOperation> BeginResolve(
            std::wstring const& instance) = 0;
    };

    [[nodiscard]] std::vector<CastDevice> DiscoverCastDevices(
        IDnsSdAdapter& adapter,
        std::uint32_t timeoutMs);

    [[nodiscard]] std::shared_ptr<IDnsSdAdapter> CreateWindowsDnsSdAdapter();
}
