#pragma once

#include "core/async/Result.h"
#include "core/model/Track.h"

#include <QByteArray>
#include <QFuture>
#include <QList>
#include <QObject>
#include <QString>

#include <functional>
#include <optional>

namespace lmp::core {

class UserDataOperationGate;
namespace db {
class CollectionRepository;
class TrackRepository;
} // namespace db
namespace remote {
class RemoteMusicService;
} // namespace remote
namespace account {
class AccountLibraryEditor;
} // namespace account

// What an import link holds (POST /v1/import-album): the album or playlist
// and its songs.
struct ImportedCollection {
    QString title;
    QString artist;
    QString provider;
    QString sourceUrl;
    QString artworkUrl;
    QString sourceLabel;
    // "album" or "playlist".
    QString kind;
    QList<Track> tracks;
};

// Account mode's import answer, read as the reference client reads it
// (resolveCollection): the songs, a title (album title, title, name, the
// songs' most common album, else "Imported playlist") and the album's
// artist as its description.
[[nodiscard]] std::optional<ImportedCollection> parseAccountImport(const QByteArray& payload, const QString& link);

// Reads the import answer as the earlier versions did ({"album": {...},
// "tracks": [...]}), with their defaults for missing fields; nothing when it
// holds no songs.
[[nodiscard]] std::optional<ImportedCollection> parseImportedCollection(const QByteArray& payload,
                                                                        const QString& link);

// Where an import landed.
struct ImportOutcome {
    // "album-collection" or "playlist", as the library's pages name them.
    QString kind;
    QString key;
    QString title;
    int songCount = 0;
};

// Imports an album or playlist link. In Account mode the songs become a
// playlist of the listener's own on the account, filed under the link (the
// reference client's import; the service tops up an earlier copy). In API
// key mode they go into the library on this PC, as the earlier versions did: an album under
// "import|<provider>|<link>", a playlist under
// "import-playlist|<provider>|<link>", its songs written to the Tracks table
// and listed in order. Importing the same link again replaces the songs.
// Refused while "Delete all data" runs, and when the music source changed
// while the provider answered.
//
// GUI thread only (the database's owner).
class LinkImportService : public QObject {
    Q_OBJECT

public:
    struct Dependencies {
        remote::RemoteMusicService& remote;
        db::TrackRepository& tracks;
        db::CollectionRepository& collections;
        UserDataOperationGate& gate;
        // Account mode: where the playlist is saved, used while `toAccount`
        // says so.
        account::AccountLibraryEditor* accountEditor = nullptr;
        std::function<bool()> toAccount = {};
    };

    // Every dependency must outlive this object.
    explicit LinkImportService(Dependencies dependencies, QObject* parent = nullptr);

    [[nodiscard]] QFuture<Result<ImportOutcome>> importLink(const QString& link);

    // The import's store step (for tests): writes the songs and the album or
    // playlist and lists them.
    [[nodiscard]] Result<ImportOutcome> store(const ImportedCollection& collection);

signals:
    void imported(const QString& kind, const QString& key);

private:
    Dependencies m_deps;
};

} // namespace lmp::core
