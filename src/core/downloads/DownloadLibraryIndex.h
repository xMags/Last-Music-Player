#pragma once

#include "core/library/UserDataOperationGate.h"
#include "core/model/Track.h"

#include <QByteArray>
#include <QHash>
#include <QList>
#include <QObject>
#include <QString>
#include <QThreadPool>
#include <QTimer>

namespace lmp::platform {
class ITagReader;
} // namespace lmp::platform

namespace lmp::core {
class DownloadManager;
struct DownloadItem;
namespace db {
class TrackRepository;
} // namespace db
} // namespace lmp::core

namespace lmp::core::downloads {

// Keeps the finished downloads in the library as songs of this PC
// (TrackRepository::syncDownloadTracks), so that while the music source is
// this PC, Library, Home and search list them beside the music folder's
// songs. Each row has the download's names, cover address and source
// address (as the Offline page's rows do), the time it finished as its date
// added, and its length from the file's tags; a file is read only when its
// row does not know its length yet.
//
// A change of the download list is written SettleMs later, and only when
// what the rows would hold changed. While user data is being cleaned up the
// write waits RetryMs and tries again.
//
// Threading: GUI thread (the database's owner). Tags are read on a worker of
// its own; the destructor waits for it.
class DownloadLibraryIndex final : public QObject {
    Q_OBJECT

public:
    static constexpr int SettleMs = 1000;
    static constexpr int RetryMs = 5000;

    struct Dependencies {
        DownloadManager& downloads;
        platform::ITagReader& tags;
        db::TrackRepository& tracks;
        UserDataOperationGate& gate;
        // The wait after a change (tests shorten it).
        int settleMs = SettleMs;
    };

    // Every dependency must outlive this object.
    explicit DownloadLibraryIndex(Dependencies dependencies, QObject* parent = nullptr);
    ~DownloadLibraryIndex() override;

    DownloadLibraryIndex(const DownloadLibraryIndex&) = delete;
    DownloadLibraryIndex& operator=(const DownloadLibraryIndex&) = delete;

    // Writes the rows now and follows the download list from then on.
    void start();
    // Writes the rows again even if the list did not change (the library
    // was emptied).
    void reindex();

    // The song a finished download is in the library.
    [[nodiscard]] static Track trackFor(const DownloadItem& item, double durationSeconds);

signals:
    // The rows were written.
    void indexed();

private:
    void schedule(int delayMs);
    void run();
    void write(const QList<Track>& songs, const QByteArray& signature);

    DownloadManager& m_downloads;
    platform::ITagReader& m_tags;
    db::TrackRepository& m_tracks;
    UserDataOperationGate& m_gate;
    int m_settleMs;

    QTimer m_timer;
    QThreadPool m_worker;
    // Set while tags are read; a change meanwhile runs again after it.
    bool m_reading = false;
    bool m_again = false;
    // What the rows hold since the last write.
    QByteArray m_written;
};

} // namespace lmp::core::downloads
