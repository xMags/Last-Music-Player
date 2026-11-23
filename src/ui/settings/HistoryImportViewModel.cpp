#include "ui/settings/HistoryImportViewModel.h"

#include "core/account/HistoryImportService.h"

namespace lmp::ui {

HistoryImportViewModel::HistoryImportViewModel(core::account::HistoryImportService& service, QObject* parent)
    : QObject(parent)
    , m_service(service)
{
    connect(&m_service, &core::account::HistoryImportService::offerChanged, this,
            &HistoryImportViewModel::offeringChanged);
    connect(&m_service, &core::account::HistoryImportService::finished, this,
            [this](int imported, int skipped) { emit message(summary(imported, skipped)); });
    connect(&m_service, &core::account::HistoryImportService::paused, this, &HistoryImportViewModel::message);
}

bool HistoryImportViewModel::offering() const
{
    return m_service.offering();
}

void HistoryImportViewModel::accept()
{
    m_service.accept();
}

void HistoryImportViewModel::decline()
{
    m_service.decline();
}

QString HistoryImportViewModel::summary(int imported, int skipped)
{
    const QString first = imported == 1 ? tr("1 compatible track was queued for account history.")
                                        : tr("%1 compatible tracks were queued for account history.").arg(imported);
    const QString second = skipped == 1 ? tr("1 local or unresolved entry was left on this PC.")
                                        : tr("%1 local or unresolved entries were left on this PC.").arg(skipped);
    return first + QLatin1Char(' ') + second;
}

} // namespace lmp::ui
