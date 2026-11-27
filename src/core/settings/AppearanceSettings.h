#pragma once

#include <QString>

namespace lmp::core::settings {

// The theme choice under the Theme key: the strings "System", "Light" and
// "Dark", compared exactly as earlier versions did. Anything else, including
// a missing key, reads as System.
enum class ThemeChoice { System, Light, Dark };

[[nodiscard]] ThemeChoice themeChoiceFromStored(const QString& stored);
[[nodiscard]] QString storedThemeChoice(ThemeChoice choice);

// The window background under the WindowBackdrop key: the strings "Solid",
// "Mica" and "Acrylic", compared exactly. Anything else, including a missing
// key, reads as Solid.
enum class BackdropChoice { Solid, Mica, Acrylic };

[[nodiscard]] BackdropChoice backdropChoiceFromStored(const QString& stored);
[[nodiscard]] QString storedBackdropChoice(BackdropChoice choice);

// The five accent choices, in the order of the picker: teal, purple, coral,
// green, amber. Index 0, teal, is the default.
inline constexpr int AccentChoiceCount = 5;

// The choice stored under AccentColor ("#AARRGGBB"). Earlier versions offered
// five colours of their own in the same order as the picker here, so both
// those colours and the picker's own seed colours name a choice; the alpha
// pair and letter case are ignored. Any other value reads as teal.
[[nodiscard]] int accentChoiceFromStored(const QString& stored);

// The value saved for a choice: the colour earlier versions use for the same
// place in the picker, as "#AARRGGBB", so an installed earlier version still
// shows the choice. An index out of range saves teal.
[[nodiscard]] QString storedAccentChoice(int index);

} // namespace lmp::core::settings
