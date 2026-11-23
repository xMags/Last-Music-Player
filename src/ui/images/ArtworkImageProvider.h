#pragma once

#include <QImage>
#include <QMutex>
#include <QQuickAsyncImageProvider>
#include <QQuickImageResponse>
#include <QSize>
#include <QString>

namespace lmp::core::remote {
class ArtworkService;
}

namespace lmp::ui {

// Serves "image://artwork/<id>" (CoverSource::Remote): the picture at the
// http(s) address the id names, fetched by core::remote::ArtworkService (the
// account relay in Account mode, the address itself otherwise) and decoded
// at the size the image asks for, on a worker thread. The engine owns the
// provider once added; the service must outlive the engine.
class ArtworkImageProvider final : public QQuickAsyncImageProvider {
public:
    explicit ArtworkImageProvider(core::remote::ArtworkService& service);

    [[nodiscard]] QQuickImageResponse* requestImageResponse(const QString& id, const QSize& requestedSize) override;

    // The picture in `bytes`, scaled down (never up) to cover `fit` when
    // `fit` is valid. A null image when the bytes are not a picture.
    [[nodiscard]] static QImage decode(const QByteArray& bytes, QSize fit);

private:
    core::remote::ArtworkService& m_service;
};

// One picture on its way. Lives on the service's thread; the engine may
// delete it at any time, and nothing runs for it after that.
class ArtworkImageResponse final : public QQuickImageResponse {
    Q_OBJECT

public:
    ArtworkImageResponse(core::remote::ArtworkService& service, QString url, QSize requestedSize);

    [[nodiscard]] QQuickTextureFactory* textureFactory() const override;
    [[nodiscard]] QString errorString() const override;

private:
    void start();
    void deliver(const QImage& image, const QString& error);

    core::remote::ArtworkService& m_service;
    QString m_url;
    QSize m_requestedSize;
    mutable QMutex m_lock;
    QImage m_image;
    QString m_error;
};

} // namespace lmp::ui
