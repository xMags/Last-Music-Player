#include "app/DeveloperPlayFolder.h"

#include <QDir>
#include <QFileInfo>
#include <QFileInfoList>

namespace lmp::app {

// DEVELOPER OPTION, removed in P06 (see the header).

std::optional<QString> developerPlayFolder(const QStringList& arguments)
{
    const qsizetype at = arguments.indexOf(QStringLiteral("--play-folder"));
    if (at < 0 || at + 1 >= arguments.size() || arguments.at(at + 1).isEmpty()) {
        return std::nullopt;
    }
    return arguments.at(at + 1);
}

QList<core::Track> developerPlayFolderTracks(const QString& folder)
{
    const QDir directory(folder);
    const QFileInfoList files = directory.entryInfoList(
        {QStringLiteral("*.mp3"), QStringLiteral("*.flac"), QStringLiteral("*.wav"), QStringLiteral("*.m4a")},
        QDir::Files | QDir::Readable, QDir::Name | QDir::IgnoreCase);

    QList<core::Track> tracks;
    tracks.reserve(files.size());
    for (const QFileInfo& file : files) {
        core::Track track;
        track.title = file.completeBaseName();
        track.filePath = QDir::toNativeSeparators(file.absoluteFilePath());
        track.sourceKind = QStringLiteral("local");
        track.origin = QStringLiteral("local");
        track.sourceLabel = QStringLiteral("On this PC");
        track.offline = true;
        tracks.append(track);
    }
    return tracks;
}

} // namespace lmp::app
