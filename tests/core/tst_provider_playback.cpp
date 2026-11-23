#include <QDir>
#include <QFile>
#include <QObject>
#include <QPromise>
#include <QTemporaryDir>
#include <QTest>
#include <QUrlQuery>

#include <memory>
#include <vector>

#include "ModeServiceFixture.h"
#include "core/catalog/CatalogClient.h"
#include "core/db/TrackRepository.h"
#include "core/model/TrackKeys.h"
#include "core/playback/ProviderTrackSourceResolver.h"
#include "core/playback/StreamCache.h"
#include "core/remote/RemoteMusicService.h"

using lmp::core::FailureKind;
using lmp::core::Result;
using lmp::test::FakeHttpTransport;

namespace {

class HoldingDownloader final : public lmp::core::IStreamDownloader {
public:
    QFuture<Result<QString>> download(const QUrl& streamUrl, const QString&) override
    {
        urls.append(streamUrl);
        auto promise = std::make_shared<QPromise<Result<QString>>>();
        promise->start();
        kept.push_back(promise);
        return promise->future();
    }
    QList<QUrl> urls;
    std::vector<std::shared_ptr<QPromise<Result<QString>>>> kept;
};

template <typename T>
[[nodiscard]] Result<T> resultOf(const QFuture<Result<T>>& future)
{
    if (!QTest::qWaitFor([&future]() { return future.isFinished(); }, 3000)) {
        return lmp::core::Failure::of(FailureKind::Unknown, QStringLiteral("did not finish"));
    }
    return future.result();
}

struct Fixture {
    Fixture()
        : catalog(modes.session.client)
        , remoteMusic({.mode = modes.modes,
                       .session = modes.session.service,
                       .account = modes.session.client,
                       .catalog = catalog,
                       .provider = modes.provider})
        , tracks(modes.library.database())
        , cache(folder.filePath(QStringLiteral("stream-cache")), gate, downloader)
        , resolver({.remote = remoteMusic, .mode = modes.modes, .cache = cache, .tracks = tracks})
    {
        modes.storeProvider(lmp::test::kProviderBase, QStringLiteral("key a"));
        (void)modes.modes.start();
    }

    lmp::test::ModeServiceFixture modes;
    lmp::core::catalog::CatalogClient catalog;
    lmp::core::remote::RemoteMusicService remoteMusic;
    lmp::core::db::TrackRepository tracks;
    QTemporaryDir folder;
    lmp::core::UserDataOperationGate gate;
    HoldingDownloader downloader;
    lmp::core::StreamCache cache;
    lmp::core::ProviderTrackSourceResolver resolver;
};

lmp::core::Track song()
{
    lmp::core::Track track;
    track.title = QStringLiteral("One");
    track.artist = QStringLiteral("Artist");
    track.sourceKind = QStringLiteral("remote");
    track.sourceTag = QStringLiteral("tag-b");
    track.remoteId = QStringLiteral("tag-b:1");
    track.sourceUrl = QStringLiteral("https://music.example.test/1");
    return track;
}

} // namespace

// API key mode playback: the provider resolver and the stream cache together.
class ProviderPlaybackTest : public QObject {
    Q_OBJECT

private slots:
    void resolvedStreamCarriesTheKeyAndIsFetchedAhead();
    void unresolvedSongStillStreamsFromItsLink();
    void cachedSongPlaysFromItsFile();
};

void ProviderPlaybackTest::resolvedStreamCarriesTheKeyAndIsFetchedAhead()
{
    Fixture fixture;
    fixture.modes.session.transport.script(
        QStringLiteral("/v1/resolve"),
        FakeHttpTransport::ok("{\"result\":{\"streamUrl\":\"http://elsewhere.example.test/v1/stream/remote%3Aabc?url=x&media_token=old\"}}"));
    const Result<QUrl> url = resultOf(fixture.resolver.resolve(song()));
    QVERIFY(url.isOk());
    QCOMPARE(url.value().toString(QUrl::FullyEncoded),
             QStringLiteral("https://provider.example.test/v1/stream/remote%3Aabc?url=x&access_token=key%20a"));
    // The stream is fetched ahead for next time.
    QCOMPARE(fixture.downloader.urls.size(), 1);
    QCOMPARE(fixture.downloader.urls.constFirst(), url.value());
}

void ProviderPlaybackTest::unresolvedSongStillStreamsFromItsLink()
{
    Fixture fixture;
    fixture.modes.session.transport.script(QStringLiteral("/v1/resolve"), FakeHttpTransport::failed(FailureKind::Server, 500));
    const Result<QUrl> url = resultOf(fixture.resolver.resolve(song()));
    QVERIFY(url.isOk());
    QVERIFY(url.value().toString(QUrl::FullyEncoded)
                .startsWith(QStringLiteral("https://provider.example.test/v1/stream/remote%3A")));
    QCOMPARE(QUrlQuery(url.value()).queryItemValue(QStringLiteral("url"), QUrl::FullyDecoded), song().sourceUrl);
    // A song without a link cannot stream.
    lmp::core::Track bare = song();
    bare.sourceUrl.clear();
    QCOMPARE(resultOf(fixture.resolver.resolve(bare)).failure().kind, FailureKind::NotFound);
}

void ProviderPlaybackTest::cachedSongPlaysFromItsFile()
{
    Fixture fixture;
    const QString key = lmp::core::track_keys::streamCacheKey(fixture.remoteMusic.scopeKeyParts(), song());
    QVERIFY(!key.isEmpty());
    const QString folder = fixture.folder.filePath(QStringLiteral("stream-cache"));
    QVERIFY(QDir().mkpath(folder));
    const QString file = QDir(folder).filePath(lmp::core::StreamCache::hashKey(key) + QStringLiteral(".0.1.mp3"));
    QFile written(file);
    QVERIFY(written.open(QIODevice::WriteOnly));
    written.write("audio");
    written.close();
    const int before = static_cast<int>(fixture.modes.session.transport.requests.size());
    const Result<QUrl> url = resultOf(fixture.resolver.resolve(song()));
    QVERIFY(url.isOk());
    QCOMPARE(url.value(), QUrl::fromLocalFile(file));
    QCOMPARE(static_cast<int>(fixture.modes.session.transport.requests.size()), before);
}

QTEST_GUILESS_MAIN(ProviderPlaybackTest)
#include "tst_provider_playback.moc"
