#pragma once

#include <QRect>

#include <optional>

namespace lmp::core {

class SettingsStore;

// The main window's size and place between runs, as the current app keeps
// them (MainWindow.xaml.cpp): WindowWidth, WindowHeight, WindowX and WindowY
// in physical pixels, the outer frame included. A size under 800 by 600 is
// neither saved nor used, a minimised window is not saved, and a saved place
// is used only while some monitor still shows it (otherwise Windows places
// the window). A maximised window keeps the last frame it had before it was
// maximised and is saved as maximised (WindowMaximized), so it opens
// maximised and restores to that frame. Pure rules; the window calls live in
// the platform layer.
struct WindowPlacement {
    static constexpr int MinimumWidth = 800;
    static constexpr int MinimumHeight = 600;

    // The saved frame: its size when that is usable, its corner when one was saved.
    struct Saved {
        std::optional<QSize> size;
        std::optional<QPoint> position;
        bool maximized = false;
    };

    [[nodiscard]] static Saved load(const SettingsStore& settings);
    // Saves `frame` unless it is too small; false when nothing was saved.
    // Writes only what changed.
    static bool save(SettingsStore& settings, const QRect& frame);
    static void saveMaximized(SettingsStore& settings, bool maximized);
    [[nodiscard]] static bool usableSize(const QSize& size);
};

} // namespace lmp::core
