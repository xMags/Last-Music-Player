#pragma once

#include <QString>

#include <optional>

#include "ui/navigation/Route.h"

namespace lmp::ui {

// What names an artist or a release at its source: the source tag and at
// least a name or an id. Text arrives from links and saved state, so every
// field is checked by RouteRules.
struct SourceIdentity {
    QString name;
    QString sourceTag;
    QString catalogId;
    QString sourceUrl;
    // The release's artist, which tells apart releases with the same title.
    QString artist;
};

// Builds catalogue routes, refusing parameters that do not name a page. The
// rules follow the other clients, so a page link means the same everywhere.
class RouteRules {
public:
    // `externalPlaylistPrefix` marks the ids of playlists opened from external
    // links; without it such playlists cannot be opened.
    explicit RouteRules(QString externalPlaylistPrefix = {});

    [[nodiscard]] std::optional<Route> catalogDetail(const QString& type,
                                                     const QString& catalogId) const;
    [[nodiscard]] std::optional<Route> libraryArtist(const QString& publicId) const;
    // `catalogArtistId` is an optional numeric catalogue id tried first.
    [[nodiscard]] std::optional<Route> sourceArtist(const SourceIdentity& identity,
                                                    const QString& catalogArtistId = {}) const;
    [[nodiscard]] std::optional<Route> sourceAlbum(const SourceIdentity& identity) const;
    [[nodiscard]] std::optional<Route> externalPlaylist(const QString& playlistId) const;
    [[nodiscard]] std::optional<Route> shared(const QString& code) const;
    [[nodiscard]] std::optional<Route> artistSection(const SourceIdentity& identity,
                                                     const QString& catalogArtistId,
                                                     const QString& sectionId,
                                                     const QString& region = {}) const;
    [[nodiscard]] std::optional<Route> shelf(const QString& shelfId) const;
    [[nodiscard]] std::optional<Route> charts(const QString& section) const;
    // A chart of `type` songs, albums or playlists. City charts are song
    // charts and may name a region; the global chart's id is fixed per type.
    [[nodiscard]] std::optional<Route> chart(const QString& type, const QString& scopeKind,
                                             const QString& scopeId,
                                             const QString& region = {}) const;

    // A region (catalogue storefront) code: two letters, lower case. Empty
    // when `value` is not one.
    [[nodiscard]] static QString normalizeRegion(const QString& value);

private:
    QString m_externalPlaylistPrefix;
};

} // namespace lmp::ui
