#include "core/presence/PresenceProtocol.h"

#include <QJsonDocument>
#include <QJsonObject>
#include <QtEndian>

#include <algorithm>
#include <cmath>

namespace lmp::core::presence {

namespace {

[[nodiscard]] qint64 milliseconds(double seconds)
{
    return std::isfinite(seconds) ? std::llround(std::max(0.0, seconds) * 1000.0) : 0;
}

[[nodiscard]] bool showsArtwork(const QString& url)
{
    return url.startsWith(QLatin1StringView("https://")) || url.startsWith(QLatin1StringView("mp:external/"))
        || (!url.isEmpty() && !url.contains(QLatin1Char('/')));
}

} // namespace

QByteArray encodeFrame(Opcode opcode, const QByteArray& payload)
{
    QByteArray frame(8, Qt::Uninitialized);
    qToLittleEndian<qint32>(static_cast<qint32>(opcode), frame.data());
    qToLittleEndian<qint32>(static_cast<qint32>(payload.size()), frame.data() + 4);
    frame.append(payload);
    return frame;
}

std::optional<Frame> FrameDecoder::next(bool& broken)
{
    broken = false;
    if (m_buffer.size() < 8) {
        return std::nullopt;
    }
    const qint32 opcode = qFromLittleEndian<qint32>(m_buffer.constData());
    const qint32 length = qFromLittleEndian<qint32>(m_buffer.constData() + 4);
    if (length < 0 || length > MaxFrameBytes) {
        broken = true;
        return std::nullopt;
    }
    if (m_buffer.size() < 8 + length) {
        return std::nullopt;
    }
    Frame frame{opcode, m_buffer.mid(8, length)};
    m_buffer.remove(0, 8 + length);
    return frame;
}

QByteArray handshakeJson(const QString& clientId)
{
    return QJsonDocument(QJsonObject{{QStringLiteral("v"), 1}, {QStringLiteral("client_id"), clientId}})
        .toJson(QJsonDocument::Compact);
}

QByteArray activityJson(const PresencePayload& payload, qint64 processId, qint64 nowMs, const QString& nonce)
{
    const qint64 position = milliseconds(payload.positionSeconds);
    const qint64 duration = milliseconds(payload.durationSeconds);
    QJsonObject activity{{QStringLiteral("type"), 2}, {QStringLiteral("name"), QStringLiteral("Last Music")}};
    if (!payload.title.isEmpty()) {
        activity.insert(QStringLiteral("details"), payload.title);
    }
    QString state = payload.artist;
    if (!payload.isPlaying) {
        if (!state.isEmpty()) {
            state += QStringLiteral(" · ");
        }
        state += QStringLiteral("Paused");
    }
    if (!state.isEmpty()) {
        activity.insert(QStringLiteral("state"), state);
    }
    if (payload.isPlaying && duration > 500) {
        activity.insert(QStringLiteral("timestamps"),
                        QJsonObject{{QStringLiteral("start"), static_cast<double>(nowMs - position)},
                                    {QStringLiteral("end"), static_cast<double>(nowMs + std::max<qint64>(0, duration - position))}});
    }
    if (showsArtwork(payload.artworkUrl)) {
        activity.insert(QStringLiteral("assets"),
                        QJsonObject{{QStringLiteral("large_image"), payload.artworkUrl},
                                    {QStringLiteral("large_text"),
                                     payload.isLocal ? QStringLiteral("Source: Local") : QStringLiteral("Source: Remote")}});
    }
    const QJsonObject root{
        {QStringLiteral("cmd"), QStringLiteral("SET_ACTIVITY")},
        {QStringLiteral("args"), QJsonObject{{QStringLiteral("pid"), static_cast<double>(processId)},
                                             {QStringLiteral("activity"), activity}}},
        {QStringLiteral("nonce"), nonce}};
    return QJsonDocument(root).toJson(QJsonDocument::Compact);
}

QByteArray clearJson(qint64 processId, const QString& nonce)
{
    const QJsonObject root{
        {QStringLiteral("cmd"), QStringLiteral("SET_ACTIVITY")},
        {QStringLiteral("args"), QJsonObject{{QStringLiteral("pid"), static_cast<double>(processId)},
                                             {QStringLiteral("activity"), QJsonValue::Null}}},
        {QStringLiteral("nonce"), nonce}};
    return QJsonDocument(root).toJson(QJsonDocument::Compact);
}

QString activityFingerprint(const PresencePayload& payload)
{
    const auto part = [](const QString& value) { return QString::number(value.toUtf8().size()) + QLatin1Char(':') + value + QLatin1Char('|'); };
    return part(payload.title) + part(payload.artist) + part(payload.artworkUrl)
        + QString::number(milliseconds(payload.durationSeconds)) + QLatin1Char('|')
        + QString::number(milliseconds(payload.positionSeconds)) + QLatin1Char('|')
        + QString::number(payload.isPlaying ? 1 : 0) + QLatin1Char('|') + QString::number(payload.isLocal ? 1 : 0);
}

std::optional<QString> jsonStringField(const QByteArray& json, const char* field)
{
    const QJsonValue value = QJsonDocument::fromJson(json).object().value(QLatin1StringView(field));
    return value.isString() ? std::optional<QString>(value.toString()) : std::nullopt;
}

bool isReadyFrame(const QByteArray& payload)
{
    return jsonStringField(payload, "cmd") == QStringLiteral("DISPATCH")
        && jsonStringField(payload, "evt") == QStringLiteral("READY");
}

bool isErrorFrame(const QByteArray& payload)
{
    return jsonStringField(payload, "evt") == QStringLiteral("ERROR");
}

bool sendAllowed(std::optional<std::chrono::milliseconds> sinceLastSend)
{
    return !sinceLastSend || *sinceLastSend >= MinSendGap;
}

std::chrono::milliseconds nextReconnectDelay(std::chrono::milliseconds current)
{
    return std::min(current * 2, ReconnectMaximum);
}

} // namespace lmp::core::presence
