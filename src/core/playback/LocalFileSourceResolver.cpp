#include "core/playback/TrackSourceResolver.h"

#include <QFileInfo>
#include <QString>
#include <QFuture>

namespace lmp::core {

QFuture<Result<QUrl>> LocalFileSourceResolver::resolve(const Track& track)
{
    if (track.filePath.isEmpty()) {
        return QtFuture::makeReadyValueFuture(
            Result<QUrl>(Failure::of(FailureKind::NotFound, QStringLiteral("The song has no file path."))));
    }
    const QFileInfo file(track.filePath);
    if (!file.isFile()) {
        return QtFuture::makeReadyValueFuture(
            Result<QUrl>(Failure::of(FailureKind::NotFound, QStringLiteral("The file does not exist."))));
    }
    return QtFuture::makeReadyValueFuture(Result<QUrl>(QUrl::fromLocalFile(file.absoluteFilePath())));
}

} // namespace lmp::core
