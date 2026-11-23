#include "ui/images/ChartArtImageProvider.h"

#include <QColor>
#include <QFile>
#include <QPainter>
#include <QStringList>
#include <QSvgRenderer>

namespace lmp::ui {

namespace {

constexpr int kDefaultSide = 184;

[[nodiscard]] bool isKey(const QString& key)
{
    return !key.isEmpty() && std::all_of(key.cbegin(), key.cend(), [](QChar character) {
        return (character >= QLatin1Char('a') && character <= QLatin1Char('z'))
            || (character >= QLatin1Char('0') && character <= QLatin1Char('9')) || character == QLatin1Char('-');
    });
}

[[nodiscard]] bool isHexColour(const QString& value)
{
    return value.size() == 6 && std::all_of(value.cbegin(), value.cend(), [](QChar character) {
               return (character >= QLatin1Char('0') && character <= QLatin1Char('9'))
                   || (character >= QLatin1Char('a') && character <= QLatin1Char('f'));
           });
}

[[nodiscard]] QString bare(const QString& colour)
{
    return colour.startsWith(QLatin1Char('#')) ? colour.mid(1).toLower() : colour.toLower();
}

} // namespace

ChartArtImageProvider::ChartArtImageProvider()
    : QQuickImageProvider(QQuickImageProvider::Image)
{
}

QString ChartArtImageProvider::sourceFor(const core::catalog::ChartVisual& visual)
{
    const core::catalog::ChartPalette palette = core::catalog::chartPalette(visual);
    // City skylines are kept as "city-<landmark>".
    const QString key = visual.family == core::catalog::ChartFamily::City ? QStringLiteral("city-") + visual.visualKey
                                                                          : visual.visualKey;
    return QStringLiteral("image://chartart/%1/%2/%3/%4/%5")
        .arg(key, bare(palette.background1), bare(palette.background2), bare(palette.accent),
             bare(palette.accent2));
}

QByteArray ChartArtImageProvider::svgFor(const QString& key, const core::catalog::ChartPalette& palette)
{
    if (!isKey(key)) {
        return {};
    }
    QFile file(QStringLiteral(":/chartart/%1.svg").arg(key));
    if (!file.open(QIODevice::ReadOnly)) {
        return {};
    }
    QString text = QString::fromUtf8(file.readAll());
    // The label band fades into the second background, darkened 6 percent.
    const QColor background2(palette.background2);
    const QColor darker = QColor::fromRgbF(background2.redF() * 0.94F, background2.greenF() * 0.94F,
                                           background2.blueF() * 0.94F);
    text.replace(QStringLiteral("{bg1}"), palette.background1);
    text.replace(QStringLiteral("{bg2dark}"), darker.name());
    text.replace(QStringLiteral("{bg2}"), palette.background2);
    text.replace(QStringLiteral("{accent2}"), palette.accent2);
    text.replace(QStringLiteral("{accent}"), palette.accent);
    return text.toUtf8();
}

QImage ChartArtImageProvider::requestImage(const QString& id, QSize* size, const QSize& requestedSize)
{
    const QStringList parts = id.split(QLatin1Char('/'));
    QImage image;
    if (parts.size() == 5 && isHexColour(parts.at(1)) && isHexColour(parts.at(2)) && isHexColour(parts.at(3))
        && isHexColour(parts.at(4))) {
        const core::catalog::ChartPalette palette{QLatin1Char('#') + parts.at(1), QLatin1Char('#') + parts.at(2),
                                                  QLatin1Char('#') + parts.at(3), QLatin1Char('#') + parts.at(4)};
        QSvgRenderer renderer(svgFor(parts.at(0), palette));
        if (renderer.isValid()) {
            const int width = requestedSize.width() > 0 ? requestedSize.width() : kDefaultSide;
            const int height = requestedSize.height() > 0 ? requestedSize.height() : width;
            image = QImage(width, height, QImage::Format_ARGB32_Premultiplied);
            image.fill(Qt::transparent);
            QPainter painter(&image);
            painter.setRenderHint(QPainter::Antialiasing);
            renderer.render(&painter);
        }
    }
    if (size != nullptr) {
        *size = image.size();
    }
    return image;
}

} // namespace lmp::ui
