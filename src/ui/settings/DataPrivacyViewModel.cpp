#include "ui/settings/DataPrivacyViewModel.h"

#include "core/account/AccountClient.h"
#include "core/account/AccountSessionService.h"
#include "core/account/MusicSyncService.h"
#include "core/app/AppDataReset.h"
#include "core/net/UserMessages.h"

namespace lmp::ui {

namespace {

[[nodiscard]] QString actionFailed()
{
    return QObject::tr("The action could not be completed.");
}

} // namespace

DataPrivacyViewModel::DataPrivacyViewModel(const Parts& parts, QObject* parent)
    : QObject(parent)
    , m_parts(parts)
{
    connect(&m_parts.sync, &core::account::MusicSyncService::libraryChanged, this, &DataPrivacyViewModel::changed);
    // What was said, or is running, belongs to the account it was for.
    connect(&m_parts.session, &core::account::AccountSessionService::ownerChanged, this, [this]() {
        m_clearedAtRevision.reset();
        m_working.clear();
        m_message.clear();
        m_messageError = false;
        emit changed();
    });
}

int DataPrivacyViewModel::historyCount() const
{
    if (m_clearedAtRevision && *m_clearedAtRevision == m_parts.sync.libraryRevision()) {
        return 0;
    }
    return m_parts.sync.historyCount(m_parts.session.profile().id);
}

QString DataPrivacyViewModel::historySubtitle() const
{
    return historySubtitleFor(historyCount());
}

bool DataPrivacyViewModel::hasHistory() const
{
    return historyCount() > 0;
}

QString DataPrivacyViewModel::historySubtitleFor(int count)
{
    if (count <= 0) {
        return tr("No synced songs in your history");
    }
    // Only the newest HistoryLimit plays are loaded; clearing removes them all.
    if (count >= HistoryLimit) {
        return tr("All %1+ synced songs. Listen Again and Most Played start over.").arg(HistoryLimit);
    }
    return tr("%1 synced %2. Listen Again and Most Played start over.")
        .arg(count)
        .arg(count == 1 ? tr("song") : tr("songs"));
}

void DataPrivacyViewModel::clearHistory()
{
    if (!m_working.isEmpty()) {
        return;
    }
    const std::optional<core::account::AccountOperationContext> context = m_parts.session.captureOperation();
    if (!context) {
        finish(actionFailed(), true);
        return;
    }
    m_working = QStringLiteral("history");
    m_message.clear();
    m_messageError = false;
    emit changed();
    m_parts.client.clearHistory(context->bearerSession)
        .then(this, [this, context = *context](const core::Result<void>& result) {
            // An answer for an account that is no longer signed in says nothing.
            if (!m_parts.session.isCurrent(context)) {
                return;
            }
            if (!result) {
                if (result.failure().kind == core::FailureKind::Unauthorized) {
                    m_parts.session.handleUnauthorized(context);
                }
                finish(core::net::userMessageOr(result.failure(), actionFailed()), true);
                return;
            }
            // The library comes again without the history, which empties
            // Listen Again and Most Played here as well.
            m_clearedAtRevision = m_parts.sync.libraryRevision();
            (void)m_parts.sync.requestSync(core::account::MusicSyncService::Lane::Library);
            finish(tr("Listening history cleared."), false);
        });
}

void DataPrivacyViewModel::resetAppData()
{
    if (!m_working.isEmpty()) {
        return;
    }
    m_working = QStringLiteral("reset");
    m_message.clear();
    m_messageError = false;
    emit changed();
    m_parts.reset.run().then(this, [this](bool erased) {
        finish(erased ? QString() : actionFailed(), !erased);
    });
}

void DataPrivacyViewModel::finish(const QString& message, bool error)
{
    m_working.clear();
    m_message = message;
    m_messageError = error;
    emit changed();
}

} // namespace lmp::ui
