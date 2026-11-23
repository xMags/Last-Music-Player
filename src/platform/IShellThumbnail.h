#pragma once

#include <QByteArray>
#include <QString>

#include <optional>

namespace lmp::platform {

// The picture the system shows for a file, as encoded image bytes. Used only
// for audio files that carry no picture of their own. Nothing when the system
// has none, it is not an image, or it is larger than the artwork limit.
//
// Thread-safe: called from the scan worker.
class IShellThumbnail {
public:
    virtual ~IShellThumbnail() = default;

    [[nodiscard]] virtual std::optional<QByteArray> thumbnail(const QString& filePath) = 0;

protected:
    IShellThumbnail() = default;
    IShellThumbnail(const IShellThumbnail&) = default;
    IShellThumbnail& operator=(const IShellThumbnail&) = default;
};

} // namespace lmp::platform
