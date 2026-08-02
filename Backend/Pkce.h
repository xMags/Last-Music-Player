#pragma once

#include <winrt/Windows.Foundation.h>

namespace LastMusicPlayer::Backend
{
    struct PkceTransaction
    {
        winrt::hstring Verifier;
        winrt::hstring Challenge;
        winrt::hstring State;
    };

    winrt::hstring CreatePkceChallenge(winrt::hstring const& verifier);
    PkceTransaction CreatePkceTransaction();
}
