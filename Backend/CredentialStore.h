#pragma once

#include <cstdint>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

#include <winrt/Windows.Foundation.h>

namespace LastMusicPlayer::Backend
{
    class SettingsManager;

    struct ICredentialApi
    {
        virtual ~ICredentialApi() = default;

        virtual bool WriteGeneric(
            std::wstring const& target,
            std::vector<std::uint8_t> const& secret,
            std::uint32_t persistence) noexcept = 0;
        virtual std::optional<std::vector<std::uint8_t>> ReadGeneric(
            std::wstring const& target) noexcept = 0;
        virtual bool DeleteGeneric(std::wstring const& target) noexcept = 0;
    };

    class Win32CredentialApi final : public ICredentialApi
    {
    public:
        bool WriteGeneric(
            std::wstring const& target,
            std::vector<std::uint8_t> const& secret,
            std::uint32_t persistence) noexcept override;
        std::optional<std::vector<std::uint8_t>> ReadGeneric(
            std::wstring const& target) noexcept override;
        bool DeleteGeneric(std::wstring const& target) noexcept override;
    };

    enum class AccountCredentialDeleteResult
    {
        Deleted,
        NotFound,
        Mismatch,
        Failed,
    };

    struct ICredentialStore
    {
        virtual ~ICredentialStore() = default;

        virtual bool WriteAccountSession(winrt::hstring const& session) noexcept = 0;
        virtual winrt::hstring ReadAccountSession() noexcept = 0;
        virtual bool DeleteAccountSession() noexcept = 0;
        virtual AccountCredentialDeleteResult DeleteAccountSessionIfMatches(
            winrt::hstring const& expectedSession) noexcept = 0;

        virtual bool WriteProviderApiKey(winrt::hstring const& apiKey) noexcept = 0;
        virtual winrt::hstring ReadProviderApiKey() noexcept = 0;
        virtual bool DeleteProviderApiKey() noexcept = 0;
    };

    class CredentialStore final : public ICredentialStore
    {
    public:
        CredentialStore();
        explicit CredentialStore(std::shared_ptr<ICredentialApi> api);

        bool WriteAccountSession(winrt::hstring const& session) noexcept override;
        winrt::hstring ReadAccountSession() noexcept override;
        bool DeleteAccountSession() noexcept override;
        AccountCredentialDeleteResult DeleteAccountSessionIfMatches(
            winrt::hstring const& expectedSession) noexcept override;

        bool WriteProviderApiKey(winrt::hstring const& apiKey) noexcept override;
        winrt::hstring ReadProviderApiKey() noexcept override;
        bool DeleteProviderApiKey() noexcept override;

    private:
        bool WriteSecret(std::wstring const& target, winrt::hstring const& secret) noexcept;
        winrt::hstring ReadSecret(std::wstring const& target) noexcept;

        std::shared_ptr<ICredentialApi> m_api;
        std::mutex m_accountMutex;
    };

    enum class CredentialMigrationStatus
    {
        NotNeeded,
        Migrated,
        SecureWriteFailed,
        SecureVerificationFailed,
        SettingsCleanupFailed,
    };

    CredentialMigrationStatus MigrateLegacyProviderApiKey(
        SettingsManager& settings,
        ICredentialStore& credentials) noexcept;

    winrt::hstring CredentialMigrationMessage(CredentialMigrationStatus status) noexcept;
}
