#include "ui/shell/ConnectivityViewModel.h"

#include "core/account/AccountSessionService.h"
#include "core/mode/ModeService.h"

namespace lmp::ui {

ConnectivityViewModel::ConnectivityViewModel(core::account::AccountSessionService& session,
                                             core::mode::ModeService& mode, QObject* parent)
    : QObject(parent)
    , m_session(session)
    , m_mode(mode)
{
    connect(&m_session, &core::account::AccountSessionService::stateChanged, this, &ConnectivityViewModel::update);
    connect(&m_mode, &core::mode::ModeService::changed, this, &ConnectivityViewModel::update);
    update();
}

void ConnectivityViewModel::update()
{
    const bool offline = m_mode.mode() == core::mode::SourceMode::Account
        && m_session.state() == core::account::AccountSessionState::Offline;
    if (offline != m_offline) {
        m_offline = offline;
        emit offlineChanged();
    }
}

} // namespace lmp::ui
