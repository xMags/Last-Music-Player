#pragma once

#include <QImage>
#include <QQuickImageProvider>
#include <QSize>
#include <QString>

namespace lmp::ui {

// The app's icons as pictures, for places that show one icon many times (a
// loading cover's glyph): every Image with the same source and size shares
// one cached texture, where an Icon builds a shape of its own each time.
//
// Source: image://icon/<name>/<rrggbb>[/outline], drawn at the requested
// size. "outline" strokes the parts the icon fills, at the icon's stroke
// width, so the glyph is line work only. An unknown name gives no picture.
//
// Thread-safe: it reads only the static icon tables, so the image loader
// threads may call it at once.
class IconImageProvider final : public QQuickImageProvider {
public:
    IconImageProvider();

    QImage requestImage(const QString& id, QSize* size, const QSize& requestedSize) override;

    // The SVG document an id draws (empty for an unknown name); for tests.
    [[nodiscard]] static QByteArray svgFor(const QString& id);
};

} // namespace lmp::ui
