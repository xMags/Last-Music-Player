#include <QList>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QTest>

#include "core/model/Track.h"
#include "ui/tracks/TrackMenuBuilder.h"

using lmp::core::Track;
using lmp::ui::MenuItemListModel;
using lmp::ui::TrackMenuBuilder;

// The song menu's items and order for what the mode allows.
class TrackMenuBuilderTest : public QObject {
    Q_OBJECT

private slots:
    void fullMenuInTheReferenceOrder();
    void likedSongOffersRemovingTheLike();
    void onAPlaylistPageOffersRemoving();
    void songsThatCannotPlayOrBeKeptLoseThoseItems();
    void singlesHaveNoAlbumItem();
    void playlistMenuStartsWithNewPlaylist();
};

namespace {

Track song(const QString& title, const QString& album)
{
    Track track;
    track.title = title;
    track.album = album;
    track.artist = QStringLiteral("Artist");
    return track;
}

QStringList ids(const QList<MenuItemListModel::Item>& items)
{
    QStringList list;
    for (const MenuItemListModel::Item& item : items) {
        list.append(item.id);
    }
    return list;
}

} // namespace

void TrackMenuBuilderTest::fullMenuInTheReferenceOrder()
{
    const auto items = TrackMenuBuilder::songMenu(song(QStringLiteral("Song"), QStringLiteral("Album")), {});
    QCOMPARE(ids(items), (QStringList{QStringLiteral("play"), QStringLiteral("next"), QStringLiteral("queue"),
                                      QStringLiteral("playlist"), QStringLiteral("like"), QStringLiteral("artist"),
                                      QStringLiteral("album")}));
    QVERIFY(items.at(3).separatorBefore);
    QVERIFY(items.at(5).separatorBefore);
    QCOMPARE(items.at(4).label, QStringLiteral("Add to Liked Songs"));
}

void TrackMenuBuilderTest::likedSongOffersRemovingTheLike()
{
    Track liked = song(QStringLiteral("Song"), QStringLiteral("Album"));
    liked.liked = true;
    const auto items = TrackMenuBuilder::songMenu(liked, {});
    QCOMPARE(items.at(4).label, QStringLiteral("Remove from Liked Songs"));
    QCOMPARE(items.at(4).iconName, QStringLiteral("heart-fill"));
}

void TrackMenuBuilderTest::onAPlaylistPageOffersRemoving()
{
    const auto items =
        TrackMenuBuilder::songMenu(song(QStringLiteral("Song"), QStringLiteral("Album")), {}, QStringLiteral("p1"));
    QCOMPARE(items.at(5).id, QStringLiteral("remove-from-playlist"));
    QVERIFY(items.at(5).danger);
}

void TrackMenuBuilderTest::songsThatCannotPlayOrBeKeptLoseThoseItems()
{
    const Track track = song(QStringLiteral("Song"), QStringLiteral("Album"));
    QCOMPARE(ids(TrackMenuBuilder::songMenu(track, {.play = false, .keep = true})),
             (QStringList{QStringLiteral("playlist"), QStringLiteral("like"), QStringLiteral("artist"),
                          QStringLiteral("album")}));
    QCOMPARE(ids(TrackMenuBuilder::songMenu(track, {.play = true, .keep = false}, QStringLiteral("p1"))),
             (QStringList{QStringLiteral("play"), QStringLiteral("next"), QStringLiteral("queue"),
                          QStringLiteral("artist"), QStringLiteral("album")}));
}

void TrackMenuBuilderTest::singlesHaveNoAlbumItem()
{
    QVERIFY(!ids(TrackMenuBuilder::songMenu(song(QStringLiteral("Song"), QStringLiteral("Song - Single")), {}))
                 .contains(QStringLiteral("album")));
    QVERIFY(!ids(TrackMenuBuilder::songMenu(song(QStringLiteral("Song (Remix)"), QStringLiteral("Song")), {}))
                 .contains(QStringLiteral("album")));
    QVERIFY(!ids(TrackMenuBuilder::songMenu(song(QStringLiteral("Song"), QString()), {}))
                 .contains(QStringLiteral("album")));
    QVERIFY(ids(TrackMenuBuilder::songMenu(song(QStringLiteral("Title Track"), QStringLiteral("The Album")), {}))
                .contains(QStringLiteral("album")));
}

void TrackMenuBuilderTest::playlistMenuStartsWithNewPlaylist()
{
    Track first;
    first.title = QStringLiteral("Road Trip");
    first.sourceUrl = QStringLiteral("playlist:1");
    Track second;
    second.title = QStringLiteral("Calm");
    second.sourceUrl = QStringLiteral("playlist:2");
    const auto items = TrackMenuBuilder::playlistMenu({first, second});
    QCOMPARE(ids(items), (QStringList{QStringLiteral("new-playlist"), QStringLiteral("to-playlist:playlist:1"),
                                      QStringLiteral("to-playlist:playlist:2")}));
    QVERIFY(items.at(1).separatorBefore);
    QVERIFY(!items.at(2).separatorBefore);
    QCOMPARE(TrackMenuBuilder::playlistMenu({}).size(), 1);
}

QTEST_GUILESS_MAIN(TrackMenuBuilderTest)
#include "tst_track_menu_builder.moc"
