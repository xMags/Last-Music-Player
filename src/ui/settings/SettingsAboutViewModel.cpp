#include "ui/settings/SettingsAboutViewModel.h"

#include <QDir>
#include <QUrl>

#include <utility>

#include "core/app/AppIdentity.h"
#include "core/db/LibraryQueries.h"
#include "core/db/LibraryQueryTypes.h"
#include "platform/IExternalLinks.h"

namespace lmp::ui {

SettingsAboutViewModel::SettingsAboutViewModel(Parts parts, QObject* parent)
    : QObject(parent)
    , m_library(parts.library)
    , m_links(parts.links)
    , m_dataRoot(std::move(parts.dataRoot))
{
}

QString SettingsAboutViewModel::title() const
{
    return QStringLiteral("%1 %2").arg(core::AppIdentity::displayName(), core::AppIdentity::version());
}

void SettingsAboutViewModel::refresh()
{
    QString line;
    if (m_library != nullptr) {
        const core::db::LibraryStats stats = m_library->libraryStats();
        line = tr("%1 tracks · %2 albums · %3 artists")
                   .arg(stats.songCount)
                   .arg(stats.albumCount)
                   .arg(stats.artistCount);
    }
    if (line == m_libraryLine) {
        return;
    }
    m_libraryLine = line;
    emit libraryLineChanged();
}

bool SettingsAboutViewModel::openDataFolder()
{
    // A first run may not have written anything yet, as earlier versions knew.
    if (!QDir().mkpath(m_dataRoot)) {
        return false;
    }
    return m_links.open(QUrl::fromLocalFile(m_dataRoot));
}

} // namespace lmp::ui
