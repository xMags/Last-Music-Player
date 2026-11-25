#pragma once

#include <QDateTime>
#include <QHash>
#include <QObject>
#include <QSet>
#include <QString>
#include <QStringList>

namespace lmp::core {

class DownloadManager;
namespace remote {
class ArtworkService;
}

// Keeps the pictures downloaded songs need on disk for good, so they show
// with no network: every finished download's cover, and extra pictures
// named by the caller (the photos of the downloaded songs' artists). A
// picture no download needs any more is released back to the disk cache;
// at the first look in each account, pictures kept for downloads removed
// while the app was closed are released too. A picture that could not be
// kept (no network) is tried again on a later change, at most every
// RetryAfter.
//
// GUI thread only.
class DownloadArtworkKeeper : public QObject {
    Q_OBJECT

public:
    static constexpr qint64 RetryAfterSeconds = 10 * 60;

    // Both outlive this object.
    DownloadArtworkKeeper(DownloadManager& downloads, remote::ArtworkService& artwork, QObject* parent = nullptr);

    // Starts following the downloads (after they were read).
    void start();
    // Pictures kept beside the covers (picture addresses).
    void setExtraPictures(const QStringList& artworkUrls);

private:
    void follow();
    void reset();

    DownloadManager& m_downloads;
    remote::ArtworkService& m_artwork;
    QStringList m_extra;
    // Kept, or being kept.
    QSet<QString> m_kept;
    // When a picture last failed to be kept.
    QHash<QString, QDateTime> m_failedAt;
    bool m_started = false;
    bool m_swept = false;
};

} // namespace lmp::core
