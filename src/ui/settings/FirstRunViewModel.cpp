#include "ui/settings/FirstRunViewModel.h"

#include "core/mode/ModeService.h"
#include "core/mode/SourceMode.h"
#include "core/settings/SettingKeys.h"
#include "core/settings/SettingsStore.h"
#include "ui/navigation/Navigator.h"
#include "ui/navigation/Route.h"

namespace lmp::ui {

namespace keys = core::settings::keys;

FirstRunViewModel::FirstRunViewModel(core::SettingsStore& settings, const core::mode::ModeService& mode,
                                     Navigator& navigator, QObject* parent)
    : QObject(parent)
    , m_settings(settings)
    , m_mode(mode)
    , m_navigator(navigator)
{
    connect(&m_mode, &core::mode::ModeService::changed, this, &FirstRunViewModel::onModeChanged);
}

void FirstRunViewModel::decide(const core::mode::SourceChoiceInputs& inputs)
{
    const bool show = core::mode::shouldOfferSourceChoice(inputs);
    if (show && !inputs.choiceSeenStored) {
        // Shown but not finished yet: a launch that ends here shows it again.
        m_settings.setBoolean(keys::SourceChoiceSeen, false);
    }
    if (show == m_shown) {
        return;
    }
    m_shown = show;
    emit shownChanged();
}

void FirstRunViewModel::finish()
{
    if (!m_shown) {
        return;
    }
    m_settings.setBoolean(keys::SourceChoiceSeen, true);
    m_shown = false;
    emit shownChanged();
}

void FirstRunViewModel::onModeChanged()
{
    if (!m_shown) {
        return;
    }
    const core::mode::SourceMode mode = m_mode.mode();
    if (mode == core::mode::SourceMode::LocalOnly) {
        return;
    }
    finish();
    // The API key form lives in Settings.
    if (mode == core::mode::SourceMode::ApiKey) {
        m_navigator.openView(static_cast<int>(Route::Kind::Settings));
    }
}

} // namespace lmp::ui
