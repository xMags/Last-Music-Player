#include "core/app/WindowPlacement.h"

#include <climits>

#include "core/settings/SettingKeys.h"
#include "core/settings/SettingsStore.h"

namespace lmp::core {

namespace keys = settings::keys;

bool WindowPlacement::usableSize(const QSize& size)
{
    return size.width() >= MinimumWidth && size.height() >= MinimumHeight;
}

WindowPlacement::Saved WindowPlacement::load(const SettingsStore& settings)
{
    Saved saved;
    const QSize size(settings.integer(keys::WindowWidth, 0), settings.integer(keys::WindowHeight, 0));
    if (usableSize(size)) {
        saved.size = size;
    }
    // The current app reads a missing corner as INT_MIN.
    const int x = settings.integer(keys::WindowX, INT_MIN);
    const int y = settings.integer(keys::WindowY, INT_MIN);
    if (x != INT_MIN && y != INT_MIN) {
        saved.position = QPoint(x, y);
    }
    saved.maximized = settings.boolean(keys::WindowMaximized, false);
    return saved;
}

bool WindowPlacement::save(SettingsStore& settings, const QRect& frame)
{
    if (!usableSize(frame.size())) {
        return false;
    }
    // Each write saves the file, so an unchanged value is not written again.
    const auto keep = [&settings](QLatin1StringView key, int value) {
        if (!settings.contains(key) || settings.integer(key, 0) != value) {
            settings.setInteger(key, value);
        }
    };
    keep(keys::WindowWidth, frame.width());
    keep(keys::WindowHeight, frame.height());
    keep(keys::WindowX, frame.x());
    keep(keys::WindowY, frame.y());
    return true;
}

void WindowPlacement::saveMaximized(SettingsStore& settings, bool maximized)
{
    if (!settings.contains(keys::WindowMaximized) || settings.boolean(keys::WindowMaximized, false) != maximized) {
        settings.setBoolean(keys::WindowMaximized, maximized);
    }
}

} // namespace lmp::core
