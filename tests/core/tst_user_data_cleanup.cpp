#include <QDir>
#include <QFile>
#include <QObject>
#include <QSignalSpy>
#include <QString>
#include <QTemporaryDir>
#include <QTest>

#include <memory>
#include <optional>

#include "FakeCredentialStore.h"
#include "FakeTagReader.h"
#include "ModeServiceFixture.h"
#include "PlayerFixture.h"
#include "core/account/ScopedCredentialStore.h"
#include "core/app/UserDataCleanup.h"
#include "core/app/WindowPlacement.h"
#include "core/db/LibraryQueries.h"
#include "core/library/ArtworkCache.h"
#include "core/library/LibraryScanner.h"
#include "core/model/CatalogSourceKey.h"
#include "core/settings/SettingKeys.h"

using lmp::core::UserDataCleanup;
using Outcome = lmp::core::UserDataCleanup::Outcome;

// "Delete all data", the window's saved frame, and the credential names a
// developer run uses.
class UserDataCleanupTest : public QObject {
    Q_OBJECT

private slots:
    void init();
    void cleanup();
    void cleanUpRemovesEverything();
    void secondCleanUpIsRefused();
    void busyOperationsStopIt();
    void waitsForARunningOperation();
    void windowFrameRules();
    void developerRunsUseTheirOwnCredentials();

private:
    [[nodiscard]] std::unique_ptr<UserDataCleanup> makeCleanup(std::chrono::milliseconds timeout);

    std::unique_ptr<PlayerFixture> m_player;
    std::unique_ptr<lmp::test::ModeServiceFixture> m_modes;
    std::unique_ptr<lmp::core::PlayerController> m_controller;
    std::unique_ptr<lmp::core::db::LibraryQueries> m_queries;
    std::unique_ptr<lmp::test::FakeTagReader> m_tags;
    std::unique_ptr<QTemporaryDir> m_folder;
    std::unique_ptr<lmp::core::ArtworkCache> m_artwork;
    std::unique_ptr<lmp::core::LibraryScanner> m_scanner;
    QString m_stateFile;
    QString m_covers;
};

void UserDataCleanupTest::init()
{
    m_player = std::make_unique<PlayerFixture>();
    QVERIFY(m_player->ok());
    m_modes = std::make_unique<lmp::test::ModeServiceFixture>();
    (void)m_modes->modes.start();
    m_controller = m_player->makeController();
    m_queries = std::make_unique<lmp::core::db::LibraryQueries>(m_player->library.database());
    m_tags = std::make_unique<lmp::test::FakeTagReader>();
    m_folder = std::make_unique<QTemporaryDir>();
    m_covers = m_folder->filePath(QStringLiteral("thumbs"));
    m_stateFile = m_folder->filePath(QStringLiteral("LastMusicState.json"));
    m_artwork = std::make_unique<lmp::core::ArtworkCache>(m_covers, nullptr);
    m_scanner = std::make_unique<lmp::core::LibraryScanner>(lmp::core::LibraryScanner::Dependencies{
        *m_tags, *m_artwork, m_player->tracks, *m_queries, m_player->settings, m_modes->session.gate});
}

void UserDataCleanupTest::cleanup()
{
    m_scanner.reset();
    m_artwork.reset();
    m_folder.reset();
    m_tags.reset();
    m_queries.reset();
    m_controller.reset();
    m_modes.reset();
    m_player.reset();
}

std::unique_ptr<UserDataCleanup> UserDataCleanupTest::makeCleanup(std::chrono::milliseconds timeout)
{
    return std::make_unique<UserDataCleanup>(UserDataCleanup::Parts{.gate = m_modes->session.gate,
                                                                    .scanner = *m_scanner,
                                                                    .player = *m_controller,
                                                                    .tracks = m_player->tracks,
                                                                    .settings = m_modes->session.settings,
                                                                    .secrets = m_modes->session.secrets,
                                                                    .session = m_modes->session.service,
                                                                    .mode = m_modes->modes,
                                                                    .stateFile = m_stateFile,
                                                                    .coverFolder = m_covers,
                                                                    .idleTimeout = timeout});
}

void UserDataCleanupTest::cleanUpRemovesEverything()
{
    const lmp::core::Track song = m_player->song(QStringLiteral("One"));
    QVERIFY(m_player->tracks.upsertLocalTrack(song, lmp::core::catalogSourceKey(song)).isOk());
    QVERIFY(m_modes->session.secrets.writeProviderApiKey(QStringLiteral("secret-key")).isOk());
    m_modes->session.settings.setString(lmp::core::settings::keys::MusicLibraryPath, QStringLiteral("C:/Music"));
    QFile state(m_stateFile);
    QVERIFY(state.open(QIODevice::WriteOnly));
    state.write("{}");
    state.close();
    QVERIFY(QDir().mkpath(m_covers));
    QFile cover(QDir(m_covers).filePath(QStringLiteral("a.jpg")));
    QVERIFY(cover.open(QIODevice::WriteOnly));
    cover.close();
    m_controller->playTrack(song);
    settle();
    QVERIFY(m_controller->currentTrack().has_value());

    std::unique_ptr<UserDataCleanup> wipe = makeCleanup(std::chrono::seconds(2));
    QSignalSpy finished(wipe.get(), &UserDataCleanup::finished);
    QSignalSpy cleared(wipe.get(), &UserDataCleanup::cleared);
    wipe->start();
    QTRY_COMPARE(finished.count(), 1);
    QCOMPARE(finished.first().at(0).value<Outcome>(), Outcome::Done);
    QCOMPARE(cleared.count(), 1);
    QVERIFY(!wipe->running());

    QCOMPARE(m_player->library.number("SELECT COUNT(*) FROM Tracks"), 0);
    QVERIFY(!m_modes->session.secrets.readProviderApiKey().value().has_value());
    QVERIFY(!m_modes->session.settings.contains(lmp::core::settings::keys::MusicLibraryPath));
    QVERIFY(!QFile::exists(m_stateFile));
    QVERIFY(!QDir(m_covers).exists());
    QVERIFY(!m_controller->currentTrack().has_value());
    QCOMPARE(m_modes->modes.mode(), lmp::core::mode::SourceMode::LocalOnly);
    // Admissions are open again.
    QVERIFY(m_modes->session.gate.tryEnter().has_value());
}

void UserDataCleanupTest::secondCleanUpIsRefused()
{
    QVERIFY(m_modes->session.gate.closeAdmissions());
    std::unique_ptr<UserDataCleanup> wipe = makeCleanup(std::chrono::seconds(2));
    QSignalSpy finished(wipe.get(), &UserDataCleanup::finished);
    wipe->start();
    QCOMPARE(finished.count(), 1);
    QCOMPARE(finished.first().at(0).value<Outcome>(), Outcome::AlreadyRunning);
    m_modes->session.gate.reopen();
}

void UserDataCleanupTest::busyOperationsStopIt()
{
    const lmp::core::Track song = m_player->song(QStringLiteral("Kept"));
    QVERIFY(m_player->tracks.upsertLocalTrack(song, lmp::core::catalogSourceKey(song)).isOk());
    std::optional<lmp::core::UserDataOperationGate::Lease> running = m_modes->session.gate.tryEnter();
    QVERIFY(running.has_value());
    std::unique_ptr<UserDataCleanup> wipe = makeCleanup(std::chrono::milliseconds(200));
    QSignalSpy finished(wipe.get(), &UserDataCleanup::finished);
    wipe->start();
    QTRY_COMPARE(finished.count(), 1);
    QCOMPARE(finished.first().at(0).value<Outcome>(), Outcome::StillBusy);
    // Nothing was cleared, and admissions reopened.
    QCOMPARE(m_player->library.number("SELECT COUNT(*) FROM Tracks"), 1);
    running.reset();
    QVERIFY(m_modes->session.gate.tryEnter().has_value());
}

void UserDataCleanupTest::waitsForARunningOperation()
{
    std::optional<lmp::core::UserDataOperationGate::Lease> running = m_modes->session.gate.tryEnter();
    std::unique_ptr<UserDataCleanup> wipe = makeCleanup(std::chrono::seconds(5));
    QSignalSpy finished(wipe.get(), &UserDataCleanup::finished);
    wipe->start();
    QTest::qWait(150);
    QCOMPARE(finished.count(), 0);
    running.reset();
    QTRY_COMPARE(finished.count(), 1);
    QCOMPARE(finished.first().at(0).value<Outcome>(), Outcome::Done);
}

void UserDataCleanupTest::windowFrameRules()
{
    lmp::core::SettingsStore& settings = m_modes->session.settings;
    // Nothing saved: no size, no place.
    lmp::core::WindowPlacement::Saved saved = lmp::core::WindowPlacement::load(settings);
    QVERIFY(!saved.size && !saved.position);
    // Too small is neither saved nor used.
    QVERIFY(!lmp::core::WindowPlacement::save(settings, QRect(10, 20, 640, 480)));
    QVERIFY(!settings.contains(lmp::core::settings::keys::WindowWidth));
    QVERIFY(lmp::core::WindowPlacement::save(settings, QRect(-1500, 40, 1700, 1000)));
    saved = lmp::core::WindowPlacement::load(settings);
    QCOMPARE(*saved.size, QSize(1700, 1000));
    QCOMPARE(*saved.position, QPoint(-1500, 40));
    settings.setInteger(lmp::core::settings::keys::WindowHeight, 500);
    QVERIFY(!lmp::core::WindowPlacement::load(settings).size);
}

void UserDataCleanupTest::developerRunsUseTheirOwnCredentials()
{
    using lmp::core::account::ScopedCredentialStore;
    lmp::test::FakeCredentialStore inner;
    QVERIFY(ScopedCredentialStore::suffixForDataRoot(QStringLiteral("C:/x"), false).isEmpty());
    const QString suffix = ScopedCredentialStore::suffixForDataRoot(QStringLiteral("C:/Temp/Data"), true);
    QVERIFY(suffix.startsWith(QStringLiteral("dev-")));
    QCOMPARE(suffix, ScopedCredentialStore::suffixForDataRoot(QStringLiteral("c:/temp/data/"), true));

    ScopedCredentialStore scoped(inner, suffix);
    QVERIFY(scoped.write(QStringLiteral("Target"), QStringLiteral("value")).isOk());
    QVERIFY(!inner.values.contains(QStringLiteral("Target")));
    QCOMPARE(inner.values.value(QStringLiteral("Target#") + suffix), QStringLiteral("value"));
    QCOMPARE(scoped.read(QStringLiteral("Target")).value().value_or(QString()), QStringLiteral("value"));
    QVERIFY(scoped.remove(QStringLiteral("Target")).isOk());
    QVERIFY(inner.values.isEmpty());

    ScopedCredentialStore plain(inner, QString());
    QVERIFY(plain.write(QStringLiteral("Target"), QStringLiteral("v")).isOk());
    QVERIFY(inner.values.contains(QStringLiteral("Target")));
}

QTEST_MAIN(UserDataCleanupTest)
#include "tst_user_data_cleanup.moc"
