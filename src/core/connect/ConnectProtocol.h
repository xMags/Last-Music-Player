#pragma once

#include <QByteArray>
#include <QJsonObject>
#include <QList>
#include <QString>
#include <QUrl>
#include <QtGlobal>

#include <chrono>
#include <optional>

// The account's device link, version 1: the messages this app sends and
// reads, and the small rules around them (reconnect delays, device ids, the
// socket address, where a remote's song has got to). Pure functions only;
// ConnectClient speaks it over a socket and ConnectSync ties it to the player.
//
// Every incoming field is checked: a message that is not a JSON object, or a
// field of the wrong type, falls back to the defaults below, and unknown
// types and fields are ignored, as the protocol asks.
namespace lmp::core::connect {

inline constexpr int ProtocolVersion = 1;
// Up Next as reported: at most this many songs after the current one.
inline constexpr qsizetype UpcomingLimit = 100;
// A list played on another device is sent as at most this many songs.
inline constexpr qsizetype TrackLimit = 500;
// The largest message the server accepts.
inline constexpr qsizetype MaxMessageBytes = 512 * 1024;

// Socket close codes with a meaning of their own.
inline constexpr int CloseReplaced = 4000;  // this device id connected elsewhere: do not reconnect
inline constexpr int CloseSignedOut = 4001; // the session ended: do not reconnect until signed in
inline constexpr int CloseSilent = 4002;    // closed here after DeadSocket without a message
inline constexpr int CloseFlooding = 4008;  // back off at least MaxBackoff

inline constexpr std::chrono::milliseconds MaxBackoff{30000};
// A connection that lasted this long starts the back-off over.
inline constexpr std::chrono::milliseconds StableConnection{60000};
// The server writes at least every 25 s; this long without a message is a dead link.
inline constexpr std::chrono::milliseconds DeadSocket{60000};

enum class DeviceKind { Web, Windows, Android };

struct Device {
    QString deviceId;
    QString name;
    DeviceKind kind = DeviceKind::Web;
    bool canPlay = true;
    bool active = false;

    friend bool operator==(const Device&, const Device&) = default;
};

// What the playing device reports. `track` and `upcoming` are the API's song
// objects as they travel; an empty `track` is no song.
struct Playback {
    QJsonObject track;
    bool playing = false;
    double positionSeconds = 0.0;
    double durationSeconds = 0.0;
    int volume = 0; // 0 to 100
    bool shuffle = false;
    int repeatMode = 0; // 0 off, 1 all, 2 one
    QList<QJsonObject> upcoming;
    QString castDevice;

    friend bool operator==(const Playback&, const Playback&) = default;
};

struct Session {
    // Empty when no device plays.
    QString activeDeviceId;
    std::optional<Playback> state;
    // How old `state` was when the server sent it.
    qint64 ageMs = 0;

    friend bool operator==(const Session&, const Session&) = default;
};

struct Command {
    enum class Type { Play, Pause, Toggle, Next, Previous, Seek, Volume, Shuffle, Repeat, PlayTracks, PlayUpcoming,
                      SetUpcoming, Enqueue };
    Type type = Type::Toggle;
    double positionSeconds = 0.0; // Seek
    int value = 0;                // Volume 0 to 100, Repeat 0 to 2, PlayTracks index, PlayUpcoming position
    bool enabled = false;         // Shuffle on, PlayTracks shuffled, Enqueue next
    QList<QJsonObject> tracks;    // PlayTracks, SetUpcoming, Enqueue

    friend bool operator==(const Command&, const Command&) = default;
};

struct ServerMessage {
    enum class Type { Welcome, Devices, Session, Command, Takeover, Stop, Error, Ignored };
    Type type = Type::Ignored;
    std::optional<qint64> epoch;
    QList<Device> devices;    // Welcome, Devices
    Session session;          // Welcome, Session
    std::optional<Command> command;
    std::optional<Playback> state; // Takeover
    bool play = true;              // Takeover
    QString errorCode;
    QString errorMessage;
};

[[nodiscard]] ServerMessage parseServerMessage(const QByteArray& text);
// Nothing for a malformed command (an unknown type, a seek without a number).
// Values out of range are clamped.
[[nodiscard]] std::optional<Command> parseCommand(const QJsonObject& object);
[[nodiscard]] std::optional<Playback> parsePlayback(const QJsonObject& object);

[[nodiscard]] QByteArray helloMessage(const QString& deviceId, const QString& name);
[[nodiscard]] QByteArray stateMessage(qint64 epoch, const Playback& state);
[[nodiscard]] QByteArray commandMessage(const Command& command);
[[nodiscard]] QByteArray transferMessage(const QString& toDeviceId, bool play);
[[nodiscard]] QByteArray releaseMessage();
[[nodiscard]] QJsonObject playbackJson(const Playback& state);
[[nodiscard]] QJsonObject commandJson(const Command& command);

// 8 to 64 of A-Z, a-z, 0-9, "_" and "-".
[[nodiscard]] bool isValidDeviceId(const QString& deviceId);
// 32 random hex digits.
[[nodiscard]] QString newDeviceId();

// The wait before reconnect attempt `attempt` (0 for the first):
// min(MaxBackoff, 1 s * 2^min(attempt, 5)), times 0.75 to 1.25 from
// `random` in [0, 1), and never below `minimum`.
[[nodiscard]] std::chrono::milliseconds reconnectDelay(int attempt, double random,
                                                       std::chrono::milliseconds minimum = {});

// The socket address for a ticket: the API origin with ws/wss for http/https,
// `path` and "?ticket=". Nothing when the origin is not http(s), the path does
// not start with "/v1/" or holds anything but path characters, or the ticket
// is empty or longer than 256 characters.
[[nodiscard]] std::optional<QUrl> socketUrl(const QString& apiOrigin, const QString& path, const QString& ticket);

// Where a session's song is `elapsedMs` after the session arrived: moved on
// by its age and the time since while playing, never past its length.
[[nodiscard]] double positionAt(const Playback& state, qint64 ageMs, qint64 elapsedMs);

} // namespace lmp::core::connect
