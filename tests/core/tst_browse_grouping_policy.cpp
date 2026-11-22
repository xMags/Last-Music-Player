#include <QList>
#include <QObject>
#include <QString>
#include <QTest>

#include "core/library/BrowseGroupingPolicy.h"

using lmp::core::browseViewPreferenceTag;
using lmp::core::BrowseResultInput;
using lmp::core::BrowseResultKind;
using lmp::core::BrowseViewPreference;
using lmp::core::buildGroupedBrowseResults;
using lmp::core::GroupedBrowseResults;
using lmp::core::parseBrowseViewPreference;

class BrowseGroupingPolicyTest : public QObject {
    Q_OBJECT

private slots:
    void browseGroupingPolicy();
    void entityFieldsAndIdentities();
    void viewPreferenceTags();
};

void BrowseGroupingPolicyTest::browseGroupingPolicy()
{
    using Kind = BrowseResultKind;
    const QList<BrowseResultInput> inputs{
        {Kind::Song, QStringLiteral("Neon Sky"), QStringLiteral("Aurora"), QStringLiteral("First Light"),
         QStringLiteral("2024"), QStringLiteral("3:20"), QStringLiteral("song-1"), 0},
        {Kind::Song, QStringLiteral("Afterglow"), QStringLiteral("Aurora"), QStringLiteral("First Light"),
         QStringLiteral("2024"), QStringLiteral("4:01"), QStringLiteral("song-2"), 0},
        {Kind::Song, QStringLiteral("Rock Song"), QStringLiteral("The Band"), QStringLiteral("Stone"), {},
         QStringLiteral("2:59"), QStringLiteral("song-3"), 0},
        {Kind::Playlist, QStringLiteral("Aurora Picks"), QStringLiteral("Playlist · 9 tracks"), {}, {}, {},
         QStringLiteral("playlist-1"), 9},
        {Kind::Playlist, QStringLiteral("Road Trip"), QStringLiteral("Aurora curator"), {}, {}, {},
         QStringLiteral("playlist-2"), 12},
    };

    const GroupedBrowseResults grouped = buildGroupedBrowseResults(inputs, QStringLiteral("aurora"), false);
    QCOMPARE(grouped.groups.size(), qsizetype{4});
    QVERIFY(grouped.groups[0].kind == Kind::Song && grouped.groups[1].kind == Kind::Album
            && grouped.groups[2].kind == Kind::Playlist && grouped.groups[3].kind == Kind::Artist);
    QCOMPARE(grouped.groups[0].items.size(), qsizetype{2});
    QVERIFY(grouped.groups[1].items.size() == 1
            && grouped.groups[1].items[0].subtitle == QStringLiteral("Aurora · 2024")
            && grouped.groups[1].items[0].memberIndices.size() == 2);
    QVERIFY(grouped.groups[2].items.size() == 1 && grouped.groups[2].items[0].title == QStringLiteral("Aurora Picks"));
    QVERIFY(grouped.groups[3].items.size() == 1 && grouped.groups[3].items[0].subtitle == QStringLiteral("1 album"));
    QCOMPARE(grouped.displayedEntityCount, qsizetype{5});

    const GroupedBrowseResults subtitleFalsePositive =
        buildGroupedBrowseResults(inputs, QStringLiteral("tracks"), false);
    QVERIFY(subtitleFalsePositive.groups.isEmpty());
    const GroupedBrowseResults artistFalsePositive =
        buildGroupedBrowseResults(inputs, QStringLiteral("curator"), false);
    QVERIFY(artistFalsePositive.groups.isEmpty());

    const QList<BrowseResultInput> resolvedUrl{
        {Kind::Song, QStringLiteral("Resolved title"), QStringLiteral("Resolved artist"),
         QStringLiteral("Resolved album"), {}, QStringLiteral("3:00"), QStringLiteral("url"), 0},
    };
    const GroupedBrowseResults withoutUrlPreservation =
        buildGroupedBrowseResults(resolvedUrl, QStringLiteral("https://media.example.test/watch/123"), false);
    QVERIFY(withoutUrlPreservation.groups.isEmpty());
    const GroupedBrowseResults withUrlPreservation =
        buildGroupedBrowseResults(resolvedUrl, QStringLiteral("https://media.example.test/watch/123"), true);
    QVERIFY(withUrlPreservation.groups.size() == 1 && withUrlPreservation.groups[0].kind == Kind::Song
            && withUrlPreservation.displayedEntityCount == 1);

    QVERIFY(parseBrowseViewPreference(QStringLiteral("list")) == BrowseViewPreference::List);
    QVERIFY(parseBrowseViewPreference(QStringLiteral(" LIST ")) == BrowseViewPreference::List);
    QVERIFY(parseBrowseViewPreference(QStringLiteral("tiles")) == BrowseViewPreference::Grid);
    QVERIFY(parseBrowseViewPreference(QString()) == BrowseViewPreference::Grid);
}

void BrowseGroupingPolicyTest::entityFieldsAndIdentities()
{
    using Kind = BrowseResultKind;
    const QList<BrowseResultInput> inputs{
        {Kind::Song, QStringLiteral("One"), QStringLiteral("Echo"), QStringLiteral("Disc"), QStringLiteral("1999"),
         QStringLiteral("1:00"), QStringLiteral("k1"), 0},
        // Same album and artist apart from case and spaces: one album entity.
        {Kind::Song, QStringLiteral("Two"), QStringLiteral(" echo "), QStringLiteral("DISC "), {},
         QStringLiteral("2:00"), QStringLiteral("k2"), 0},
        {Kind::Song, QStringLiteral("Three"), QStringLiteral("Echo"), QStringLiteral("Other Disc"), {}, {},
         QStringLiteral("k3"), 0},
        {Kind::Song, QStringLiteral("Echo Single"), QString(), QString(), {}, {}, QStringLiteral("k4"), 0},
        {Kind::Playlist, QStringLiteral("Echoes"), {}, {}, {}, {}, QStringLiteral("p1"), 1},
    };

    const GroupedBrowseResults grouped = buildGroupedBrowseResults(inputs, QStringLiteral("  ECHO "), false);
    QCOMPARE(grouped.groups.size(), qsizetype{4});

    const auto& songs = grouped.groups[0].items;
    QCOMPARE(songs.size(), qsizetype{4});
    QCOMPARE(songs[0].subtitle, QStringLiteral("Echo · Disc"));
    QCOMPARE(songs[0].meta, QStringLiteral("1:00"));
    QCOMPARE(songs[0].key, QStringLiteral("k1"));
    QCOMPARE(songs[3].subtitle, QString());
    QCOMPARE(songs[3].memberIndices, QList<qsizetype>{3});

    // Albums come from songs with an album; the first member names the entity.
    const auto& albums = grouped.groups[1].items;
    QCOMPARE(albums.size(), qsizetype{2});
    QCOMPARE(albums[0].title, QStringLiteral("Disc"));
    QCOMPARE(albums[0].key, QStringLiteral("Disc"));
    QCOMPARE(albums[0].subtitle, QStringLiteral("Echo · 1999"));
    QCOMPARE(albums[0].memberIndices, (QList<qsizetype>{0, 1}));
    QCOMPARE(albums[0].representativeIndex, qsizetype{0});
    QCOMPARE(albums[1].subtitle, QStringLiteral("Echo"));

    const auto& playlists = grouped.groups[2].items;
    QCOMPARE(playlists.size(), qsizetype{1});
    QCOMPARE(playlists[0].subtitle, QStringLiteral("1 track"));
    QCOMPARE(playlists[0].representativeIndex, qsizetype{4});

    const auto& artists = grouped.groups[3].items;
    QCOMPARE(artists.size(), qsizetype{1});
    QCOMPARE(artists[0].title, QStringLiteral("Echo"));
    QCOMPARE(artists[0].memberIndices, (QList<qsizetype>{0, 1, 2}));
    QCOMPARE(artists[0].subtitle, QStringLiteral("2 albums"));
    QCOMPARE(grouped.displayedEntityCount, qsizetype{8});

    QVERIFY(buildGroupedBrowseResults(inputs, QStringLiteral(" \t "), true).groups.isEmpty());
    QCOMPARE(buildGroupedBrowseResults(inputs, QStringLiteral("1999"), false).groups.size(), qsizetype{1});
}

void BrowseGroupingPolicyTest::viewPreferenceTags()
{
    QCOMPARE(browseViewPreferenceTag(BrowseViewPreference::List), QStringLiteral("list"));
    QCOMPARE(browseViewPreferenceTag(BrowseViewPreference::Grid), QStringLiteral("grid"));
    QVERIFY(parseBrowseViewPreference(browseViewPreferenceTag(BrowseViewPreference::List))
            == BrowseViewPreference::List);
}

QTEST_GUILESS_MAIN(BrowseGroupingPolicyTest)
#include "tst_browse_grouping_policy.moc"
