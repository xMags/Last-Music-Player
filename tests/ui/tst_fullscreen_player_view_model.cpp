#include <QObject>
#include <QSignalSpy>
#include <QStringList>
#include <QTest>

#include <memory>

#include "../core/PlayerFixture.h"
#include "core/settings/SettingKeys.h"
#include "ui/player/FullscreenPlayerViewModel.h"
#include "ui/shell/ShellLayoutViewModel.h"
#include "ui/toasts/ToastQueue.h"

using lmp::core::PlayerController;
using lmp::core::Track;
using lmp::ui::FullscreenPlayerViewModel;
namespace keys = lmp::core::settings::keys;

class FullscreenPlayerViewModelTest : public QObject {
    Q_OBJECT

private slots:
    void init();
    void cleanup();

    void lyricsIsTheDefaultPanel();
    void readsTheStoredPanel();
    void pickingTheShownPanelHidesItAndEveryChoiceIsStored();
    void opensOnlyWithASong();
    void theSidePanelNeedsMoreThan900Pixels();
    void albumLineJoinsAlbumAndYear();
    void upcomingListsTheVisibleQueue();

private:
    [[nodiscard]] std::unique_ptr<FullscreenPlayerViewModel> makeViewModel();
    void play(const QStringList& names);

    std::unique_ptr<PlayerFixture> m_fixture;
    std::unique_ptr<PlayerController> m_player;
    std::unique_ptr<lmp::ui::ShellLayoutViewModel> m_layout;
    std::unique_ptr<lmp::ui::ToastQueue> m_toasts;
};

void FullscreenPlayerViewModelTest::init()
{
    m_fixture = std::make_unique<PlayerFixture>();
    QVERIFY(m_fixture->ok());
    m_player = m_fixture->makeController();
    m_layout = std::make_unique<lmp::ui::ShellLayoutViewModel>();
    m_toasts = std::make_unique<lmp::ui::ToastQueue>();
}

void FullscreenPlayerViewModelTest::cleanup()
{
    m_toasts.reset();
    m_layout.reset();
    m_player.reset();
    m_fixture.reset();
}

std::unique_ptr<FullscreenPlayerViewModel> FullscreenPlayerViewModelTest::makeViewModel()
{
    return std::make_unique<FullscreenPlayerViewModel>(*m_player, m_fixture->settings, *m_layout, *m_toasts);
}

void FullscreenPlayerViewModelTest::play(const QStringList& names)
{
    const QList<Track> songs = m_fixture->songs(names);
    m_player->playInContext(songs.first(), songs);
    settle();
}

void FullscreenPlayerViewModelTest::lyricsIsTheDefaultPanel()
{
    const auto fullscreen = makeViewModel();
    QCOMPARE(fullscreen->panel(), FullscreenPlayerViewModel::Panel::Lyrics);
    QVERIFY(fullscreen->lyricsChosen());
    QVERIFY(!fullscreen->queueChosen());
    QVERIFY(!fullscreen->isOpen());
}

void FullscreenPlayerViewModelTest::readsTheStoredPanel()
{
    m_fixture->settings.setString(keys::FullscreenPanel, QStringLiteral("queue"));
    QCOMPARE(makeViewModel()->panel(), FullscreenPlayerViewModel::Panel::Queue);
    m_fixture->settings.setString(keys::FullscreenPanel, QStringLiteral("none"));
    QCOMPARE(makeViewModel()->panel(), FullscreenPlayerViewModel::Panel::None);
    m_fixture->settings.setString(keys::FullscreenPanel, QStringLiteral("Queue"));
    QCOMPARE(makeViewModel()->panel(), FullscreenPlayerViewModel::Panel::Lyrics);
    m_fixture->settings.setInteger(keys::FullscreenPanel, 2);
    QCOMPARE(makeViewModel()->panel(), FullscreenPlayerViewModel::Panel::Lyrics);
}

void FullscreenPlayerViewModelTest::pickingTheShownPanelHidesItAndEveryChoiceIsStored()
{
    const auto fullscreen = makeViewModel();
    QSignalSpy changed(fullscreen.get(), &FullscreenPlayerViewModel::panelChanged);
    fullscreen->pickLyrics();
    QCOMPARE(fullscreen->panel(), FullscreenPlayerViewModel::Panel::None);
    QCOMPARE(m_fixture->settings.string(keys::FullscreenPanel), QStringLiteral("none"));
    fullscreen->pickQueue();
    QCOMPARE(fullscreen->panel(), FullscreenPlayerViewModel::Panel::Queue);
    QCOMPARE(m_fixture->settings.string(keys::FullscreenPanel), QStringLiteral("queue"));
    fullscreen->pickLyrics();
    QCOMPARE(fullscreen->panel(), FullscreenPlayerViewModel::Panel::Lyrics);
    QCOMPARE(m_fixture->settings.string(keys::FullscreenPanel), QStringLiteral("lyrics"));
    QCOMPARE(changed.count(), 3);

    // The file on disk holds it for the next launch.
    lmp::core::SettingsStore reread(m_fixture->settingsPath());
    QCOMPARE(reread.string(keys::FullscreenPanel), QStringLiteral("lyrics"));
}

void FullscreenPlayerViewModelTest::opensOnlyWithASong()
{
    const auto fullscreen = makeViewModel();
    fullscreen->openPlayer();
    QVERIFY(!fullscreen->isOpen());
    QCOMPARE(m_toasts->count(), 1);

    play({QStringLiteral("One")});
    QSignalSpy open(fullscreen.get(), &FullscreenPlayerViewModel::openChanged);
    fullscreen->toggle();
    QVERIFY(fullscreen->isOpen());
    fullscreen->openPlayer();
    QCOMPARE(open.count(), 1);
    fullscreen->toggle();
    QVERIFY(!fullscreen->isOpen());
    fullscreen->openPlayer();
    fullscreen->close();
    QVERIFY(!fullscreen->isOpen());
    QCOMPARE(open.count(), 4);
    QCOMPARE(m_toasts->count(), 1);
}

void FullscreenPlayerViewModelTest::theSidePanelNeedsMoreThan900Pixels()
{
    const auto fullscreen = makeViewModel();
    m_layout->setWindowWidth(900);
    QVERIFY(!fullscreen->panelFits());
    QVERIFY(!fullscreen->sidePanelShown());
    QSignalSpy changed(fullscreen.get(), &FullscreenPlayerViewModel::panelChanged);
    m_layout->setWindowWidth(901);
    QVERIFY(changed.count() >= 1);
    QVERIFY(fullscreen->panelFits());
    QVERIFY(fullscreen->sidePanelShown());
    fullscreen->pickLyrics(); // hides it
    QVERIFY(fullscreen->panelFits());
    QVERIFY(!fullscreen->sidePanelShown());
}

void FullscreenPlayerViewModelTest::albumLineJoinsAlbumAndYear()
{
    const auto fullscreen = makeViewModel();
    QVERIFY(fullscreen->albumLine().isEmpty());
    Track track = m_fixture->song(QStringLiteral("One"));
    track.album = QStringLiteral("  Album  ");
    track.year = 2021;
    m_player->playTrack(track);
    settle();
    QCOMPARE(fullscreen->albumLine(), QStringLiteral("Album · 2021"));
    track.year = 0;
    m_player->playTrack(track);
    settle();
    QCOMPARE(fullscreen->albumLine(), QStringLiteral("Album"));
}

void FullscreenPlayerViewModelTest::upcomingListsTheVisibleQueue()
{
    const auto fullscreen = makeViewModel();
    QVERIFY(!fullscreen->hasUpcoming());
    QSignalSpy upcoming(fullscreen.get(), &FullscreenPlayerViewModel::upcomingChanged);
    play({QStringLiteral("One"), QStringLiteral("Two"), QStringLiteral("Three")});
    QVERIFY(fullscreen->hasUpcoming());
    QVERIFY(upcoming.count() >= 1);
    QAbstractItemModel* model = fullscreen->upcoming();
    QCOMPARE(model->rowCount(), 2);
    QCOMPARE(model->data(model->index(0, 0)).toString(), QStringLiteral("Two · Artist"));
}

QTEST_GUILESS_MAIN(FullscreenPlayerViewModelTest)
#include "tst_fullscreen_player_view_model.moc"
