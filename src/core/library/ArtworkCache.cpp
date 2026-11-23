#include "core/library/ArtworkCache.h"

#include <QByteArray>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>
#include <QUrl>

#include <array>
#include <optional>
#include <utility>

#include "core/library/EmbeddedArtwork.h"
#include "platform/IShellThumbnail.h"

namespace lmp::core {

namespace {

// The picture formats the cache can hold; a lookup probes each, because a
// file's name records the format its picture really had.
constexpr std::array<QLatin1StringView, 7> kExtensions{
    QLatin1StringView(".jpg"), QLatin1StringView(".png"), QLatin1StringView(".webp"), QLatin1StringView(".gif"),
    QLatin1StringView(".bmp"), QLatin1StringView(".tif"), QLatin1StringView(".ico"),
};

constexpr QLatin1StringView kGeneration{"-v2"};

QString fileAddress(const QString& path)
{
    return QUrl::fromLocalFile(path).toString();
}

} // namespace

ArtworkCache::ArtworkCache(QString thumbsFolder, platform::IShellThumbnail* shellThumbnails)
    : m_folder(std::move(thumbsFolder))
    , m_shellThumbnails(shellThumbnails)
{
}

QString ArtworkCache::cacheKey(const QString& filePath)
{
    const QByteArray digest = QCryptographicHash::hash(filePath.toUtf8(), QCryptographicHash::Sha1).toHex();
    return QString::fromLatin1(digest.left(16)) + kGeneration;
}

QString ArtworkCache::cachedFile(const QString& key) const
{
    const QDir folder(m_folder);
    for (const QLatin1StringView extension : kExtensions) {
        const QFileInfo candidate(folder.filePath(key + extension));
        if (candidate.isFile() && candidate.size() > 0) {
            return candidate.absoluteFilePath();
        }
    }
    return {};
}

QString ArtworkCache::ensure(const QString& filePath) const
{
    if (filePath.isEmpty() || !QDir().mkpath(m_folder)) {
        return {};
    }
    const QString key = cacheKey(filePath);
    if (const QString cached = cachedFile(key); !cached.isEmpty()) {
        return fileAddress(cached);
    }

    QByteArray bytes;
    QString extension = QStringLiteral(".jpg");
    if (const std::optional<EmbeddedArtwork> embedded = tryReadEmbeddedArtworkFromFile(filePath)) {
        bytes = embedded->bytes;
        extension = artworkFileExtension(embedded->mimeType);
    } else if (m_shellThumbnails != nullptr) {
        if (std::optional<QByteArray> thumbnail = m_shellThumbnails->thumbnail(filePath)) {
            bytes = std::move(*thumbnail);
        }
    }
    if (bytes.isEmpty()) {
        return {};
    }

    const QString target = QDir(m_folder).filePath(key + extension);
    QSaveFile out(target);
    if (!out.open(QIODevice::WriteOnly) || out.write(bytes) != bytes.size() || !out.commit()) {
        return {};
    }
    return fileAddress(QFileInfo(target).absoluteFilePath());
}

} // namespace lmp::core
