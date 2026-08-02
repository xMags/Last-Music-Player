#include "pch.h"
#include "Backend/CredentialStore.h"

#include "Backend/SettingsManager.h"

#include <windows.h>
#include <wincred.h>

#include <limits>

#pragma comment(lib, "Advapi32.lib")

namespace LastMusicPlayer::Backend
{
    namespace
    {
        constexpr wchar_t AccountSessionTarget[] = L"LastMusicPlayer/AccountSession/v1";
        constexpr wchar_t ProviderApiKeyTarget[] = L"LastMusicPlayer/ProviderApiKey/v1";
        constexpr wchar_t CredentialUserName[] = L"Last Music Player";

        std::vector<std::uint8_t> ToUtf8(winrt::hstring const& value)
        {
            if (value.empty())
            {
                return {};
            }

            auto required = ::WideCharToMultiByte(
                CP_UTF8,
                WC_ERR_INVALID_CHARS,
                value.c_str(),
                static_cast<int>(value.size()),
                nullptr,
                0,
                nullptr,
                nullptr);
            if (required <= 0)
            {
                return {};
            }

            std::vector<std::uint8_t> result(static_cast<std::size_t>(required));
            if (::WideCharToMultiByte(
                CP_UTF8,
                WC_ERR_INVALID_CHARS,
                value.c_str(),
                static_cast<int>(value.size()),
                reinterpret_cast<char*>(result.data()),
                required,
                nullptr,
                nullptr) != required)
            {
                return {};
            }
            return result;
        }

        winrt::hstring FromUtf8(std::vector<std::uint8_t> const& value)
        {
            if (value.empty() || value.size() > static_cast<std::size_t>((std::numeric_limits<int>::max)()))
            {
                return {};
            }

            auto required = ::MultiByteToWideChar(
                CP_UTF8,
                MB_ERR_INVALID_CHARS,
                reinterpret_cast<char const*>(value.data()),
                static_cast<int>(value.size()),
                nullptr,
                0);
            if (required <= 0)
            {
                return {};
            }

            std::wstring result(static_cast<std::size_t>(required), L'\0');
            if (::MultiByteToWideChar(
                CP_UTF8,
                MB_ERR_INVALID_CHARS,
                reinterpret_cast<char const*>(value.data()),
                static_cast<int>(value.size()),
                result.data(),
                required) != required)
            {
                return {};
            }
            return winrt::hstring{ result };
        }
    }

    bool Win32CredentialApi::WriteGeneric(
        std::wstring const& target,
        std::vector<std::uint8_t> const& secret,
        std::uint32_t persistence) noexcept
    {
        if (target.empty() || secret.empty() || secret.size() > CRED_MAX_CREDENTIAL_BLOB_SIZE)
        {
            return false;
        }

        CREDENTIALW credential{};
        credential.Type = CRED_TYPE_GENERIC;
        credential.TargetName = const_cast<wchar_t*>(target.c_str());
        credential.CredentialBlobSize = static_cast<DWORD>(secret.size());
        credential.CredentialBlob = const_cast<LPBYTE>(secret.data());
        credential.Persist = persistence;
        credential.UserName = const_cast<wchar_t*>(CredentialUserName);
        return ::CredWriteW(&credential, 0) != FALSE;
    }

    std::optional<std::vector<std::uint8_t>> Win32CredentialApi::ReadGeneric(
        std::wstring const& target) noexcept
    {
        if (target.empty())
        {
            return std::nullopt;
        }

        PCREDENTIALW credential{};
        if (!::CredReadW(target.c_str(), CRED_TYPE_GENERIC, 0, &credential))
        {
            return std::nullopt;
        }

        std::vector<std::uint8_t> secret;
        if (credential->CredentialBlob && credential->CredentialBlobSize > 0)
        {
            secret.assign(
                credential->CredentialBlob,
                credential->CredentialBlob + credential->CredentialBlobSize);
        }
        ::CredFree(credential);
        return secret;
    }

    bool Win32CredentialApi::DeleteGeneric(std::wstring const& target) noexcept
    {
        if (target.empty())
        {
            return false;
        }
        if (::CredDeleteW(target.c_str(), CRED_TYPE_GENERIC, 0))
        {
            return true;
        }
        return ::GetLastError() == ERROR_NOT_FOUND;
    }

    CredentialStore::CredentialStore()
        : CredentialStore(std::make_shared<Win32CredentialApi>())
    {
    }

    CredentialStore::CredentialStore(std::shared_ptr<ICredentialApi> api)
        : m_api(std::move(api))
    {
    }

    bool CredentialStore::WriteSecret(
        std::wstring const& target,
        winrt::hstring const& secret) noexcept
    {
        if (!m_api || secret.empty())
        {
            return false;
        }
        auto encoded = ToUtf8(secret);
        if (encoded.empty())
        {
            return false;
        }
        return m_api->WriteGeneric(target, encoded, CRED_PERSIST_LOCAL_MACHINE);
    }

    winrt::hstring CredentialStore::ReadSecret(std::wstring const& target) noexcept
    {
        if (!m_api)
        {
            return {};
        }
        auto secret = m_api->ReadGeneric(target);
        if (!secret)
        {
            return {};
        }
        return FromUtf8(*secret);
    }

    bool CredentialStore::WriteAccountSession(winrt::hstring const& session) noexcept
    {
        std::lock_guard guard{ m_accountMutex };
        return WriteSecret(AccountSessionTarget, session);
    }

    winrt::hstring CredentialStore::ReadAccountSession() noexcept
    {
        std::lock_guard guard{ m_accountMutex };
        return ReadSecret(AccountSessionTarget);
    }

    bool CredentialStore::DeleteAccountSession() noexcept
    {
        std::lock_guard guard{ m_accountMutex };
        return m_api && m_api->DeleteGeneric(AccountSessionTarget);
    }

    AccountCredentialDeleteResult CredentialStore::DeleteAccountSessionIfMatches(
        winrt::hstring const& expectedSession) noexcept
    {
        if (expectedSession.empty())
        {
            return AccountCredentialDeleteResult::Mismatch;
        }

        std::lock_guard guard{ m_accountMutex };
        if (!m_api)
        {
            return AccountCredentialDeleteResult::Failed;
        }

        auto encoded = m_api->ReadGeneric(AccountSessionTarget);
        if (!encoded)
        {
            return AccountCredentialDeleteResult::NotFound;
        }
        auto current = FromUtf8(*encoded);
        if (current.empty())
        {
            return AccountCredentialDeleteResult::Failed;
        }
        if (current != expectedSession)
        {
            return AccountCredentialDeleteResult::Mismatch;
        }
        return m_api->DeleteGeneric(AccountSessionTarget)
            ? AccountCredentialDeleteResult::Deleted
            : AccountCredentialDeleteResult::Failed;
    }

    bool CredentialStore::WriteProviderApiKey(winrt::hstring const& apiKey) noexcept
    {
        return WriteSecret(ProviderApiKeyTarget, apiKey);
    }

    winrt::hstring CredentialStore::ReadProviderApiKey() noexcept
    {
        return ReadSecret(ProviderApiKeyTarget);
    }

    bool CredentialStore::DeleteProviderApiKey() noexcept
    {
        return m_api && m_api->DeleteGeneric(ProviderApiKeyTarget);
    }

    CredentialMigrationStatus MigrateLegacyProviderApiKey(
        SettingsManager& settings,
        ICredentialStore& credentials) noexcept
    {
        try
        {
            auto legacyApiKey = settings.GetString(L"ProviderApiKey", L"");
            if (legacyApiKey.empty())
            {
                return CredentialMigrationStatus::NotNeeded;
            }
            if (!credentials.WriteProviderApiKey(legacyApiKey))
            {
                return CredentialMigrationStatus::SecureWriteFailed;
            }
            if (credentials.ReadProviderApiKey() != legacyApiKey)
            {
                return CredentialMigrationStatus::SecureVerificationFailed;
            }
            if (!settings.Remove(L"ProviderApiKey"))
            {
                return CredentialMigrationStatus::SettingsCleanupFailed;
            }
            return CredentialMigrationStatus::Migrated;
        }
        catch (...)
        {
            return CredentialMigrationStatus::SettingsCleanupFailed;
        }
    }

    winrt::hstring CredentialMigrationMessage(CredentialMigrationStatus status) noexcept
    {
        switch (status)
        {
        case CredentialMigrationStatus::NotNeeded:
        case CredentialMigrationStatus::Migrated:
            return {};
        case CredentialMigrationStatus::SecureWriteFailed:
        case CredentialMigrationStatus::SecureVerificationFailed:
            return L"API key migration failed. Reconnect the integration.";
        case CredentialMigrationStatus::SettingsCleanupFailed:
            return L"API key was secured, but the old settings file could not be cleaned.";
        default:
            return L"API key migration failed.";
        }
    }
}
