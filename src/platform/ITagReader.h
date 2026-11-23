#pragma once

#include <QDateTime>
#include <QString>

#include "core/async/Result.h"

namespace lmp::platform {

// What the system knows about one audio file. Empty strings where a tag is
// missing; the scanner fills in the fallbacks.
struct TrackTags {
    QString title;
    QString artist;
    QString album;
    // The first genre.
    QString genre;
    double durationSeconds = 0;
    // The file's creation and last change times, invalid when unknown.
    QDateTime created;
    QDateTime modified;
};

// Reads a file's music tags and dates through the system's property system.
// A failure means the file could not be read at all (the scanner skips it);
// a single missing tag is not a failure.
//
// Thread-safe: called from the scan worker.
class ITagReader {
public:
    virtual ~ITagReader() = default;

    [[nodiscard]] virtual core::Result<TrackTags> read(const QString& filePath) = 0;

protected:
    ITagReader() = default;
    ITagReader(const ITagReader&) = default;
    ITagReader& operator=(const ITagReader&) = default;
};

} // namespace lmp::platform
