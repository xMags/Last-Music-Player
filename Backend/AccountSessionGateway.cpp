#include "pch.h"
#include "Backend/AccountSessionGateway.h"

#include "Backend/AccountClient.h"
#include "Backend/BuildConfig.h"
#include "Backend/LoopbackCallback.h"
#include "Backend/Pkce.h"

#include <chrono>
#include <mutex>
#include <string>
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

        winrt::hstring AuthorizeUri(
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
                    auto launched = co_await winrt::Windows::System::Launcher::LaunchUriAsync(
                        winrt::Windows::Foundation::Uri{
                            AuthorizeUri(m_client, redirectUri, transaction)
                        });
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
                    winrt::Windows::Data::Json::JsonObject root{ nullptr };
                    if (!winrt::Windows::Data::Json::JsonObject::TryParse(exchange, root) || !root)
                    {
                        throw winrt::hresult_error(E_FAIL, L"Account sign-in returned an invalid response.");
                    }
                    auto bearer = JsonString(root, L"access_token");
                    if (bearer.empty())
                    {
                        throw winrt::hresult_error(E_FAIL, L"Account sign-in did not return a session.");
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

    AccountProfile ParseAccountProfile(winrt::hstring const& payload)
    {
        AccountProfile profile;
        winrt::Windows::Data::Json::JsonObject root{ nullptr };
        if (!winrt::Windows::Data::Json::JsonObject::TryParse(payload, root) || !root)
        {
            return profile;
        }

        auto user = root;
        if (root.HasKey(L"user"))
        {
            auto value = root.GetNamedValue(L"user");
            if (value.ValueType() != winrt::Windows::Data::Json::JsonValueType::Object)
            {
                return profile;
            }
            user = value.GetObject();
        }

        profile.Id = JsonString(user, L"id");
        profile.DisplayName = JsonString(user, L"displayName");
        if (profile.DisplayName.empty())
        {
            profile.DisplayName = JsonString(user, L"fullName");
        }
        profile.Username = JsonString(user, L"username");
        profile.AvatarUrl = JsonString(user, L"avatarUrl");
        profile.PlanLabel = JsonString(user, L"plan");
        return profile;
    }

    std::unique_ptr<IAccountSessionGateway> CreateAccountSessionGateway(
        AccountClient& client)
    {
        return std::make_unique<AccountSessionGateway>(client);
    }
}
