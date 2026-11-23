#pragma once

#include <QMap>
#include <QMetaType>
#include <QString>
#include <QtQml/qqmlregistration.h>

namespace lmp::ui {

// One place in the application: a main view, or a catalogue page with the
// parameters that name it. Routes are plain values; RouteRules builds the
// catalogue ones and refuses parameters that do not name a page.
class Route {
    Q_GADGET
    QML_VALUE_TYPE(route)

    Q_PROPERTY(Kind kind READ kind CONSTANT FINAL)

public:
    enum class Kind {
        Home,
        Browse,
        Library,
        Settings,
        Offline,
        // An album, song, playlist or artist from the catalogue, by its id.
        CatalogDetail,
        // An artist from the user's library, by its public id.
        LibraryArtist,
        // An artist or a release known by its source identity.
        SourceArtist,
        SourceAlbum,
        // A playlist opened from an external link.
        ExternalPlaylist,
        // The item behind a share link.
        Shared,
        // One section of an artist page (top songs, albums, ...).
        ArtistSection,
        // A discovery shelf with a page of its own.
        Shelf,
        // A list of charts, and one chart.
        Charts,
        Chart,
    };
    Q_ENUM(Kind)

    Route() = default;
    explicit Route(Kind kind, QMap<QString, QString> params = {});

    [[nodiscard]] Kind kind() const { return m_kind; }
    [[nodiscard]] bool isMainView() const;
    // A parameter by name; empty when the route has none of that name.
    Q_INVOKABLE QString param(const QString& name) const;
    [[nodiscard]] const QMap<QString, QString>& params() const { return m_params; }

    friend bool operator==(const Route&, const Route&) = default;

private:
    Kind m_kind = Kind::Home;
    QMap<QString, QString> m_params;
};

// The route kinds for QML, as `Route.Settings` and so on.
namespace RouteForeign {
Q_NAMESPACE
QML_NAMED_ELEMENT(Route)
QML_FOREIGN_NAMESPACE(lmp::ui::Route)
} // namespace RouteForeign

} // namespace lmp::ui

Q_DECLARE_METATYPE(lmp::ui::Route)
