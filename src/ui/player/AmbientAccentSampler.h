#pragma once

#include <QColor>
#include <QImage>
#include <QObject>
#include <QPointer>
#include <QString>
#include <QThreadPool>

#include <optional>

#include "core/async/RequestScope.h"
#include "core/playback/ArtworkAccent.h"
#include "ui/player/CoverSource.h"

namespace lmp::core {
class PlayerController;
}

namespace lmp::ui {

class Theme;

// Tints the panels in the current song's colour (Theme.ambient):
// - nothing playing: the theme's default ambient colour;
// - a new song: at once the tint of its placeholder gradient, then, once
//   its cover is decoded and sampled (ArtworkAccent), the tint of the cover.
//
// Lives on the GUI thread. Decoding and sampling run on its own one-thread
// pool; the answer comes back through a continuation bound to this object
// and is dropped when a newer song started meanwhile (RequestScope).
// shutdown() (also run by the destructor) drops queued work and waits for
// the running task.
class AmbientAccentSampler final : public QObject {
    Q_OBJECT

public:
    explicit AmbientAccentSampler(core::PlayerController& player, QObject* parent = nullptr);
    ~AmbientAccentSampler() override;

    AmbientAccentSampler(const AmbientAccentSampler&) = delete;
    AmbientAccentSampler& operator=(const AmbientAccentSampler&) = delete;

    // The theme is made by the QML engine after this object; it gets the
    // current tint at once. The engine owns it, so it is held guarded.
    void setTheme(Theme* theme);
    void shutdown();

    // The tint applied last; invalid while the theme's default is in use.
    [[nodiscard]] QColor ambient() const { return m_ambient; }

    // The cover's accent: the picture drawn at the sampling size, then
    // ArtworkAccent::vibrantColour. Blocking; any thread.
    [[nodiscard]] static std::optional<core::AccentRgb> sampleImage(const QImage& image);
    // The colour a gradient placeholder for `artworkId` stands for.
    [[nodiscard]] static core::AccentRgb placeholderAccent(const QString& artworkId);

signals:
    void ambientChanged();

private:
    void refresh();
    void apply(const QColor& ambient);

    core::PlayerController& m_player;
    QPointer<Theme> m_theme;
    core::RequestScope m_requests;
    QThreadPool m_pool;
    // What the tint was last worked out for, so a redraw of the same song
    // (a like, a refreshed title) does not sample again.
    std::optional<QString> m_identity;
    CoverSource m_source;
    QColor m_ambient;
    bool m_shutDown = false;
};

} // namespace lmp::ui
