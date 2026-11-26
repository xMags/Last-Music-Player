#include "ui/player/EmbeddedCoverImageProvider.h"

#include <optional>

#include "ui/player/CoverSource.h"

namespace lmp::ui {

EmbeddedCoverImageProvider::EmbeddedCoverImageProvider(CoverOutput output)
    : QQuickImageProvider(QQuickImageProvider::Image, QQmlImageProviderBase::ForceAsynchronousImageLoading)
    , m_output(output)
{
}

QImage EmbeddedCoverImageProvider::requestImage(const QString& id, QSize* size, const QSize& requestedSize)
{
    const std::optional<QString> path = CoverSource::embeddedPathFromProviderId(id);
    QImage image;
    if (path) {
        const CoverSource source{.kind = CoverSource::Kind::Embedded, .path = *path, .url = {}};
        image = m_output == CoverOutput::Wash ? coverWash(source.load(), {.size = coverWashSize(requestedSize)})
                                              : source.load(requestedSize);
    }
    if (size != nullptr) {
        *size = image.size();
    }
    // A null image makes the Image item report an error, so the placeholder shows.
    return image;
}

} // namespace lmp::ui
