#pragma once

#include "platform/IAudioPlayer.h"

#include <QElapsedTimer>
#include <QPointer>
#include <QTimer>

namespace lmp::core::cast {

class CastSession;
struct CastMediaStatus;

// The audio player contract (platform::IAudioPlayer) on a cast device, so
// the player controller drives a device as it drives this PC: open sends a
// LOAD with the song's details, the device's MEDIA_STATUS answers become
// the contract's states, and the position is asked for every second while
// playing and moved on in between.
//
// A status of the media that played before the latest open is ignored, so
// an old song's end never ends the new one.
//
// GUI thread.
class CastAudioPlayer final : public platform::IAudioPlayer {
    Q_OBJECT

public:
    explicit CastAudioPlayer(QObject* parent = nullptr);

    // The session commands go to (nullptr while not casting). It must
    // outlive its use here or be replaced first.
    void setSession(CastSession* session);
    // What the device shows for the next open.
    void setMetadata(const QString& title, const QString& artist, const QString& artworkUrl);

    [[nodiscard]] quint64 open(const QUrl& source, bool autoplay, double startAtSeconds) override;
    void play() override;
    void pause() override;
    void seek(double seconds) override;
    void stop() override;
    void setVolume(double volume) override;
    void setOutputDevice(const QString& deviceId) override;
    [[nodiscard]] quint64 currentLoadId() const override { return m_loadId; }
    [[nodiscard]] State state() const override { return m_state; }

    // Called by the session's mediaStatus signal; public for tests.
    void applyStatus(const CastMediaStatus& status);

    static constexpr int StatusPollMs = 1000;
    static constexpr int PositionTickMs = 500;

private:
    void report(State state);
    void tick();

    QPointer<CastSession> m_session;
    QString m_title;
    QString m_artist;
    QString m_artworkUrl;
    quint64 m_loadId = 0;
    State m_state = State::Idle;
    // The device's media session of the latest open (0 until it reports one),
    // and the one before it, whose statuses are dropped.
    qint64 m_mediaSessionId = 0;
    qint64 m_previousMediaSessionId = 0;
    double m_duration = 0.0;
    double m_position = 0.0;
    QElapsedTimer m_sinceStatus;
    QTimer m_poll;
    QTimer m_tick;
};

} // namespace lmp::core::cast
