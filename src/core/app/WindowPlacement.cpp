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
    return saved;
}

bool WindowPlacement::save(SettingsStore& settings, const QRect& frame)
{
    if (!usableSize(frame.size())) {
        return false;
    }
    settings.setInteger(keys::WindowWidth, frame.width());
    settings.setInteger(keys::WindowHeight, frame.height());
    settings.setInteger(keys::WindowX, frame.x());
    settings.setInteger(keys::WindowY, frame.y());
    return true;
}

} // namespace lmp::core
