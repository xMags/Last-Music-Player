#include <QByteArray>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QObject>
#include <QTemporaryDir>
#include <QTest>

#include <functional>
#include <vector>

#include "core/library/UserDataOperationGate.h"
#include "core/playback/LastTrackIdentity.h"
#include "core/playback/PlaybackStateFile.h"
#include "core/playback/PlaybackStateSnapshot.h"

using lmp::core::LegacyHistoryEntry;
using lmp::core::PlaybackState;
using lmp::core::PlaybackStateFile;
using lmp::core::SavedQueue;
using lmp::core::Track;
using lmp::core::UserDataOperationGate;

namespace {

Track localSong(const QString& path, const QString& title)
{
    Track track;
    track.title = title;
    track.artist = QStringLiteral("Artist");
    track.sourceKind = QStringLiteral("local");
    track.filePath = path;
    track.durationSeconds = 200.0;
    return track;
}

Track remoteSong(const QString& remoteId, const QString& sourceUrl)
{
    Track track;
    track.title = QStringLiteral("Far Away");
    track.artist = QStringLiteral("Band");
    track.sourceKind = QStringLiteral("remote");
    track.origin = QStringLiteral("Account");
    track.remoteId = remoteId;
    track.sourceUrl = sourceUrl;
    return track;
}

QByteArray readBytes(const QString& path)
{
    QFile file(path);
    return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
}

bool writeBytes(const QString& path, const QByteArray& bytes)
{
    QFile file(path);
    return file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size();
}

// A hand-written file in the shape the oldest versions left behind, with
// every key they read.
QByteArray legacyFile()
{
    return QByteArrayLiteral("{\n"
        "  \"LastTrack\": \"C:\\\\Music\\\\One.mp3\",\n"
        "  \"Volume\": 0.45,\n"
        "  \"HomePlaySequence\": 12,\n"
        "  \"HomeHistory\": [\n"
        "    {\"title\": \"One\", \"artist\": \"First\", \"album\": \"Debut\", \"genre\": \"Rock\",\n"
        "     \"filePath\": \"C:\\\\Music\\\\One.mp3\", \"artworkUrl\": \"thumbs\\\\one.jpg\",\n"
        "     \"dateAdded\": \"2024-01-02\", \"duration\": \"3:20\", \"sourceKind\": \"local\",\n"
        "     \"provider\": \"Local\", \"sourceUrl\": \"\", \"sourceLabel\": \"On this PC\",\n"
        "     \"key\": \"custom-key\", \"durationSeconds\": 200, \"dateAddedSortKey\": 20240102,\n"
        "     \"playCount\": 7, \"lastPlayedOrder\": 11, \"isLiked\": true},\n"
        "    \"not an object\",\n"
        "    {\"title\": \"Two\", \"filePath\": \"https://example.invalid/two.mp3\", \"playCount\": -4,\n"
        "     \"lastPlayedOrder\": 3.9}\n"
        "  ],\n"
        "  \"SomethingNewer\": {\"kept\": false}\n"
        "}");
}

} // namespace

class PlaybackStateFileTest : public QObject {
    Q_OBJECT

private slots:
    void lastTrackHasThreeForms();
    void lastTrackValueMatchesItsSong();
    void writesTheLegacyShapeAndQueueSnapshot();
    void writeIsAtomicUtf8WithoutTemporaryFile();
    void newestWriteWinsWhenOlderFinishesLast();
    void writeSkippedWhileGateIsClosed();
    void readsLegacyShapedFile();
    void wrongTypeInAReadKeySpoilsTheFile();
    void corruptOrMissingFileReadsAsNothing();
    void queueSnapshotRoundTrip();
    void unusableQueueSnapshotIsIgnoredAlone();
    void readMarksMissingLocalHistoryFiles();
    void clearDeletesFilesAndDropsQueuedWrites();
};

void PlaybackStateFileTest::lastTrackHasThreeForms()
{
    QCOMPARE(lmp::core::lastTrackValue(localSong(QStringLiteral("C:\\Music\\A.mp3"), QStringLiteral("A"))),
             QStringLiteral("C:\\Music\\A.mp3"));
    QCOMPARE(lmp::core::lastTrackValue(remoteSong(QStringLiteral("abc"), QStringLiteral("https://x.invalid/a"))),
             QStringLiteral("remote-id:abc"));
    QCOMPARE(lmp::core::lastTrackValue(remoteSong(QString(), QStringLiteral("https://x.invalid/a"))),
             QStringLiteral("source:remote|account|https://x.invalid/a"));
    // A web address as the path is not a path.
    Track streamed = remoteSong(QString(), QString());
    streamed.filePath = QStringLiteral("https://x.invalid/b");
    QCOMPARE(lmp::core::lastTrackValue(streamed), QStringLiteral("source:remote|account|https://x.invalid/b"));
    // Nothing to key on: the home history key is the fallback.
    Track bare;
    bare.title = QStringLiteral("  Hello,  World! ");
    bare.artist = QStringLiteral("ABC");
    QCOMPARE(lmp::core::lastTrackValue(bare), QStringLiteral("source:local||hello world|abc"));
    Track bareRemote = remoteSong(QString(), QString());
    QCOMPARE(lmp::core::lastTrackValue(bareRemote), QStringLiteral("source:remote|far away|band"));
}

void PlaybackStateFileTest::lastTrackValueMatchesItsSong()
{
    const Track local = localSong(QStringLiteral("C:\\Music\\A.mp3"), QStringLiteral("A"));
    const Track byId = remoteSong(QStringLiteral("abc"), QStringLiteral("https://x.invalid/a"));
    const Track bySource = remoteSong(QString(), QStringLiteral("https://x.invalid/b"));
    for (const Track& track : {local, byId, bySource}) {
        QVERIFY(lmp::core::matchesLastTrackValue(track, lmp::core::lastTrackValue(track)));
    }
    QVERIFY(!lmp::core::matchesLastTrackValue(local, lmp::core::lastTrackValue(byId)));
    QVERIFY(!lmp::core::matchesLastTrackValue(byId, lmp::core::lastTrackValue(bySource)));
    QVERIFY(!lmp::core::matchesLastTrackValue(local, QString()));
    // The path is compared exactly, as before.
    QVERIFY(!lmp::core::matchesLastTrackValue(local, QStringLiteral("c:\\music\\a.mp3")));
}

void PlaybackStateFileTest::writesTheLegacyShapeAndQueueSnapshot()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    UserDataOperationGate gate;
    PlaybackStateFile file(dir.filePath(QStringLiteral("LastMusicState.json")), gate);

    const Track current = localSong(QStringLiteral("C:\\Music\\B.mp3"), QStringLiteral("B"));
    PlaybackState state;
    state.lastTrack = lmp::core::lastTrackValue(current);
    state.volume = 0.25;
    state.homePlaySequence = 9; // never written
    state.homeHistory.append(LegacyHistoryEntry{current, QStringLiteral("k"), 1, 1, false});
    SavedQueue queue;
    queue.queue.contextQueue = {localSong(QStringLiteral("C:\\Music\\A.mp3"), QStringLiteral("A")), current};
    queue.queue.contextIndex = 1;
    queue.currentTrack = current;
    queue.playOrder = true;
    state.queue = queue;

    QVERIFY(file.write(state));
    file.flush();

    const QJsonObject object = QJsonDocument::fromJson(readBytes(file.filePath())).object();
    QCOMPARE(object.keys(), (QStringList{QStringLiteral("LastTrack"), QStringLiteral("QueueSnapshot"),
                                         QStringLiteral("Volume")}));
    QVERIFY(object.value(QStringLiteral("LastTrack")).isString());
    QCOMPARE(object.value(QStringLiteral("LastTrack")).toString(), QStringLiteral("C:\\Music\\B.mp3"));
    QVERIFY(object.value(QStringLiteral("Volume")).isDouble());
    QCOMPARE(object.value(QStringLiteral("Volume")).toDouble(), 0.25);
    const QJsonObject snapshot = object.value(QStringLiteral("QueueSnapshot")).toObject();
    QCOMPARE(snapshot.keys(), (QStringList{QStringLiteral("contextIndex"), QStringLiteral("contextQueue"),
                                           QStringLiteral("currentTrack"), QStringLiteral("playOrder"),
                                           QStringLiteral("userQueue")}));
    QCOMPARE(snapshot.value(QStringLiteral("contextIndex")).toInt(), 1);
    QCOMPARE(snapshot.value(QStringLiteral("playOrder")).toBool(), true);
    QCOMPARE(snapshot.value(QStringLiteral("currentTrack")).toObject().value(QStringLiteral("filePath")).toString(),
             QStringLiteral("C:\\Music\\B.mp3"));

    // Nothing playing: an empty LastTrack and no queue, as before.
    QVERIFY(file.write(PlaybackState{}));
    file.flush();
    QCOMPARE(readBytes(file.filePath()), QByteArrayLiteral("{\"LastTrack\":\"\",\"Volume\":0.7}"));
}

void PlaybackStateFileTest::writeIsAtomicUtf8WithoutTemporaryFile()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    UserDataOperationGate gate;
    const QString path = dir.filePath(QStringLiteral("nested/LastMusicState.json"));
    PlaybackStateFile file(path, gate);
    PlaybackState state;
    state.lastTrack = QStringLiteral("C:\\Musik\\Grüße \u266B.flac");
    QVERIFY(file.write(state));
    file.flush();

    const QByteArray bytes = readBytes(path);
    QVERIFY(bytes.startsWith('{')); // no byte order mark
    QVERIFY(bytes.contains(QStringLiteral("Grüße \u266B").toUtf8()));
    QCOMPARE(QDir(dir.filePath(QStringLiteral("nested"))).entryList(QDir::Files | QDir::Hidden | QDir::System),
             QStringList{QStringLiteral("LastMusicState.json")});
    const std::optional<PlaybackState> read = PlaybackStateFile::readNow(path);
    QVERIFY(read.has_value());
    QCOMPARE(read->lastTrack, state.lastTrack);
}

void PlaybackStateFileTest::newestWriteWinsWhenOlderFinishesLast()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    UserDataOperationGate gate;
    std::vector<std::function<void()>> held;
    PlaybackStateFile file(dir.filePath(QStringLiteral("LastMusicState.json")), gate,
                           [&held](std::function<void()> job) { held.push_back(std::move(job)); });

    PlaybackState older;
    older.lastTrack = QStringLiteral("C:\\older.mp3");
    PlaybackState newer;
    newer.lastTrack = QStringLiteral("C:\\newer.mp3");
    QVERIFY(file.write(older));
    QVERIFY(file.write(newer));
    QCOMPARE(held.size(), std::size_t{2});

    held.at(1)(); // the newer write finishes first
    held.at(0)(); // the stale one must not overwrite it
    const std::optional<PlaybackState> read = PlaybackStateFile::readNow(file.filePath());
    QVERIFY(read.has_value());
    QCOMPARE(read->lastTrack, newer.lastTrack);

    // A stale job that runs alone writes nothing either.
    QVERIFY(QFile::remove(file.filePath()));
    held.at(0)();
    QVERIFY(!QFile::exists(file.filePath()));
}

void PlaybackStateFileTest::writeSkippedWhileGateIsClosed()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    UserDataOperationGate gate;
    PlaybackStateFile file(dir.filePath(QStringLiteral("LastMusicState.json")), gate);
    QVERIFY(gate.closeAdmissions());
    QVERIFY(!file.write(PlaybackState{}));
    file.flush();
    QVERIFY(!QFile::exists(file.filePath()));
    gate.reopen();
    QVERIFY(file.write(PlaybackState{}));
    file.flush();
    QVERIFY(QFile::exists(file.filePath()));
}

void PlaybackStateFileTest::readsLegacyShapedFile()
{
    const std::optional<PlaybackState> state = lmp::core::parsePlaybackState(legacyFile());
    QVERIFY(state.has_value());
    QCOMPARE(state->lastTrack, QStringLiteral("C:\\Music\\One.mp3"));
    QCOMPARE(state->volume, 0.45);
    QCOMPARE(state->homePlaySequence, quint64{12});
    QVERIFY(!state->queue.has_value());
    QCOMPARE(state->homeHistory.size(), qsizetype{2}); // the string entry is skipped

    const LegacyHistoryEntry& first = state->homeHistory.at(0);
    QCOMPARE(first.track.title, QStringLiteral("One"));
    QCOMPARE(first.track.artist, QStringLiteral("First"));
    QCOMPARE(first.track.album, QStringLiteral("Debut"));
    QCOMPARE(first.track.genre, QStringLiteral("Rock"));
    QCOMPARE(first.track.filePath, QStringLiteral("C:\\Music\\One.mp3"));
    QCOMPARE(first.track.artworkUrl, QStringLiteral("thumbs\\one.jpg"));
    QCOMPARE(first.track.dateAdded, QStringLiteral("2024-01-02"));
    QCOMPARE(first.track.durationText, QStringLiteral("3:20"));
    QCOMPARE(first.track.sourceKind, QStringLiteral("local"));
    QCOMPARE(first.track.origin, QStringLiteral("Local"));
    QCOMPARE(first.track.sourceUrl, QString());
    QCOMPARE(first.track.sourceLabel, QStringLiteral("On this PC"));
    QCOMPARE(first.key, QStringLiteral("custom-key"));
    QCOMPARE(first.track.durationSeconds, 200.0);
    QCOMPARE(first.track.dateAddedSortKey, 20240102.0);
    QCOMPARE(first.playCount, quint32{7});
    QCOMPARE(first.lastPlayedOrder, quint64{11});
    QVERIFY(first.track.liked);

    // Missing keys take the old defaults: kind from the path, label from
    // the kind; counts are never negative and drop their fraction.
    const LegacyHistoryEntry& second = state->homeHistory.at(1);
    QCOMPARE(second.track.sourceKind, QStringLiteral("remote"));
    QCOMPARE(second.track.sourceLabel, QStringLiteral("Remote"));
    QCOMPARE(second.key, QString());
    QCOMPARE(second.playCount, quint32{0});
    QCOMPARE(second.lastPlayedOrder, quint64{3});
    QVERIFY(!second.track.liked);
}

void PlaybackStateFileTest::wrongTypeInAReadKeySpoilsTheFile()
{
    QVERIFY(!lmp::core::parsePlaybackState(QByteArrayLiteral("{\"LastTrack\": 5}")));
    QVERIFY(!lmp::core::parsePlaybackState(QByteArrayLiteral("{\"LastTrack\": null}")));
    QVERIFY(!lmp::core::parsePlaybackState(QByteArrayLiteral("{\"Volume\": \"loud\"}")));
    QVERIFY(!lmp::core::parsePlaybackState(QByteArrayLiteral("{\"HomePlaySequence\": true}")));
    QVERIFY(!lmp::core::parsePlaybackState(QByteArrayLiteral("{\"HomeHistory\": [{\"playCount\": \"3\"}]}")));
    QVERIFY(!lmp::core::parsePlaybackState(QByteArrayLiteral("{\"HomeHistory\": [{\"isLiked\": 1}]}")));
    QVERIFY(!lmp::core::parsePlaybackState(QByteArrayLiteral("{\"HomeHistory\": [{\"title\": 7}]}")));
    // Not an array: there is simply no history.
    const std::optional<PlaybackState> noHistory =
        lmp::core::parsePlaybackState(QByteArrayLiteral("{\"LastTrack\": \"x\", \"HomeHistory\": {\"a\": 1}}"));
    QVERIFY(noHistory.has_value());
    QVERIFY(noHistory->homeHistory.isEmpty());
    // An empty object is a valid, empty state.
    const std::optional<PlaybackState> empty = lmp::core::parsePlaybackState(QByteArrayLiteral("{}"));
    QVERIFY(empty.has_value());
    QCOMPARE(*empty, PlaybackState{});
}

void PlaybackStateFileTest::corruptOrMissingFileReadsAsNothing()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.filePath(QStringLiteral("LastMusicState.json"));
    QVERIFY(!PlaybackStateFile::readNow(path));

    const QList<QByteArray> unreadable{
        QByteArray(),
        QByteArrayLiteral("{\"LastTrack\": \"C:\\\\a.mp3\""),
        QByteArrayLiteral("[1, 2]"),
        QByteArrayLiteral("{\"LastTrack\": \"\xC3\x28\"}"),
    };
    for (const QByteArray& bytes : unreadable) {
        QVERIFY(writeBytes(path, bytes));
        QVERIFY(!PlaybackStateFile::readNow(path));
        // Left where it is: earlier versions never moved this file aside.
        QVERIFY(QFile::exists(path));
    }

    UserDataOperationGate gate;
    PlaybackStateFile file(path, gate);
    QFuture<std::optional<PlaybackState>> future = file.readAsync();
    future.waitForFinished();
    QVERIFY(!future.result().has_value());
}

void PlaybackStateFileTest::queueSnapshotRoundTrip()
{
    PlaybackState state;
    const Track a = localSong(QStringLiteral("C:\\A.mp3"), QStringLiteral("A"));
    const Track b = localSong(QStringLiteral("C:\\B.mp3"), QStringLiteral("B"));
    const Track c = localSong(QStringLiteral("C:\\C.mp3"), QStringLiteral("C"));
    state.lastTrack = lmp::core::lastTrackValue(b);
    state.volume = 0.5;
    SavedQueue queue;
    queue.queue.contextQueue = {a, b};
    queue.queue.contextIndex = 1;
    queue.queue.userQueue = {c};
    queue.currentTrack = b;
    queue.playOrder = true;
    state.queue = queue;

    const std::optional<PlaybackState> read = lmp::core::parsePlaybackState(lmp::core::serializePlaybackState(state));
    QVERIFY(read.has_value());
    QCOMPARE(*read, state);
}

void PlaybackStateFileTest::unusableQueueSnapshotIsIgnoredAlone()
{
    const std::optional<PlaybackState> notObject =
        lmp::core::parsePlaybackState(QByteArrayLiteral("{\"LastTrack\": \"x\", \"QueueSnapshot\": [1]}"));
    QVERIFY(notObject.has_value());
    QCOMPARE(notObject->lastTrack, QStringLiteral("x"));
    QVERIFY(!notObject->queue.has_value());

    const std::optional<PlaybackState> noCurrent = lmp::core::parsePlaybackState(
        QByteArrayLiteral("{\"LastTrack\": \"x\", \"QueueSnapshot\": {\"contextQueue\": [], \"contextIndex\": 0}}"));
    QVERIFY(noCurrent.has_value());
    QVERIFY(!noCurrent->queue.has_value());
}

void PlaybackStateFileTest::readMarksMissingLocalHistoryFiles()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString present = QDir::toNativeSeparators(dir.filePath(QStringLiteral("here.mp3")));
    QVERIFY(writeBytes(present, QByteArrayLiteral("x")));
    const QString gone = QDir::toNativeSeparators(dir.filePath(QStringLiteral("gone.mp3")));

    PlaybackState state;
    state.homeHistory.append(LegacyHistoryEntry{localSong(present, QStringLiteral("Here")), {}, 1, 1, false});
    state.homeHistory.append(LegacyHistoryEntry{localSong(gone, QStringLiteral("Gone")), {}, 1, 2, false});
    state.homeHistory.append(
        LegacyHistoryEntry{remoteSong(QString(), QStringLiteral("https://x.invalid/r")), {}, 1, 3, false});
    // Written by hand, as the writer never writes the history.
    QJsonObject object;
    QJsonArray history;
    for (const LegacyHistoryEntry& entry : state.homeHistory) {
        history.append(entry.track.toSnapshotJson());
    }
    object.insert(QStringLiteral("HomeHistory"), history);
    const QString path = dir.filePath(QStringLiteral("LastMusicState.json"));
    QVERIFY(writeBytes(path, QJsonDocument(object).toJson()));

    const std::optional<PlaybackState> read = PlaybackStateFile::readNow(path);
    QVERIFY(read.has_value());
    QCOMPARE(read->homeHistory.size(), qsizetype{3});
    QVERIFY(!read->homeHistory.at(0).localFileMissing);
    QVERIFY(read->homeHistory.at(1).localFileMissing);
    QVERIFY(!read->homeHistory.at(2).localFileMissing);
}

void PlaybackStateFileTest::clearDeletesFilesAndDropsQueuedWrites()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    UserDataOperationGate gate;
    std::vector<std::function<void()>> held;
    const QString path = dir.filePath(QStringLiteral("LastMusicState.json"));
    PlaybackStateFile file(path, gate, [&held](std::function<void()> job) { held.push_back(std::move(job)); });
    QVERIFY(writeBytes(path, QByteArrayLiteral("{}")));
    QVERIFY(writeBytes(path + QStringLiteral(".tmp"), QByteArrayLiteral("{")));

    QVERIFY(file.write(PlaybackState{}));
    QVERIFY(file.clear());
    QVERIFY(!QFile::exists(path));
    QVERIFY(!QFile::exists(path + QStringLiteral(".tmp")));
    held.at(0)();
    QVERIFY(!QFile::exists(path));
    QVERIFY(file.clear()); // nothing left to delete is fine
}

QTEST_GUILESS_MAIN(PlaybackStateFileTest)
#include "tst_playback_state_file.moc"
