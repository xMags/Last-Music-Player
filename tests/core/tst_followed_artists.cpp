#include <QList>
#include <QObject>
#include <QRegularExpression>
#include <QString>
#include <QStringList>
#include <QTest>

#include "TempLibrary.h"
#include "core/db/AccountRecords.h"
#include "core/db/FollowedArtistRepository.h"
#include "core/db/GroupQueries.h"
#include "core/db/TrackRepository.h"
#include "core/model/Track.h"

using lmp::core::FailureKind;
using lmp::core::Track;
using lmp::core::db::FollowedArtist;
using lmp::core::db::FollowedArtistRepository;
using lmp::core::db::GroupQueries;
using lmp::core::db::TrackRepository;

namespace {

const QString kScope = QStringLiteral("account|owner-a");

FollowedArtist artist(const QString& name, const QString& catalogId = {}, const QString& sourceUrl = {})
{
    FollowedArtist followed;
    followed.scopeKey = kScope;
    followed.name = name;
    followed.catalogId = catalogId;
    followed.sourceUrl = sourceUrl;
    followed.provider = catalogId.isEmpty() ? QString() : QStringLiteral("Source-A");
    return followed;
}

Track localSong(const QString& title, const QString& artistName)
{
    Track track;
    track.title = title;
    track.artist = artistName;
    track.album = QStringLiteral("Album");
    track.durationSeconds = 100;
    track.filePath = QStringLiteral("C:/Music/") + title + QStringLiteral(".flac");
    return track;
}

const Track* findCard(const QList<Track>& cards, const QString& title)
{
    for (const Track& card : cards) {
        if (card.title == title) {
            return &card;
        }
    }
    return nullptr;
}

} // namespace

class FollowedArtistsTest : public QObject {
    Q_OBJECT

private slots:
    void followStoresTheComputedIdentity();
    void followRefusesBadInput();
    void followingAgainKeepsTheFirstTime();
    void unfollowAndLookupsAreScoped();
    void artistCardsShowFollowedArtists();
};

void FollowedArtistsTest::followStoresTheComputedIdentity()
{
    TempLibrary library;
    QVERIFY(library.ok());
    FollowedArtistRepository followed(library.database());

    QVERIFY(followed.followArtist(artist(QStringLiteral("  The   Band "), QStringLiteral("cat-1"))));
    QVERIFY(followed.followArtist(artist(QStringLiteral("Url Artist"), {}, QStringLiteral("https://music.example.test/a/2"))));
    FollowedArtist byName = artist(QStringLiteral("Name  Only"));
    byName.identityKey = QStringLiteral("name|name only");
    byName.addedAtUtc = QStringLiteral("2020-01-01T00:00:00Z");
    QVERIFY(followed.followArtist(byName));

    const QList<FollowedArtist> loaded = followed.loadFollowedArtists(kScope);
    QCOMPARE(loaded.size(), 3);
    // Oldest last.
    QCOMPARE(loaded.at(2).name, QStringLiteral("Name  Only"));
    QCOMPARE(loaded.at(2).identityKey, QStringLiteral("name|name only"));
    QCOMPARE(loaded.at(2).addedAtUtc, QStringLiteral("2020-01-01T00:00:00Z"));
    QStringList keys{loaded.at(0).identityKey, loaded.at(1).identityKey};
    keys.sort();
    QCOMPARE(keys, (QStringList{QStringLiteral("catalog|source-a|cat-1"), QStringLiteral("url|https://music.example.test/a/2")}));

    // An empty time means now, in the ISO 8601 form.
    static const QRegularExpression isoUtc(QStringLiteral("^\\d{4}-\\d{2}-\\d{2}T\\d{2}:\\d{2}:\\d{2}Z$"));
    QVERIFY(isoUtc.match(loaded.at(0).addedAtUtc).hasMatch());
    QVERIFY(followed.isArtistFollowed(kScope, QStringLiteral("catalog|source-a|cat-1")));
}

void FollowedArtistsTest::followRefusesBadInput()
{
    TempLibrary library;
    QVERIFY(library.ok());
    FollowedArtistRepository followed(library.database());

    FollowedArtist noScope = artist(QStringLiteral("A"));
    noScope.scopeKey.clear();
    const auto refused = followed.followArtist(noScope);
    QVERIFY(!refused);
    QCOMPARE(refused.failure().kind, FailureKind::NotFound);
    QVERIFY(!followed.followArtist(artist(QString())));
    QVERIFY(!followed.followArtist(artist(QString(257, QLatin1Char('n')))));
    QVERIFY(followed.followArtist(artist(QString(256, QLatin1Char('n')))));
    FollowedArtist longScope = artist(QStringLiteral("A"));
    longScope.scopeKey = QString(161, QLatin1Char('s'));
    QVERIFY(!followed.followArtist(longScope));
    FollowedArtist longProvider = artist(QStringLiteral("A"), QStringLiteral("cat-1"));
    longProvider.provider = QString(81, QLatin1Char('p'));
    QVERIFY(!followed.followArtist(longProvider));
    QVERIFY(!followed.followArtist(artist(QStringLiteral("A"), QString(257, QLatin1Char('c')))));
    QVERIFY(!followed.followArtist(
        artist(QStringLiteral("A"), {}, QStringLiteral("https://music.example.test/") + QString(2048, QLatin1Char('u')))));
    QVERIFY(!followed.followArtist(artist(QStringLiteral("A"), {}, QStringLiteral("https://music.example.test/a?token=x"))));
    QVERIFY(!followed.followArtist(artist(QStringLiteral("A"), {}, QStringLiteral("ftp://music.example.test/a"))));
    FollowedArtist wrongKey = artist(QStringLiteral("A"));
    wrongKey.identityKey = QStringLiteral("name|b");
    QVERIFY(!followed.followArtist(wrongKey));
    // A name of white space only has no identity.
    QVERIFY(!followed.followArtist(artist(QStringLiteral("   "))));

    QCOMPARE(library.number("SELECT COUNT(*) FROM FollowedArtists;"), 1);
}

void FollowedArtistsTest::followingAgainKeepsTheFirstTime()
{
    TempLibrary library;
    QVERIFY(library.ok());
    FollowedArtistRepository followed(library.database());

    FollowedArtist first = artist(QStringLiteral("Old Name"), QStringLiteral("cat-1"));
    first.addedAtUtc = QStringLiteral("2026-01-01T00:00:00Z");
    QVERIFY(followed.followArtist(first));
    FollowedArtist renamed = artist(QStringLiteral("New Name"), QStringLiteral("cat-1"));
    renamed.addedAtUtc = QStringLiteral("2026-05-05T00:00:00Z");
    renamed.sourceUrl = QStringLiteral("https://music.example.test/a/1");
    QVERIFY(followed.followArtist(renamed));

    const QList<FollowedArtist> loaded = followed.loadFollowedArtists(kScope);
    QCOMPARE(loaded.size(), 1);
    QCOMPARE(loaded.at(0).name, QStringLiteral("New Name"));
    QCOMPARE(loaded.at(0).sourceUrl, QStringLiteral("https://music.example.test/a/1"));
    QCOMPARE(loaded.at(0).addedAtUtc, QStringLiteral("2026-01-01T00:00:00Z"));
}

void FollowedArtistsTest::unfollowAndLookupsAreScoped()
{
    TempLibrary library;
    QVERIFY(library.ok());
    FollowedArtistRepository followed(library.database());
    QVERIFY(followed.followArtist(artist(QStringLiteral("Shared"))));
    FollowedArtist elsewhere = artist(QStringLiteral("Shared"));
    elsewhere.scopeKey = QStringLiteral("api-key|server");
    QVERIFY(followed.followArtist(elsewhere));

    const QString key = QStringLiteral("name|shared");
    QVERIFY(followed.unfollowArtist(kScope, key));
    QVERIFY(!followed.isArtistFollowed(kScope, key));
    QVERIFY(followed.isArtistFollowed(QStringLiteral("api-key|server"), key));
    QVERIFY(followed.unfollowArtist(kScope, key));
    QVERIFY(followed.loadFollowedArtists(kScope).isEmpty());

    QVERIFY(!followed.unfollowArtist(QString(), key));
    QVERIFY(!followed.unfollowArtist(kScope, QString(2401, QLatin1Char('k'))));
    QVERIFY(!followed.isArtistFollowed(QString(161, QLatin1Char('s')), key));
    QVERIFY(followed.loadFollowedArtists(QString()).isEmpty());
}

void FollowedArtistsTest::artistCardsShowFollowedArtists()
{
    TempLibrary library;
    QVERIFY(library.ok());
    TrackRepository tracks(library.database());
    QVERIFY(tracks.upsertLocalTrack(localSong(QStringLiteral("One"), QStringLiteral("Local Band")), QStringLiteral("local|1")));
    QVERIFY(tracks.upsertLocalTrack(localSong(QStringLiteral("Two"), QStringLiteral("Other Band")), QStringLiteral("local|2")));
    FollowedArtistRepository followed(library.database());
    FollowedArtist matching = artist(QStringLiteral("local  band"));
    matching.addedAtUtc = QStringLiteral("2026-03-01T00:00:00Z");
    QVERIFY(followed.followArtist(matching));
    FollowedArtist remote = artist(QStringLiteral("Remote Act"), QStringLiteral("cat-9"));
    remote.sourceUrl = QStringLiteral("https://music.example.test/a/9");
    remote.addedAtUtc = QStringLiteral("2026-04-01T00:00:00Z");
    QVERIFY(followed.followArtist(remote));
    FollowedArtist plain = artist(QStringLiteral("Plain Act"));
    plain.scopeKey = QStringLiteral("account|owner-b");
    QVERIFY(followed.followArtist(plain));

    GroupQueries groups(library.database());
    const QList<Track> withoutScope = groups.loadArtists();
    QCOMPARE(withoutScope.size(), 2);
    QVERIFY(!findCard(withoutScope, QStringLiteral("Local Band"))->followed);

    const QList<Track> cards = groups.loadArtists(kScope);
    QCOMPARE(cards.size(), 3);
    const Track* local = findCard(cards, QStringLiteral("Local Band"));
    QVERIFY(local != nullptr);
    QVERIFY(local->followed);
    QCOMPARE(local->artist, QStringLiteral("1 songs - 1 albums  ") + QChar(char16_t{0x00B7}) + QStringLiteral("  Following"));
    QCOMPARE(local->dateAdded, QStringLiteral("2026-03-01T00:00:00Z"));
    QCOMPARE(local->dateAddedSortKey, 1772323200.0);
    QVERIFY(!findCard(cards, QStringLiteral("Other Band"))->followed);

    // Not in the library: appended as its own card.
    QCOMPARE(cards.back().title, QStringLiteral("Remote Act"));
    const Track& added = cards.back();
    QCOMPARE(added.artist, QStringLiteral("Following"));
    QCOMPARE(added.sourceKind, QStringLiteral("artist"));
    QCOMPARE(added.sourceUrl, QStringLiteral("https://music.example.test/a/9"));
    QCOMPARE(added.artistCatalogId, QStringLiteral("cat-9"));
    QCOMPARE(added.sourceTag, QStringLiteral("Source-A"));
    QCOMPARE(added.sourceLabel, QStringLiteral("Remote"));
    QVERIFY(added.followed);
    QCOMPARE(added.trackCount, 0);
    QCOMPARE(added.dateAddedSortKey, 1775001600.0);

    // Without an address the card opens by name.
    QCOMPARE(findCard(groups.loadArtists(QStringLiteral("account|owner-b")), QStringLiteral("Plain Act"))->sourceUrl,
             QStringLiteral("Plain Act"));
}

QTEST_GUILESS_MAIN(FollowedArtistsTest)
#include "tst_followed_artists.moc"
