#include "ui/player/CoverSource.h"

#include <QByteArray>
#include <QDir>
#include <QFileInfo>
#include <QImageReader>

#include "core/library/EmbeddedArtwork.h"
#include "core/playback/QueueTrackIdentity.h"

namespace lmp::ui {

namespace {

constexpr QByteArray::Base64Options kIdEncoding =
    QByteArray::Base64UrlEncoding | QByteArray::OmitTrailingEquals;

[[nodiscard]] QImage fitWithin(QImage image, QSize fit)
{
    if (image.isNull() || !fit.isValid() || fit.isEmpty()) {
        return image;
    }
    const QSize covering = image.size().scaled(fit, Qt::KeepAspectRatioByExpanding);
    if (covering.width() >= image.width()) {
        return image; // never scaled up
    }
    return image.scaled(covering, Qt::KeepAspectRatio, Qt::SmoothTransformation);
}

} // namespace

CoverSource CoverSource::forTrack(const core::Track& track)
{
    const QString artwork = track.artworkUrl.trimmed();
    if (!artwork.isEmpty()) {
        const QUrl url(artwork);
        const QString scheme = url.scheme().toLower();
        if (scheme == QLatin1StringView("http") || scheme == QLatin1StringView("https")) {
            return CoverSource{.kind = Kind::Remote, .path = {}, .url = url};
        }
        if (url.isLocalFile()) {
            return CoverSource{.kind = Kind::ImageFile, .path = url.toLocalFile(), .url = {}};
        }
        if (QDir::isAbsolutePath(artwork)) {
            return CoverSource{.kind = Kind::ImageFile, .path = artwork, .url = {}};
        }
        // Anything else (an unknown scheme) is not shown.
    }
    if (!track.isRemote() && !track.filePath.isEmpty()) {
        return CoverSource{.kind = Kind::Embedded, .path = track.filePath, .url = {}};
    }
    return {};
}

CoverSource CoverSource::forLibraryTrack(const core::Track& track)
{
    CoverSource source = forTrack(track);
    if (source.kind == Kind::Embedded) {
        return {};
    }
    return source;
}

QString CoverSource::placeholderIdFor(const core::Track& track)
{
    const QString identity = core::queueTrackIdentity(track);
    return identity.isEmpty() ? track.title : identity;
}

std::optional<QString> CoverSource::embeddedPathFromProviderId(QStringView id)
{
    const QByteArray::FromBase64Result decoded =
        QByteArray::fromBase64Encoding(id.toLatin1(), kIdEncoding | QByteArray::AbortOnBase64DecodingErrors);
    if (!decoded) {
        return std::nullopt;
    }
    const QString path = QString::fromUtf8(*decoded);
    if (path.isEmpty() || !QDir::isAbsolutePath(path)) {
        return std::nullopt;
    }
    return path;
}

QUrl CoverSource::displayUrl() const
{
    switch (kind) {
    case Kind::Remote:
        return url;
    case Kind::ImageFile:
        return QUrl::fromLocalFile(path);
    case Kind::Embedded:
        return QUrl(QStringLiteral("image://%1/%2")
                        .arg(ProviderId, QString::fromLatin1(path.toUtf8().toBase64(kIdEncoding))));
    case Kind::None:
        break;
    }
    return {};
}

bool CoverSource::decodesLocally() const
{
    return kind == Kind::ImageFile || kind == Kind::Embedded;
}

QImage CoverSource::load(QSize fit) const
{
    if (kind == Kind::ImageFile) {
        if (!QFileInfo(path).isFile()) {
            return {};
        }
        QImageReader reader(path);
        reader.setAutoTransform(true);
        return fitWithin(reader.read(), fit);
    }
    if (kind == Kind::Embedded) {
        const std::optional<core::EmbeddedArtwork> artwork = core::tryReadEmbeddedArtworkFromFile(path);
        if (!artwork) {
            return {};
        }
        return fitWithin(QImage::fromData(artwork->bytes), fit);
    }
    return {};
}

} // namespace lmp::ui
