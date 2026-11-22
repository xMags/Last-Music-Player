#include "ui/player/AmbientAccentSampler.h"

#include <QFuture>
#include <QSize>
#include <QtConcurrent/QtConcurrentRun>

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

#include "core/model/ArtworkPlaceholder.h"
#include "core/playback/PlayerController.h"
#include "ui/theme/Theme.h"
#include "ui/theme/ThemePalette.h"

namespace lmp::ui {

namespace {

// Covers are decoded no larger than this before they are drawn small.
constexpr int kDecodeSize = 256;

[[nodiscard]] QColor toColor(core::AccentRgb rgb)
{
    return QColor(rgb.red, rgb.green, rgb.blue);
}

} // namespace

AmbientAccentSampler::AmbientAccentSampler(core::PlayerController& player, QObject* parent)
    : QObject(parent)
    , m_player(player)
{
    m_pool.setMaxThreadCount(1);
    connect(&m_player, &core::PlayerController::currentTrackChanged, this, &AmbientAccentSampler::refresh);
    refresh();
}

AmbientAccentSampler::~AmbientAccentSampler()
{
    shutdown();
}

void AmbientAccentSampler::setTheme(Theme* theme)
{
    m_theme = theme;
    if (m_theme.isNull()) {
        return;
    }
    if (m_ambient.isValid()) {
        m_theme->setAmbient(m_ambient);
    } else {
        m_theme->resetAmbient();
    }
}

void AmbientAccentSampler::shutdown()
{
    if (m_shutDown) {
        return;
    }
    m_shutDown = true;
    m_requests.invalidate();
    m_pool.clear();
    m_pool.waitForDone();
}

std::optional<core::AccentRgb> AmbientAccentSampler::sampleImage(const QImage& image)
{
    if (image.isNull()) {
        return std::nullopt;
    }
    constexpr int side = core::ArtworkAccent::SampleSize;
    // Drawn to fill the square whatever its shape, as the design samples it;
    // canvas pixels are read back without premultiplied alpha.
    const QImage small = image.scaled(side, side, Qt::IgnoreAspectRatio, Qt::SmoothTransformation)
                             .convertToFormat(QImage::Format_RGBA8888);
    if (small.isNull()) {
        return std::nullopt;
    }
    std::vector<std::uint8_t> pixels;
    pixels.reserve(static_cast<std::size_t>(side) * side * 4);
    for (int row = 0; row < small.height(); ++row) {
        const uchar* line = small.constScanLine(row);
        pixels.insert(pixels.end(), line, line + static_cast<std::ptrdiff_t>(small.width()) * 4);
    }
    return core::ArtworkAccent::vibrantColour(std::span<const std::uint8_t>(pixels));
}

core::AccentRgb AmbientAccentSampler::placeholderAccent(const QString& artworkId)
{
    const int index = core::ArtworkPlaceholder::gradientIndexForId(artworkId, static_cast<int>(kArtworkGradientCount));
    const QColor start = artworkGradients().at(static_cast<std::size_t>(index)).first;
    return core::AccentRgb{start.red(), start.green(), start.blue()};
}

void AmbientAccentSampler::refresh()
{
    if (m_shutDown) {
        return;
    }
    const std::optional<core::Track>& track = m_player.currentTrack();
    if (!track) {
        m_identity.reset();
        m_source = {};
        m_requests.invalidate();
        apply(QColor());
        return;
    }
    const QString identity = CoverSource::placeholderIdFor(*track);
    const CoverSource source = CoverSource::forTrack(*track);
    if (m_identity == identity && m_source == source) {
        return;
    }
    m_identity = identity;
    m_source = source;

    // The placeholder's colour first, so the tint moves at once.
    apply(toColor(core::ArtworkAccent::ambientFor(placeholderAccent(identity))));
    const core::RequestScope::Token token = m_requests.next();
    if (!source.decodesLocally()) {
        // Remote covers are not fetched for sampling yet; the placeholder's
        // tint stays.
        return;
    }
    QtConcurrent::run(&m_pool, [source]() { return sampleImage(source.load(QSize(kDecodeSize, kDecodeSize))); })
        .then(this, [this, token](const std::optional<core::AccentRgb>& accent) {
            if (!m_requests.isCurrent(token) || !accent) {
                return;
            }
            apply(toColor(core::ArtworkAccent::ambientFor(*accent)));
        });
}

void AmbientAccentSampler::apply(const QColor& ambient)
{
    if (ambient == m_ambient) {
        return;
    }
    m_ambient = ambient;
    if (m_theme) {
        if (m_ambient.isValid()) {
            m_theme->setAmbient(m_ambient);
        } else {
            m_theme->resetAmbient();
        }
    }
    emit ambientChanged();
}

} // namespace lmp::ui
