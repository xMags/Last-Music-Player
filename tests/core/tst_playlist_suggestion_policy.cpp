#include <QList>
#include <QObject>
#include <QString>
#include <QTest>

#include "core/library/PlaylistSuggestionPolicy.h"

using lmp::core::rankPlaylistSuggestions;
using lmp::core::SuggestionCandidate;
using lmp::core::SuggestionContext;

class PlaylistSuggestionPolicyTest : public QObject {
    Q_OBJECT

private slots:
    void playlistSuggestionPolicy();
    void emptyFieldsNeverMatchAndBothSignalsAdd();
};

void PlaylistSuggestionPolicyTest::playlistSuggestionPolicy()
{
    const QList<SuggestionCandidate> candidates{
        {QStringLiteral("existing"), QStringLiteral("artist-a"), QStringLiteral("genre-a"), 100},
        {QStringLiteral("artist"), QStringLiteral("artist-a"), QStringLiteral("genre-z"), 1},
        {QStringLiteral("genre-high"), QStringLiteral("artist-z"), QStringLiteral("genre-a"), 20},
        {QStringLiteral("genre-low"), QStringLiteral("artist-y"), QStringLiteral("genre-a"), 2},
        {QStringLiteral("unmatched"), QStringLiteral("artist-x"), QStringLiteral("genre-x"), 1000},
    };
    const SuggestionContext context{
        {QStringLiteral("existing")},
        {QStringLiteral("artist-a")},
        {QStringLiteral("genre-a")},
    };

    const QList<qsizetype> ranked = rankPlaylistSuggestions(candidates, context, 3);
    QCOMPARE(ranked.size(), qsizetype{3});
    QCOMPARE(ranked[0], qsizetype{1});
    QVERIFY(ranked[1] == 2 && ranked[2] == 3);
    QCOMPARE(rankPlaylistSuggestions(candidates, context, 1).size(), qsizetype{1});
    QVERIFY(rankPlaylistSuggestions(candidates, {}, 5).isEmpty());
}

void PlaylistSuggestionPolicyTest::emptyFieldsNeverMatchAndBothSignalsAdd()
{
    const QList<SuggestionCandidate> candidates{
        {QStringLiteral("blank"), QString(), QString(), 500},
        {QStringLiteral("genre"), QStringLiteral("artist-z"), QStringLiteral("genre-a"), 50},
        {QString(), QStringLiteral("artist-a"), QStringLiteral("genre-a"), 0},
        {QStringLiteral("tie-later"), QStringLiteral("artist-z"), QStringLiteral("genre-a"), 50},
    };
    const SuggestionContext context{
        {QString()},
        {QStringLiteral("artist-a"), QString(), QStringLiteral("artist-a")},
        {QStringLiteral("genre-a"), QString()},
    };

    // An empty key is never treated as a member; artist plus genre scores 3.
    QCOMPARE(rankPlaylistSuggestions(candidates, context, 10), (QList<qsizetype>{2, 1, 3}));
    QVERIFY(rankPlaylistSuggestions(candidates, context, 0).isEmpty());
    QVERIFY(rankPlaylistSuggestions(candidates, context, -1).isEmpty());
    QVERIFY(rankPlaylistSuggestions({}, context, 5).isEmpty());
}

QTEST_GUILESS_MAIN(PlaylistSuggestionPolicyTest)
#include "tst_playlist_suggestion_policy.moc"
