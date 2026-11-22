#include <QObject>
#include <QString>
#include <QTest>

#include "core/model/ArtistIdentity.h"

using lmp::core::ArtistIdentity;
using lmp::core::artistIdentityKey;
using lmp::core::normalizeArtistName;
using lmp::core::sameArtistIdentity;

class ArtistIdentityTest : public QObject {
    Q_OBJECT

private slots:
    void keysPreferTheStrongestIdentity();
    void namesNormalise();
    void sameIdentityFollowsTheStrongestSide();
};

void ArtistIdentityTest::keysPreferTheStrongestIdentity()
{
    const ArtistIdentity catalogArtist{QStringLiteral("CAT-one"), QStringLiteral("https://music.example/artist/one"),
                                       QStringLiteral("  Artist  One "), QStringLiteral("Source-Music")};
    QCOMPARE(artistIdentityKey(catalogArtist), QStringLiteral("catalog|source-music|CAT-one"));
    QCOMPARE(artistIdentityKey({{}, QStringLiteral("https://music.example/artist/one"), QStringLiteral("A"), {}}),
             QStringLiteral("url|https://music.example/artist/one"));
    QCOMPARE(artistIdentityKey({{}, {}, QStringLiteral(" Artist\tONE "), {}}), QStringLiteral("name|artist one"));
    QCOMPARE(artistIdentityKey({{}, {}, QStringLiteral("  "), {}}), QString());
}

void ArtistIdentityTest::namesNormalise()
{
    QCOMPARE(normalizeArtistName(QStringLiteral("  ARTIST\t One  ")), QStringLiteral("artist one"));
    QCOMPARE(normalizeArtistName(QStringLiteral("a\n\nb")), QStringLiteral("a b"));
    QCOMPARE(normalizeArtistName(QString()), QString());
}

void ArtistIdentityTest::sameIdentityFollowsTheStrongestSide()
{
    const ArtistIdentity catalogArtist{QStringLiteral("CAT-one"), QStringLiteral("https://music.example/artist/one"),
                                       QStringLiteral("  Artist  One "), QStringLiteral("Source-Music")};
    QVERIFY(sameArtistIdentity(catalogArtist,
                               {QStringLiteral("CAT-one"), QStringLiteral("https://other.example"),
                                QStringLiteral("Renamed"), QStringLiteral("source-music")}));
    QVERIFY(sameArtistIdentity(catalogArtist, {QStringLiteral("CAT-one"), {}, QStringLiteral("Renamed"), {}}));
    QVERIFY(!sameArtistIdentity(catalogArtist,
                                {QStringLiteral("CAT-two"), {}, QStringLiteral("Artist One"), QStringLiteral("source-music")}));
    QVERIFY(!sameArtistIdentity(catalogArtist,
                                {QStringLiteral("CAT-one"), {}, QStringLiteral("Artist One"), QStringLiteral("other")}));
    // A catalogue id on one side only never falls back to the name.
    QVERIFY(!sameArtistIdentity(catalogArtist, {{}, {}, QStringLiteral("Artist One"), {}}));
    QVERIFY(sameArtistIdentity({{}, QStringLiteral("https://music.example/artist/one"), QStringLiteral("Old name"), {}},
                               {{}, QStringLiteral("https://music.example/artist/one"), QStringLiteral("New name"), {}}));
    QVERIFY(!sameArtistIdentity({{}, QStringLiteral("https://music.example/artist/one"), QStringLiteral("Same"), {}},
                                {{}, {}, QStringLiteral("Same"), {}}));
    QVERIFY(sameArtistIdentity({{}, {}, QStringLiteral(" Artist One "), {}}, {{}, {}, QStringLiteral("artist   one"), {}}));
    QVERIFY(!sameArtistIdentity({{}, {}, QStringLiteral(""), {}}, {{}, {}, QStringLiteral(""), {}}));
}

QTEST_GUILESS_MAIN(ArtistIdentityTest)
#include "tst_artist_identity.moc"
