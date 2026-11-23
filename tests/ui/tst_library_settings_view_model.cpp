#include <QDir>
#include <QFile>
#include <QList>
#include <QObject>
#include <QSignalSpy>
#include <QString>
#include <QStringList>
#include <QTemporaryDir>
#include <QTest>
#include <QUrl>
#include <QVariantMap>

#include <memory>
#include <optional>

#include "../core/FakeTagReader.h"
#include "../core/TempLibrary.h"
#include "core/db/LibraryQueries.h"
#include "core/db/TrackRepository.h"
#include "core/library/ArtworkCache.h"
#include "core/library/LibraryScanner.h"
#include "core/library/UserDataOperationGate.h"
#include "core/settings/SettingKeys.h"
#include "core/settings/SettingsStore.h"
#include "platform/IExternalLinks.h"
#include "platform/IFolderPicker.h"
#include "ui/settings/LibrarySettingsViewModel.h"

using lmp::ui::LibrarySettingsViewModel;
namespace keys = lmp::core::settings::keys;

namespace {

class FakePicker final : public lmp::platform::IFolderPicker {
public:
    std::optional<QString> pickFolder(quintptr /*ownerWindow*/, const QString& startFolder) override
    {
        ++calls;
        lastStart = startFolder;
        return answer;
    }

    std::optional<QString> answer;
    QString lastStart;
    int calls = 0;
};

class FakeLinks final : public lmp::platform::IExternalLinks {
public:
    bool open(const QUrl& url) override
    {
        opened.append(url);
        return true;
    }

    QList<QUrl> opened;
};

} // namespace

// The Library card: folder, scan with progress and cancel, format chips.
class LibrarySettingsViewModelTest : public QObject {
    Q_OBJECT

private slots:
    void init();
    void cleanup();
    void chipsFollowScanFormats();
    void togglingWritesTheFormatsInChipOrder();
    void choosingAFolderSavesAndScansIt();
    void cancellingAPickerChangesNothing();
    void rescanReportsProgressAndTheCount();
    void cancelStopsTheScan();
    void openFolderOpensTheSavedFolder();

private:
    void touch(const QString& name);

    std::unique_ptr<QTemporaryDir> m_dir;
    std::unique_ptr<TempLibrary> m_library;
    std::unique_ptr<lmp::core::SettingsStore> m_settings;
    std::unique_ptr<lmp::core::db::TrackRepository> m_tracks;
    std::unique_ptr<lmp::core::db::LibraryQueries> m_queries;
    std::unique_ptr<lmp::test::FakeTagReader> m_tags;
    std::unique_ptr<lmp::core::ArtworkCache> m_artwork;
    std::unique_ptr<lmp::core::UserDataOperationGate> m_gate;
    std::unique_ptr<lmp::core::LibraryScanner> m_scanner;
    std::unique_ptr<FakePicker> m_picker;
    std::unique_ptr<FakeLinks> m_links;
    std::unique_ptr<LibrarySettingsViewModel> m_model;
    QString m_music;
};

void LibrarySettingsViewModelTest::init()
{
    m_dir = std::make_unique<QTemporaryDir>();
    QVERIFY(m_dir->isValid());
    m_music = QDir::toNativeSeparators(m_dir->filePath(QStringLiteral("Music")));
    QVERIFY(QDir().mkpath(m_music));
    m_library = std::make_unique<TempLibrary>();
    QVERIFY(m_library->ok());
    m_settings = std::make_unique<lmp::core::SettingsStore>(m_dir->filePath(QStringLiteral("Settings.json")));
    m_tracks = std::make_unique<lmp::core::db::TrackRepository>(m_library->database());
    m_queries = std::make_unique<lmp::core::db::LibraryQueries>(m_library->database());
    m_tags = std::make_unique<lmp::test::FakeTagReader>();
    m_artwork = std::make_unique<lmp::core::ArtworkCache>(m_dir->filePath(QStringLiteral("thumbs")), nullptr);
    m_gate = std::make_unique<lmp::core::UserDataOperationGate>();
    m_scanner = std::make_unique<lmp::core::LibraryScanner>(lmp::core::LibraryScanner::Dependencies{
        *m_tags, *m_artwork, *m_tracks, *m_queries, *m_settings, *m_gate});
    m_picker = std::make_unique<FakePicker>();
    m_links = std::make_unique<FakeLinks>();
    m_model = std::make_unique<LibrarySettingsViewModel>(
        LibrarySettingsViewModel::Parts{*m_settings, *m_scanner, *m_gate, *m_picker, *m_links});
}

void LibrarySettingsViewModelTest::cleanup()
{
    m_model.reset();
    m_links.reset();
    m_picker.reset();
    m_scanner.reset();
    m_gate.reset();
    m_artwork.reset();
    m_tags.reset();
    m_queries.reset();
    m_tracks.reset();
    m_settings.reset();
    m_library.reset();
    m_dir.reset();
}

void LibrarySettingsViewModelTest::touch(const QString& name)
{
    QFile file(QDir(m_music).filePath(name));
    QVERIFY(file.open(QIODevice::WriteOnly));
    file.write("x");
}

void LibrarySettingsViewModelTest::chipsFollowScanFormats()
{
    const QVariantList chips = m_model->formats();
    QCOMPARE(chips.size(), 8);
    QCOMPARE(chips.at(0).toMap().value(QStringLiteral("label")).toString(), QStringLiteral("MP3"));
    QVERIFY(chips.at(0).toMap().value(QStringLiteral("checked")).toBool());
    QVERIFY(chips.at(1).toMap().value(QStringLiteral("checked")).toBool());
    QVERIFY(!chips.at(2).toMap().value(QStringLiteral("checked")).toBool());
    QCOMPARE(chips.at(7).toMap().value(QStringLiteral("label")).toString(), QStringLiteral("WMA"));
}

void LibrarySettingsViewModelTest::togglingWritesTheFormatsInChipOrder()
{
    QSignalSpy changed(m_model.get(), &LibrarySettingsViewModel::formatsChanged);
    m_model->toggleFormat(QStringLiteral(".ogg"));
    m_model->toggleFormat(QStringLiteral(".wav"));
    QCOMPARE(m_settings->string(keys::ScanFormats), QStringLiteral(".mp3,.flac,.wav,.ogg"));
    m_model->toggleFormat(QStringLiteral(".mp3"));
    QCOMPARE(m_settings->string(keys::ScanFormats), QStringLiteral(".flac,.wav,.ogg"));
    m_model->toggleFormat(QStringLiteral(".exe"));
    QCOMPARE(m_settings->string(keys::ScanFormats), QStringLiteral(".flac,.wav,.ogg"));
    QVERIFY(changed.count() >= 3);
}

void LibrarySettingsViewModelTest::choosingAFolderSavesAndScansIt()
{
    touch(QStringLiteral("A - One.mp3"));
    touch(QStringLiteral("A - Two.mp3"));
    m_picker->answer = m_music;
    QSignalSpy folder(m_model.get(), &LibrarySettingsViewModel::folderChanged);
    m_model->changeFolder();
    QCOMPARE(m_settings->string(keys::MusicLibraryPath), m_music);
    QCOMPARE(folder.count(), 1);
    QVERIFY(m_model->scanning());
    QTRY_VERIFY_WITH_TIMEOUT(!m_model->scanning(), 10000);
    QCOMPARE(m_model->scanStatus(), QStringLiteral("2 tracks in your library"));
    QCOMPARE(m_library->number("SELECT COUNT(*) FROM Tracks WHERE IsActive=1;"), 2);

    // The picker opens in the saved folder next time.
    m_picker->answer.reset();
    m_model->changeFolder();
    QCOMPARE(m_picker->lastStart, m_music);
}

void LibrarySettingsViewModelTest::cancellingAPickerChangesNothing()
{
    m_model->changeFolder();
    QCOMPARE(m_picker->calls, 1);
    QVERIFY(!m_settings->contains(keys::MusicLibraryPath));
    QVERIFY(!m_model->scanning());
}

void LibrarySettingsViewModelTest::rescanReportsProgressAndTheCount()
{
    touch(QStringLiteral("A - One.mp3"));
    m_settings->setString(keys::MusicLibraryPath, m_music);
    QStringList statuses;
    connect(m_model.get(), &LibrarySettingsViewModel::scanChanged, this,
            [this, &statuses]() { statuses.append(m_model->scanStatus()); });
    m_model->scan();
    QVERIFY(m_model->canCancel());
    QTRY_VERIFY_WITH_TIMEOUT(!m_model->scanning(), 10000);
    QCOMPARE(statuses.first(), QStringLiteral("Scanning your music folder..."));
    QVERIFY(statuses.contains(QStringLiteral("Scanning 1 / 1 tracks...")));
    QVERIFY(statuses.contains(QStringLiteral("Updating library database...")));
    QCOMPARE(statuses.last(), QStringLiteral("1 track in your library"));
}
void LibrarySettingsViewModelTest::cancelStopsTheScan()
{
    for (int i = 0; i < 40; ++i) {
        touch(QStringLiteral("A - Song %1.mp3").arg(i));
    }
    m_tags->slow = true;
    m_settings->setString(keys::MusicLibraryPath, m_music);
    m_model->scan();
    QTRY_VERIFY_WITH_TIMEOUT(m_model->scanProgress() >= 0, 5000);
    m_model->cancelScan();
    QCOMPARE(m_model->scanStatus(), QStringLiteral("Cancelling scan..."));
    QTRY_VERIFY_WITH_TIMEOUT(!m_model->scanning(), 10000);
    QCOMPARE(m_model->scanStatus(), QStringLiteral("Scan cancelled"));
    QCOMPARE(m_library->number("SELECT COUNT(*) FROM Tracks;"), 0);
}

void LibrarySettingsViewModelTest::openFolderOpensTheSavedFolder()
{
    m_model->openFolder();
    QVERIFY(m_links->opened.isEmpty());
    m_settings->setString(keys::MusicLibraryPath, m_music);
    m_model->openFolder();
    QCOMPARE(m_links->opened.size(), 1);
    QCOMPARE(QDir::toNativeSeparators(m_links->opened.first().toLocalFile()), m_music);
}

QTEST_MAIN(LibrarySettingsViewModelTest)
#include "tst_library_settings_view_model.moc"
