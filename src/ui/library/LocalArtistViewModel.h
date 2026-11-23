#pragma once

#include <QList>
#include <QObject>
#include <QString>
#include <QUrl>
#include <QtQml/qqmlregistration.h>

#include "core/model/Track.h"
#include "ui/library/CollectionCardModel.h"
#include "ui/tracks/TrackListModel.h"

namespace lmp::core {
class PlayerController;
class TrackActions;
} // namespace lmp::core

namespace lmp::core::db {
class LibraryQueries;
}

namespace lmp::ui {

class Navigator;
class TrackMenuController;

// The page of an artist from the local library (`App.localArtist`), as the
// current app builds it from the artist's songs (BuildLocalArtistDetail):
// the songs most played first, the picture of the first song with a cover,
// and the albums the songs carry (case-insensitively grouped, "Unknown
// Album" left out), each counting its songs. The page shows the header
// with Play and Shuffle, the top songs (five, or all after "Show all") and
// the albums shelf. It follows the navigator: a Collection route of kind
// "artist" opens it.
//
// GUI thread only.
class LocalArtistViewModel : public QObject {
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Provided by App.localArtist")

    Q_PROPERTY(QString name READ name NOTIFY opened FINAL)
    Q_PROPERTY(QUrl artworkUrl READ artworkUrl NOTIFY opened FINAL)
    Q_PROPERTY(QString artworkId READ artworkId NOTIFY opened FINAL)
    Q_PROPERTY(QString artworkLabel READ artworkLabel NOTIFY opened FINAL)
    // "12 songs · 3 albums".
    Q_PROPERTY(QString info READ info NOTIFY opened FINAL)
    Q_PROPERTY(lmp::ui::TrackListModel* topSongs READ topSongs CONSTANT FINAL)
    Q_PROPERTY(bool showingAll READ showingAll WRITE setShowingAll NOTIFY opened FINAL)
    // More songs than the top five.
    Q_PROPERTY(bool hasMore READ hasMore NOTIFY opened FINAL)
    Q_PROPERTY(lmp::ui::CollectionCardModel* albums READ albums CONSTANT FINAL)

public:
    struct Parts {
        core::db::LibraryQueries& library;
        core::TrackActions& actions;
        core::PlayerController& player;
        TrackMenuController& trackMenu;
        Navigator& navigator;
    };

    static constexpr int TopSongCount = 5;

    // Everything in `parts` is owned by the composition root and outlives this.
    explicit LocalArtistViewModel(Parts parts, QObject* parent = nullptr);

    void open(const QString& name);

    [[nodiscard]] QString name() const { return m_name; }
    [[nodiscard]] QUrl artworkUrl() const { return m_artworkUrl; }
    [[nodiscard]] QString artworkId() const { return QStringLiteral("artist:") + m_name; }
    [[nodiscard]] QString artworkLabel() const;
    [[nodiscard]] QString info() const;
    [[nodiscard]] TrackListModel* topSongs() { return &m_topSongs; }
    [[nodiscard]] bool showingAll() const { return m_showingAll; }
    void setShowingAll(bool all);
    [[nodiscard]] bool hasMore() const { return m_songs.size() > TopSongCount; }
    [[nodiscard]] CollectionCardModel* albums() { return &m_albums; }

    Q_INVOKABLE void play();
    Q_INVOKABLE void shuffle();
    Q_INVOKABLE void playSong(int row);
    Q_INVOKABLE void likeSong(int row);
    Q_INVOKABLE void prepareSongMenu(int row);
    Q_INVOKABLE void openAlbum(int row);

signals:
    void opened();

private:
    void followNavigator();
    void showSongs();
    void followPlayer();

    core::db::LibraryQueries& m_library;
    core::TrackActions& m_actions;
    core::PlayerController& m_player;
    TrackMenuController& m_trackMenu;
    Navigator& m_navigator;

    QString m_name;
    QUrl m_artworkUrl;
    QList<core::Track> m_songs;
    bool m_showingAll = false;
    TrackListModel m_topSongs;
    CollectionCardModel m_albums;
};

} // namespace lmp::ui
