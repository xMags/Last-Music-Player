#pragma once

#include <QByteArray>
#include <QString>

#include <optional>

namespace lmp::core::cast {

// The cast channel's namespaces and fixed ids.
inline constexpr char ConnectionNamespace[] = "urn:x-cast:com.google.cast.tp.connection";
inline constexpr char HeartbeatNamespace[] = "urn:x-cast:com.google.cast.tp.heartbeat";
inline constexpr char ReceiverNamespace[] = "urn:x-cast:com.google.cast.receiver";
inline constexpr char MediaNamespace[] = "urn:x-cast:com.google.cast.media";
inline constexpr char SenderId[] = "sender-0";
inline constexpr char ReceiverId[] = "receiver-0";
// The default media receiver app.
inline constexpr char DefaultMediaReceiverAppId[] = "CC1AD845";

// Frames over 8 MiB, or empty ones, close the connection.
inline constexpr qint64 MaxCastMessageBytes = 8 * 1024 * 1024;

// One message: its namespace, destination and JSON payload (always the
// string payload type).
struct CastMessage {
    QString nameSpace;
    QString source;
    QString destination;
    QByteArray payload;
};

// A frame: a big-endian 32-bit length, then the message's protocol-buffer
// encoding (fields 1 protocol version 0, 2 source, 3 destination, 4
// namespace, 5 payload type 0 (string), 6 payload). Written by hand, as the
// earlier versions did, so no protocol-buffer library is needed.
[[nodiscard]] QByteArray encodeCastFrame(const QString& nameSpace, const QString& destination,
                                         const QByteArray& payload);

// Reads one message body (without the length). Nothing for a body that
// cannot be read.
[[nodiscard]] std::optional<CastMessage> decodeCastMessage(const QByteArray& body);

// Collects bytes from the socket and hands out whole message bodies.
class CastFrameDecoder {
public:
    void append(const QByteArray& bytes) { m_buffer.append(bytes); }
    // The next whole body, or nothing yet; `broken` is set for an empty or
    // oversized frame.
    [[nodiscard]] std::optional<QByteArray> next(bool& broken);
    void clear() { m_buffer.clear(); }

private:
    QByteArray m_buffer;
};

} // namespace lmp::core::cast
