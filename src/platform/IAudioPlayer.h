#pragma once

#include <QObject>
#include <QString>
#include <QUrl>
#include <QtGlobal>

namespace lmp::platform {

// Plays one source at a time: a local file or a remote address.
//
// Every call to open() starts a new load and returns its id, a number that
// only grows. Every signal names the load it belongs to; once open() has
// been called again, nothing more is emitted for the earlier id, so a
// listener only has to compare the id with the one it last received from
// open(). Commands (play, pause, seek) always act on the current load.
//
// Event order for one load (all signals arrive on the object's thread, never
// from inside open()):
//   open, autoplay:  Loading, durationChanged, positionChanged, Paused,
//                    Playing, positionChanged while playing, then Ended
//                    (preceded by a positionChanged at the full duration).
//   open, no play:   Loading, durationChanged, positionChanged, Paused.
//   failure:         errorOccurred, then Error; nothing more for that load.
// The Paused after Loading means "metadata loaded": the duration is known
// and the source can be played or sought. durationChanged is skipped while
// the duration is still unknown and is repeated if it later changes.
class IAudioPlayer : public QObject {
    Q_OBJECT

public:
    enum class State {
        Idle,    // nothing open, or stopped
        Loading, // opening the source
        Paused,  // open and not playing
        Playing, // playing, or buffering while asked to play
        Ended,   // played to the end
        Error,   // the load failed; open again to retry
    };
    Q_ENUM(State)

    enum class ErrorKind {
        Aborted,            // the player gave up on the source
        Network,            // the address could not be read
        Decode,             // the data could not be decoded
        SourceNotSupported, // not an address or format the player can open
        OutputDevice,       // no usable audio output device
        Unknown,
    };
    Q_ENUM(ErrorKind)

    ~IAudioPlayer() override = default;

    IAudioPlayer(const IAudioPlayer&) = delete;
    IAudioPlayer& operator=(const IAudioPlayer&) = delete;

    // Replaces whatever is open with `source` (a local file or an http(s)
    // address). Playback starts once open when `autoplay` is true. A positive
    // `startAtSeconds` is applied once the source is open.
    [[nodiscard]] virtual quint64 open(const QUrl& source, bool autoplay,
                                       double startAtSeconds) = 0;

    // Play after the end starts again from the beginning.
    virtual void play() = 0;
    virtual void pause() = 0;
    // Moves to `seconds` (clamped to the duration). While loading, the
    // position replaces the start position. After the end, the state becomes
    // Paused at the new position.
    virtual void seek(double seconds) = 0;
    // Closes the source and reports Idle for the current load.
    virtual void stop() = 0;
    // 0 is silent, 1 is full volume; values outside are clamped.
    virtual void setVolume(double volume) = 0;
    // Sends the sound to the output with this system id; empty, or an id the
    // system no longer knows, is the system default.
    virtual void setOutputDevice(const QString& deviceId) = 0;

    [[nodiscard]] virtual quint64 currentLoadId() const = 0;
    [[nodiscard]] virtual State state() const = 0;

signals:
    void stateChanged(quint64 loadId, lmp::platform::IAudioPlayer::State state);
    void positionChanged(quint64 loadId, double seconds);
    void durationChanged(quint64 loadId, double seconds);
    // `detail` is for logs only.
    void errorOccurred(quint64 loadId, lmp::platform::IAudioPlayer::ErrorKind kind,
                       const QString& detail);

protected:
    explicit IAudioPlayer(QObject* parent = nullptr) : QObject(parent) {}
};

} // namespace lmp::platform
