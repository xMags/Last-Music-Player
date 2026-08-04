#include "pch.h"

#include "Backend/ProfileIdentity.h"

#include <cwctype>
#include <string>

namespace LastMusicPlayer::Backend
{
    namespace
    {
        bool IsSignedIn(AccountSessionStatus status) noexcept
        {
            // Offline counts: the cached profile is the one the user last
            // signed in as, and the account library is still what plays.
            return status == AccountSessionStatus::Validated
                || status == AccountSessionStatus::Offline;
        }
    }

    winrt::hstring TrimProfileName(winrt::hstring const& value)
    {
        std::wstring text{ value.c_str() };
        while (!text.empty() && std::iswspace(text.front()))
        {
            text.erase(text.begin());
        }
        while (!text.empty() && std::iswspace(text.back()))
        {
            text.pop_back();
        }
        return winrt::hstring{ text };
    }

    ProfileIdentity ChooseProfileIdentity(
        RemoteAccessMode mode,
        AccountSessionStatus status,
        AccountProfile const& profile,
        winrt::hstring const& manualName)
    {
        auto manual = TrimProfileName(manualName);

        if (mode != RemoteAccessMode::Account || !IsSignedIn(status))
        {
            return ProfileIdentity{
                manual.empty() ? winrt::hstring{ kDefaultListenerName } : manual,
                winrt::hstring{},
                winrt::hstring{},
                ProfileIdentitySource::Manual
            };
        }

        // A signed-in account can still be missing the parts the server treats
        // as optional, so each one falls back independently rather than
        // dropping the whole identity back to manual.
        auto name = TrimProfileName(profile.DisplayName);
        if (name.empty())
        {
            name = TrimProfileName(profile.Username);
        }
        if (name.empty())
        {
            name = manual;
        }

        return ProfileIdentity{
            name.empty() ? winrt::hstring{ kDefaultListenerName } : name,
            profile.AvatarUrl,
            TrimProfileName(profile.PlanLabel),
            ProfileIdentitySource::Account
        };
    }
}
