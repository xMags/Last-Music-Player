#include "ui/settings/SettingsViewModel.h"

#include <QColor>

#include "core/account/ProfileIdentity.h"
#include "core/mode/ModeService.h"
#include "core/mode/SourceMode.h"
#include "core/playback/PlayerController.h"
#include "core/settings/AppearanceSettings.h"
#include "core/settings/PreferenceDefaults.h"
#include "core/settings/SettingKeys.h"
#include "core/settings/SettingsStore.h"
#include "ui/theme/ThemePalette.h"

namespace lmp::ui {

namespace {

namespace keys = core::settings::keys;

// The defaults earlier versions read with.
constexpr bool GaplessByDefault = true;
constexpr bool ShowAlbumArtByDefault = true;
constexpr bool MediaControlsByDefault = true;
constexpr int CloseToTray = 0;
constexpr int CloseQuits = 1;


} // namespace

SettingsViewModel::SettingsViewModel(core::SettingsStore& settings, core::PlayerController& player,
                                     const core::mode::ModeService& mode, QObject* parent)
    : QObject(parent)
    , m_settings(settings)
    , m_player(player)
    , m_mode(mode)
{
    connect(&m_mode, &core::mode::ModeService::changed, this, &SettingsViewModel::preferencesChanged);
    // Any key may back a row, and the store says "all of them" after a
    // reset or reload, so every change refreshes the rows.
    connect(&m_settings, &core::SettingsStore::changed, this, &SettingsViewModel::preferencesChanged);
    connect(&m_player, &core::PlayerController::autoplayChanged, this, &SettingsViewModel::preferencesChanged);
}

QString SettingsViewModel::theme() const
{
    return core::settings::storedThemeChoice(core::settings::themeChoiceFromStored(m_settings.string(keys::Theme)));
}

void SettingsViewModel::setTheme(const QString& theme)
{
    const QString stored = core::settings::storedThemeChoice(core::settings::themeChoiceFromStored(theme));
    if (m_settings.string(keys::Theme) == stored) {
        return;
    }
    m_settings.setString(keys::Theme, stored);
}

int SettingsViewModel::accentIndex() const
{
    return core::settings::accentChoiceFromStored(m_settings.string(keys::AccentColor));
}

void SettingsViewModel::setAccentIndex(int index)
{
    if (index < 0 || index >= core::settings::AccentChoiceCount) {
        return;
    }
    const QString stored = core::settings::storedAccentChoice(index);
    if (m_settings.string(keys::AccentColor) == stored) {
        return;
    }
    m_settings.setString(keys::AccentColor, stored);
}

QVariantList SettingsViewModel::accentSeeds() const
{
    QVariantList seeds;
    for (const BrandAccent& accent : brandAccents()) {
        seeds.append(QVariant::fromValue(accent.seed));
    }
    return seeds;
}

QStringList SettingsViewModel::accentNames() const
{
    return {tr("Teal"), tr("Purple"), tr("Coral"), tr("Green"), tr("Amber")};
}

bool SettingsViewModel::autoplay() const
{
    return m_player.autoplay();
}

void SettingsViewModel::setAutoplay(bool autoplay)
{
    if (m_player.autoplay() == autoplay) {
        return;
    }
    // Through the player, which keeps the value it acts on.
    m_player.setAutoplay(autoplay);
}

bool SettingsViewModel::gapless() const
{
    return m_settings.boolean(keys::Gapless, GaplessByDefault);
}

void SettingsViewModel::setGapless(bool gapless)
{
    if (this->gapless() == gapless && m_settings.contains(keys::Gapless)) {
        return;
    }
    m_settings.setBoolean(keys::Gapless, gapless);
}

bool SettingsViewModel::showAlbumArt() const
{
    return m_settings.boolean(keys::ShowAlbumArt, ShowAlbumArtByDefault);
}

void SettingsViewModel::setShowAlbumArt(bool show)
{
    if (showAlbumArt() == show && m_settings.contains(keys::ShowAlbumArt)) {
        return;
    }
    m_settings.setBoolean(keys::ShowAlbumArt, show);
}

bool SettingsViewModel::mediaControls() const
{
    return m_settings.boolean(keys::WindowsMediaControls, MediaControlsByDefault);
}

void SettingsViewModel::setMediaControls(bool enabled)
{
    if (mediaControls() == enabled && m_settings.contains(keys::WindowsMediaControls)) {
        return;
    }
    m_settings.setBoolean(keys::WindowsMediaControls, enabled);
}

int SettingsViewModel::closeBehavior() const
{
    // Earlier versions closed to the tray only on 0 and quit on any other
    // number, so every other number reads as Quit.
    return m_settings.integer(keys::CloseBehavior, CloseToTray) == CloseToTray ? CloseToTray : CloseQuits;
}

void SettingsViewModel::setCloseBehavior(int behavior)
{
    if (behavior != CloseToTray && behavior != CloseQuits) {
        return;
    }
    if (m_settings.contains(keys::CloseBehavior) && m_settings.integer(keys::CloseBehavior, CloseToTray) == behavior) {
        return;
    }
    m_settings.setInteger(keys::CloseBehavior, behavior);
}

QStringList SettingsViewModel::closeChoices() const
{
    return {tr("Minimize to tray"), tr("Quit")};
}

QString SettingsViewModel::displayName() const
{
    return trimmedName(m_settings.string(keys::UserDisplayName));
}

bool SettingsViewModel::displayNameShown() const
{
    return !m_mode.accountSelected();
}

QString SettingsViewModel::trackView() const
{
    // Grid is the default; only "list" chooses the table.
    return m_settings.string(keys::TrackView) == QLatin1StringView("list") ? QStringLiteral("list")
                                                                           : QStringLiteral("grid");
}

void SettingsViewModel::setTrackView(const QString& view)
{
    const QString stored = view == QLatin1StringView("list") ? QStringLiteral("list") : QStringLiteral("grid");
    if (m_settings.contains(keys::TrackView) && trackView() == stored) {
        return;
    }
    m_settings.setString(keys::TrackView, stored);
}

bool SettingsViewModel::isDisplayNameEdit(const QString& text) const
{
    return trimmedName(text) != displayName();
}

QString SettingsViewModel::saveDisplayName(const QString& text)
{
    const QString trimmed = trimmedName(text);
    m_settings.setString(keys::UserDisplayName, trimmed);
    return trimmed;
}

void SettingsViewModel::resetDefaults()
{
    core::resetPreferencesToDefaults(m_settings);
    // The player keeps its own copy of autoplay.
    m_player.setAutoplay(m_settings.boolean(keys::Autoplay, true));
}

QString SettingsViewModel::trimmedName(const QString& text)
{
    return core::account::trimProfileName(text);
}

} // namespace lmp::ui
