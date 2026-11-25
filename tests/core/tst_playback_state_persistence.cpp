#include <QByteArray>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QObject>
#include <QSignalSpy>
#include <QString>
#include <QTemporaryDir>
#include <QTest>

#include <memory>

#include "PlayerFixture.h"
#include "core/app/AppPaths.h"
#include "core/db/LibraryQueries.h"
#include "core/library/UserDataOperationGate.h"
#include "core/model/CatalogSourceKey.h"
#include "core/playback/PlaybackStatePersistence.h"
#include "core/playback/PlaybackStateRestore.h"
#include "core/playback/QueueTrackIdentity.h"
#include "core/settings/SettingKeys.h"

using lmp::core::PlaybackStatePersistence;
using lmp::core::PlayerController;
using lmp::core::Track;

namespace {

bool createFile(const QString& path)
{
    QFile file(path);
    return file.open(QIODevice::WriteOnly) && file.write("x", 1) == 1;
}

QJsonObject historyEntry(const Track& track, int playCount, int lastPlayedOrder, const QString& key = {})
{
    QJsonObject object;
    object.insert(QStringLiteral("title"), track.title);
    object.insert(QStringLiteral("artist"), track.artist);
    object.insert(QStringLiteral("filePath"), track.filePath);
    object.insert(QStringLiteral("sourceKind"), track.sourceKind);
    object.insert(QStringLiteral("sourceUrl"), track.sourceUrl);
    object.insert(QStringLiteral("durationSeconds"), track.durationSeconds);
    object.insert(QStringLiteral("playCount"), playCount);
    object.insert(QStringLiteral("lastPlayedOrder"), lastPlayedOrder);
    if (!key.isEmpty()) {
        object.insert(QStringLiteral("key"), key);
    }
    return object;
}

// One launch: a controller and the persistence around it, sharing the
// fixture's settings, database and fake audio with the other launches.
class Session {
public:
    Session(PlayerFixture& fixture, const lmp::core::AppPaths& paths, lmp::core::UserDataOperationGate& gate)
        : library(fixture.library.database())
        , player(fixture.makeController())
        , persistence(std::make_unique<PlaybackStatePersistence>(
              lmp::core::PlaybackStatePersistenceServices{*player, fixture.settings, fixture.tracks, library, gate,
                                                          fixture.clock},
              paths))
    {
    }

    [[nodiscard]] bool restore()
    {
        QSignalSpy finished(persistence.get(), &PlaybackStatePersistence::launchRestoreFinished);
        persistence->restoreAtLaunch();
        return finished.wait(5000);
    }

    lmp::core::db::LibraryQueries library;
    std::unique_ptr<PlayerController> player;
    std::unique_ptr<PlaybackStatePersistence> persistence;
};

} // namespace

class PlaybackStatePersistenceTest : public QObject {
    Q_OBJECT

private slots:
    void savedDownloadedCopiesComeBackAsAccountSongs();
    void init();
    void cleanup();

    void launchShowsLastTrackAndFirstPlayResumes();
    void lastTrackWithDeletedFileIsNotShown();
    void queueSnapshotRestoresTheQueue();
    void migrationWritesRowsAndSetsGuard();
    void migrationDoesNotRunTwice();
    void corruptFileStartsEmptyAndIsRewritten();
    void stateIsSavedAfterChangesSettle();
    void resumePointFollowsPlayback();
    void shutdownSavesResumePointAndState();
    void shutdownBeforeRestoreKeepsTheFile();

private:
    [[nodiscard]] QString statePath() const { return m_paths->stateFile(); }
    [[nodiscard]] QJsonObject stateJson() const;
    void writeState(const QJsonObject& object);
    [[nodiscard]] Track existingSong(const QString& name);
    void addToHistory(const Track& track, quint64 order, double resumeSeconds = 0.0);
    [[nodiscard]] double storedPosition(const Track& track);
    [[nodiscard]] qint64 storedNumber(const QString& sql);

    std::unique_ptr<PlayerFixture> m_fixture;
    std::unique_ptr<QTemporaryDir> m_stateDir;
    std::unique_ptr<lmp::core::AppPaths> m_paths;
    std::unique_ptr<lmp::core::UserDataOperationGate> m_gate;
};

void PlaybackStatePersistenceTest::init()
{
    m_fixture = std::make_unique<PlayerFixture>();
    QVERIFY(m_fixture->ok());
    m_stateDir = std::make_unique<QTemporaryDir>();
    QVERIFY(m_stateDir->isValid());
    m_paths = std::make_unique<lmp::core::AppPaths>(m_stateDir->path());
    m_gate = std::make_unique<lmp::core::UserDataOperationGate>();
}

void PlaybackStatePersistenceTest::cleanup()
{
    m_gate.reset();
    m_paths.reset();
    m_stateDir.reset();
    m_fixture.reset();
}

QJsonObject PlaybackStatePersistenceTest::stateJson() const
{
    QFile file(statePath());
    if (!file.open(QIODevice::ReadOnly)) {
        return {};
    }
    return QJsonDocument::fromJson(file.readAll()).object();
}

void PlaybackStatePersistenceTest::writeState(const QJsonObject& object)
{
    QFile file(statePath());
    QVERIFY(file.open(QIODevice::WriteOnly));
    const QByteArray bytes = QJsonDocument(object).toJson();
    QCOMPARE(file.write(bytes), bytes.size());
}

Track PlaybackStatePersistenceTest::existingSong(const QString& name)
{
    const Track track = m_fixture->song(name);
    if (!createFile(track.filePath)) {
        qWarning("could not create %s", qPrintable(track.filePath));
    }
    return track;
}

void PlaybackStatePersistenceTest::addToHistory(const Track& track, quint64 order, double resumeSeconds)
{
    const QString key = lmp::core::catalogSourceKey(track);
    QVERIFY(m_fixture->tracks.upsertLocalTrack(track, key).isOk());
    QVERIFY(m_fixture->tracks.recordPlayback(key, order).isOk());
    QVERIFY(m_fixture->tracks.recordPlaybackPosition(key, resumeSeconds).isOk());
}

double PlaybackStatePersistenceTest::storedPosition(const Track& track)
{
    auto statement =
        m_fixture->library.connection().prepare("SELECT LastPositionSeconds FROM Tracks WHERE SourceKey=?;");
    if (!statement) {
        return -1.0;
    }
    statement.value().bindText(1, lmp::core::catalogSourceKey(track));
    return statement.value().step().valueOr(false) ? statement.value().columnDouble(0) : -1.0;
}

qint64 PlaybackStatePersistenceTest::storedNumber(const QString& sql)
{
    return m_fixture->library.number(sql.toUtf8().constData());
}

void PlaybackStatePersistenceTest::launchShowsLastTrackAndFirstPlayResumes()
{
    const Track one = existingSong(QStringLiteral("One"));
    const Track two = existingSong(QStringLiteral("Two"));
    addToHistory(two, 1);
    addToHistory(one, 2, 95.0);
    QJsonObject state;
    state.insert(QStringLiteral("LastTrack"), one.filePath);
    state.insert(QStringLiteral("Volume"), 0.7);
    writeState(state);

    Session session(*m_fixture, *m_paths, *m_gate);
    QVERIFY(session.restore());
    PlayerController& player = *session.player;
    QVERIFY(player.currentTrack().has_value());
    QCOMPARE(player.currentTrack()->title, QStringLiteral("One"));
    QCOMPARE(player.state(), PlayerController::State::Idle);
    QCOMPARE(player.positionSeconds(), 95.0);
    QCOMPARE(player.durationSeconds(), 200.0);
    QVERIFY(m_fixture->audio.opens.isEmpty()); // shown, not opened
    QCOMPARE(m_fixture->resolver.calls, 0);
    QCOMPARE(storedPosition(one), 95.0); // showing it wrote nothing

    player.togglePlay();
    settle();
    QCOMPARE(m_fixture->audio.opens.size(), qsizetype{1});
    QCOMPARE(lastOpenedTitle(m_fixture->audio), QStringLiteral("One"));
    QCOMPARE(m_fixture->audio.opens.last().startAtSeconds, 95.0);
    QVERIFY(m_fixture->audio.opens.last().autoplay);
    QCOMPARE(storedPosition(one), 95.0);
}

void PlaybackStatePersistenceTest::lastTrackWithDeletedFileIsNotShown()
{
    const Track gone = m_fixture->song(QStringLiteral("Gone")); // no file on disk
    addToHistory(gone, 1, 95.0);
    QJsonObject state;
    state.insert(QStringLiteral("LastTrack"), gone.filePath);
    writeState(state);

    Session session(*m_fixture, *m_paths, *m_gate);
    QVERIFY(session.restore());
    QVERIFY(!session.player->currentTrack().has_value());
    session.persistence->shutdown();
    QCOMPARE(stateJson().value(QStringLiteral("LastTrack")).toString(), QString());
}

void PlaybackStatePersistenceTest::queueSnapshotRestoresTheQueue()
{
    const QList<Track> songs{existingSong(QStringLiteral("A")), existingSong(QStringLiteral("B")),
                             existingSong(QStringLiteral("C")), existingSong(QStringLiteral("D"))};
    const Track extra = existingSong(QStringLiteral("E"));
    {
        Session first(*m_fixture, *m_paths, *m_gate);
        QVERIFY(first.restore());
        first.player->playInContext(songs.at(1), songs);
        first.player->addToQueue({extra});
        settle();
        first.persistence->shutdown();
    }
    const QJsonObject saved = stateJson();
    QCOMPARE(saved.value(QStringLiteral("LastTrack")).toString(), songs.at(1).filePath);
    QVERIFY(saved.value(QStringLiteral("QueueSnapshot")).isObject());

    // B never reached the history (under 8 s played): the saved queue's
    // current song stands in for it.
    m_fixture->audio.opens.clear();
    Session second(*m_fixture, *m_paths, *m_gate);
    QVERIFY(second.restore());
    PlayerController& player = *second.player;
    QVERIFY(player.currentTrack().has_value());
    QCOMPARE(player.currentTrack()->title, QStringLiteral("B"));
    QCOMPARE(player.positionSeconds(), 0.0);
    QCOMPARE(visibleTitles(player), (QStringList{QStringLiteral("E"), QStringLiteral("C"), QStringLiteral("D")}));
    QCOMPARE(player.queue().state().contextQueue.size(), qsizetype{4});
    QCOMPARE(player.queue().state().contextIndex, qsizetype{1});
    QVERIFY(m_fixture->audio.opens.isEmpty());

    player.next();
    settle();
    QCOMPARE(lastOpenedTitle(m_fixture->audio), QStringLiteral("E"));
}

void PlaybackStatePersistenceTest::migrationWritesRowsAndSetsGuard()
{
    const Track old = existingSong(QStringLiteral("Old"));
    addToHistory(old, 3);
    const Track kept = existingSong(QStringLiteral("Kept"));
    const Track gone = m_fixture->song(QStringLiteral("Gone"));
    Track remote;
    remote.title = QStringLiteral("Far");
    remote.artist = QStringLiteral("Band");
    remote.sourceKind = QStringLiteral("remote");
    remote.sourceUrl = QStringLiteral("https://example.com/far");

    QJsonArray history;
    history.append(historyEntry(kept, 4, 9));
    history.append(historyEntry(gone, 6, 8));   // file gone: skipped
    history.append(historyEntry(kept, 40, 90)); // same key again: skipped
    history.append(historyEntry(old, 50, 99));  // already in the history: skipped
    history.append(historyEntry(remote, 2, 5));
    QJsonObject state;
    state.insert(QStringLiteral("LastTrack"), kept.filePath);
    state.insert(QStringLiteral("Volume"), 0.3);
    state.insert(QStringLiteral("HomePlaySequence"), 9);
    state.insert(QStringLiteral("HomeHistory"), history);
    writeState(state);

    Session session(*m_fixture, *m_paths, *m_gate);
    QVERIFY(session.restore());
    QVERIFY(m_fixture->settings.boolean(lmp::core::settings::keys::LegacyHomeHistoryMigrated));

    const QString keptKey = lmp::core::catalogSourceKey(kept);
    QCOMPARE(storedNumber(QStringLiteral("SELECT PlayCount FROM Tracks WHERE SourceKey='%1';").arg(keptKey)), 4);
    QCOMPARE(storedNumber(QStringLiteral("SELECT LastPlayedOrder FROM Tracks WHERE SourceKey='%1';").arg(keptKey)),
             9);
    QCOMPARE(storedNumber(QStringLiteral("SELECT PlayCount FROM Tracks WHERE SourceKey='%1';")
                              .arg(lmp::core::catalogSourceKey(old))),
             1);
    QCOMPARE(storedNumber(QStringLiteral("SELECT PlayCount FROM Tracks WHERE SourceKey='%1';")
                              .arg(lmp::core::catalogSourceKey(remote))),
             2);
    const QByteArray remoteKindSql = QStringLiteral("SELECT SourceKind FROM Tracks WHERE SourceKey='%1';")
                                         .arg(lmp::core::catalogSourceKey(remote))
                                         .toUtf8();
    QCOMPARE(m_fixture->library.text(remoteKindSql.constData()), QStringLiteral("remote"));
    QCOMPARE(storedNumber(QStringLiteral("SELECT COUNT(*) FROM Tracks;")), 3);

    // The last song came from the moved history.
    QVERIFY(session.player->currentTrack().has_value());
    QCOMPARE(session.player->currentTrack()->title, QStringLiteral("Kept"));

    // Rewritten at once without the old keys.
    session.persistence->shutdown();
    const QJsonObject rewritten = stateJson();
    QVERIFY(!rewritten.contains(QStringLiteral("HomeHistory")));
    QVERIFY(!rewritten.contains(QStringLiteral("HomePlaySequence")));
    QCOMPARE(rewritten.value(QStringLiteral("LastTrack")).toString(), kept.filePath);
}

void PlaybackStatePersistenceTest::migrationDoesNotRunTwice()
{
    const Track kept = existingSong(QStringLiteral("Kept"));
    QJsonObject state;
    state.insert(QStringLiteral("HomeHistory"), QJsonArray{historyEntry(kept, 4, 9)});
    writeState(state);
    {
        Session first(*m_fixture, *m_paths, *m_gate);
        QVERIFY(first.restore());
        first.persistence->shutdown();
    }
    const QString keptKey = lmp::core::catalogSourceKey(kept);
    QCOMPARE(storedNumber(QStringLiteral("SELECT PlayCount FROM Tracks WHERE SourceKey='%1';").arg(keptKey)), 4);

    // An old version wrote its history back; it is not merged again.
    state.insert(QStringLiteral("HomeHistory"), QJsonArray{historyEntry(kept, 40, 90)});
    writeState(state);
    Session second(*m_fixture, *m_paths, *m_gate);
    QVERIFY(second.restore());
    QCOMPARE(storedNumber(QStringLiteral("SELECT PlayCount FROM Tracks WHERE SourceKey='%1';").arg(keptKey)), 4);
    QCOMPARE(storedNumber(QStringLiteral("SELECT LastPlayedOrder FROM Tracks WHERE SourceKey='%1';").arg(keptKey)),
             9);
}

void PlaybackStatePersistenceTest::corruptFileStartsEmptyAndIsRewritten()
{
    QFile file(statePath());
    QVERIFY(file.open(QIODevice::WriteOnly));
    QVERIFY(file.write("{\"LastTrack\": ") > 0);
    file.close();

    Session session(*m_fixture, *m_paths, *m_gate);
    QVERIFY(session.restore());
    QVERIFY(!session.player->currentTrack().has_value());
    QVERIFY(!m_fixture->settings.contains(lmp::core::settings::keys::LegacyHomeHistoryMigrated));
    session.persistence->shutdown();
    const QJsonObject rewritten = stateJson();
    QCOMPARE(rewritten.value(QStringLiteral("LastTrack")).toString(QStringLiteral("missing")), QString());
    QCOMPARE(rewritten.value(QStringLiteral("Volume")).toDouble(), 0.7);
}

void PlaybackStatePersistenceTest::stateIsSavedAfterChangesSettle()
{
    const Track one = existingSong(QStringLiteral("One"));
    Session session(*m_fixture, *m_paths, *m_gate);
    QVERIFY(session.restore());
    QTRY_VERIFY(stateJson().contains(QStringLiteral("LastTrack")));
    QCOMPARE(stateJson().value(QStringLiteral("LastTrack")).toString(), QString());

    session.player->playTrack(one);
    settle();
    m_fixture->clock.advance(PlaybackStatePersistence::SaveDelay.count() - 1);
    QTest::qWait(50);
    QCOMPARE(stateJson().value(QStringLiteral("LastTrack")).toString(), QString());
    m_fixture->clock.advance(1);
    QTRY_COMPARE(stateJson().value(QStringLiteral("LastTrack")).toString(), one.filePath);
    QVERIFY(stateJson().value(QStringLiteral("QueueSnapshot")).isObject());
}

void PlaybackStatePersistenceTest::resumePointFollowsPlayback()
{
    const Track one = existingSong(QStringLiteral("One"));
    const Track two = existingSong(QStringLiteral("Two"));
    addToHistory(one, 1);
    addToHistory(two, 2);
    Session session(*m_fixture, *m_paths, *m_gate);
    QVERIFY(session.restore());
    PlayerController& player = *session.player;
    FakeAudioPlayer& audio = m_fixture->audio;

    player.playInContext(one, {one, two});
    settle();
    audio.start(200.0);
    audio.position(100.0);
    QCOMPARE(storedPosition(one), 100.0);
    audio.position(105.0); // under 10 s on
    QCOMPARE(storedPosition(one), 100.0);

    player.togglePlay(); // pause: the exact point goes in
    audio.paused();
    QCOMPARE(player.state(), PlayerController::State::Paused);
    QCOMPARE(storedPosition(one), 105.0);

    player.togglePlay();
    audio.playing();
    audio.position(150.0);
    QCOMPARE(storedPosition(one), 150.0);
    audio.position(155.0);
    player.next(); // left before its end: saved exactly
    settle();
    QCOMPARE(storedPosition(one), 155.0);

    audio.start(200.0);
    audio.position(120.0);
    QCOMPARE(storedPosition(two), 120.0);
    audio.end(); // played to its end: cleared
    QCOMPARE(storedPosition(two), 0.0);
    QCOMPARE(storedPosition(one), 155.0);
}

void PlaybackStatePersistenceTest::shutdownSavesResumePointAndState()
{
    const Track one = existingSong(QStringLiteral("One"));
    addToHistory(one, 1);
    Session session(*m_fixture, *m_paths, *m_gate);
    QVERIFY(session.restore());
    session.player->playTrack(one);
    settle();
    m_fixture->audio.start(200.0);
    m_fixture->audio.position(100.0);
    m_fixture->audio.position(104.0);
    QCOMPARE(storedPosition(one), 100.0);

    session.persistence->shutdown();
    QCOMPARE(storedPosition(one), 104.0);
    QCOMPARE(stateJson().value(QStringLiteral("LastTrack")).toString(), one.filePath);

    // Nothing is saved after shutdown.
    m_fixture->audio.position(150.0);
    QCOMPARE(storedPosition(one), 104.0);
}

void PlaybackStatePersistenceTest::shutdownBeforeRestoreKeepsTheFile()
{
    QJsonObject state;
    state.insert(QStringLiteral("LastTrack"), QStringLiteral("C:\\kept.mp3"));
    writeState(state);
    Session session(*m_fixture, *m_paths, *m_gate);
    session.persistence->shutdown();
    QCOMPARE(stateJson().value(QStringLiteral("LastTrack")).toString(), QStringLiteral("C:\\kept.mp3"));
}

void PlaybackStatePersistenceTest::savedDownloadedCopiesComeBackAsAccountSongs()
{
    // An account song as the library has it...
    Track song;
    song.title = QStringLiteral("Song");
    song.sourceKind = QStringLiteral("remote");
    song.origin = QStringLiteral("account");
    song.remoteId = QStringLiteral("r1");
    song.sourceUrl = QStringLiteral("https://music.example.test/t/r1");
    // ...and its downloaded copy, saved in a queue by an earlier version.
    Track copy;
    copy.title = song.title;
    copy.sourceKind = QStringLiteral("local");
    copy.filePath = QStringLiteral("C:/Music/Offline/Song-12345678.m4a");
    copy.sourceUrl = song.sourceUrl;
    // A file of this PC that only has a source address stays a file.
    Track other = copy;
    other.sourceUrl = QStringLiteral("https://music.example.test/t/unknown");
    other.filePath = QStringLiteral("C:/Music/Other.mp3");

    lmp::core::PlaybackState state;
    lmp::core::SavedQueue queue;
    queue.queue.contextQueue = {copy, other};
    queue.queue.contextIndex = 0;
    queue.currentTrack = copy;
    state.queue = queue;
    Track current = copy;
    QVERIFY(lmp::core::holdsDownloadedCopies(state, current));
    lmp::core::restoreAccountSongs(state, current, {song});

    QCOMPARE(lmp::core::queueTrackIdentity(current), QStringLiteral("remote|r1"));
    QCOMPARE(current.filePath, copy.filePath); // still plays from its file
    QCOMPARE(lmp::core::queueTrackIdentity(state.queue->currentTrack), QStringLiteral("remote|r1"));
    QCOMPARE(lmp::core::queueTrackIdentity(state.queue->queue.contextQueue.at(0)), QStringLiteral("remote|r1"));
    QCOMPARE(state.queue->queue.contextQueue.at(1).sourceKind, QStringLiteral("local"));
}

QTEST_GUILESS_MAIN(PlaybackStatePersistenceTest)
#include "tst_playback_state_persistence.moc"
