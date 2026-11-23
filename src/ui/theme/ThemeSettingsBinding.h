#pragma once

#include <QObject>
#include <QPointer>
#include <QString>

#include "ui/theme/ThemeColors.h"

namespace lmp::core {
class SettingsStore;
}

namespace lmp::ui {

// Keeps the theme on the saved choice: applies the Theme and AccentColor
// settings when a theme is attached (at start-up, before the first frame)
// and again whenever either key changes or the settings are reset, whoever
// changed them. Settings screens only write the keys.
//
// Lives on the GUI thread with the theme. The theme is owned by the QML
// engine, so it is held by a guarded pointer.
class ThemeSettingsBinding : public QObject {
    Q_OBJECT

public:
    // The settings are owned by the composition root and outlive this.
    explicit ThemeSettingsBinding(core::SettingsStore& settings, QObject* parent = nullptr);

    void attach(ThemeColors* theme);

    // The theme mode and accent a stored choice stands for.
    [[nodiscard]] static ThemeColors::ThemeMode modeFor(const QString& storedTheme);
    [[nodiscard]] static ThemeColors::Accent accentFor(const QString& storedAccent);

private:
    void apply();

    core::SettingsStore& m_settings;
    QPointer<ThemeColors> m_theme;
};

} // namespace lmp::ui
