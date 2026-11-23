#include "core/app/UserDataCleanup.h"

#include <QDir>
#include <QFile>
#include <QStringList>

#include "core/account/AccountSessionService.h"
#include "core/account/SecretStore.h"
#include "core/db/TrackRepository.h"
#include "core/library/LibraryScanner.h"
#include "core/library/UserDataOperationGate.h"
#include "core/mode/ModeService.h"
#include "core/mode/SourceMode.h"
#include "core/playback/PlayerController.h"
#include "core/settings/SettingsStore.h"

namespace lmp::core {

namespace {

constexpr auto kPollInterval = std::chrono::milliseconds(50);

} // namespace

UserDataCleanup::UserDataCleanup(Parts parts, QObject* parent)
    : QObject(parent)
    , m_parts(std::move(parts))
{
    m_poll.setInterval(kPollInterval);
    connect(&m_poll, &QTimer::timeout, this, &UserDataCleanup::waitForIdle);
}

void UserDataCleanup::start()
{
    if (m_running || !m_parts.gate.closeAdmissions()) {
        emit finished(Outcome::AlreadyRunning, {});
        return;
    }
    m_running = true;
    emit runningChanged();
    // Stop what would write more: the scan, a sign-in, the player.
    m_parts.scanner.cancel();
    m_parts.session.cancelSignIn();
    m_parts.player.reset();
    // Waiting on the event loop, not blocking it: a scan finishing its last
    // batch needs the loop to let go of its lease.
    // The first check is on the next turn of the loop too, so the clean-up
    // never runs inside the caller (a dialog whose page it may close).
    m_deadline = std::chrono::steady_clock::now() + m_parts.idleTimeout;
    m_poll.start();
}

void UserDataCleanup::waitForIdle()
{
    if (m_parts.gate.waitForIdle(std::chrono::milliseconds(0))) {
        m_poll.stop();
        clearEverything();
        return;
    }
    if (std::chrono::steady_clock::now() >= m_deadline) {
        m_poll.stop();
        finish(Outcome::StillBusy, {});
    }
}

void UserDataCleanup::clearEverything()
{
    QStringList failed;
    if (!m_parts.session.removeLocalSession()) {
        failed.append(tr("the signed-in session"));
    }
    if (!m_parts.secrets.removeProviderApiKey()) {
        failed.append(tr("the API key"));
    }
    if (!m_parts.mode.setMode(mode::SourceMode::LocalOnly)) {
        failed.append(tr("the music source"));
    }
    if (QFile::exists(m_parts.stateFile) && !QFile::remove(m_parts.stateFile)) {
        failed.append(tr("the saved playback state"));
    }
    if (!m_parts.tracks.clearAllUserData()) {
        failed.append(tr("the library"));
    }
    if (QDir folder(m_parts.coverFolder); folder.exists() && !folder.removeRecursively()) {
        failed.append(tr("the cover cache"));
    }
    // Last, as the current app does: the steps above may write settings.
    if (!m_parts.settings.reset()) {
        failed.append(tr("the settings"));
    }
    emit cleared();
    finish(failed.isEmpty() ? Outcome::Done : Outcome::Partly, failed.join(QStringLiteral(", ")));
}

void UserDataCleanup::finish(Outcome outcome, const QString& detail)
{
    m_parts.gate.reopen();
    m_running = false;
    emit runningChanged();
    emit finished(outcome, detail);
}

} // namespace lmp::core
