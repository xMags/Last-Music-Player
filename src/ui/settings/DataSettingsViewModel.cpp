#include "ui/settings/DataSettingsViewModel.h"

#include "ui/toasts/ToastQueue.h"

namespace lmp::ui {

DataSettingsViewModel::DataSettingsViewModel(core::UserDataCleanup& cleanup, ToastQueue& toasts, QObject* parent)
    : QObject(parent)
    , m_cleanup(cleanup)
    , m_toasts(toasts)
{
    connect(&m_cleanup, &core::UserDataCleanup::runningChanged, this, &DataSettingsViewModel::busyChanged);
    connect(&m_cleanup, &core::UserDataCleanup::finished, this, &DataSettingsViewModel::onFinished);
}

bool DataSettingsViewModel::confirms(const QString& typed) const
{
    return typed == ConfirmWord;
}

bool DataSettingsViewModel::deleteAll(const QString& typed)
{
    if (!confirms(typed) || m_cleanup.running()) {
        return false;
    }
    m_status = tr("Cleaning up app data…");
    emit statusChanged();
    m_cleanup.start();
    return true;
}

void DataSettingsViewModel::onFinished(core::UserDataCleanup::Outcome outcome, const QString& detail)
{
    using Outcome = core::UserDataCleanup::Outcome;
    switch (outcome) {
    case Outcome::Done:
        m_status = tr("Everything was cleaned up. Music files on disk were not touched.");
        m_toasts.show(tr("All app data deleted"));
        break;
    case Outcome::Partly:
        m_status = tr("Cleaned up, except: %1.").arg(detail);
        m_toasts.show(m_status, true);
        break;
    case Outcome::AlreadyRunning:
        m_status = tr("Another cleanup operation is already in progress.");
        break;
    case Outcome::StillBusy:
        m_status = tr("Cleanup stopped because a background operation did not finish. Try again after it completes.");
        m_toasts.show(m_status, true);
        break;
    }
    emit statusChanged();
}

} // namespace lmp::ui
