#pragma once

#include <QString>

namespace lmp::platform {
class IShellThumbnail;
}

namespace lmp::core {

// The cover pictures of local files, copied into the thumbs folder so they
// can be shown by address. A file's picture is taken byte for byte from its
// own tags (ID3, FLAC, MP4), so the tagger's resolution survives; only a file
// with no picture of its own falls back to the system thumbnail.
//
// Cache names: the first 16 hex digits of the SHA-1 of the file's path
// (UTF-8), then "-v2" (the earlier cache generation marker) and the picture's
// own extension. Earlier versions named the files with a compiler-specific
// hash that cannot be reproduced; their files stay valid because the database
// keeps each picture's full address, and a rescan writes the new names.
//
// Thread-safe for different files; meant for the scan worker.
class ArtworkCache {
public:
    // The thumbnail source may be null (no fallback). It must outlive this.
    ArtworkCache(QString thumbsFolder, platform::IShellThumbnail* shellThumbnails);

    // The file address ("file:///...") of the cached picture for `filePath`,
    // writing it first when it is not cached yet; empty when the file has no
    // picture or it could not be written.
    [[nodiscard]] QString ensure(const QString& filePath) const;

    // The cache name without extension, for tests.
    [[nodiscard]] static QString cacheKey(const QString& filePath);

private:
    [[nodiscard]] QString cachedFile(const QString& key) const;

    QString m_folder;
    platform::IShellThumbnail* m_shellThumbnails;
};

} // namespace lmp::core
