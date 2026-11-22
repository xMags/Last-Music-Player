#include <QDir>
#include <QFileInfo>
#include <QObject>
#include <QString>
#include <QTemporaryDir>
#include <QTest>
#include <QtGlobal>

#include "core/app/AppPaths.h"

using lmp::core::AppPaths;

// The names are a contract with the data folders of installed versions.
class AppPathsTest : public QObject {
    Q_OBJECT

private slots:
    void namesMatchEarlierVersions();
    void forCurrentUserUsesLocalAppData();
    void forCurrentUserFallsBackToTheCurrentFolder();
    void constructingTouchesNothingOnDisk();
};

void AppPathsTest::namesMatchEarlierVersions()
{
    const QString root = QStringLiteral("C:/Data/Last Music Player");
    const AppPaths paths(root);
    QCOMPARE(paths.root(), root);
    QCOMPARE(paths.settingsFile(), root + QStringLiteral("/Settings.json"));
    QCOMPARE(paths.stateFile(), root + QStringLiteral("/LastMusicState.json"));
    QCOMPARE(paths.databaseFile(), root + QStringLiteral("/lastmusic.db"));
    QCOMPARE(paths.downloadsFile(), root + QStringLiteral("/Downloads.json"));
    QCOMPARE(paths.offlineFolder(), root + QStringLiteral("/Offline"));
    QCOMPARE(paths.thumbsFolder(), root + QStringLiteral("/thumbs"));
    QCOMPARE(paths.streamCacheFolder(), root + QStringLiteral("/stream-cache"));
}

void AppPathsTest::forCurrentUserUsesLocalAppData()
{
    QTemporaryDir fake;
    QVERIFY(fake.isValid());
    const QByteArray saved = qgetenv("LOCALAPPDATA");
    qputenv("LOCALAPPDATA", QDir::toNativeSeparators(fake.path()).toLocal8Bit());
    const AppPaths paths = AppPaths::forCurrentUser();
    qputenv("LOCALAPPDATA", saved);
    QCOMPARE(paths.root(), QDir::cleanPath(fake.path() + QStringLiteral("/Last Music Player")));
}

void AppPathsTest::forCurrentUserFallsBackToTheCurrentFolder()
{
    const QByteArray saved = qgetenv("LOCALAPPDATA");
    qunsetenv("LOCALAPPDATA");
    const AppPaths paths = AppPaths::forCurrentUser();
    qputenv("LOCALAPPDATA", saved);
    QCOMPARE(paths.root(), QDir::cleanPath(QDir::currentPath() + QStringLiteral("/Last Music Player")));
}

void AppPathsTest::constructingTouchesNothingOnDisk()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const AppPaths paths(dir.filePath(QStringLiteral("Last Music Player")));
    QVERIFY(!QFileInfo::exists(paths.root()));
}

QTEST_GUILESS_MAIN(AppPathsTest)
#include "tst_app_paths.moc"
