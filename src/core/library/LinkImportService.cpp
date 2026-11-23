#include "core/library/LinkImportService.h"

#include "core/account/AccountLibraryEditor.h"
#include "core/catalog/CatalogParser.h"
#include "core/db/CollectionRepository.h"
#include "core/db/TrackRepository.h"
#include "core/library/UserDataOperationGate.h"
#include "core/model/CatalogSourceKey.h"
#include "core/net/RemoteUrlPolicy.h"
#include "core/remote/RemoteMusicService.h"

#include <QJsonDocument>
#include <QJsonObject>
#include <QHash>

namespace lmp::core {

namespace {

[[nodiscard]] QString text(const QJsonObject& object, QLatin1StringView name, const QString& fallback)
{
    const QJsonValue value = object.value(name);
    const QString found = value.isString() ? value.toString().trimmed() : QString();
    return found.isEmpty() ? fallback : found;
}

} // namespace

std::optional<ImportedCollection> parseImportedCollection(const QByteArray& payload, const QString& link)
{
    const QJsonObject root = QJsonDocument::fromJson(payload).object();
    ImportedCollection collection;
    collection.tracks = catalog::parseProviderTracks(root.value(QLatin1StringView("tracks")));
    if (collection.tracks.isEmpty()) {
        return std::nullopt;
    }
    const QJsonObject album = root.value(QLatin1StringView("album")).toObject();
    collection.provider = text(album, QLatin1StringView("provider"), QStringLiteral("remote"));
    collection.sourceUrl = text(album, QLatin1StringView("sourceUrl"), link.trimmed());
    collection.title = text(album, QLatin1StringView("title"), QStringLiteral("Imported Album"));
    collection.artist = text(album, QLatin1StringView("artist"), QStringLiteral("Music API"));
    collection.artworkUrl = text(album, QLatin1StringView("artworkUrl"), QString());
    collection.sourceLabel = text(album, QLatin1StringView("sourceLabel"), QStringLiteral("Music API"));
    collection.kind = text(album, QLatin1StringView("collectionKind"), QStringLiteral("album")).toLower()
            == QLatin1StringView("playlist")
        ? QStringLiteral("playlist")
        : QStringLiteral("album");
    return collection;
}

std::optional<ImportedCollection> parseAccountImport(const QByteArray& payload, const QString& link)
{
    ImportedCollection collection;
    collection.tracks = catalog::parseProviderTrackList(payload);
    if (collection.tracks.isEmpty()) {
        return std::nullopt;
    }
    const QJsonObject root = QJsonDocument::fromJson(payload).object();
    const QJsonObject album = root.value(QLatin1StringView("album")).toObject();
    QHash<QString, int> albumCounts;
    QString dominant;
    for (const Track& track : collection.tracks) {
        const QString name = track.album.trimmed();
        if (name.isEmpty()) {
            continue;
        }
        const int count = ++albumCounts[name];
        if (dominant.isEmpty() || count > albumCounts.value(dominant)) {
            dominant = name;
        }
    }
    QString title = text(album, QLatin1StringView("title"), QString());
    if (title.isEmpty()) {
        title = text(root, QLatin1StringView("title"), text(root, QLatin1StringView("name"), QString()));
    }
    if (title.isEmpty()) {
        title = dominant.isEmpty() ? QStringLiteral("Imported playlist") : dominant;
    }
    collection.title = title;
    collection.artist = text(album, QLatin1StringView("artist"), QString());
    collection.sourceUrl = link.trimmed();
    collection.kind = QStringLiteral("playlist");
    return collection;
}

LinkImportService::LinkImportService(Dependencies dependencies, QObject* parent)
    : QObject(parent)
    , m_deps(dependencies)
{
}

QFuture<Result<ImportOutcome>> LinkImportService::importLink(const QString& link)
{
    const QString trimmed = link.trimmed();
    if (!net::isHttpUrl(trimmed)) {
        return QtFuture::makeReadyValueFuture(
            Result<ImportOutcome>(Failure::of(FailureKind::NotFound, QStringLiteral("Paste an album or playlist link."))));
    }
    if (m_deps.accountEditor != nullptr && m_deps.toAccount && m_deps.toAccount()) {
        account::AccountLibraryEditor* editor = m_deps.accountEditor;
        return m_deps.remote.importCollection(trimmed)
            .then(this,
                  [editor, trimmed](const Result<QByteArray>& answer) -> QFuture<Result<ImportOutcome>> {
                      if (!answer) {
                          return QtFuture::makeReadyValueFuture(Result<ImportOutcome>(answer.failure()));
                      }
                      const std::optional<ImportedCollection> collection = parseAccountImport(answer.value(), trimmed);
                      if (!collection) {
                          return QtFuture::makeReadyValueFuture(Result<ImportOutcome>(
                              Failure::of(FailureKind::NotFound, QStringLiteral("No songs were found at that link."))));
                      }
                      const QString title = collection->title;
                      const int count = static_cast<int>(collection->tracks.size());
                      return editor->saveCollection(title, collection->artist, collection->tracks, trimmed)
                          .then([title, count](const Result<QString>& key) -> Result<ImportOutcome> {
                              if (!key) {
                                  return key.failure();
                              }
                              return ImportOutcome{QStringLiteral("playlist"), key.value(), title, count};
                          });
                  })
            .unwrap();
    }
    return m_deps.remote.importCollection(trimmed).then(
        this, [this, trimmed](const Result<QByteArray>& answer) -> Result<ImportOutcome> {
            if (!answer) {
                return answer.failure();
            }
            const std::optional<ImportedCollection> collection = parseImportedCollection(answer.value(), trimmed);
            if (!collection) {
                return Failure::of(FailureKind::NotFound, QStringLiteral("No songs were found at that link."));
            }
            Result<ImportOutcome> outcome = store(*collection);
            if (outcome) {
                emit imported(outcome.value().kind, outcome.value().key);
            }
            return outcome;
        });
}

Result<ImportOutcome> LinkImportService::store(const ImportedCollection& collection)
{
    const std::optional<UserDataOperationGate::Lease> lease = m_deps.gate.tryEnter();
    if (!lease) {
        return Failure::of(FailureKind::Cancelled, QStringLiteral("Cleanup is in progress."));
    }
    QList<qint64> trackIds;
    for (const Track& track : collection.tracks) {
        const QString key = catalogSourceKey(track);
        if (key.isEmpty()) {
            continue;
        }
        const Result<qint64> id = track.isRemote() ? m_deps.tracks.upsertRemoteTrack(track, key)
                                                   : m_deps.tracks.upsertLocalTrack(track, key);
        if (id && id.value() > 0) {
            trackIds.append(id.value());
        }
    }
    if (trackIds.isEmpty()) {
        return Failure::of(FailureKind::NotFound, QStringLiteral("No playable songs were saved."));
    }
    Track entry;
    entry.title = collection.title;
    entry.artist = collection.artist;
    entry.origin = collection.provider;
    entry.sourceUrl = collection.sourceUrl;
    entry.artworkUrl = collection.artworkUrl;
    entry.sourceLabel = collection.sourceLabel;
    const QString provider = collection.provider.toLower();
    if (collection.kind == QLatin1StringView("playlist")) {
        entry.sourceKind = QStringLiteral("manual");
        const QString key = QStringLiteral("import-playlist|") + provider + QLatin1Char('|') + collection.sourceUrl;
        const Result<qint64> playlist = m_deps.collections.upsertPlaylist(entry, key);
        if (!playlist) {
            return playlist.failure();
        }
        if (const Result<void> listed = m_deps.collections.replacePlaylistTracks(playlist.value(), trackIds); !listed) {
            return listed.failure();
        }
        return ImportOutcome{QStringLiteral("playlist"), key, collection.title, static_cast<int>(trackIds.size())};
    }
    entry.sourceKind = collection.provider;
    const QString key = QStringLiteral("import|") + provider + QLatin1Char('|') + collection.sourceUrl;
    const Result<qint64> album = m_deps.collections.upsertAlbumCollection(entry, key);
    if (!album) {
        return album.failure();
    }
    if (const Result<void> listed = m_deps.collections.replaceAlbumCollectionTracks(album.value(), trackIds); !listed) {
        return listed.failure();
    }
    return ImportOutcome{QStringLiteral("album-collection"), key, collection.title, static_cast<int>(trackIds.size())};
}

} // namespace lmp::core
