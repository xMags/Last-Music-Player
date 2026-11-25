#pragma once

#include <QDateTime>
#include <QHash>
#include <QObject>
#include <QSet>
#include <QString>
#include <QStringList>
#include <QTimer>

#include <deque>

namespace lmp::core::catalog {
class CatalogService;
}

namespace lmp::ui {

// Photos for the Offline page's Artists tab (offline-artist-photos.ts).
// Downloaded songs know their artist by name only, so each name is looked
// up the way artist pages are (the catalogue's artist for that name), and
// the photo's address is remembered on this PC in a small JSON file, so the
// tab keeps its photos with no network (the picture itself is kept on disk
// beside the downloads' covers). A name with no photo is asked about again
// after MissRetrySeconds; a lookup that failed (no network, no catalogue)
// is not remembered and is asked again next time. At most LookupsAtOnce
// run together, and the file keeps the newest MaxRemembered names.
//
// GUI thread only.
class OfflineArtistPhotos : public QObject {
    Q_OBJECT

public:
    static constexpr qint64 MissRetrySeconds = 24 * 60 * 60;
    static constexpr int MaxRemembered = 2000;
    static constexpr int LookupsAtOnce = 3;

    // `catalog` outlives this object; `file` is where the photos are remembered.
    OfflineArtistPhotos(core::catalog::CatalogService& catalog, QString file, QObject* parent = nullptr);
    ~OfflineArtistPhotos() override;

    OfflineArtistPhotos(const OfflineArtistPhotos&) = delete;
    OfflineArtistPhotos& operator=(const OfflineArtistPhotos&) = delete;

    // The photo known for an artist, or empty.
    [[nodiscard]] QString known(const QString& name) const;
    // Looks up every name with no photo yet (and no recent miss).
    void lookUp(const QStringList& names);

signals:
    void photoFound(const QString& name, const QString& artworkUrl);

private:
    struct Remembered {
        // Empty when the catalogue had no photo for the name.
        QString url;
        QDateTime at;
    };

    [[nodiscard]] static QString keyFor(const QString& name);
    void load();
    void save();
    void pump();

    core::catalog::CatalogService& m_catalog;
    QString m_file;
    QHash<QString, Remembered> m_photos;
    std::deque<QString> m_queue;
    QSet<QString> m_waiting;
    int m_running = 0;
    // Saves are gathered: one write for a burst of finds.
    QTimer m_saveDelay;
};

} // namespace lmp::ui
