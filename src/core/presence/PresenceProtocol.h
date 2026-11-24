#pragma once

#include <QByteArray>
#include <QString>

#include <chrono>
#include <cstdint>
#include <optional>

namespace lmp::core::presence {

// What the rich presence shows for a song; an empty title clears it.
struct PresencePayload {
    QString title;
    QString artist;
    // An https address, a "mp:external/..." asset or a bare asset key;
    // anything else is left out.
    QString artworkUrl;
    double durationSeconds = 0.0;
    double positionSeconds = 0.0;
    bool isPlaying = true;
    // Drives "Source: Local" or "Source: Remote" on the picture.
    bool isLocal = false;
};

// The IPC frame opcodes.
enum class Opcode : std::int32_t { Handshake = 0, Frame = 1, Close = 2, Ping = 3, Pong = 4 };

// Frames are capped at 1 MiB; a longer one closes the connection.
inline constexpr std::int32_t MaxFrameBytes = 1024 * 1024;

// The earlier versions' timing: at most one activity every 4 s, 3 s for a
// reply or the handshake, reconnects from 1 s doubling to 30 s.
inline constexpr std::chrono::milliseconds MinSendGap{4000};
inline constexpr std::chrono::milliseconds ReplyTimeout{3000};
inline constexpr std::chrono::milliseconds HandshakeTimeout{3000};
inline constexpr std::chrono::milliseconds ReconnectStart{1000};
inline constexpr std::chrono::milliseconds ReconnectMaximum{30000};

// A frame: opcode and payload length as little-endian 32-bit integers, then
// the payload.
[[nodiscard]] QByteArray encodeFrame(Opcode opcode, const QByteArray& payload);

struct Frame {
    std::int32_t opcode = 0;
    QByteArray payload;
};

// Collects bytes from the pipe and hands out whole frames.
class FrameDecoder {
public:
    void append(const QByteArray& bytes) { m_buffer.append(bytes); }
    // The next whole frame, or nothing yet. `broken` is set for a length
    // outside 0 to MaxFrameBytes.
    [[nodiscard]] std::optional<Frame> next(bool& broken);
    void clear() { m_buffer.clear(); }

private:
    QByteArray m_buffer;
};

// The picture Discord can load for a song's `artworkUrl`: an https address as
// it is, or an account cover link ("/v1/music/art/<id>", which the account
// service serves without sign-in) on `accountOrigin` when that is https.
// Empty for anything else.
[[nodiscard]] QString presenceArtworkUrl(const QString& artworkUrl, const QString& accountOrigin);

[[nodiscard]] QByteArray handshakeJson(const QString& clientId);
// SET_ACTIVITY for the song, as the earlier versions built it: listening
// (type 2), named "Last Music", details the title, state the artist (and
// "Paused"), timestamps while playing a song with a length, the picture
// with its source label.
[[nodiscard]] QByteArray activityJson(const PresencePayload& payload, qint64 processId, qint64 nowMs,
                                      const QString& nonce);
[[nodiscard]] QByteArray clearJson(qint64 processId, const QString& nonce);
// What makes two activities the same (title, artist, picture, length,
// position to the millisecond, playing, local).
[[nodiscard]] QString activityFingerprint(const PresencePayload& payload);

// A string field of a JSON object, or nothing.
[[nodiscard]] std::optional<QString> jsonStringField(const QByteArray& json, const char* field);
[[nodiscard]] bool isReadyFrame(const QByteArray& payload);
[[nodiscard]] bool isErrorFrame(const QByteArray& payload);

// Whether an activity may be sent now, `sinceLastSend` after the last one
// (nothing when none was sent yet).
[[nodiscard]] bool sendAllowed(std::optional<std::chrono::milliseconds> sinceLastSend);
// The next reconnect delay after `current`.
[[nodiscard]] std::chrono::milliseconds nextReconnectDelay(std::chrono::milliseconds current);

} // namespace lmp::core::presence
