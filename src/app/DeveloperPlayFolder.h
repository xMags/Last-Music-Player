#pragma once

#include <QList>
#include <QString>
#include <QStringList>

#include <optional>

#include "core/model/Track.h"

namespace lmp::app {

// DEVELOPER OPTION, removed in P06 when the library exists: starting the app
// with "--play-folder <path>" plays the audio files of that folder, so there
// is something to play before the library can be browsed.

// The folder given after "--play-folder", or nothing when the option is absent
// or has no value.
[[nodiscard]] std::optional<QString> developerPlayFolder(const QStringList& arguments);

// The .mp3, .flac, .wav and .m4a files directly in `folder` (not in its
// subfolders), sorted by file name, as local songs titled by their file names.
[[nodiscard]] QList<core::Track> developerPlayFolderTracks(const QString& folder);

} // namespace lmp::app
