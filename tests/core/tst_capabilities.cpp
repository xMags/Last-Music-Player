#include <QByteArray>
#include <QObject>
#include <QSet>
#include <QString>
#include <QStringList>
#include <QTest>

#include <array>

#include "core/account/AccountSessionState.h"
#include "core/mode/Capabilities.h"

using lmp::core::account::AccountSessionState;
using lmp::core::mode::Capabilities;
using lmp::core::mode::CapabilityInputs;
using lmp::core::mode::isSourceModeAvailable;
using lmp::core::mode::isSourceModeSelectable;
using lmp::core::mode::ModeConditions;
using lmp::core::mode::SourceMode;

namespace {

struct Row {
    const char* name;
    bool Capabilities::*member;
    // Expected with every condition met, for LocalOnly, ApiKey and Account.
    std::array<bool, 3> ready;
};

// One entry per row of the capability matrix.
const std::array kRows{
    Row{"searchOnline", &Capabilities::searchOnline, {false, true, true}},
    Row{"browseCatalogue", &Capabilities::browseCatalogue, {false, false, true}},
    Row{"chooseRegion", &Capabilities::chooseRegion, {false, false, true}},
    Row{"showCatalogueHome", &Capabilities::showCatalogueHome, {false, false, true}},
    Row{"showOnlineMixes", &Capabilities::showOnlineMixes, {false, true, true}},
    Row{"showLyrics", &Capabilities::showLyrics, {false, true, true}},
    Row{"playRadio", &Capabilities::playRadio, {false, true, true}},
    Row{"streamRemoteSongs", &Capabilities::streamRemoteSongs, {false, true, true}},
    Row{"prefetchUpcomingSongs", &Capabilities::prefetchUpcomingSongs, {false, true, false}},
    Row{"downloadSongs", &Capabilities::downloadSongs, {false, true, true}},
    Row{"castToDevices", &Capabilities::castToDevices, {false, false, true}},
    Row{"openOnlineArtistPages", &Capabilities::openOnlineArtistPages, {false, true, true}},
    Row{"openOnlineAlbumPages", &Capabilities::openOnlineAlbumPages, {false, true, true}},
    Row{"loadArtistPictures", &Capabilities::loadArtistPictures, {false, true, true}},
    Row{"importFromLink", &Capabilities::importFromLink, {false, true, true}},
    Row{"followArtistsOnServer", &Capabilities::followArtistsOnServer, {false, false, true}},
    Row{"browseServerArtistList", &Capabilities::browseServerArtistList, {false, false, true}},
    Row{"editServerPlaylists", &Capabilities::editServerPlaylists, {false, false, true}},
    Row{"likeOnServer", &Capabilities::likeOnServer, {false, false, true}},
    Row{"recordServerHistory", &Capabilities::recordServerHistory, {false, false, true}},
    Row{"syncLibrary", &Capabilities::syncLibrary, {false, false, true}},
    Row{"importLocalHistory", &Capabilities::importLocalHistory, {false, false, true}},
    Row{"showAccountProfile", &Capabilities::showAccountProfile, {false, false, true}},
    Row{"lookUpPresenceArtwork", &Capabilities::lookUpPresenceArtwork, {false, true, false}},
    Row{"chooseLowQualityStreams", &Capabilities::chooseLowQualityStreams, {false, false, true}},
    Row{"lowQualityOnThisConnection", &Capabilities::lowQualityOnThisConnection, {false, false, true}},
};

constexpr std::array kModes{SourceMode::LocalOnly, SourceMode::ApiKey, SourceMode::Account};

[[nodiscard]] const char* modeLabel(SourceMode mode)
{
    switch (mode) {
    case SourceMode::ApiKey:
        return "ApiKey";
    case SourceMode::Account:
        return "Account";
    case SourceMode::LocalOnly:
        break;
    }
    return "LocalOnly";
}

[[nodiscard]] qsizetype modeIndex(SourceMode mode)
{
    return mode == SourceMode::LocalOnly ? 0 : (mode == SourceMode::ApiKey ? 1 : 2);
}

// Every condition met: a validated account on a build with a service, a
// configured provider listing both detail pages, online and metered.
[[nodiscard]] CapabilityInputs readyInputs(SourceMode mode)
{
    CapabilityInputs inputs;
    inputs.mode = mode;
    inputs.conditions = ModeConditions{true, AccountSessionState::Validated, true};
    inputs.providerCapabilities = {QStringLiteral("artist-detail-v1"), QStringLiteral("album-detail-v1")};
    inputs.connectivity = {true, true};
    return inputs;
}

// Checks every row: the rows named in `onlyTrue` are true, all others false.
void expectOnly(const CapabilityInputs& inputs, const QSet<QString>& onlyTrue)
{
    const Capabilities capabilities = Capabilities::compute(inputs);
    for (const Row& row : kRows) {
        const bool expected = onlyTrue.contains(QString::fromLatin1(row.name));
        QVERIFY2(capabilities.*row.member == expected, row.name);
    }
}

// The rows that are true with every condition met in `mode`, minus `removed`.
[[nodiscard]] QSet<QString> readyRowsExcept(SourceMode mode, const QStringList& removed = {})
{
    QSet<QString> rows;
    for (const Row& row : kRows) {
        const QString name = QString::fromLatin1(row.name);
        if (row.ready[static_cast<std::size_t>(modeIndex(mode))] && !removed.contains(name)) {
            rows.insert(name);
        }
    }
    return rows;
}

} // namespace

class CapabilitiesTest : public QObject {
    Q_OBJECT

private slots:
    void matrixWithEveryConditionMet_data();
    void matrixWithEveryConditionMet();
    void modeAvailability_data();
    void modeAvailability();
    void offlineSessionDoesNotSync();
    void accountWithoutSessionOffersNothing_data();
    void accountWithoutSessionOffersNothing();
    void buildWithoutServiceKeepsOnlyTheProfile_data();
    void buildWithoutServiceKeepsOnlyTheProfile();
    void unconfiguredProviderOffersNothing();
    void noNetworkStopsStreaming_data();
    void noNetworkStopsStreaming();
    void lowQualityOnlyOnMeteredConnections();
    void providerPagesFollowItsCapabilities();
    void providerCapabilitiesOnlyCountInApiKeyMode();
};

void CapabilitiesTest::matrixWithEveryConditionMet_data()
{
    QTest::addColumn<int>("row");
    QTest::addColumn<SourceMode>("mode");
    for (int row = 0; row < static_cast<int>(kRows.size()); ++row) {
        for (const SourceMode mode : kModes) {
            QTest::addRow("%s %s", kRows[static_cast<std::size_t>(row)].name, modeLabel(mode)) << row << mode;
        }
    }
}

void CapabilitiesTest::matrixWithEveryConditionMet()
{
    QFETCH(int, row);
    QFETCH(SourceMode, mode);
    const Row& entry = kRows[static_cast<std::size_t>(row)];
    const Capabilities capabilities = Capabilities::compute(readyInputs(mode));
    QCOMPARE(capabilities.*entry.member, entry.ready[static_cast<std::size_t>(modeIndex(mode))]);
}

void CapabilitiesTest::modeAvailability_data()
{
    QTest::addColumn<SourceMode>("mode");
    QTest::addColumn<bool>("service");
    QTest::addColumn<AccountSessionState>("session");
    QTest::addColumn<bool>("provider");
    QTest::addColumn<bool>("available");
    QTest::addColumn<bool>("selectable");
    using State = AccountSessionState;
    QTest::newRow("local always") << SourceMode::LocalOnly << false << State::SignedOut << false << true << true;
    QTest::newRow("api key configured") << SourceMode::ApiKey << false << State::SignedOut << true << true << true;
    QTest::newRow("api key unconfigured") << SourceMode::ApiKey << true << State::Validated << false << false << true;
    QTest::newRow("account validated") << SourceMode::Account << true << State::Validated << false << true << true;
    QTest::newRow("account offline") << SourceMode::Account << true << State::Offline << false << true << true;
    QTest::newRow("account signing in") << SourceMode::Account << true << State::SigningIn << true << false << false;
    QTest::newRow("account signed out") << SourceMode::Account << true << State::SignedOut << true << false << false;
    QTest::newRow("account without service") << SourceMode::Account << false << State::Validated << true << false
                                             << false;
    QTest::newRow("offline without service") << SourceMode::Account << false << State::Offline << true << false
                                             << false;
}

void CapabilitiesTest::modeAvailability()
{
    QFETCH(SourceMode, mode);
    QFETCH(bool, service);
    QFETCH(AccountSessionState, session);
    QFETCH(bool, provider);
    QFETCH(bool, available);
    QFETCH(bool, selectable);
    const ModeConditions conditions{service, session, provider};
    QCOMPARE(isSourceModeAvailable(mode, conditions), available);
    QCOMPARE(isSourceModeSelectable(mode, conditions), selectable);
}

void CapabilitiesTest::offlineSessionDoesNotSync()
{
    CapabilityInputs inputs = readyInputs(SourceMode::Account);
    inputs.conditions.sessionState = AccountSessionState::Offline;
    expectOnly(inputs, readyRowsExcept(SourceMode::Account,
                                       {QStringLiteral("syncLibrary"), QStringLiteral("importLocalHistory")}));
}

void CapabilitiesTest::accountWithoutSessionOffersNothing_data()
{
    QTest::addColumn<AccountSessionState>("session");
    QTest::newRow("signed out") << AccountSessionState::SignedOut;
    QTest::newRow("signing in") << AccountSessionState::SigningIn;
}

void CapabilitiesTest::accountWithoutSessionOffersNothing()
{
    QFETCH(AccountSessionState, session);
    CapabilityInputs inputs = readyInputs(SourceMode::Account);
    inputs.conditions.sessionState = session;
    expectOnly(inputs, {});
}

void CapabilitiesTest::buildWithoutServiceKeepsOnlyTheProfile_data()
{
    QTest::addColumn<AccountSessionState>("session");
    QTest::newRow("validated") << AccountSessionState::Validated;
    QTest::newRow("offline") << AccountSessionState::Offline;
}

void CapabilitiesTest::buildWithoutServiceKeepsOnlyTheProfile()
{
    QFETCH(AccountSessionState, session);
    CapabilityInputs inputs = readyInputs(SourceMode::Account);
    inputs.conditions.accountServiceAvailable = false;
    inputs.conditions.sessionState = session;
    expectOnly(inputs, {QStringLiteral("showAccountProfile")});
}

void CapabilitiesTest::unconfiguredProviderOffersNothing()
{
    CapabilityInputs inputs = readyInputs(SourceMode::ApiKey);
    inputs.conditions.providerConfigured = false;
    expectOnly(inputs, {});
}

void CapabilitiesTest::noNetworkStopsStreaming_data()
{
    QTest::addColumn<SourceMode>("mode");
    QTest::newRow("LocalOnly") << SourceMode::LocalOnly;
    QTest::newRow("ApiKey") << SourceMode::ApiKey;
    QTest::newRow("Account") << SourceMode::Account;
}

void CapabilitiesTest::noNetworkStopsStreaming()
{
    QFETCH(SourceMode, mode);
    CapabilityInputs inputs = readyInputs(mode);
    inputs.connectivity.online = false;
    expectOnly(inputs, readyRowsExcept(mode, {QStringLiteral("playRadio"), QStringLiteral("streamRemoteSongs"),
                                              QStringLiteral("prefetchUpcomingSongs")}));
}

void CapabilitiesTest::lowQualityOnlyOnMeteredConnections()
{
    CapabilityInputs inputs = readyInputs(SourceMode::Account);
    inputs.connectivity.metered = false;
    expectOnly(inputs, readyRowsExcept(SourceMode::Account, {QStringLiteral("lowQualityOnThisConnection")}));
}

void CapabilitiesTest::providerPagesFollowItsCapabilities()
{
    CapabilityInputs inputs = readyInputs(SourceMode::ApiKey);
    inputs.providerCapabilities = {QStringLiteral("artist-detail-v1")};
    expectOnly(inputs, readyRowsExcept(SourceMode::ApiKey, {QStringLiteral("openOnlineAlbumPages")}));

    inputs.providerCapabilities = {QStringLiteral("album-detail-v1"), QStringLiteral("search-v1")};
    expectOnly(inputs, readyRowsExcept(SourceMode::ApiKey, {QStringLiteral("openOnlineArtistPages")}));

    inputs.providerCapabilities.clear();
    expectOnly(inputs, readyRowsExcept(SourceMode::ApiKey, {QStringLiteral("openOnlineArtistPages"),
                                                            QStringLiteral("openOnlineAlbumPages")}));

    // The account service has its own pages, whatever a provider listed.
    CapabilityInputs account = readyInputs(SourceMode::Account);
    account.providerCapabilities.clear();
    expectOnly(account, readyRowsExcept(SourceMode::Account));
}

void CapabilitiesTest::providerCapabilitiesOnlyCountInApiKeyMode()
{
    expectOnly(readyInputs(SourceMode::LocalOnly), {});
}

QTEST_GUILESS_MAIN(CapabilitiesTest)
#include "tst_capabilities.moc"
