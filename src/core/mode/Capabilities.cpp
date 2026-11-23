#include "core/mode/Capabilities.h"

#include "core/provider/ProviderClient.h"

namespace lmp::core::mode {

namespace {

using account::AccountSessionState;

// Validated and offline sessions both count: the offline one still has its
// owner and the cached library that belongs to it.
[[nodiscard]] bool hasSignedInSession(AccountSessionState state)
{
    return state == AccountSessionState::Validated || state == AccountSessionState::Offline;
}

} // namespace

bool isSourceModeAvailable(SourceMode mode, const ModeConditions& conditions)
{
    switch (mode) {
    case SourceMode::Account:
        return conditions.accountServiceAvailable && hasSignedInSession(conditions.sessionState);
    case SourceMode::ApiKey:
        return conditions.providerConfigured;
    case SourceMode::LocalOnly:
        break;
    }
    return true;
}

bool isSourceModeSelectable(SourceMode mode, const ModeConditions& conditions)
{
    return mode == SourceMode::ApiKey || isSourceModeAvailable(mode, conditions);
}

Capabilities Capabilities::compute(const CapabilityInputs& inputs)
{
    const bool available = isSourceModeAvailable(inputs.mode, inputs.conditions);
    const bool account = inputs.mode == SourceMode::Account && available;
    const bool provider = inputs.mode == SourceMode::ApiKey && available;
    const bool remote = account || provider;
    // Background sync and the history offer need a session the service has
    // just confirmed; an offline one would only record failures.
    const bool accountValidated = account && inputs.conditions.sessionState == AccountSessionState::Validated;
    const bool online = inputs.connectivity.online;

    Capabilities result;
    result.searchOnline = remote;
    result.browseCatalogue = account;
    result.chooseRegion = account;
    result.showCatalogueHome = account;
    result.showOnlineMixes = remote;
    result.showLyrics = remote;
    // Without a network a stream can only fail; downloaded songs still play.
    result.playRadio = remote && online;
    result.streamRemoteSongs = remote && online;
    result.prefetchUpcomingSongs = provider && online;
    result.downloadSongs = remote;
    result.castToDevices = account;
    result.createShareLinks = account;
    // The account service always has these pages; a provider only when its
    // status answer lists them.
    result.openOnlineArtistPages =
        account || (provider && inputs.providerCapabilities.contains(provider::ArtistDetailCapability));
    result.openOnlineAlbumPages =
        account || (provider && inputs.providerCapabilities.contains(provider::AlbumDetailCapability));
    result.loadArtistPictures = remote;
    result.importFromLink = remote;
    result.followArtistsOnServer = account;
    result.browseServerArtistList = account;
    result.editServerPlaylists = account;
    result.likeOnServer = account;
    result.recordServerHistory = account;
    result.syncLibrary = accountValidated;
    result.importLocalHistory = accountValidated;
    // The cached profile is the one the user last signed in as, also in a
    // build that cannot reach the service.
    result.showAccountProfile =
        inputs.mode == SourceMode::Account && hasSignedInSession(inputs.conditions.sessionState);
    result.lookUpPresenceArtwork = provider;
    result.chooseLowQualityStreams = account;
    result.lowQualityOnThisConnection = account && inputs.connectivity.metered;
    result.controlOtherDevices = accountValidated && online;
    return result;
}

} // namespace lmp::core::mode
