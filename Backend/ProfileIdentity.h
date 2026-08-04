#pragma once

#include "Backend/AccountSessionGateway.h"
#include "Backend/AccountSessionService.h"
#include "Backend/RemoteMusicService.h"

#include <winrt/Windows.Foundation.h>

namespace LastMusicPlayer::Backend
{
    // Shown wherever the app has no name of its own to use.
    inline constexpr wchar_t const* kDefaultListenerName = L"Listener";

    enum class ProfileIdentitySource
    {
        // The name the user typed into Settings. No avatar, no plan.
        Manual,

        // The MagnetoFX profile behind the active account session.
        Account,
    };

    struct ProfileIdentity
    {
        winrt::hstring Name;
        winrt::hstring AvatarUrl;
        winrt::hstring PlanLabel;
        ProfileIdentitySource Source{ ProfileIdentitySource::Manual };
    };

    // Which identity the whole app presents: sidebar, greeting, "Made for X",
    // and the Settings identity card.
    //
    // The account only owns the identity while the user has actually chosen to
    // run on it and a session backs that choice; every other combination falls
    // back to the manually typed name. The manual name is never overwritten
    // here, so leaving and re-entering account mode restores it verbatim.
    ProfileIdentity ChooseProfileIdentity(
        RemoteAccessMode mode,
        AccountSessionStatus status,
        AccountProfile const& profile,
        winrt::hstring const& manualName);

    // Surrounding whitespace trimmed; empty when nothing is left.
    winrt::hstring TrimProfileName(winrt::hstring const& value);
}
