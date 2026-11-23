#pragma once

#include <QImage>
#include <QQuickImageProvider>
#include <QSize>
#include <QString>

#include "core/catalog/ChartArt.h"

namespace lmp::ui {

// Drawn chart covers for QML: "image://chartart/<key>/<bg1>/<bg2>/<accent>/<accent2>",
// the colours as six hex digits without '#'. The key names one of the cover
// templates kept as resources (":/chartart/<key>.svg"); the template's
// colour placeholders take the palette, and Qt SVG draws it at the size asked
// for (184 x 184 by default). A key without a template gives an empty image.
//
// Thread safety: requestImage may run on QML's loader threads; it reads only
// resources and its arguments.
class ChartArtImageProvider final : public QQuickImageProvider {
public:
    ChartArtImageProvider();

    QImage requestImage(const QString& id, QSize* size, const QSize& requestedSize) override;

    // The image source for a visual in its palette.
    [[nodiscard]] static QString sourceFor(const core::catalog::ChartVisual& visual);
    // The template with the palette put in; empty without a template.
    [[nodiscard]] static QByteArray svgFor(const QString& key, const core::catalog::ChartPalette& palette);
};

} // namespace lmp::ui
