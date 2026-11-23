#pragma once

#include <QImage>
#include <QLatin1StringView>
#include <QSize>
#include <QString>
#include <QStringView>
#include <QUrl>

#include <optional>

#include "core/model/Track.h"

namespace lmp::ui {

// Where a song's cover picture comes from, and how to show and decode it:
// - Remote: an http(s) address, shown through the ArtworkImageProvider
//   ("image://artwork/...", which fetches it the way the mode needs);
// - ImageFile: a picture file on this machine;
// - Embedded: the picture stored inside a local audio file, shown through
//   the EmbeddedCoverImageProvider ("image://embedded-cover/...");
// - None: no picture; the gradient placeholder shows.
// A plain value; load() is blocking and belongs on a worker thread.
struct CoverSource {
    enum class Kind { None, Remote, ImageFile, Embedded };

    // The image provider ids registered with the QML engine.
    static constexpr QLatin1StringView ProviderId{"embedded-cover"};
    static constexpr QLatin1StringView RemoteProviderId{"artwork"};

    Kind kind = Kind::None;
    // ImageFile: the picture; Embedded: the audio file that holds it.
    QString path;
    // Remote: the address.
    QUrl url;

    // The song's artwork address when it has one, otherwise the picture
    // inside its local file.
    [[nodiscard]] static CoverSource forTrack(const core::Track& track);
    // A song read from the library, whose file the scan has already looked
    // into: an empty artwork means it has no picture, so its file is not
    // asked again (which would fail for every song without a cover).
    [[nodiscard]] static CoverSource forLibraryTrack(const core::Track& track);
    // The id that picks the song's placeholder gradient: its queue identity
    // (server id or file path), or its title when it has neither.
    [[nodiscard]] static QString placeholderIdFor(const core::Track& track);
    // The audio file path an "image://embedded-cover/<id>" request names.
    [[nodiscard]] static std::optional<QString> embeddedPathFromProviderId(QStringView id);
    // The http(s) address an "image://artwork/<id>" request names.
    [[nodiscard]] static std::optional<QString> remoteUrlFromProviderId(QStringView id);

    // What an Image item shows; empty for None.
    [[nodiscard]] QUrl displayUrl() const;
    // True for the kinds load() can decode (pictures on this machine).
    [[nodiscard]] bool decodesLocally() const;
    // Decodes the picture, scaled down to cover `fit` (keeping its shape)
    // when `fit` is valid and smaller. A null image when there is none or it
    // cannot be read. Blocking file I/O and decoding: worker threads only.
    [[nodiscard]] QImage load(QSize fit = {}) const;

    friend bool operator==(const CoverSource&, const CoverSource&) = default;
};

} // namespace lmp::ui
