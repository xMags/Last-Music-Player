// Creation, destruction and event wiring of the Windows audio player, and the
// media helpers its commands share. The commands and the state they drive
// are in WindowsAudioPlayer.cpp.

#include "platform/windows/WindowsAudioPlayerNative.h"

#include <QLatin1StringView>
#include <QMetaObject>
#include <QUrl>

#include <chrono>
#include <cmath>
#include <cstdint>

#include <windows.h>
#include <audioclient.h>
#include <mferror.h>

namespace lmp::platform::windows {

namespace {

namespace wf = winrt::Windows::Foundation;
namespace wmc = winrt::Windows::Media::Core;
namespace wmp = winrt::Windows::Media::Playback;

// The load id stored on the player's current source, or 0 when it has none.
// Called on the player's event threads; the player is free-threaded.
[[nodiscard]] quint64 loadIdOf(const wmp::MediaPlayer& player) noexcept
{
    try {
        const wmc::MediaSource source = player.Source().try_as<wmc::MediaSource>();
        if (!source) {
            return 0;
        }
        const wf::IInspectable value = source.CustomProperties().TryLookup(media::LoadIdProperty);
        return winrt::unbox_value_or<std::uint64_t>(value, 0);
    } catch (...) {
        return 0;
    }
}

// The system reports a missing, busy or vanished output device through the
// extended code; the player's own error then says little more than "unknown".
[[nodiscard]] bool isOutputDeviceFailure(HRESULT code) noexcept
{
    switch (code) {
    case MF_E_NO_AUDIO_PLAYBACK_DEVICE:
    case MF_E_AUDIO_PLAYBACK_DEVICE_IN_USE:
    case MF_E_AUDIO_PLAYBACK_DEVICE_INVALIDATED:
    case AUDCLNT_E_DEVICE_INVALIDATED:
    case AUDCLNT_E_DEVICE_IN_USE:
    case AUDCLNT_E_ENDPOINT_CREATE_FAILED:
    case AUDCLNT_E_SERVICE_NOT_RUNNING:
        return true;
    default:
        return false;
    }
}

[[nodiscard]] IAudioPlayer::ErrorKind classifyFailure(wmp::MediaPlayerError error,
                                                      HRESULT extendedCode) noexcept
{
    if (isOutputDeviceFailure(extendedCode)) {
        return IAudioPlayer::ErrorKind::OutputDevice;
    }
    switch (error) {
    case wmp::MediaPlayerError::Aborted: return IAudioPlayer::ErrorKind::Aborted;
    case wmp::MediaPlayerError::NetworkError: return IAudioPlayer::ErrorKind::Network;
    case wmp::MediaPlayerError::DecodingError: return IAudioPlayer::ErrorKind::Decode;
    case wmp::MediaPlayerError::SourceNotSupported: return IAudioPlayer::ErrorKind::SourceNotSupported;
    case wmp::MediaPlayerError::Unknown: break;
    }
    return IAudioPlayer::ErrorKind::Unknown;
}

// Posts `action` to the owner's thread. The lock only spans the post itself,
// which queues an event and calls no other code. A posted event that has not
// run when the owner is destroyed is discarded with it.
template <typename Action>
void postToOwner(const std::shared_ptr<AudioPlayerEventBridge>& bridge, Action action)
{
    const std::lock_guard<std::mutex> lock(bridge->mutex);
    WindowsAudioPlayer* owner = bridge->owner;
    if (owner == nullptr) {
        return;
    }
    // Fails only when the owner's thread has no event loop, in which case
    // there is nobody to tell.
    const bool posted = QMetaObject::invokeMethod(
        owner, [owner, action = std::move(action)]() { action(*owner); }, Qt::QueuedConnection);
    static_cast<void>(posted);
}

} // namespace

namespace media {

double toSeconds(wf::TimeSpan span) noexcept
{
    const double seconds = std::chrono::duration<double>(span).count();
    return std::isfinite(seconds) && seconds > 0.0 ? seconds : 0.0;
}

wf::TimeSpan toTimeSpan(double seconds)
{
    return std::chrono::duration_cast<wf::TimeSpan>(std::chrono::duration<double>(seconds));
}

double durationOf(const wmp::MediaPlaybackSession& session) noexcept
{
    try {
        return toSeconds(session.NaturalDuration());
    } catch (...) {
        return 0.0;
    }
}

double positionOf(const wmp::MediaPlaybackSession& session) noexcept
{
    try {
        return toSeconds(session.Position());
    } catch (...) {
        return 0.0;
    }
}

// The address the player is given: local files as file addresses, remote
// sources as they are. Anything else is refused.
std::optional<QString> playerAddress(const QUrl& source)
{
    if (!source.isValid()) {
        return std::nullopt;
    }
    if (source.isLocalFile()) {
        const QString path = source.toLocalFile();
        if (path.isEmpty()) {
            return std::nullopt;
        }
        return QUrl::fromLocalFile(path).toString(QUrl::FullyEncoded);
    }
    const QString scheme = source.scheme().toLower();
    if ((scheme != u"http" && scheme != u"https") || source.host().isEmpty()) {
        return std::nullopt;
    }
    return source.toString(QUrl::FullyEncoded);
}

QString describeFailure(const char* call, winrt::hresult code)
{
    return QStringLiteral("%1 failed with result 0x%2")
        .arg(QLatin1StringView(call))
        .arg(static_cast<quint32>(code.value), 8, 16, QLatin1Char('0'));
}

} // namespace media

WindowsAudioPlayer::Native::~Native()
{
    {
        const std::lock_guard<std::mutex> lock(bridge->mutex);
        bridge->owner = nullptr;
    }
    // Revoked before the player goes, so no new event reaches the bridge.
    opened.revoke();
    ended.revoke();
    failed.revoke();
    sessionState.revoke();
    sessionDuration.revoke();
    if (!player) {
        return;
    }
    // Shutting down: a player that fails to close has nobody left to tell.
    const std::optional<QString> closeFailure = media::runPlayerCall("Close", [this]() {
        player.Pause();
        player.Source(nullptr);
        player.Close();
    });
    static_cast<void>(closeFailure);
}

WindowsAudioPlayer::WindowsAudioPlayer(QObject* parent)
    : IAudioPlayer(parent), m_native(std::make_unique<Native>())
{
    m_positionTimer.setInterval(media::PositionInterval);
    connect(&m_positionTimer, &QTimer::timeout, this, &WindowsAudioPlayer::reportPosition);

    const std::optional<QString> failure = media::runPlayerCall("MediaPlayer", [this]() {
        wmp::MediaPlayer player;
        // The app drives playback itself and owns the window's media
        // session; the player's own command handling would add a second one.
        player.CommandManager().IsEnabled(false);
        // Playback starts only when asked for (open with autoplay, or play).
        player.AutoPlay(false);
        player.AudioDeviceType(wmp::MediaPlayerAudioDeviceType::Multimedia);
        m_native->session = player.PlaybackSession();
        m_native->player = std::move(player);
    });
    if (failure) {
        // Without a player every open reports an error; see openSource().
        m_native->player = nullptr;
        m_native->session = nullptr;
        return;
    }
    m_native->bridge->owner = this;
    connectPlayerEvents();
}

WindowsAudioPlayer::~WindowsAudioPlayer() = default;

void WindowsAudioPlayer::connectPlayerEvents()
{
    Native& native = *m_native;
    const std::shared_ptr<AudioPlayerEventBridge> bridge = native.bridge;

    // Every handler runs on a player thread. It reads what it needs there,
    // tagged with the load id of the source it is about, and hands over.
    native.opened = native.player.MediaOpened(
        winrt::auto_revoke, [bridge](const wmp::MediaPlayer& sender, const wf::IInspectable&) {
            const quint64 loadId = loadIdOf(sender);
            const double duration = media::durationOf(sender.PlaybackSession());
            postToOwner(bridge, [loadId, duration](WindowsAudioPlayer& owner) {
                owner.handleOpened(loadId, duration);
            });
        });
    native.ended = native.player.MediaEnded(
        winrt::auto_revoke, [bridge](const wmp::MediaPlayer& sender, const wf::IInspectable&) {
            const quint64 loadId = loadIdOf(sender);
            const double duration = media::durationOf(sender.PlaybackSession());
            postToOwner(bridge, [loadId, duration](WindowsAudioPlayer& owner) {
                owner.handleEnded(loadId, duration);
            });
        });
    native.failed = native.player.MediaFailed(
        winrt::auto_revoke,
        [bridge](const wmp::MediaPlayer& sender, const wmp::MediaPlayerFailedEventArgs& args) {
            const quint64 loadId = loadIdOf(sender);
            const wmp::MediaPlayerError error = args.Error();
            const HRESULT extended = args.ExtendedErrorCode().value;
            const ErrorKind kind = classifyFailure(error, extended);
            const QString detail = QStringLiteral("player error %1, result 0x%2")
                                       .arg(static_cast<int>(error))
                                       .arg(static_cast<quint32>(extended), 8, 16, QLatin1Char('0'));
            postToOwner(bridge, [loadId, kind, detail](WindowsAudioPlayer& owner) {
                owner.handleFailed(loadId, kind, detail);
            });
        });
    native.sessionState = native.session.PlaybackStateChanged(
        winrt::auto_revoke,
        [bridge](const wmp::MediaPlaybackSession& sender, const wf::IInspectable&) {
            const quint64 loadId = loadIdOf(sender.MediaPlayer());
            const double position = media::positionOf(sender);
            SessionState state = SessionState::None;
            switch (sender.PlaybackState()) {
            case wmp::MediaPlaybackState::Opening: state = SessionState::Opening; break;
            case wmp::MediaPlaybackState::Buffering: state = SessionState::Buffering; break;
            case wmp::MediaPlaybackState::Playing: state = SessionState::Playing; break;
            case wmp::MediaPlaybackState::Paused: state = SessionState::Paused; break;
            case wmp::MediaPlaybackState::None: break;
            }
            postToOwner(bridge, [loadId, state, position](WindowsAudioPlayer& owner) {
                owner.handleSessionState(loadId, state, position);
            });
        });
    native.sessionDuration = native.session.NaturalDurationChanged(
        winrt::auto_revoke,
        [bridge](const wmp::MediaPlaybackSession& sender, const wf::IInspectable&) {
            const quint64 loadId = loadIdOf(sender.MediaPlayer());
            const double duration = media::durationOf(sender);
            postToOwner(bridge, [loadId, duration](WindowsAudioPlayer& owner) {
                owner.handleDuration(loadId, duration);
            });
        });
}

} // namespace lmp::platform::windows
