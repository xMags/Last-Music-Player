#include "core/account/PortablePreferenceSettings.h"

#include "core/settings/SettingKeys.h"
#include "core/settings/SettingsStore.h"
#include "core/text/CaseFold.h"

#include <QLatin1StringView>

namespace lmp::core::account {

namespace keys = settings::keys;

QLatin1StringView settingKeyFor(PortablePreferenceKey key)
{
    switch (key) {
    case PortablePreferenceKey::Theme:
        return keys::Theme;
    case PortablePreferenceKey::Accent:
        return keys::AccentColor;
    case PortablePreferenceKey::GaplessPlayback:
        return keys::Gapless;
    case PortablePreferenceKey::DataSaver:
        return keys::DataSaver;
    case PortablePreferenceKey::Storefront:
        return keys::CatalogStorefront;
    }
    return {};
}

std::optional<PortablePreferenceKey> preferenceForSettingKey(QStringView settingKey)
{
    for (const PortablePreferenceKey key :
         {PortablePreferenceKey::Theme, PortablePreferenceKey::Accent, PortablePreferenceKey::GaplessPlayback,
          PortablePreferenceKey::DataSaver, PortablePreferenceKey::Storefront}) {
        if (settingKey == settingKeyFor(key)) {
            return key;
        }
    }
    return std::nullopt;
}

QString localPreferenceValue(const SettingsStore& settings, PortablePreferenceKey key)
{
    const auto quoted = [](const QString& value) { return QLatin1Char('"') + value + QLatin1Char('"'); };
    switch (key) {
    case PortablePreferenceKey::Theme:
        return quoted(foldCaseAscii(settings.string(keys::Theme, QStringLiteral("System"))));
    case PortablePreferenceKey::Accent: {
        QString value = settings.string(keys::AccentColor, QStringLiteral("#FF0097B2"));
        if (value.size() == 9) {
            value = QLatin1Char('#') + value.mid(3);
        }
        return quoted(value);
    }
    case PortablePreferenceKey::GaplessPlayback:
        return settings.boolean(keys::Gapless, true) ? QStringLiteral("true") : QStringLiteral("false");
    case PortablePreferenceKey::DataSaver:
        return settings.boolean(keys::DataSaver, false) ? QStringLiteral("true") : QStringLiteral("false");
    case PortablePreferenceKey::Storefront:
        return quoted(settings.string(keys::CatalogStorefront, QString(DefaultCatalogStorefront)));
    }
    return {};
}

void applyPortablePreference(SettingsStore& settings, PortablePreferenceKey key, const QString& valueJson)
{
    const std::optional<QString> validated = validatePortablePreferenceValue(key, valueJson);
    if (!validated) {
        return;
    }
    const PortablePreferences values = portablePreferencesFromValues({{key, *validated}});
    switch (key) {
    case PortablePreferenceKey::Theme:
        if (values.theme && !values.theme->isEmpty()) {
            QString value = *values.theme;
            value[0] = value.at(0).toUpper();
            settings.setString(keys::Theme, value);
        }
        return;
    case PortablePreferenceKey::Accent:
        if (values.accent) {
            settings.setString(keys::AccentColor, QStringLiteral("#FF") + values.accent->mid(1));
        }
        return;
    case PortablePreferenceKey::GaplessPlayback:
        if (values.gaplessPlayback) {
            settings.setBoolean(keys::Gapless, *values.gaplessPlayback);
        }
        return;
    case PortablePreferenceKey::DataSaver:
        if (values.dataSaver) {
            settings.setBoolean(keys::DataSaver, *values.dataSaver);
        }
        return;
    case PortablePreferenceKey::Storefront:
        if (values.storefront) {
            settings.setString(keys::CatalogStorefront, *values.storefront);
        }
        return;
    }
}

} // namespace lmp::core::account
