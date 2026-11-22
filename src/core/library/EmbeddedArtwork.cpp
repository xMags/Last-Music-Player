#include "core/library/EmbeddedArtwork.h"

#include "core/library/ArtworkByteSource.h"
#include "core/library/ArtworkPictureCandidate.h"
#include "core/library/FlacArtworkReader.h"
#include "core/library/Id3ArtworkReader.h"
#include "core/library/Mp4ArtworkReader.h"

#include <QFile>
#include <QIODevice>
#include <QLatin1StringView>

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <utility>

namespace lmp::core {

namespace {

std::optional<EmbeddedArtwork> readFromSource(artwork::ArtworkByteSource& source)
{
    artwork::ArtworkPictureCandidate best;

    std::uint64_t audioStart = 0;
    artwork::Id3TagLayout layout;
    if (artwork::tryReadId3TagLayout(source, 0, layout)) {
        artwork::parseId3Tag(source, layout, best);
        if (best.hasFrontCover()) {
            return std::move(best.artwork);
        }
        audioStart = layout.totalSize;
    }

    // A FLAC stream can sit behind an ID3v2 tag even though the format does
    // not call for one, so probe both the file start and the byte just past
    // any tag found.
    const std::array<std::uint64_t, 2> probes{0, audioStart};
    const std::size_t probeCount = audioStart == 0 ? std::size_t{1} : probes.size();

    std::array<std::uint8_t, 12> signature{};
    for (std::size_t index = 0; index < probeCount; ++index) {
        if (!source.read(probes[index], signature.data(), signature.size())) {
            continue;
        }
        if (std::memcmp(signature.data(), "fLaC", 4) == 0) {
            artwork::parseFlacMetadata(source, probes[index], best);
        } else if (std::memcmp(signature.data() + 4, "ftyp", 4) == 0) {
            artwork::parseMp4Atoms(source, probes[index], source.size(), 0, best);
        }
        if (best.hasFrontCover()) {
            break;
        }
    }

    if (!best.present) {
        return std::nullopt;
    }
    return std::move(best.artwork);
}

} // namespace

std::optional<EmbeddedArtwork> tryReadEmbeddedArtwork(QByteArrayView fileBytes) noexcept
{
    try {
        if (fileBytes.isEmpty()) {
            return std::nullopt;
        }
        artwork::ArtworkByteSource source(fileBytes);
        return readFromSource(source);
    } catch (...) {
        // Allocation failures inside the parse mean "no picture", as for any
        // other unreadable input.
        return std::nullopt;
    }
}

std::optional<EmbeddedArtwork> tryReadEmbeddedArtworkFromFile(const QString& filePath) noexcept
{
    try {
        QFile file(filePath);
        if (!file.open(QIODevice::ReadOnly) || file.size() == 0) {
            return std::nullopt;
        }
        artwork::ArtworkByteSource source(file);
        return readFromSource(source);
    } catch (...) {
        return std::nullopt;
    }
}

QString artworkFileExtension(QStringView mimeType)
{
    if (mimeType == QLatin1StringView("image/png")) {
        return QStringLiteral(".png");
    }
    if (mimeType == QLatin1StringView("image/gif")) {
        return QStringLiteral(".gif");
    }
    if (mimeType == QLatin1StringView("image/bmp")) {
        return QStringLiteral(".bmp");
    }
    if (mimeType == QLatin1StringView("image/webp")) {
        return QStringLiteral(".webp");
    }
    if (mimeType == QLatin1StringView("image/tiff")) {
        return QStringLiteral(".tif");
    }
    if (mimeType == QLatin1StringView("image/x-icon")) {
        return QStringLiteral(".ico");
    }
    return QStringLiteral(".jpg");
}

} // namespace lmp::core
