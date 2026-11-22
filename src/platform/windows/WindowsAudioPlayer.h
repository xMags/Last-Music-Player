#pragma once

#include <QObject>
#include <QString>
#include <QTimer>
#include <QUrl>
#include <QtGlobal>

#include <memory>

#include "platform/IAudioPlayer.h"

namespace lmp::platform::windows {

// The Windows media player behind IAudioPlayer.
//
// Threading: create, use and destroy it on one thread with a running event
// loop (the GUI thread in the application). It needs no apartment of its
// own: the player is free-threaded, so it works in the single-threaded
// apartment Qt sets up on the GUI thread and, without one, in the
// process-wide multithreaded apartment the runtime falls back to. The
// player raises its events on its own threads; each one is posted to this
// object's thread and only acted on there, so every signal is emitted on
// this object's thread.
//
// The player's own system media transport integration is switched off, so
// it registers no session of its own with the system media controls.
class WindowsAudioPlayer final : public IAudioPlayer {
    Q_OBJECT

public:
    explicit WindowsAudioPlayer(QObject* parent = nullptr);
    ~WindowsAudioPlayer() override;

    WindowsAudioPlayer(const WindowsAudioPlayer&) = delete;
    WindowsAudioPlayer& operator=(const WindowsAudioPlayer&) = delete;

    [[nodiscard]] quint64 open(const QUrl& source, bool autoplay,
                               double startAtSeconds) override;
    void play() override;
    void pause() override;
    void seek(double seconds) override;
    void stop() override;
    void setVolume(double volume) override;

    [[nodiscard]] quint64 currentLoadId() const override { return m_loadId; }
    [[nodiscard]] State state() const override { return m_state; }

private:
    // Holds every Windows type, so this header needs none of them.
    struct Native;

    // The player session's own state, as reported by its events.
    enum class SessionState { None, Opening, Buffering, Playing, Paused };

    void connectPlayerEvents();
    void openSource(quint64 loadId, const QUrl& source, bool playNow);

    // Each of these runs on this object's thread with the load id the player
    // event belonged to, and does nothing for a load that is not current.
    void handleOpened(quint64 loadId, double durationSeconds);
    void handleSessionState(quint64 loadId, SessionState state, double positionSeconds);
    void handleDuration(quint64 loadId, double durationSeconds);
    void handleEnded(quint64 loadId, double durationSeconds);
    void handleFailed(quint64 loadId, ErrorKind kind, const QString& detail);

    void finishOpen(double durationSeconds);
    void failLater(quint64 loadId, ErrorKind kind, const QString& detail);
    void fail(ErrorKind kind, const QString& detail);
    void updateDuration(double durationSeconds);
    void setState(State state);
    void reportPosition();
    [[nodiscard]] bool hasActiveLoad() const;
    [[nodiscard]] double sessionPosition() const;
    [[nodiscard]] double sessionDuration() const;

    std::unique_ptr<Native> m_native;
    // Reports the position while playing; the player's own position event
    // is not a steady ticker.
    QTimer m_positionTimer;

    quint64 m_loadId = 0;
    State m_state = State::Idle;
    // The source has reported that it is open (metadata loaded).
    bool m_opened = false;
    // Playback was asked for and not paused since.
    bool m_wantsPlay = false;
    // Applied when the source opens; 0 means from the start.
    double m_startAtSeconds = 0.0;
    double m_durationSeconds = 0.0;
};

} // namespace lmp::platform::windows
