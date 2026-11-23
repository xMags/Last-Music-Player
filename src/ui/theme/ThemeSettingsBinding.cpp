#include "ui/theme/ThemeSettingsBinding.h"

#include "core/settings/AppearanceSettings.h"
#include "core/settings/SettingKeys.h"
#include "core/settings/SettingsStore.h"

namespace lmp::ui {

namespace keys = core::settings::keys;

ThemeSettingsBinding::ThemeSettingsBinding(core::SettingsStore& settings, QObject* parent)
    : QObject(parent)
    , m_settings(settings)
{
    connect(&m_settings, &core::SettingsStore::changed, this, [this](const QString& key) {
        if (key.isEmpty() || key == keys::Theme || key == keys::AccentColor) {
            apply();
        }
    });
}

void ThemeSettingsBinding::attach(ThemeColors* theme)
{
    m_theme = theme;
    apply();
}

ThemeColors::ThemeMode ThemeSettingsBinding::modeFor(const QString& storedTheme)
{
    switch (core::settings::themeChoiceFromStored(storedTheme)) {
    case core::settings::ThemeChoice::Light:
        return ThemeColors::ThemeMode::Light;
    case core::settings::ThemeChoice::Dark:
        return ThemeColors::ThemeMode::Dark;
    case core::settings::ThemeChoice::System:
        break;
    }
    return ThemeColors::ThemeMode::System;
}

ThemeColors::Accent ThemeSettingsBinding::accentFor(const QString& storedAccent)
{
    // The picker order is the order of the Accent values.
    return static_cast<ThemeColors::Accent>(core::settings::accentChoiceFromStored(storedAccent));
}

void ThemeSettingsBinding::apply()
{
    if (m_theme.isNull()) {
        return;
    }
    m_theme->setMode(modeFor(m_settings.string(keys::Theme)));
    m_theme->setAccent(accentFor(m_settings.string(keys::AccentColor)));
}

} // namespace lmp::ui
