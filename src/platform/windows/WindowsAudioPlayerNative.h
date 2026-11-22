#pragma once

// Internal to the Windows audio player: the Windows media player objects
// behind WindowsAudioPlayer. Only its own source files include this, so the
// rest of the project never sees a Windows Runtime header.

#include "platform/windows/WindowsAudioPlayer.h"

#include <QString>
#include <QUrl>

#include <chrono>
#include <memory>
#include <mutex>
#include <optional>
#include <utility>

// The Windows SDK projection headers are not warning-clean at the level this
// project builds with; silence them here only, not for the code around them.
#pragma warning(push, 0)
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Media.Core.h>
#include <winrt/Windows.Media.Playback.h>
#pragma warning(pop)

namespace lmp::platform::windows {

// Lets player event handlers reach the player object without outliving it.
// A handler may be running on a player thread at the very moment the object
// is destroyed (revoking a handler does not wait for a call already in
// progress), so handlers reach the object only through this, under its lock.
struct AudioPlayerEventBridge {
    std::mutex mutex;
    WindowsAudioPlayer* owner = nullptr;
};

struct WindowsAudioPlayer::Native {
    Native() = default;
    ~Native();

    Native(const Native&) = delete;
    Native& operator=(const Native&) = delete;

    // Null when the system could not create a player.
    winrt::Windows::Media::Playback::MediaPlayer player{nullptr};
    winrt::Windows::Media::Playback::MediaPlaybackSession session{nullptr};
    std::shared_ptr<AudioPlayerEventBridge> bridge = std::make_shared<AudioPlayerEventBridge>();
    winrt::Windows::Media::Playback::MediaPlayer::MediaOpened_revoker opened;
    winrt::Windows::Media::Playback::MediaPlayer::MediaEnded_revoker ended;
    winrt::Windows::Media::Playback::MediaPlayer::MediaFailed_revoker failed;
    winrt::Windows::Media::Playback::MediaPlaybackSession::PlaybackStateChanged_revoker sessionState;
    winrt::Windows::Media::Playback::MediaPlaybackSession::NaturalDurationChanged_revoker
        sessionDuration;
};

namespace media {

// Interval of the position report while playing, as the earlier versions of
// the app polled it.
inline constexpr std::chrono::milliseconds PositionInterval{500};

// Stored on each media source so an event can be matched to the load that
// opened it, whatever source the player holds by the time it is handled.
inline constexpr wchar_t LoadIdProperty[] = L"LastMusicPlayer.LoadId";

// Seconds in a player time span; 0 for anything negative or not finite.
[[nodiscard]] double toSeconds(winrt::Windows::Foundation::TimeSpan span) noexcept;
[[nodiscard]] winrt::Windows::Foundation::TimeSpan toTimeSpan(double seconds);

// Read from a session on any thread; 0 when the session cannot say.
[[nodiscard]] double durationOf(
    const winrt::Windows::Media::Playback::MediaPlaybackSession& session) noexcept;
[[nodiscard]] double positionOf(
    const winrt::Windows::Media::Playback::MediaPlaybackSession& session) noexcept;

// The address the player is given: local files as file addresses, remote
// http(s) sources as they are. Nothing for anything else.
[[nodiscard]] std::optional<QString> playerAddress(const QUrl& source);

[[nodiscard]] QString describeFailure(const char* call, winrt::hresult code);

// Runs one player call. Returns a log detail when it threw, nothing otherwise.
template <typename Call>
[[nodiscard]] std::optional<QString> runPlayerCall(const char* name, Call&& call)
{
    try {
        std::forward<Call>(call)();
        return std::nullopt;
    } catch (...) {
        return describeFailure(name, winrt::to_hresult());
    }
}

} // namespace media

} // namespace lmp::platform::windows
