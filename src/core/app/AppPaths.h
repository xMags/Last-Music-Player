#pragma once

#include <QString>

namespace lmp::core {

// Where the application keeps its data: one folder per user, with the file and
// folder names that every earlier version used. Installed copies upgrade in
// place, so none of these names may change.
class AppPaths {
public:
    // A data folder rooted at `root` (tests pass a temporary folder).
    explicit AppPaths(QString root);

    // The real folder: the per-user local application data folder (the
    // LOCALAPPDATA environment variable) plus AppIdentity::dataFolderName().
    // Without that variable it falls back to a folder of that name in the
    // current directory, as earlier versions did. In a Debug build a
    // non-empty DataRootVariable names the data folder itself instead.
    [[nodiscard]] static AppPaths forCurrentUser();

    // LMP_DATA_ROOT: the data folder for a developer run (Debug builds only).
    static constexpr const char* DataRootVariable = "LMP_DATA_ROOT";
    // Whether forCurrentUser() uses DataRootVariable (a Debug developer run).
    [[nodiscard]] static bool developerDataRoot();

    [[nodiscard]] QString root() const { return m_root; }

    // Settings.json: the user's settings.
    [[nodiscard]] QString settingsFile() const;
    // LastMusicState.json: the queue and window state between launches.
    [[nodiscard]] QString stateFile() const;
    // lastmusic.db: the library database (with its -wal and -shm files).
    [[nodiscard]] QString databaseFile() const;
    // Downloads.json: the download list.
    [[nodiscard]] QString downloadsFile() const;
    // Offline: the default download folder (the DownloadFolder setting overrides it).
    [[nodiscard]] QString offlineFolder() const;
    // thumbs: cover art read from local files.
    [[nodiscard]] QString thumbsFolder() const;
    // stream-cache: audio fetched ahead in API key mode.
    [[nodiscard]] QString streamCacheFolder() const;
    // picture-cache: covers and artist photos shown, and those downloads keep.
    [[nodiscard]] QString pictureCacheFolder() const;
    // download-extras: covers and lyrics fetched for downloads whose files
    // could not take them yet.
    [[nodiscard]] QString downloadExtrasFolder() const;

private:
    [[nodiscard]] QString child(const QString& name) const;

    QString m_root;
};

} // namespace lmp::core
