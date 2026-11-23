#pragma once

#include "platform/IShellThumbnail.h"
#include "platform/ITagReader.h"

namespace lmp::platform::windows {

// Reads tags, dates and the shell thumbnail through Windows.Storage, the API
// earlier versions used (StorageFile music properties, basic properties and
// GetThumbnailAsync). Every call blocks its thread until Windows answers, so
// it is meant for the scan worker; each calling thread joins the
// multi-threaded COM apartment on first use.
class WindowsTagReader final : public ITagReader, public IShellThumbnail {
public:
    WindowsTagReader() = default;

    [[nodiscard]] core::Result<TrackTags> read(const QString& filePath) override;
    [[nodiscard]] std::optional<QByteArray> thumbnail(const QString& filePath) override;
};

} // namespace lmp::platform::windows
