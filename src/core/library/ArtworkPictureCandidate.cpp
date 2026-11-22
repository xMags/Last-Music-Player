#include "core/library/ArtworkPictureCandidate.h"

#include <cstddef>
#include <cstring>

namespace lmp::core::artwork {

namespace {

bool startsWith(const std::vector<std::uint8_t>& bytes, std::size_t offset, const char* signature,
                std::size_t length) noexcept
{
    if (offset > bytes.size() || bytes.size() - offset < length) {
        return false;
    }
    return std::memcmp(bytes.data() + offset, signature, length) == 0;
}

} // namespace

QString sniffImageMimeType(const std::vector<std::uint8_t>& bytes)
{
    if (startsWith(bytes, 0, "\xFF\xD8\xFF", 3)) {
        return QStringLiteral("image/jpeg");
    }
    if (startsWith(bytes, 0, "\x89PNG\r\n\x1A\n", 8)) {
        return QStringLiteral("image/png");
    }
    if (startsWith(bytes, 0, "GIF87a", 6) || startsWith(bytes, 0, "GIF89a", 6)) {
        return QStringLiteral("image/gif");
    }
    if (startsWith(bytes, 0, "BM", 2)) {
        return QStringLiteral("image/bmp");
    }
    if (startsWith(bytes, 0, "RIFF", 4) && startsWith(bytes, 8, "WEBP", 4)) {
        return QStringLiteral("image/webp");
    }
    if (startsWith(bytes, 0, "II\x2A\x00", 4) || startsWith(bytes, 0, "MM\x00\x2A", 4)) {
        return QStringLiteral("image/tiff");
    }
    if (startsWith(bytes, 0, "\x00\x00\x01\x00", 4)) {
        return QStringLiteral("image/x-icon");
    }
    return {};
}

void ArtworkPictureCandidate::offer(const std::vector<std::uint8_t>& bytes, bool frontCover)
{
    if (bytes.empty() || bytes.size() > MaxEmbeddedArtworkBytes) {
        return;
    }
    QString type = sniffImageMimeType(bytes);
    if (type.isEmpty()) {
        return;
    }
    if (present && (!frontCover || isFrontCover)) {
        return;
    }

    QByteArray copy(static_cast<qsizetype>(bytes.size()), Qt::Uninitialized);
    std::memcpy(copy.data(), bytes.data(), bytes.size());
    artwork.bytes = std::move(copy);
    artwork.mimeType = std::move(type);
    isFrontCover = frontCover;
    present = true;
}

} // namespace lmp::core::artwork
