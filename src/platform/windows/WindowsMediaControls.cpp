#include "platform/windows/WindowsMediaControls.h"

#include <QLoggingCategory>
#include <QMetaObject>
#include <QString>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <memory>
#include <mutex>
#include <optional>
#include <utility>

#include <windows.h>
#include <systemmediatransportcontrolsinterop.h>

// The Windows SDK projection headers are not warning-clean at the level this
// project builds with; silence them here only, not for the code around them.
#pragma warning(push, 0)
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Media.h>
#include <winrt/Windows.Storage.FileProperties.h>
#include <winrt/Windows.Storage.Streams.h>
#include <winrt/Windows.Storage.h>
#pragma warning(pop)

namespace lmp::platform::windows {

namespace {

Q_LOGGING_CATEGORY(lcMediaControls, "lmp.platform.mediacontrols")

namespace wf = winrt::Windows::Foundation;
namespace wm = winrt::Windows::Media;
namespace wst = winrt::Windows::Storage;
namespace wsfp = winrt::Windows::Storage::FileProperties;
namespace wss = winrt::Windows::Storage::Streams;

// Edge of the cover thumbnail asked of the system, in pixels.
constexpr std::uint32_t ThumbnailSize = 1024;

// Lets system callbacks reach the controls object without outliving it. A
// callback may be running on a system thread at the very moment the object
// is destroyed, so callbacks reach it only through this, under its lock.
struct MediaControlsEventBridge {
    std::mutex mutex;
    WindowsMediaControls* owner = nullptr;
};

// Posts `action` to the owner's thread. The lock only spans the post itself,
// which queues an event and calls no other code. A posted event that has not
// run when the owner is destroyed is discarded with it.
template <typename Action>
void postToOwner(const std::shared_ptr<MediaControlsEventBridge>& bridge, Action action)
{
    const std::lock_guard<std::mutex> lock(bridge->mutex);
    WindowsMediaControls* owner = bridge->owner;
    if (owner == nullptr) {
        return;
    }
    // Fails only when the owner's thread has no event loop, in which case
    // there is nobody to tell.
    const bool posted = QMetaObject::invokeMethod(
        owner, [owner, action = std::move(action)]() { action(*owner); }, Qt::QueuedConnection);
    static_cast<void>(posted);
}

// Runs one call on the system controls and logs it when it throws. Returns
// whether it went through.
template <typename Call>
bool runControlsCall(const char* name, Call&& call)
{
    try {
        std::forward<Call>(call)();
        return true;
    } catch (...) {
        qCWarning(lcMediaControls) << "System media controls call" << name << "failed, HRESULT"
                                   << Qt::hex << static_cast<quint32>(winrt::to_hresult().value);
        return false;
    }
}

[[nodiscard]] std::optional<IMediaControls::MediaCommand> commandFor(
    wm::SystemMediaTransportControlsButton button) noexcept
{
    switch (button) {
    case wm::SystemMediaTransportControlsButton::Play: return IMediaControls::MediaCommand::Play;
    case wm::SystemMediaTransportControlsButton::Pause: return IMediaControls::MediaCommand::Pause;
    case wm::SystemMediaTransportControlsButton::Stop: return IMediaControls::MediaCommand::Stop;
    case wm::SystemMediaTransportControlsButton::Next: return IMediaControls::MediaCommand::Next;
    case wm::SystemMediaTransportControlsButton::Previous: return IMediaControls::MediaCommand::Previous;
    default: return std::nullopt;
    }
}

[[nodiscard]] wm::MediaPlaybackStatus systemStatus(IMediaControls::PlaybackStatus status) noexcept
{
    switch (status) {
    case IMediaControls::PlaybackStatus::Closed: return wm::MediaPlaybackStatus::Closed;
    case IMediaControls::PlaybackStatus::Changing: return wm::MediaPlaybackStatus::Changing;
    case IMediaControls::PlaybackStatus::Stopped: return wm::MediaPlaybackStatus::Stopped;
    case IMediaControls::PlaybackStatus::Playing: return wm::MediaPlaybackStatus::Playing;
    case IMediaControls::PlaybackStatus::Paused: return wm::MediaPlaybackStatus::Paused;
    }
    return wm::MediaPlaybackStatus::Stopped;
}

[[nodiscard]] wf::TimeSpan toTimeSpan(double seconds)
{
    return std::chrono::duration_cast<wf::TimeSpan>(std::chrono::duration<double>(seconds));
}

} // namespace

struct WindowsMediaControls::Native {
    Native() = default;
    ~Native()
    {
        {
            const std::lock_guard<std::mutex> lock(bridge->mutex);
            bridge->owner = nullptr;
        }
        buttonPressed.revoke();
    }

    Native(const Native&) = delete;
    Native& operator=(const Native&) = delete;

    // Asks the system for the music thumbnail of `path` on its thread pool
    // and hands it to the owner's thread, where it is shown only while
    // `generation` is still the current metadata. Owned by nobody: once
    // started it runs to its end, and its result is dropped when a newer
    // song came first or the owner is gone (the bridge is detached then).
    static winrt::fire_and_forget loadThumbnail(std::shared_ptr<MediaControlsEventBridge> bridge,
                                                quint64 generation, winrt::hstring path);
    static void showThumbnail(WindowsMediaControls& owner, quint64 generation,
                              const wss::RandomAccessStreamReference& thumbnail);

    // Null until attach() succeeds.
    wm::SystemMediaTransportControls controls{nullptr};
    wm::SystemMediaTransportControls::ButtonPressed_revoker buttonPressed;
    std::shared_ptr<MediaControlsEventBridge> bridge = std::make_shared<MediaControlsEventBridge>();
    // The current song's cover; null for none or until it is ready.
    wss::RandomAccessStreamReference thumbnail{nullptr};
};

winrt::fire_and_forget WindowsMediaControls::Native::loadThumbnail(
    std::shared_ptr<MediaControlsEventBridge> bridge, quint64 generation, winrt::hstring path)
{
    wss::RandomAccessStreamReference reference{nullptr};
    try {
        co_await winrt::resume_background();
        const wst::StorageFile file = co_await wst::StorageFile::GetFileFromPathAsync(path);
        const wsfp::StorageItemThumbnail picture = co_await file.GetThumbnailAsync(
            wsfp::ThumbnailMode::MusicView, ThumbnailSize, wsfp::ThumbnailOptions::ResizeThumbnail);
        // A file without a picture of its own gets an icon; none is shown then.
        if (picture && picture.Type() == wsfp::ThumbnailType::Image) {
            reference = wss::RandomAccessStreamReference::CreateFromStream(picture);
        }
    } catch (...) {
        reference = nullptr; // a missing or unreadable file shows no cover
    }
    try {
        postToOwner(bridge, [generation, reference](WindowsMediaControls& owner) {
            showThumbnail(owner, generation, reference);
        });
    } catch (...) {
        // Only an allocation failure lands here; the cover is then not shown.
    }
}

void WindowsMediaControls::Native::showThumbnail(WindowsMediaControls& owner, quint64 generation,
                                                 const wss::RandomAccessStreamReference& thumbnail)
{
    if (generation != owner.m_metadataGeneration) {
        return;
    }
    owner.m_native->thumbnail = thumbnail;
    owner.applyMetadata();
}

WindowsMediaControls::WindowsMediaControls(QObject* parent)
    : IMediaControls(parent)
    , m_native(std::make_unique<Native>())
{
    m_native->bridge->owner = this;
}

WindowsMediaControls::~WindowsMediaControls()
{
    // Nothing reaches this object from a system thread after this.
    {
        const std::lock_guard<std::mutex> lock(m_native->bridge->mutex);
        m_native->bridge->owner = nullptr;
    }
    if (m_native->controls) {
        m_native->buttonPressed.revoke();
        static_cast<void>(runControlsCall("IsEnabled", [this]() { m_native->controls.IsEnabled(false); }));
    }
}

bool WindowsMediaControls::attach(quintptr windowHandle)
{
    if (m_native->controls || windowHandle == 0) {
        return false;
    }
    HWND window = reinterpret_cast<HWND>(windowHandle); // a WId is the HWND on Windows
    wm::SystemMediaTransportControls controls{nullptr};
    const bool obtained = runControlsCall("GetForWindow", [window, &controls]() {
        const auto factory = winrt::get_activation_factory<wm::SystemMediaTransportControls,
                                                           ISystemMediaTransportControlsInterop>();
        winrt::check_hresult(factory->GetForWindow(window, winrt::guid_of<wm::SystemMediaTransportControls>(),
                                                   winrt::put_abi(controls)));
    });
    if (!obtained || !controls) {
        return false;
    }
    const bool configured = runControlsCall("Configure", [&controls]() {
        controls.IsPlayEnabled(true);
        controls.IsPauseEnabled(true);
        controls.IsNextEnabled(true);
        controls.IsPreviousEnabled(true);
    });
    if (!configured) {
        return false;
    }
    const std::shared_ptr<MediaControlsEventBridge> bridge = m_native->bridge;
    const bool subscribed = runControlsCall("ButtonPressed", [this, &controls, bridge]() {
        m_native->buttonPressed = controls.ButtonPressed(
            winrt::auto_revoke,
            [bridge](const wm::SystemMediaTransportControls&,
                     const wm::SystemMediaTransportControlsButtonPressedEventArgs& args) {
                try {
                    const std::optional<MediaCommand> command = commandFor(args.Button());
                    if (command) {
                        postToOwner(bridge, [command = *command](WindowsMediaControls& owner) {
                            emit owner.commandReceived(command);
                        });
                    }
                } catch (...) {
                    // A press whose button cannot be read is dropped.
                }
            });
    });
    if (!subscribed) {
        return false;
    }
    m_native->controls = controls;
    applyEnabled();
    applyStatus();
    applyTimeline();
    requestThumbnail();
    return true;
}

void WindowsMediaControls::setEnabled(bool enabled)
{
    if (enabled == m_enabled) {
        return;
    }
    m_enabled = enabled;
    applyEnabled();
}

void WindowsMediaControls::setMetadata(const MediaControlsMetadata& metadata)
{
    if (metadata == m_metadata) {
        return;
    }
    m_metadata = metadata;
    ++m_metadataGeneration;
    m_native->thumbnail = nullptr;
    requestThumbnail();
}

void WindowsMediaControls::setPlaybackStatus(PlaybackStatus status)
{
    m_status = status;
    applyStatus();
}

void WindowsMediaControls::setTimeline(double positionSeconds, double durationSeconds)
{
    m_duration = std::isfinite(durationSeconds) ? std::max(0.0, durationSeconds) : 0.0;
    m_position = std::isfinite(positionSeconds) ? std::clamp(positionSeconds, 0.0, m_duration) : 0.0;
    applyTimeline();
}

void WindowsMediaControls::applyEnabled()
{
    if (!m_native->controls) {
        return;
    }
    static_cast<void>(runControlsCall("IsEnabled", [this]() { m_native->controls.IsEnabled(m_enabled); }));
}

void WindowsMediaControls::applyStatus()
{
    if (!m_native->controls) {
        return;
    }
    const wm::MediaPlaybackStatus status = systemStatus(m_status);
    static_cast<void>(runControlsCall("PlaybackStatus", [this, status]() { m_native->controls.PlaybackStatus(status); }));
}

void WindowsMediaControls::applyTimeline()
{
    if (!m_native->controls) {
        return;
    }
    // With an unknown duration the timeline is emptied, so the previous
    // song's progress is not left on show.
    const double duration = m_duration;
    const double position = duration > 0.0 ? m_position : 0.0;
    static_cast<void>(runControlsCall("UpdateTimelineProperties", [this, duration, position]() {
        wm::SystemMediaTransportControlsTimelineProperties timeline;
        timeline.StartTime(wf::TimeSpan{0});
        timeline.EndTime(toTimeSpan(duration));
        timeline.Position(toTimeSpan(position));
        m_native->controls.UpdateTimelineProperties(timeline);
    }));
}

void WindowsMediaControls::requestThumbnail()
{
    if (!m_native->controls) {
        return; // attach() asks for it
    }
    if (m_metadata.title.isEmpty() || m_metadata.artworkFilePath.isEmpty()) {
        applyMetadata();
        return;
    }
    // The text is shown together with the cover, in one update, once the
    // thumbnail is ready (or known to be missing).
    Native::loadThumbnail(m_native->bridge, m_metadataGeneration,
                          winrt::hstring(m_metadata.artworkFilePath.toStdWString()));
}

void WindowsMediaControls::applyMetadata()
{
    if (!m_native->controls) {
        return;
    }
    static_cast<void>(runControlsCall("DisplayUpdater", [this]() {
        const wm::SystemMediaTransportControlsDisplayUpdater updater = m_native->controls.DisplayUpdater();
        if (m_metadata.title.isEmpty()) {
            updater.ClearAll();
            updater.Update();
            return;
        }
        updater.Type(wm::MediaPlaybackType::Music);
        const wm::MusicDisplayProperties music = updater.MusicProperties();
        music.Title(winrt::hstring(m_metadata.title.toStdWString()));
        music.Artist(winrt::hstring(m_metadata.artist.toStdWString()));
        music.AlbumTitle(winrt::hstring(m_metadata.album.toStdWString()));
        updater.Thumbnail(m_native->thumbnail);
        updater.Update();
    }));
}

} // namespace lmp::platform::windows
