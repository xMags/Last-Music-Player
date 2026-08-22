#pragma once

// Account service origins are supplied by configured builds through MSBuild
// properties. Empty defaults leave AccountClient unconfigured and the account
// UI reports unavailable.
#ifndef LAST_MUSIC_ACCOUNT_API_ORIGIN
#define LAST_MUSIC_ACCOUNT_API_ORIGIN L""
#endif

#ifndef LAST_MUSIC_ACCOUNT_FRONTEND_ORIGIN
#define LAST_MUSIC_ACCOUNT_FRONTEND_ORIGIN L""
#endif

#ifndef LAST_MUSIC_ACCOUNT_MEDIA_ORIGIN
#define LAST_MUSIC_ACCOUNT_MEDIA_ORIGIN LAST_MUSIC_ACCOUNT_API_ORIGIN
#endif

namespace LastMusicPlayer::Backend::BuildConfig
{
    inline constexpr wchar_t AccountApiOrigin[] = LAST_MUSIC_ACCOUNT_API_ORIGIN;
    inline constexpr wchar_t AccountFrontendOrigin[] = LAST_MUSIC_ACCOUNT_FRONTEND_ORIGIN;
    inline constexpr wchar_t AccountMediaOrigin[] = LAST_MUSIC_ACCOUNT_MEDIA_ORIGIN;
    inline constexpr wchar_t DesktopClientId[] = L"last-music-desktop";
    inline constexpr wchar_t DesktopRequestedWith[] = L"last-music-desktop";
    inline constexpr wchar_t DesktopCallbackPath[] = L"/callback";
}
