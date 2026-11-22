#include "core/text/StrictUtf8.h"

#include <QStringDecoder>
#include <QStringEncoder>

namespace lmp::core::text {

std::optional<QString> decodeUtf8Strict(QByteArrayView utf8)
{
    if (utf8.isEmpty()) {
        return QString();
    }
    // Stateless: a sequence cut off at the end is an error, not something to
    // finish in a later call. No BOM handling: a leading BOM is kept as text,
    // as the earlier conversion kept it.
    QStringDecoder decoder(QStringDecoder::Utf8,
                           QStringDecoder::Flag::Stateless | QStringDecoder::Flag::ConvertInitialBom);
    QString decoded = decoder.decode(utf8);
    if (decoder.hasError()) {
        return std::nullopt;
    }
    return decoded;
}

std::optional<QByteArray> encodeUtf8Strict(QStringView text)
{
    if (text.isEmpty()) {
        return QByteArray();
    }
    QStringEncoder encoder(QStringEncoder::Utf8, QStringEncoder::Flag::Stateless);
    QByteArray encoded = encoder.encode(text);
    if (encoder.hasError()) {
        return std::nullopt;
    }
    return encoded;
}

} // namespace lmp::core::text
