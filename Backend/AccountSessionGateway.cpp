#include "pch.h"
#include "Backend/AccountSessionGateway.h"

#include "Backend/AccountClient.h"
#include "Backend/BuildConfig.h"
#include "Backend/LoopbackCallback.h"
#include "Backend/Pkce.h"

#include <windows.h>
#ifdef GetObject
#undef GetObject
#endif

#include <algorithm>
#include <chrono>
#include <cwctype>
#include <mutex>
#include <string>
#include <string_view>
#include <utility>

#include <winrt/Windows.Data.Json.h>
#include <winrt/Windows.System.h>

namespace LastMusicPlayer::Backend
{
    namespace
    {
        winrt::hstring JsonString(
            winrt::Windows::Data::Json::JsonObject const& object,
            wchar_t const* key)
        {
            if (!object || !object.HasKey(key))
            {
                return {};
            }
            auto value = object.GetNamedValue(key);
            return value.ValueType() == winrt::Windows::Data::Json::JsonValueType::String
                ? value.GetString()
                : winrt::hstring{};
        }

        bool ContainsControlCharacter(winrt::hstring const& value) noexcept
        {
            return std::any_of(value.begin(), value.end(), [](wchar_t character)
            {
                return character < 0x20
                    || (character >= 0x7f && character <= 0x9f);
            });
        }

        bool EqualsAsciiCaseInsensitive(
            winrt::hstring const& value,
            std::wstring_view expected) noexcept
        {
            if (value.size() != expected.size())
            {
                return false;
            }
            for (decltype(value.size()) index{}; index < value.size(); ++index)
            {
                if (std::towlower(value[index])
                    != std::towlower(expected[index]))
                {
                    return false;
                }
            }
            return true;
        }

        bool IsValidBearerToken(winrt::hstring const& value) noexcept
        {
            bool sawDataCharacter{};
            bool sawPadding{};
            for (auto character : value)
            {
                if (character == L'=')
                {
                    sawPadding = true;
                    continue;
                }
                if (sawPadding)
                {
                    return false;
                }

                auto allowed = (character >= L'a' && character <= L'z')
                    || (character >= L'A' && character <= L'Z')
                    || (character >= L'0' && character <= L'9')
                    || character == L'-'
                    || character == L'.'
                    || character == L'_'
                    || character == L'~'
                    || character == L'+'
                    || character == L'/';
                if (!allowed)
                {
                    return false;
                }
                sawDataCharacter = true;
            }
            return sawDataCharacter;
        }

        winrt::hstring BoundedProfileText(
            winrt::hstring const& value,
            std::size_t maximumCharacters)
        {
            return value.size() <= maximumCharacters
                && !ContainsControlCharacter(value)
                ? value
                : winrt::hstring{};
        }

        winrt::hstring BuildAuthorizeUriUnchecked(
            AccountClient const& client,
            winrt::hstring const& redirectUri,
            PkceTransaction const& transaction)
        {
            std::wstring uri{ client.BaseOrigin().c_str() };
            uri += L"/v1/sso/authorize?client_id=";
            uri += BuildConfig::DesktopClientId;
            uri += L"&redirect_uri=";
            uri += winrt::Windows::Foundation::Uri::EscapeComponent(redirectUri).c_str();
            uri += L"&state=";
            uri += winrt::Windows::Foundation::Uri::EscapeComponent(transaction.State).c_str();
            uri += L"&code_challenge=";
            uri += winrt::Windows::Foundation::Uri::EscapeComponent(transaction.Challenge).c_str();
            uri += L"&code_challenge_method=S256";
            return winrt::hstring{ uri };
        }

        class AccountSessionGateway final : public IAccountSessionGateway
        {
        public:
            explicit AccountSessionGateway(AccountClient& client) noexcept
                : m_client(client)
            {
            }

            bool IsConfigured() const noexcept override
            {
                return m_client.IsConfigured();
            }

            winrt::Windows::Foundation::IAsyncOperation<winrt::hstring>
                AcquireBearerSessionAsync(std::uint64_t intent) override
            {
                if (!IsConfigured())
                {
                    throw winrt::hresult_error(
                        E_NOT_VALID_STATE,
                        L"Account integration is unavailable in this build.");
                }

                auto callback = std::make_shared<LoopbackCallback>();
                if (!callback->Start())
                {
                    throw winrt::hresult_error(
                        E_FAIL,
                        L"Could not start the secure sign-in callback.");
                }

                std::shared_ptr<LoopbackCallback> previous;
                bool superseded{};
                {
                    std::lock_guard guard{ m_mutex };
                    if (m_activeCallback && m_activeIntent > intent)
                    {
                        superseded = true;
                    }
                    else
                    {
                        previous = std::exchange(m_activeCallback, callback);
                        m_activeIntent = intent;
                    }
                }
                if (superseded)
                {
                    callback->Cancel();
                    throw winrt::hresult_canceled();
                }
                if (previous)
                {
                    previous->Cancel();
                }

                try
                {
                    auto transaction = CreatePkceTransaction();
                    auto redirectUri = callback->RedirectUri();
                    auto authorizeUri = BuildAccountAuthorizeUri(
                        m_client,
                        redirectUri,
                        transaction);
                    if (authorizeUri.empty())
                    {
                        throw winrt::hresult_error(
                            E_FAIL,
                            L"Account sign-in configuration is invalid.");
                    }
                    auto launched = co_await winrt::Windows::System::Launcher::LaunchUriAsync(
                        winrt::Windows::Foundation::Uri{ authorizeUri });
                    if (!launched)
                    {
                        callback->Cancel();
                        throw winrt::hresult_error(E_FAIL, L"Could not open the system browser.");
                    }

                    co_await winrt::resume_background();
                    auto result = callback->WaitForCallback(
                        transaction.State,
                        std::chrono::steady_clock::now() + std::chrono::minutes(10));
                    ReleaseCallback(intent, callback);
                    if (result.Status != LoopbackCallbackStatus::Success)
                    {
                        if (result.Status == LoopbackCallbackStatus::Canceled)
                        {
                            throw winrt::hresult_canceled();
                        }
                        throw winrt::hresult_error(
                            result.Status == LoopbackCallbackStatus::TimedOut ? HRESULT_FROM_WIN32(WAIT_TIMEOUT) : E_FAIL,
                            result.Status == LoopbackCallbackStatus::TimedOut
                                ? L"Account sign-in timed out."
                                : L"Account sign-in callback was not accepted.");
                    }

                    auto exchange = co_await m_client.ExchangeCodeAsync(
                        result.Code,
                        redirectUri,
                        transaction.Verifier);
                    auto bearer = ParseAccountBearerSession(exchange);
                    if (bearer.empty())
                    {
                        throw winrt::hresult_error(
                            E_FAIL,
                            L"Account sign-in did not return a valid session.");
                    }
                    co_return bearer;
                }
                catch (...)
                {
                    ReleaseCallback(intent, callback);
                    throw;
                }
            }

            void CancelAcquireBearerSession(std::uint64_t throughIntent) noexcept override
            {
                std::shared_ptr<LoopbackCallback> callback;
                {
                    std::lock_guard guard{ m_mutex };
                    if (m_activeCallback && m_activeIntent <= throughIntent)
                    {
                        callback = m_activeCallback;
                    }
                }
                if (callback)
                {
                    callback->Cancel();
                }
            }

            winrt::Windows::Foundation::IAsyncAction GetProfileAsync(
                winrt::hstring const& bearerSession,
                std::function<void(AccountProfile const&)> receiveProfile) override
            {
                auto payload = co_await m_client.GetMeAsync(bearerSession);
                auto profile = ParseAccountProfile(payload);
                if (profile.Id.empty())
                {
                    throw winrt::hresult_error(E_FAIL, L"Could not validate the account session.");
                }
                if (!receiveProfile)
                {
                    throw winrt::hresult_error(E_INVALIDARG, L"Account profile receiver is required.");
                }
                receiveProfile(profile);
            }

            winrt::Windows::Foundation::IAsyncAction LogoutAsync(
                winrt::hstring const& bearerSession) override
            {
                co_await m_client.LogoutAsync(bearerSession);
            }

        private:
            void ReleaseCallback(
                std::uint64_t intent,
                std::shared_ptr<LoopbackCallback> const& callback) noexcept
            {
                std::lock_guard guard{ m_mutex };
                if (m_activeIntent == intent && m_activeCallback == callback)
                {
                    m_activeCallback.reset();
                    m_activeIntent = 0;
                }
            }

            AccountClient& m_client;
            std::mutex m_mutex;
            std::shared_ptr<LoopbackCallback> m_activeCallback;
            std::uint64_t m_activeIntent{};
        };
    }

    winrt::hstring BuildAccountAuthorizeUri(
        AccountClient const& client,
        winrt::hstring const& redirectUri,
        PkceTransaction const& transaction)
    {
        if (!client.IsConfigured()
            || redirectUri.empty()
            || transaction.State.empty()
            || transaction.Challenge.empty())
        {
            return {};
        }
        return BuildAuthorizeUriUnchecked(
            client,
            redirectUri,
            transaction);
    }

    winrt::hstring ParseAccountBearerSession(winrt::hstring const& payload)
    {
        constexpr std::size_t MaxBearerCharacters = 16 * 1024;

        winrt::Windows::Data::Json::JsonObject root{ nullptr };
        if (!winrt::Windows::Data::Json::JsonObject::TryParse(payload, root)
            || !root)
        {
            return {};
        }

        if (root.HasKey(L"token_type"))
        {
            auto tokenTypeValue = root.GetNamedValue(L"token_type");
            if (tokenTypeValue.ValueType()
                    != winrt::Windows::Data::Json::JsonValueType::String
                || !EqualsAsciiCaseInsensitive(
                    tokenTypeValue.GetString(),
                    L"Bearer"))
            {
                return {};
            }
        }

        auto bearer = JsonString(root, L"access_token");
        if (bearer.empty()
            || bearer.size() > MaxBearerCharacters
            || ContainsControlCharacter(bearer)
            || !IsValidBearerToken(bearer))
        {
            return {};
        }
        return bearer;
    }

    AccountProfile ParseAccountProfile(winrt::hstring const& payload)
    {
        constexpr std::size_t MaxOwnerIdCharacters = 256;
        constexpr std::size_t MaxDisplayNameCharacters = 256;
        constexpr std::size_t MaxUsernameCharacters = 128;
        constexpr std::size_t MaxPlanCharacters = 128;

        AccountProfile profile;
        winrt::Windows::Data::Json::JsonObject root{ nullptr };
        if (!winrt::Windows::Data::Json::JsonObject::TryParse(payload, root)
            || !root)
        {
            return profile;
        }

        auto user = root;
        if (root.HasKey(L"user"))
        {
            auto value = root.GetNamedValue(L"user");
            if (value.ValueType()
                != winrt::Windows::Data::Json::JsonValueType::Object)
            {
                return profile;
            }
            user = value.GetObject();
        }

        profile.Id = BoundedProfileText(
            JsonString(user, L"id"),
            MaxOwnerIdCharacters);
        if (profile.Id.empty()
            || std::any_of(profile.Id.begin(), profile.Id.end(), [](wchar_t character)
            {
                return std::iswspace(character) != 0;
            }))
        {
            return {};
        }

        profile.DisplayName = BoundedProfileText(
            JsonString(user, L"displayName"),
            MaxDisplayNameCharacters);
        if (profile.DisplayName.empty())
        {
            profile.DisplayName = BoundedProfileText(
                JsonString(user, L"fullName"),
                MaxDisplayNameCharacters);
        }
        profile.Username = BoundedProfileText(
            JsonString(user, L"username"),
            MaxUsernameCharacters);
        profile.PlanLabel = BoundedProfileText(
            JsonString(user, L"plan"),
            MaxPlanCharacters);

        // The account API sends the picture either as a URL to fetch or as
        // the image itself inlined in a base64 data URI, so both forms are
        // accepted, each against its own check.
        auto avatarUrl = JsonString(user, L"avatarUrl");
        if (IsSafeAccountProfileUrl(avatarUrl) || IsSafeInlineProfileImage(avatarUrl))
        {
            profile.AvatarUrl = avatarUrl;
        }
        return profile;
    }

    std::unique_ptr<IAccountSessionGateway> CreateAccountSessionGateway(
        AccountClient& client)
    {
        return std::make_unique<AccountSessionGateway>(client);
    }
}
