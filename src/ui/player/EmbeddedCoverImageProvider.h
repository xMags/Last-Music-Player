#pragma once

#include <QImage>
#include <QQuickImageProvider>
#include <QSize>
#include <QString>

#include "ui/images/CoverWash.h"

namespace lmp::ui {

// Serves "image://embedded-cover/<id>": the cover picture stored inside a
// local audio file (CoverSource::Embedded names the file in the id). The
// engine calls it on its image loading thread, never on the UI thread
// (loading is forced to be asynchronous), and owns it once added. With
// CoverOutput::Wash (registered as "embedded-cover-wash") it serves the
// cover's colour wash instead.
class EmbeddedCoverImageProvider final : public QQuickImageProvider {
public:
    explicit EmbeddedCoverImageProvider(CoverOutput output = CoverOutput::Picture);

    [[nodiscard]] QImage requestImage(const QString& id, QSize* size, const QSize& requestedSize) override;

private:
    CoverOutput m_output;
};

} // namespace lmp::ui
