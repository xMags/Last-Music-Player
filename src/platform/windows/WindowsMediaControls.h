#pragma once

#include <QObject>
#include <QtGlobal>

#include <memory>

#include "platform/IMediaControls.h"

namespace lmp::platform::windows {

// The system media transport controls of the application window: the
// session the system routes the hardware media keys to and shows in its
// media flyout. Bound to the window with attach(), the way the system wants
// it for a desktop app; the media player's own session is switched off (see
// WindowsAudioPlayer), so this is the app's only session.
//
// Play, pause, next and previous are offered. The cover is the system's
// music thumbnail of the given file, made on the system's thread pool.
//
// Threading: create, use and destroy it on the GUI thread, the window's
// thread. Button presses arrive on a system thread and are posted to this
// object's thread; commandReceived is emitted there. Everything set before
// attach() is kept and applied when the window is attached.
class WindowsMediaControls final : public IMediaControls {
    Q_OBJECT

public:
    explicit WindowsMediaControls(QObject* parent = nullptr);
    ~WindowsMediaControls() override;

    WindowsMediaControls(const WindowsMediaControls&) = delete;
    WindowsMediaControls& operator=(const WindowsMediaControls&) = delete;

    // Binds the controls to the window `windowHandle` (an HWND). Returns
    // false, and stays inactive, when the system offers no controls for it.
    [[nodiscard]] bool attach(quintptr windowHandle);

    void setEnabled(bool enabled) override;
    void setMetadata(const MediaControlsMetadata& metadata) override;
    void setPlaybackStatus(PlaybackStatus status) override;
    void setTimeline(double positionSeconds, double durationSeconds) override;

private:
    // Holds every Windows type, so this header needs none of them.
    struct Native;

    void applyEnabled();
    void applyStatus();
    void applyTimeline();
    // Shows the metadata; the cover follows once its thumbnail is ready.
    void applyMetadata();
    void requestThumbnail();

    std::unique_ptr<Native> m_native;
    bool m_enabled = true;
    MediaControlsMetadata m_metadata;
    PlaybackStatus m_status = PlaybackStatus::Closed;
    double m_position = 0.0;
    double m_duration = 0.0;
    // Grows with every metadata change, so a late thumbnail of an earlier
    // song is dropped.
    quint64 m_metadataGeneration = 0;
};

} // namespace lmp::platform::windows
