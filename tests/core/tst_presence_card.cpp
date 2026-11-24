#include "core/presence/PresenceCard.h"
#include "core/settings/SettingKeys.h"
#include "core/settings/SettingsStore.h"

#include <QObject>
#include <QTemporaryDir>
#include <QTest>

namespace presence = lmp::core::presence;
using presence::PausedBehavior;
using presence::PresenceLayout;
using presence::PresenceOptions;
using presence::PresenceVisibility;

namespace {

const QString kTitle = QStringLiteral("Night Geometry");
const QString kArtist = QStringLiteral("Signal Garden");
const QString kAlbum = QStringLiteral("Afterimage Radio");
const QString kDot = QStringLiteral(" · ");

} // namespace

// The Discord card's choices: the private card, the one-line layout, the
// Cast device, and when the card is shown at all.
class PresenceCardTest : public QObject {
    Q_OBJECT

private slots:
    void defaultCardIsTitleThenArtist()
    {
        const presence::PresenceLines lines = presence::presenceLines(kTitle, kArtist, kAlbum, {}, {});
        QCOMPARE(lines.details, kTitle);
        QCOMPARE(lines.state, kArtist);
        QVERIFY(lines.showArtwork);
        // Empty lines read as the reference's placeholders.
        const presence::PresenceLines empty = presence::presenceLines({}, {}, {}, {}, {});
        QCOMPARE(empty.details, QStringLiteral("Unknown title"));
        QCOMPARE(empty.state, QStringLiteral("Unknown artist"));
    }

    void privateCardNamesNothing()
    {
        PresenceOptions privateSong;
        privateSong.showSong = false;
        const presence::PresenceLines lines = presence::presenceLines(kTitle, kArtist, kAlbum, privateSong, {});
        QCOMPARE(lines.details, QStringLiteral("Listening to music"));
        QCOMPARE(lines.state, QStringLiteral("Song kept private"));
        QVERIFY(!lines.showArtwork);
    }

    void oneLineLayoutPutsTheAlbumUnderneath()
    {
        PresenceOptions oneLine;
        oneLine.layout = PresenceLayout::TitleArtistThenAlbum;
        const presence::PresenceLines lines = presence::presenceLines(kTitle, kArtist, kAlbum, oneLine, {});
        QCOMPARE(lines.details, kTitle + kDot + kArtist);
        QCOMPARE(lines.state, kAlbum);
        // A single with no album of its own keeps the artist on the second line.
        QCOMPARE(presence::presenceLines(kTitle, kArtist, kTitle, oneLine, {}).state, kArtist);
    }

    void castingNamesTheDeviceUnlessTurnedOff()
    {
        const QString tv = QStringLiteral("Living Room TV");
        QCOMPARE(presence::presenceLines(kTitle, kArtist, kAlbum, {}, tv).state, kArtist + kDot + QStringLiteral("on ") + tv);
        PresenceOptions noDevice;
        noDevice.showCastDevice = false;
        QCOMPARE(presence::presenceLines(kTitle, kArtist, kAlbum, noDevice, tv).state, kArtist);
    }

    void visibilityFollowsPauseAndSnooze()
    {
        const PresenceVisibility keep;
        QVERIFY(presence::shouldPublish(keep, 0, 5000));
        QVERIFY(presence::shouldPublish(keep, 1000, 5000));

        PresenceVisibility later;
        later.whenPaused = PausedBehavior::HideAfterDelay;
        QVERIFY(presence::shouldPublish(later, 1000, 1000 + presence::PausedHideDelaySeconds - 1));
        QVERIFY(!presence::shouldPublish(later, 1000, 1000 + presence::PausedHideDelaySeconds));
        QCOMPARE(presence::nextVisibilityChange(later, 1000, 1010).value_or(-1), 1000 + presence::PausedHideDelaySeconds);

        PresenceVisibility atOnce;
        atOnce.whenPaused = PausedBehavior::Hide;
        // The gap between two songs is not a pause.
        QVERIFY(presence::shouldPublish(atOnce, 1000, 1001));
        QVERIFY(!presence::shouldPublish(atOnce, 1000, 1000 + presence::PausedHideGraceSeconds));

        PresenceVisibility snoozed;
        snoozed.snoozeUntil = 2000;
        QVERIFY(!presence::shouldPublish(snoozed, 0, 1999));
        QVERIFY(presence::shouldPublish(snoozed, 0, 2000));
        QCOMPARE(presence::nextVisibilityChange(snoozed, 0, 1500).value_or(-1), qint64(2000));
        QVERIFY(!presence::nextVisibilityChange(keep, 0, 1500).has_value());
    }

    void choicesReadFromSettings()
    {
        QTemporaryDir folder;
        lmp::core::SettingsStore settings(folder.filePath(QStringLiteral("Settings.json")));
        namespace keys = lmp::core::settings::keys;
        // Nothing stored: the defaults.
        QVERIFY(presence::presenceOptionsFrom(settings).showSong);
        QCOMPARE(presence::presenceVisibilityFrom(settings).whenPaused, PausedBehavior::Show);

        settings.setBoolean(keys::DiscordShowSong, false);
        settings.setString(keys::DiscordLayout, QStringLiteral("titleArtistThenAlbum"));
        settings.setBoolean(keys::DiscordShowCastDevice, false);
        settings.setString(keys::DiscordWhenPaused, QStringLiteral("hideAfterDelay"));
        settings.setNumber(keys::DiscordSnoozeUntil, 1'800'000'000.0);
        const PresenceOptions options = presence::presenceOptionsFrom(settings);
        QVERIFY(!options.showSong);
        QCOMPARE(options.layout, PresenceLayout::TitleArtistThenAlbum);
        QVERIFY(!options.showCastDevice);
        const PresenceVisibility visibility = presence::presenceVisibilityFrom(settings);
        QCOMPARE(visibility.whenPaused, PausedBehavior::HideAfterDelay);
        QCOMPARE(visibility.snoozeUntil, qint64(1'800'000'000));

        // Unknown words and impossible times read as the defaults.
        settings.setString(keys::DiscordLayout, QStringLiteral("sideways"));
        settings.setString(keys::DiscordWhenPaused, QStringLiteral("sometimes"));
        settings.setNumber(keys::DiscordSnoozeUntil, -5.0);
        QCOMPARE(presence::presenceOptionsFrom(settings).layout, PresenceLayout::TitleThenArtist);
        QCOMPARE(presence::presenceVisibilityFrom(settings).whenPaused, PausedBehavior::Show);
        QCOMPARE(presence::presenceVisibilityFrom(settings).snoozeUntil, qint64(0));
    }
};

QTEST_GUILESS_MAIN(PresenceCardTest)
#include "tst_presence_card.moc"
