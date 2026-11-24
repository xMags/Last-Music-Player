#include "core/downloads/OfflineCollections.h"

#include <QCoreApplication>
#include <QHash>
#include <QRegularExpression>
#include <QSet>

#include <algorithm>

namespace lmp::core::offline {

namespace {

constexpr qsizetype MinAlbumSongs = 2;
constexpr qsizetype MinGenreSongs = 2;

QString text(const char* source)
{
    return QCoreApplication::translate("OfflineCollections", source);
}

bool isUnknownLabel(const QString& label)
{
    static const QRegularExpression unknown(QStringLiteral("^unknown (artist|album|genre)$"),
                                            QRegularExpression::CaseInsensitiveOption);
    return unknown.match(label).hasMatch();
}

QString foldedKey(const QString& label)
{
    return label.simplified().toCaseFolded();
}

// The album name as a grouping key, without a soundtrack tag at its end.
QString albumNameKey(const QString& label)
{
    static const QRegularExpression soundtrack(
        QStringLiteral(R"(\s*[(\[](original\s+)?(motion\s+picture\s+)?(soundtrack|ost|music from the motion picture)[)\]]\s*$)"),
        QRegularExpression::CaseInsensitiveOption);
    QString bare = label;
    bare.remove(soundtrack);
    return foldedKey(bare.isEmpty() ? label : bare);
}

// The same song saved twice is one song: by its file, else its title and artist.
QString songKey(const Track& track)
{
    if (!track.filePath.isEmpty()) {
        return track.filePath;
    }
    return foldedKey(track.title) + QLatin1Char('\n') + foldedKey(track.artist);
}

// Biggest first, then by name ignoring case.
void sortBySize(QList<OfflineGroup>& groups)
{
    std::stable_sort(groups.begin(), groups.end(), [](const OfflineGroup& left, const OfflineGroup& right) {
        if (left.tracks.size() != right.tracks.size()) {
            return left.tracks.size() > right.tracks.size();
        }
        return QString::compare(left.name, right.name, Qt::CaseInsensitive) < 0;
    });
}

void setCountSubtitles(QList<OfflineGroup>& groups)
{
    for (OfflineGroup& group : groups) {
        group.subtitle = songCountText(static_cast<int>(group.tracks.size()));
    }
}

} // namespace

QString songCountText(int count)
{
    return count == 1 ? text("1 song") : text("%1 songs").arg(count);
}

QList<Track> downloadedPart(const QStringList& collectionKeys, const QList<Track>& downloaded,
                            const QStringList& downloadedKeys)
{
    QHash<QString, qsizetype> byKey;
    const qsizetype count = std::min(downloaded.size(), downloadedKeys.size());
    for (qsizetype index = 0; index < count; ++index) {
        const QString& key = downloadedKeys.at(index);
        if (!key.isEmpty() && !byKey.contains(key)) {
            byKey.insert(key, index);
        }
    }
    QList<Track> part;
    QSet<QString> seen;
    for (const QString& key : collectionKeys) {
        const auto found = byKey.constFind(key);
        if (found == byKey.constEnd() || seen.contains(key)) {
            continue;
        }
        seen.insert(key);
        part.append(downloaded.at(found.value()));
    }
    return part;
}

QString playlistSubtitle(int downloadedCount, int total)
{
    if (downloadedCount >= total) {
        return songCountText(total);
    }
    return text("%1 of %2").arg(downloadedCount).arg(songCountText(total));
}

QString mainArtist(const Track& track)
{
    const QString identity = track.primaryArtistName.trimmed();
    if (!identity.isEmpty() && !isUnknownLabel(identity)) {
        return identity;
    }
    static const QRegularExpression credit(QStringLiteral(R"(,|&|\s+x\s+|\s+and\s+|\s+feat\.?\s+|\s+ft\.?\s+)"),
                                           QRegularExpression::CaseInsensitiveOption);
    for (const QString& part : track.artist.split(credit)) {
        const QString name = part.trimmed();
        if (!name.isEmpty()) {
            return name;
        }
    }
    return {};
}

QList<OfflineGroup> albumGroups(const QList<Track>& downloaded, const QStringList& playlistNames)
{
    QSet<QString> playlists;
    for (const QString& name : playlistNames) {
        if (!name.trimmed().isEmpty()) {
            playlists.insert(foldedKey(name));
        }
    }
    struct Entry {
        QString key;
        QString name;
        QList<Track> tracks;
        QSet<QString> songs;
    };
    QHash<QString, qsizetype> indexByKey;
    QList<Entry> entries;
    for (const Track& track : downloaded) {
        const QString label = track.album.trimmed();
        if (label.isEmpty() || isUnknownLabel(label) || playlists.contains(foldedKey(label))) {
            continue;
        }
        const QString key = albumNameKey(label);
        qsizetype index = indexByKey.value(key, -1);
        if (index < 0) {
            index = entries.size();
            indexByKey.insert(key, index);
            entries.append({.key = key, .name = label, .tracks = {}, .songs = {}});
        }
        Entry& entry = entries[index];
        // Shown under its shorter spelling, without the soundtrack tag.
        if (label.size() < entry.name.size()) {
            entry.name = label;
        }
        const QString song = songKey(track);
        if (!entry.songs.contains(song)) {
            entry.songs.insert(song);
            entry.tracks.append(track);
        }
    }
    QList<OfflineGroup> groups;
    for (const Entry& entry : std::as_const(entries)) {
        if (entry.tracks.size() < MinAlbumSongs) {
            continue;
        }
        const QString artist = mainArtist(entry.tracks.constFirst());
        groups.append({.kind = GroupKind::Album,
                       .key = entry.key,
                       .name = entry.name,
                       .subtitle = artist.isEmpty() ? songCountText(static_cast<int>(entry.tracks.size())) : artist,
                       .tracks = entry.tracks});
    }
    sortBySize(groups);
    return groups;
}

QList<OfflineGroup> artistGroups(const QList<Track>& downloaded)
{
    QHash<QString, qsizetype> indexByKey;
    QList<OfflineGroup> groups;
    for (const Track& track : downloaded) {
        const QString name = mainArtist(track);
        if (name.isEmpty() || isUnknownLabel(name)) {
            continue;
        }
        const QString key = name.toCaseFolded();
        qsizetype index = indexByKey.value(key, -1);
        if (index < 0) {
            index = groups.size();
            indexByKey.insert(key, index);
            groups.append({.kind = GroupKind::Artist, .key = key, .name = name, .subtitle = {}, .tracks = {}});
        }
        groups[index].tracks.append(track);
    }
    setCountSubtitles(groups);
    sortBySize(groups);
    return groups;
}

QList<OfflineGroup> genreGroups(const QList<Track>& downloaded)
{
    QHash<QString, qsizetype> indexByKey;
    QList<OfflineGroup> groups;
    QList<QSet<QString>> songs;
    for (const Track& track : downloaded) {
        const QString name = track.genre.trimmed();
        if (name.isEmpty() || isUnknownLabel(name)) {
            continue;
        }
        const QString key = name.toCaseFolded();
        qsizetype index = indexByKey.value(key, -1);
        if (index < 0) {
            index = groups.size();
            indexByKey.insert(key, index);
            groups.append({.kind = GroupKind::Genre, .key = key, .name = name, .subtitle = {}, .tracks = {}});
            songs.append(QSet<QString>{});
        }
        // A song saved twice is listed and counted once.
        const QString song = songKey(track);
        if (!songs[index].contains(song)) {
            songs[index].insert(song);
            groups[index].tracks.append(track);
        }
    }
    setCountSubtitles(groups);
    sortBySize(groups);
    const bool anySizeable = std::any_of(groups.cbegin(), groups.cend(), [](const OfflineGroup& group) {
        return group.tracks.size() >= MinGenreSongs;
    });
    if (anySizeable) {
        groups.removeIf([](const OfflineGroup& group) { return group.tracks.size() < MinGenreSongs; });
    }
    return groups;
}

} // namespace lmp::core::offline
