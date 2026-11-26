#include "ui/images/ArtworkImageProvider.h"

#include "core/remote/ArtworkService.h"
#include "ui/player/CoverSource.h"

#include <QBuffer>
#include <QFuture>
#include <QImageReader>
#include <QMetaObject>
#include <QMutexLocker>
#include <QtConcurrent/QtConcurrentRun>

#include <optional>
#include <utility>

namespace lmp::ui {

ArtworkImageProvider::ArtworkImageProvider(core::remote::ArtworkService& service, CoverOutput output)
    : m_service(service)
    , m_output(output)
{
}

QQuickImageResponse* ArtworkImageProvider::requestImageResponse(const QString& id, const QSize& requestedSize)
{
    const std::optional<QString> url = CoverSource::remoteUrlFromProviderId(id);
    return new ArtworkImageResponse(m_service, url.value_or(QString()), requestedSize, m_output);
}

QImage ArtworkImageProvider::decode(const QByteArray& bytes, QSize fit)
{
    QBuffer buffer;
    buffer.setData(bytes);
    if (!buffer.open(QIODevice::ReadOnly)) {
        return {};
    }
    QImageReader reader(&buffer);
    reader.setAutoTransform(true);
    // Decoded at full size, then reduced with area averaging. Asking the
    // reader for the smaller size instead lets the JPEG decoder shrink the
    // picture itself, which measured 15-25% less fine detail at tile sizes.
    const QImage image = reader.read();
    if (image.isNull() || !fit.isValid() || fit.isEmpty()) {
        return image;
    }
    const QSize covering = image.size().scaled(fit, Qt::KeepAspectRatioByExpanding);
    if (covering.width() >= image.width()) {
        return image; // never scaled up
    }
    return image.scaled(covering, Qt::KeepAspectRatio, Qt::SmoothTransformation);
}

ArtworkImageResponse::ArtworkImageResponse(core::remote::ArtworkService& service, QString url, QSize requestedSize,
                                           CoverOutput output)
    : m_service(service)
    , m_url(std::move(url))
    , m_requestedSize(requestedSize)
    , m_output(output)
{
    // The engine asks on its image thread; the service answers on its own.
    // Posted rather than called: a response the engine deletes first never
    // starts.
    moveToThread(service.thread());
    QMetaObject::invokeMethod(this, &ArtworkImageResponse::start, Qt::QueuedConnection);
}

void ArtworkImageResponse::cancel()
{
    // Called on the engine's image thread. The engine deletes a response only
    // once it has finished, so a cancelled one still finishes, on its own
    // thread like every other answer, and skips any work not yet started.
    m_cancelled.store(true);
    QMetaObject::invokeMethod(
        this, [this]() { deliver({}, QStringLiteral("cancelled")); }, Qt::QueuedConnection);
}

void ArtworkImageResponse::start()
{
    if (m_cancelled.load()) {
        return; // finished by cancel()
    }
    if (m_url.isEmpty()) {
        deliver({}, QStringLiteral("not a picture address"));
        return;
    }
    const QSize fit = m_requestedSize;
    const CoverOutput output = m_output;
    m_service.fetch(m_url).then(this, [this, fit, output](const core::Result<QByteArray>& bytes) {
        if (!bytes) {
            deliver({}, QStringLiteral("picture could not be fetched"));
            return;
        }
        if (m_cancelled.load()) {
            return; // finished by cancel(); no picture to decode for
        }
        // Decoding can take a while for a large picture: off this thread.
        QtConcurrent::run([data = bytes.value(), fit, output]() {
            if (output == CoverOutput::Wash) {
                return coverWash(ArtworkImageProvider::decode(data, {}), {.size = coverWashSize(fit)});
            }
            return ArtworkImageProvider::decode(data, fit);
        })
            .then(this, [this](const QImage& image) {
                deliver(image, image.isNull() ? QStringLiteral("not a picture") : QString());
            });
    });
}

void ArtworkImageResponse::deliver(const QImage& image, const QString& error)
{
    // Exactly once: the engine deletes the response after its first finish.
    if (m_delivered) {
        return;
    }
    m_delivered = true;
    {
        const QMutexLocker locker(&m_lock);
        m_image = image;
        m_error = error;
    }
    emit finished();
}

QQuickTextureFactory* ArtworkImageResponse::textureFactory() const
{
    const QMutexLocker locker(&m_lock);
    return QQuickTextureFactory::textureFactoryForImage(m_image);
}

QString ArtworkImageResponse::errorString() const
{
    const QMutexLocker locker(&m_lock);
    return m_error;
}

} // namespace lmp::ui
