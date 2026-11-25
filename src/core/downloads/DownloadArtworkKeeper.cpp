#include "core/downloads/DownloadArtworkKeeper.h"

#include "core/downloads/DownloadManager.h"
#include "core/remote/ArtworkService.h"

#include <QPointer>

namespace lmp::core {

DownloadArtworkKeeper::DownloadArtworkKeeper(DownloadManager& downloads, remote::ArtworkService& artwork,
                                             QObject* parent)
    : QObject(parent)
    , m_downloads(downloads)
    , m_artwork(artwork)
{
}

void DownloadArtworkKeeper::start()
{
    if (m_started) {
        return;
    }
    m_started = true;
    connect(&m_downloads, &DownloadManager::changed, this, &DownloadArtworkKeeper::follow);
    // Another account's (or mode's) pictures are kept in its own folder.
    connect(&m_artwork, &remote::ArtworkService::scopeChanged, this, &DownloadArtworkKeeper::reset);
    follow();
}

void DownloadArtworkKeeper::setExtraPictures(const QStringList& artworkUrls)
{
    if (artworkUrls == m_extra) {
        return;
    }
    m_extra = artworkUrls;
    if (m_started) {
        follow();
    }
}

void DownloadArtworkKeeper::reset()
{
    m_kept.clear();
    m_failedAt.clear();
    m_swept = false;
    follow();
}

void DownloadArtworkKeeper::follow()
{
    QSet<QString> wanted;
    for (const DownloadJobSummary& summary : m_downloads.snapshot().jobs) {
        for (const DownloadItem& item : summary.job.items) {
            if (item.state == DownloadItemState::Completed && !item.artworkUrl.isEmpty()) {
                wanted.insert(item.artworkUrl);
            }
        }
    }
    for (const QString& url : m_extra) {
        if (!url.isEmpty()) {
            wanted.insert(url);
        }
    }

    // Once per account: what removed downloads left kept goes back.
    if (!m_swept && m_artwork.diskReady()) {
        m_swept = true;
        m_artwork.keepOnly(wanted.values());
    }

    for (auto it = m_kept.begin(); it != m_kept.end();) {
        if (wanted.contains(*it)) {
            ++it;
            continue;
        }
        m_artwork.release(*it);
        it = m_kept.erase(it);
    }

    const QDateTime now = QDateTime::currentDateTimeUtc();
    for (const QString& url : wanted) {
        if (m_kept.contains(url)) {
            continue;
        }
        if (const auto failed = m_failedAt.constFind(url);
            failed != m_failedAt.constEnd() && failed->secsTo(now) < RetryAfterSeconds) {
            continue;
        }
        m_kept.insert(url);
        const QPointer<DownloadArtworkKeeper> self(this);
        m_artwork.keep(url).then(this, [self, url](bool kept) {
            if (self == nullptr || kept) {
                return;
            }
            self->m_kept.remove(url);
            self->m_failedAt.insert(url, QDateTime::currentDateTimeUtc());
        });
    }
}

} // namespace lmp::core
