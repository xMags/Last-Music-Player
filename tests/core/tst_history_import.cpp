#include "core/account/HistoryImport.h"
#include "core/account/HistoryImportService.h"
#include "core/library/UserDataOperationGate.h"

#include <QJsonDocument>
#include <QJsonObject>
#include <QSignalSpy>
#include <QTest>

#include <map>

namespace account = lmp::core::account;
namespace db = lmp::core::db;
using lmp::core::Failure;
using lmp::core::FailureKind;
using lmp::core::Result;
using lmp::core::Track;

namespace {

Track remoteSong(const QString& title, const QString& url)
{
    Track track;
    track.title = title;
    track.artist = QStringLiteral("Artist");
    track.sourceKind = QStringLiteral("remote");
    track.sourceUrl = url;
    return track;
}

db::HistoryImportCandidate candidate(const Track& track, const QString& key, const QString& playedAt)
{
    return db::HistoryImportCandidate{track, key, playedAt};
}

QByteArray resolveAnswer(const QString& id, const QString& url, const QString& title)
{
    return QJsonDocument(QJsonObject{{QStringLiteral("result"),
                                      QJsonObject{{QStringLiteral("track"),
                                                   QJsonObject{{QStringLiteral("id"), id},
                                                               {QStringLiteral("sourceUrl"), url},
                                                               {QStringLiteral("title"), title},
                                                               {QStringLiteral("durationMs"), 185000}}}}}})
        .toJson(QJsonDocument::Compact);
}

// Everything the service works with, in memory.
struct Fixture {
    Fixture()
    {
        service = std::make_unique<account::HistoryImportService>(account::HistoryImportService::Dependencies{
            .gate = gate,
            .accountId = [this]() { return owner; },
            .loadState = [this](const QString& id) {
                db::AccountSyncState state = states[id];
                state.accountId = id;
                return state;
            },
            .saveState = [this](const db::AccountSyncState& state) {
                if (failSave) {
                    return false;
                }
                states[state.accountId] = state;
                return true;
            },
            .candidates = [this]() { return candidates; },
            .resolve = [this](const QString& url) {
                resolvedUrls.append(url);
                const auto found = answers.find(url);
                const Result<QByteArray> answer = found == answers.end()
                    ? Result<QByteArray>(Failure::of(FailureKind::NotFound, QStringLiteral("unknown"), 404))
                    : found->second;
                return QtFuture::makeReadyValueFuture(answer);
            },
            .enqueue = [this](const db::PlaybackEventRecord& event) {
                events.append(event);
                return true;
            },
            .sendHistory = [this]() { ++historySends; },
        });
    }

    [[nodiscard]] QString state() const
    {
        const auto found = states.find(owner);
        return found == states.end() ? QString() : found->second.legacyHistoryImportState;
    }

    lmp::core::UserDataOperationGate gate;
    QString owner = QStringLiteral("owner-a");
    std::map<QString, db::AccountSyncState> states;
    QList<db::HistoryImportCandidate> candidates;
    std::map<QString, Result<QByteArray>> answers;
    QStringList resolvedUrls;
    QList<db::PlaybackEventRecord> events;
    int historySends = 0;
    bool failSave = false;
    std::unique_ptr<account::HistoryImportService> service;
};

} // namespace

class HistoryImportTest : public QObject {
    Q_OBJECT

private slots:
    // The earlier versions' event id cases.
    void eventIdsAreStableOwnerScopedUuids()
    {
        const QString first = account::historyImportEventId(QStringLiteral("owner-a"), QStringLiteral("remote|source-a"));
        const QString repeated = account::historyImportEventId(QStringLiteral("owner-a"), QStringLiteral("remote|source-a"));
        const QString another = account::historyImportEventId(QStringLiteral("owner-b"), QStringLiteral("remote|source-a"));
        QCOMPARE(first.size(), 36);
        QCOMPARE(first, repeated);
        QVERIFY(first != another);
        QVERIFY(!first.contains(QStringLiteral("source-a")));
        QCOMPARE(first.at(14), QLatin1Char('5'));
        QVERIFY(QStringLiteral("89ab").contains(first.at(19)));
        QVERIFY(account::historyImportEventId(QString(), QStringLiteral("k")).isEmpty());
    }

    void playedAtIsNormalised()
    {
        QCOMPARE(account::normalizeLegacyPlayedAt(QStringLiteral("2024-03-05 07:08:09")),
                 QStringLiteral("2024-03-05T07:08:09Z"));
        QCOMPARE(account::normalizeLegacyPlayedAt(QStringLiteral("2024-03-05T07:08:09Z")),
                 QStringLiteral("2024-03-05T07:08:09Z"));
        QVERIFY(account::normalizeLegacyPlayedAt(QStringLiteral("yesterday")).isEmpty());
        QVERIFY(account::normalizeLegacyPlayedAt(QStringLiteral("2024-03-05")).isEmpty());
    }

    void resolvedTrackComesFromTheAnswer()
    {
        const Track legacy = remoteSong(QStringLiteral("Old title"), QStringLiteral("https://example.invalid/a"));
        const std::optional<Track> track = account::resolvedImportTrack(
            resolveAnswer(QStringLiteral("t1"), QStringLiteral("https://example.invalid/a"), QStringLiteral("New")), legacy);
        QVERIFY(track);
        QCOMPARE(track->remoteId, QStringLiteral("t1"));
        QCOMPARE(track->title, QStringLiteral("New"));
        QCOMPARE(track->artist, QStringLiteral("Artist"));
        QCOMPARE(track->durationSeconds, 185.0);
        QCOMPARE(track->durationText, QStringLiteral("3:05"));
        QCOMPARE(track->origin, QStringLiteral("account"));
        QVERIFY(!account::resolvedImportTrack(QByteArrayLiteral("{\"result\":{}}"), legacy));
        QVERIFY(!account::resolvedImportTrack(QByteArrayLiteral("not json"), legacy));
    }

    void nothingToImportAsksNothing()
    {
        Fixture fixture;
        fixture.service->offerIfDue();
        QVERIFY(!fixture.service->offering());
        QVERIFY(fixture.state().isEmpty());
    }

    void declineIsRememberedAndNotAskedAgain()
    {
        Fixture fixture;
        fixture.candidates = {candidate(remoteSong(QStringLiteral("a"), QStringLiteral("https://example.invalid/a")),
                                        QStringLiteral("k-a"), QStringLiteral("2024-01-01 10:00:00"))};
        QSignalSpy offer(fixture.service.get(), &account::HistoryImportService::offerChanged);
        fixture.service->offerIfDue();
        QVERIFY(fixture.service->offering());
        fixture.service->decline();
        QVERIFY(!fixture.service->offering());
        QCOMPARE(fixture.state(), QStringLiteral("declined"));
        fixture.service->offerIfDue();
        QVERIFY(!fixture.service->offering());
        QCOMPARE(offer.count(), 2);
        QVERIFY(fixture.events.isEmpty());
    }

    void acceptImportsResolvableSongsAndSkipsTheRest()
    {
        Fixture fixture;
        Track local;
        local.title = QStringLiteral("Local");
        local.sourceKind = QStringLiteral("local");
        local.filePath = QStringLiteral("C:/music/local.mp3");
        fixture.candidates = {
            candidate(remoteSong(QStringLiteral("a"), QStringLiteral("https://example.invalid/a")), QStringLiteral("k-a"),
                      QStringLiteral("2024-01-01 10:00:00")),
            candidate(local, QStringLiteral("k-local"), QStringLiteral("2024-01-01 10:00:00")),
            candidate(remoteSong(QStringLiteral("gone"), QStringLiteral("https://example.invalid/gone")),
                      QStringLiteral("k-gone"), QStringLiteral("2024-01-02 10:00:00")),
            candidate(remoteSong(QStringLiteral("baddate"), QStringLiteral("https://example.invalid/b")),
                      QStringLiteral("k-b"), QStringLiteral("someday")),
        };
        fixture.answers.emplace(QStringLiteral("https://example.invalid/a"),
                                resolveAnswer(QStringLiteral("t-a"), QStringLiteral("https://example.invalid/a"),
                                              QStringLiteral("a")));
        QSignalSpy finished(fixture.service.get(), &account::HistoryImportService::finished);
        fixture.service->offerIfDue();
        fixture.service->accept();
        QCOMPARE(finished.count(), 1);
        QCOMPARE(finished.first().at(0).toInt(), 1);
        QCOMPARE(finished.first().at(1).toInt(), 3);
        QCOMPARE(fixture.events.size(), 1);
        const db::PlaybackEventRecord& event = fixture.events.first();
        QCOMPARE(event.accountId, QStringLiteral("owner-a"));
        QCOMPARE(event.remoteTrackId, QStringLiteral("t-a"));
        QCOMPARE(event.playedAtUtc, QStringLiteral("2024-01-01T10:00:00Z"));
        QCOMPARE(event.eventId, account::historyImportEventId(QStringLiteral("owner-a"), QStringLiteral("k-a")));
        // The local file was never sent to the service.
        QVERIFY(!fixture.resolvedUrls.contains(QString()));
        QCOMPARE(fixture.resolvedUrls.size(), 2);
        QCOMPARE(fixture.state(), QStringLiteral("completed"));
        QCOMPARE(fixture.historySends, 1);
        QVERIFY(!fixture.service->importing());
    }

    void aNetworkFailurePausesAndTheNextSyncResumes()
    {
        Fixture fixture;
        fixture.candidates = {candidate(remoteSong(QStringLiteral("a"), QStringLiteral("https://example.invalid/a")),
                                        QStringLiteral("k-a"), QStringLiteral("2024-01-01 10:00:00"))};
        fixture.answers.emplace(QStringLiteral("https://example.invalid/a"),
                                Result<QByteArray>(Failure::of(FailureKind::Network, QStringLiteral("offline"))));
        QSignalSpy paused(fixture.service.get(), &account::HistoryImportService::paused);
        fixture.service->offerIfDue();
        fixture.service->accept();
        QCOMPARE(paused.count(), 1);
        QCOMPARE(fixture.state(), QStringLiteral("accepted"));
        QVERIFY(fixture.events.isEmpty());

        // The next sync carries on without asking.
        fixture.answers.erase(QStringLiteral("https://example.invalid/a"));
        fixture.answers.emplace(QStringLiteral("https://example.invalid/a"),
                                resolveAnswer(QStringLiteral("t-a"), QStringLiteral("https://example.invalid/a"),
                                              QStringLiteral("a")));
        fixture.service->offerIfDue();
        QVERIFY(!fixture.service->offering());
        QCOMPARE(fixture.events.size(), 1);
        QCOMPARE(fixture.state(), QStringLiteral("completed"));
    }

    void anotherOwnerStopsTheImport()
    {
        Fixture fixture;
        fixture.candidates = {candidate(remoteSong(QStringLiteral("a"), QStringLiteral("https://example.invalid/a")),
                                        QStringLiteral("k-a"), QStringLiteral("2024-01-01 10:00:00"))};
        fixture.service->offerIfDue();
        QVERIFY(fixture.service->offering());
        // Signed out (or another account) before answering.
        fixture.owner = QStringLiteral("owner-b");
        fixture.service->accept();
        QVERIFY(fixture.states.find(QStringLiteral("owner-a")) == fixture.states.end()
                || fixture.states[QStringLiteral("owner-a")].legacyHistoryImportState.isEmpty());
        QVERIFY(fixture.events.isEmpty());
    }

    void aClosedGateStopsTheImport()
    {
        Fixture fixture;
        fixture.candidates = {candidate(remoteSong(QStringLiteral("a"), QStringLiteral("https://example.invalid/a")),
                                        QStringLiteral("k-a"), QStringLiteral("2024-01-01 10:00:00"))};
        fixture.states[QStringLiteral("owner-a")].legacyHistoryImportState = QStringLiteral("accepted");
        // "Delete all data" started: admissions closed, a new generation.
        QVERIFY(fixture.gate.closeAdmissions());
        fixture.service->offerIfDue();
        QVERIFY(fixture.events.isEmpty());
        fixture.gate.reopen();
    }
};

QTEST_GUILESS_MAIN(HistoryImportTest)
#include "tst_history_import.moc"
