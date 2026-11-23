#include "core/cast/CastMessage.h"

#include <QtEndian>

namespace lmp::core::cast {

namespace {

void writeVarint(QByteArray& out, quint64 value)
{
    while (value >= 0x80U) {
        out.append(static_cast<char>((value & 0x7FU) | 0x80U));
        value >>= 7U;
    }
    out.append(static_cast<char>(value));
}

void writeLengthField(QByteArray& out, unsigned fieldNumber, const QByteArray& data)
{
    writeVarint(out, (static_cast<quint64>(fieldNumber) << 3U) | 2U);
    writeVarint(out, static_cast<quint64>(data.size()));
    out.append(data);
}

void writeVarintField(QByteArray& out, unsigned fieldNumber, quint64 value)
{
    writeVarint(out, static_cast<quint64>(fieldNumber) << 3U);
    writeVarint(out, value);
}

// Reads a varint at `pos`; nothing when the input ends inside it or it is
// longer than ten bytes.
[[nodiscard]] std::optional<quint64> readVarint(const QByteArray& data, qsizetype& pos)
{
    quint64 result = 0;
    for (unsigned shift = 0; shift <= 63U; shift += 7U) {
        if (pos >= data.size()) {
            return std::nullopt;
        }
        const auto byte = static_cast<quint8>(data.at(pos++));
        result |= static_cast<quint64>(byte & 0x7FU) << shift;
        if ((byte & 0x80U) == 0) {
            return result;
        }
    }
    return std::nullopt;
}

} // namespace

QByteArray encodeCastFrame(const QString& nameSpace, const QString& destination, const QByteArray& payload)
{
    QByteArray body;
    writeVarintField(body, 1, 0); // protocol_version CASTV2_1_0
    writeLengthField(body, 2, QByteArray(SenderId));
    writeLengthField(body, 3, destination.toUtf8());
    writeLengthField(body, 4, nameSpace.toUtf8());
    writeVarintField(body, 5, 0); // payload_type STRING
    writeLengthField(body, 6, payload);
    QByteArray frame(4, Qt::Uninitialized);
    qToBigEndian<quint32>(static_cast<quint32>(body.size()), frame.data());
    frame.append(body);
    return frame;
}

std::optional<CastMessage> decodeCastMessage(const QByteArray& body)
{
    CastMessage message;
    qsizetype pos = 0;
    while (pos < body.size()) {
        const std::optional<quint64> tag = readVarint(body, pos);
        if (!tag) {
            return std::nullopt;
        }
        const quint64 field = *tag >> 3U;
        const quint64 wireType = *tag & 7U;
        switch (wireType) {
        case 0:
            if (!readVarint(body, pos)) {
                return std::nullopt;
            }
            break;
        case 1:
            pos += 8;
            break;
        case 5:
            pos += 4;
            break;
        case 2: {
            const std::optional<quint64> length = readVarint(body, pos);
            if (!length || *length > static_cast<quint64>(body.size() - pos)) {
                return std::nullopt;
            }
            const QByteArray value = body.mid(pos, static_cast<qsizetype>(*length));
            pos += static_cast<qsizetype>(*length);
            if (field == 2) {
                message.source = QString::fromUtf8(value);
            } else if (field == 3) {
                message.destination = QString::fromUtf8(value);
            } else if (field == 4) {
                message.nameSpace = QString::fromUtf8(value);
            } else if (field == 6) {
                message.payload = value;
            }
            break;
        }
        default:
            return std::nullopt;
        }
        if (pos > body.size()) {
            return std::nullopt;
        }
    }
    if (message.nameSpace.isEmpty()) {
        return std::nullopt;
    }
    return message;
}

std::optional<QByteArray> CastFrameDecoder::next(bool& broken)
{
    broken = false;
    if (m_buffer.size() < 4) {
        return std::nullopt;
    }
    const quint32 length = qFromBigEndian<quint32>(m_buffer.constData());
    if (length == 0 || length > MaxCastMessageBytes) {
        broken = true;
        return std::nullopt;
    }
    if (m_buffer.size() < 4 + static_cast<qsizetype>(length)) {
        return std::nullopt;
    }
    QByteArray body = m_buffer.mid(4, static_cast<qsizetype>(length));
    m_buffer.remove(0, 4 + static_cast<qsizetype>(length));
    return body;
}

} // namespace lmp::core::cast
