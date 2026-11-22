#pragma once

#include <QString>
#include <QStringList>

namespace lmp::ui {

// Registers the fonts that ship inside the application with the font
// database. Call once at start-up, after the application object exists and
// before any QML is loaded.
class FontLoader {
public:
    // Returns the family names that were registered. A font that fails to
    // load is logged and skipped; the text then falls back to the next family
    // in its stack.
    static QStringList loadBundledFonts();

    // The first family in `stack` that the font database knows, or the last
    // entry when none is installed (the database then picks a substitute).
    [[nodiscard]] static QString firstAvailableFamily(const QStringList& stack);
};

} // namespace lmp::ui
