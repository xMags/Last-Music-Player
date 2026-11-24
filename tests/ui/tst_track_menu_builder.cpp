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
    void mixSitsBeforeGoToArtist();
    void downloadItemFollowsTheSongsState();
    void shareFollowsMix();
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

void TrackMenuBuilderTest::mixSitsBeforeGoToArtist()
{
    const auto items = TrackMenuBuilder::songMenu(song(QStringLiteral("Song"), QStringLiteral("Album")), {.mix = true});
    QCOMPARE(ids(items), (QStringList{QStringLiteral("play"), QStringLiteral("next"), QStringLiteral("queue"),
                                      QStringLiteral("playlist"), QStringLiteral("like"), QStringLiteral("mix"),
                                      QStringLiteral("artist"), QStringLiteral("album")}));
    QCOMPARE(items.at(5).label, QStringLiteral("Start mix"));
    QVERIFY(items.at(5).separatorBefore);
    QVERIFY(items.at(6).separatorBefore);
}

void TrackMenuBuilderTest::shareFollowsMix()
{
    const auto alone = TrackMenuBuilder::songMenu(song(QStringLiteral("Song"), QStringLiteral("Album")), {.share = true});
    QCOMPARE(alone.at(5).id, QStringLiteral("share"));
    QVERIFY(alone.at(5).separatorBefore);
    const auto both =
        TrackMenuBuilder::songMenu(song(QStringLiteral("Song"), QStringLiteral("Album")), {.mix = true, .share = true});
    QCOMPARE(both.at(5).id, QStringLiteral("mix"));
    QCOMPARE(both.at(6).id, QStringLiteral("share"));
    QVERIFY(!both.at(6).separatorBefore);
    QCOMPARE(both.at(7).id, QStringLiteral("artist"));
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

void TrackMenuBuilderTest::downloadItemFollowsTheSongsState()
{
    const Track track = song(QStringLiteral("Song"), QStringLiteral("Album"));
    const auto itemFor = [&track](const QString& state) {
        const auto items = TrackMenuBuilder::songMenu(track, {.download = state});
        for (const MenuItemListModel::Item& item : items) {
            if (item.id.contains(QStringLiteral("download"))) {
                return item;
            }
        }
        return MenuItemListModel::Item{};
    };
    QVERIFY(itemFor(QString()).id.isEmpty());
    QCOMPARE(itemFor(QStringLiteral("offer")).id, QStringLiteral("download"));
    QCOMPARE(itemFor(QStringLiteral("done")).label, QStringLiteral("Remove download"));
    QCOMPARE(itemFor(QStringLiteral("pending")).label, QStringLiteral("Cancel download"));
    QCOMPARE(itemFor(QStringLiteral("failed")).id, QStringLiteral("retry-download"));
    QVERIFY(itemFor(QStringLiteral("offer")).separatorBefore);
    // Right after Like.
    const QStringList list = ids(TrackMenuBuilder::songMenu(track, {.download = QStringLiteral("offer")}));
    QCOMPARE(list.indexOf(QStringLiteral("download")), list.indexOf(QStringLiteral("like")) + 1);
}

QTEST_GUILESS_MAIN(TrackMenuBuilderTest)
#include "tst_track_menu_builder.moc"
