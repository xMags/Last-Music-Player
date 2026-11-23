#pragma once

#include "core/connect/ConnectProtocol.h"
#include "core/model/Track.h"
#include "core/playback/PlaybackClock.h"

#include <QHash>
#include <QList>
#include <QObject>
#include <QSet>
#include <QString>

#include <cstdint>
#include <functional>
#include <memory>
#include <optional>

namespace lmp::core {
class PlayerController;
} // namespace lmp::core

namespace lmp::core::connect {

class ConnectClient;

// Ties the device link to this PC's player.
//
// While this PC plays for the account it reports its playback (changes
// arriving together make one report 150 ms later; a seek is a jump of more
// than 2 s; at least every 15 s while playing) and applies the commands the
// other devices send. While another device plays, this PC is its remote:
// the player here pauses, remote() holds what plays there, and the remote*
// calls send commands. When that device goes away, what it played becomes
// this PC's own paused session, so Play continues it here.
//
// Playing here while another device plays (a media key, a song started
// from a list) takes the session over: the report claims it.
//
// GUI thread. The client, player and clock must outlive this object.
class ConnectSync final : public QObject {
    Q_OBJECT

public:
    // A seek or volume change made here wins over reports for this long, so
    // a report sent before the command arrived does not pull the slider back.
    static constexpr std::chrono::milliseconds ControlSettle{1500};
    static constexpr std::chrono::milliseconds PublishDelay{150};
    static constexpr std::chrono::milliseconds Heartbeat{15000};
    static constexpr double SeekReportSeconds = 2.0;

    struct Dependencies {
        ConnectClient& client;
        PlayerController& player;
        PlaybackClock& clock;
        // The cast device's name while casting, otherwise empty.
        std::function<QString()> castDeviceName;
    };

    // What plays on the other device.
    struct RemoteView {
        std::optional<Track> track;
        bool playing = false;
        double durationSeconds = 0.0;
        int volume = 0; // 0 to 100
        bool shuffle = false;
        int repeatMode = 0;
        QList<Track> upcoming;
        // "Phone", or "Living Room (via Phone)" while that device casts.
        QString deviceLabel;
    };

    explicit ConnectSync(Dependencies dependencies, QObject* parent = nullptr);
    ~ConnectSync() override;

    [[nodiscard]] bool isRemote() const noexcept { return m_remoteMode; }
    [[nodiscard]] const RemoteView& remote() const noexcept { return m_remote; }
    // Where the other device's song has got to by now.
    [[nodiscard]] double remotePosition() const;

    // Controls of the other device; nothing unless this PC is its remote.
    void remoteTogglePlay();
    void remoteNext();
    void remotePrevious();
    // The command is sent once the value has settled (a drag sends one).
    void remoteSeek(double seconds);
    void remoteSetVolume(int volume);
    void remoteSetShuffle(bool shuffle);
    void remoteSetRepeatMode(int repeatMode);
    void remotePlayUpcoming(int position);
    void remoteClearUpcoming();

    // Moves playback to that device (this PC's own id takes it back). A paused
    // session this PC never reported is reported first, so there is
    // something to hand over. When nothing can be moved, `notice` says why.
    void playOn(const QString& deviceId);

signals:
    // Remote mode began or ended, or the other device's song or state changed.
    void remoteChanged();
    // The other device's song moved on (every 500 ms while it plays).
    void remotePositionChanged();
    // Something the user should be told (a refused transfer).
    void notice(const QString& message);

private:
    struct Sent {
        QString signature;
        double positionSeconds = 0.0;
        std::uint64_t at = 0;
        bool playing = false;
    };

    void onSession();
    void onCommand(const Command& command);
    void onTakeover(const Playback& state, bool play);
    void onStop();
    void onError(const QString& code);
    void showRemote(const Session& session);
    void enterRemote();
    // Leaves remote mode; with `adopt` what the other device played becomes
    // this PC's paused session.
    void leaveRemote(bool adopt);
    void schedulePublish(bool force);
    void publish();
    // This PC's playback as it would be reported; `reported` receives its Up
    // Next songs by the id they travel with.
    [[nodiscard]] Playback snapshot(QHash<QString, Track>& reported) const;
    [[nodiscard]] QString signatureOf(const Playback& state) const;
    [[nodiscard]] Track trackFor(const QJsonObject& object) const;
    [[nodiscard]] QList<Track> tracksFor(const QList<QJsonObject>& objects) const;
    [[nodiscard]] bool localPlaying() const;
    void setRemoteClock(double positionSeconds, bool playing);
    void syncTicker();
    void send(const Command& command);

    Dependencies m_dependencies;
    std::unique_ptr<PlaybackTimer> m_publishTimer;
    std::unique_ptr<PlaybackTimer> m_heartbeat;
    std::unique_ptr<PlaybackTimer> m_ticker;
    std::unique_ptr<PlaybackTimer> m_seekTimer;
    std::unique_ptr<PlaybackTimer> m_volumeTimer;
    std::optional<Sent> m_lastSent;
    // The Up Next songs of the last report, by the id they travelled with,
    // so a remote's list of them maps back to the songs here (files too).
    QHash<QString, Track> m_reported;
    bool m_hasReported = false;

    bool m_remoteMode = false;
    RemoteView m_remote;
    double m_remoteClockPosition = 0.0;
    std::uint64_t m_remoteClockAt = 0;
    std::uint64_t m_seekHeldUntil = 0;
    std::uint64_t m_volumeHeldUntil = 0;
    double m_pendingSeek = 0.0;
    int m_pendingVolume = 0;
};

} // namespace lmp::core::connect
