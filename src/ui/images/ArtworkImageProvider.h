#pragma once

#include <QImage>
#include <QMutex>
#include <QQuickAsyncImageProvider>
#include <QQuickImageResponse>
#include <QSize>
#include <QString>

#include <atomic>

#include "ui/images/CoverWash.h"

namespace lmp::core::remote {
class ArtworkService;
}

namespace lmp::ui {

// Serves "image://artwork/<id>" (CoverSource::Remote): the picture at the
// http(s) address the id names, fetched by core::remote::ArtworkService (the
// account relay in Account mode, the address itself otherwise) and decoded
// at the size the image asks for, on a worker thread. With CoverOutput::Wash
// (registered as "artwork-wash") it serves the picture's colour wash
// instead. The engine owns the provider once added; the service must
// outlive the engine.
class ArtworkImageProvider final : public QQuickAsyncImageProvider {
public:
    explicit ArtworkImageProvider(core::remote::ArtworkService& service, CoverOutput output = CoverOutput::Picture);

    [[nodiscard]] QQuickImageResponse* requestImageResponse(const QString& id, const QSize& requestedSize) override;

    // The picture in `bytes`, scaled down (never up) to cover `fit` when
    // `fit` is valid. A null image when the bytes are not a picture.
    [[nodiscard]] static QImage decode(const QByteArray& bytes, QSize fit);

private:
    core::remote::ArtworkService& m_service;
    CoverOutput m_output;
};

// One picture on its way. Lives on the service's thread and finishes there,
// once; the engine deletes it after that, and nothing runs for it then.
class ArtworkImageResponse final : public QQuickImageResponse {
    Q_OBJECT

public:
    ArtworkImageResponse(core::remote::ArtworkService& service, QString url, QSize requestedSize, CoverOutput output);

    [[nodiscard]] QQuickTextureFactory* textureFactory() const override;
    [[nodiscard]] QString errorString() const override;
    // The engine no longer wants the picture; the response finishes without it.
    void cancel() override;

private:
    void start();
    void deliver(const QImage& image, const QString& error);

    core::remote::ArtworkService& m_service;
    QString m_url;
    QSize m_requestedSize;
    CoverOutput m_output;
    mutable QMutex m_lock;
    QImage m_image;
    QString m_error;
    // Set by cancel() on the engine's thread, read on this one.
    std::atomic_bool m_cancelled{false};
    // This object's thread only.
    bool m_delivered = false;
};

} // namespace lmp::ui
