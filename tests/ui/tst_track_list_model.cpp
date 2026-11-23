#include <QList>
#include <QObject>
#include <QSignalSpy>
#include <QString>
#include <QTest>

#include <algorithm>
#include <memory>

#include "core/model/Track.h"
#include "ui/tracks/TrackListModel.h"

using lmp::core::Track;
using lmp::ui::TrackListModel;

namespace {

Track song(int number)
{
    Track track;
    track.title = QStringLiteral("Song %1 (Official Video)").arg(number);
    track.artist = QStringLiteral("Artist");
    track.album = QStringLiteral("Album - Single");
    track.durationText = QStringLiteral("3:00");
    track.sourceKind = QStringLiteral("local");
    track.filePath = QStringLiteral("C:/Music/%1.mp3").arg(number);
    return track;
}

// A list of `total` songs served in pages, counting the pages asked for.
class FakePages final : public lmp::ui::TrackPageSource {
public:
    explicit FakePages(int total)
        : m_total(total)
    {
    }

    int count() override { return m_total; }

    QList<Track> page(int offset, int limit) override
    {
        ++pages;
        QList<Track> result;
        for (int i = offset; i < std::min(offset + limit, m_total - shrinkBy); ++i) {
            result.append(song(i));
        }
        return result;
    }

    int pages = 0;
    int shrinkBy = 0;

private:
    int m_total;
};

} // namespace

// The song list model: short lists, pages, display text and row updates.
class TrackListModelTest : public QObject {
    Q_OBJECT

private slots:
    void shortListShowsDisplayText();
    void longListLoadsInPages();
    void fetchAllLoadsEverything();
    void shrunkenSourceStopsAsking();
    void playingAndLikedChangeOnlyTheirRows();
};

void TrackListModelTest::shortListShowsDisplayText()
{
    TrackListModel model;
    model.setTracks({song(1), song(2)});
    QCOMPARE(model.rowCount(), 2);
    QCOMPARE(model.totalCount(), 2);
    const QModelIndex first = model.index(0);
    QCOMPARE(model.data(first, TrackListModel::TitleRole).toString(), QStringLiteral("Song 1"));
    QCOMPARE(model.data(first, TrackListModel::AlbumRole).toString(), QStringLiteral("Album"));
    QCOMPARE(model.data(first, TrackListModel::NumberRole).toInt(), 1);
    QCOMPARE(model.data(first, TrackListModel::SourceKeyRole).toString(), QStringLiteral("local|c:/music/1.mp3"));
    QVERIFY(!model.canFetchMore({}));
    QCOMPARE(model.trackAt(1).title, song(2).title);
}

void TrackListModelTest::longListLoadsInPages()
{
    auto pages = std::make_shared<FakePages>(250);
    TrackListModel model;
    model.setSource(pages);
    QCOMPARE(model.totalCount(), 250);
    QCOMPARE(model.rowCount(), TrackListModel::PageSize);
    QCOMPARE(pages->pages, 1);
    QVERIFY(model.canFetchMore({}));
    model.fetchMore({});
    QCOMPARE(model.rowCount(), 200);
    model.fetchMore({});
    QCOMPARE(model.rowCount(), 250);
    QVERIFY(!model.canFetchMore({}));
    QCOMPARE(pages->pages, 3);
    QCOMPARE(model.data(model.index(249), TrackListModel::NumberRole).toInt(), 250);
}

void TrackListModelTest::fetchAllLoadsEverything()
{
    auto pages = std::make_shared<FakePages>(330);
    TrackListModel model;
    model.setSource(pages);
    model.fetchAll();
    QCOMPARE(model.rowCount(), 330);
    QCOMPARE(model.tracks().size(), 330);
}

void TrackListModelTest::shrunkenSourceStopsAsking()
{
    auto pages = std::make_shared<FakePages>(150);
    pages->shrinkBy = 60;
    TrackListModel model;
    model.setSource(pages);
    model.fetchAll();
    QCOMPARE(model.rowCount(), 90);
    QCOMPARE(model.totalCount(), 90);
    QVERIFY(!model.canFetchMore({}));
}

void TrackListModelTest::playingAndLikedChangeOnlyTheirRows()
{
    TrackListModel model;
    model.setTracks({song(1), song(2), song(3)});
    QSignalSpy changed(&model, &TrackListModel::dataChanged);
    const QString second = model.data(model.index(1), TrackListModel::SourceKeyRole).toString();

    model.setPlayingKey(second);
    QCOMPARE(changed.count(), 1);
    QCOMPARE(changed.first().at(0).toModelIndex().row(), 1);
    QVERIFY(model.data(model.index(1), TrackListModel::PlayingRole).toBool());
    QVERIFY(!model.data(model.index(0), TrackListModel::PlayingRole).toBool());

    changed.clear();
    model.setPlayingKey(model.data(model.index(2), TrackListModel::SourceKeyRole).toString());
    QCOMPARE(changed.count(), 2);
    QVERIFY(!model.data(model.index(1), TrackListModel::PlayingRole).toBool());

    changed.clear();
    model.setLiked(second, true);
    QCOMPARE(changed.count(), 1);
    QVERIFY(model.data(model.index(1), TrackListModel::LikedRole).toBool());
    model.setLiked(second, true);
    QCOMPARE(changed.count(), 1);
    model.setLiked(QStringLiteral("local|nowhere"), true);
    QCOMPARE(changed.count(), 1);
}

QTEST_GUILESS_MAIN(TrackListModelTest)
#include "tst_track_list_model.moc"
