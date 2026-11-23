#pragma once

#include "core/account/AccountSessionState.h"
#include "core/mode/SourceMode.h"

#include <QMetaType>
#include <QObject>
#include <QStringList>

namespace lmp::core::mode {

// What the mode rules need to know about the two remote sources.
struct ModeConditions {
    // The build has an account service to sign in to.
    bool accountServiceAvailable = false;
    account::AccountSessionState sessionState = account::AccountSessionState::SignedOut;
    // A provider base address is saved and its API key is stored.
    bool providerConfigured = false;
};

// Whether a mode can serve requests now. LocalOnly always; ApiKey with a
// configured provider; Account with an account service and a session that is
// validated or offline.
[[nodiscard]] bool isSourceModeAvailable(SourceMode mode, const ModeConditions& conditions);

// Whether a mode may be entered. ApiKey always: its address and key are
// typed into the card that entering the mode shows. The others only when
// available: an account session comes from a browser sign-in, which commits
// the switch itself.
[[nodiscard]] bool isSourceModeSelectable(SourceMode mode, const ModeConditions& conditions);

// The state of this machine's network.
struct Connectivity {
    bool online = true;
    // The connection costs money per byte (fixed or variable cost, roaming
    // or over its data limit).
    bool metered = false;
};

struct CapabilityInputs {
    SourceMode mode = SourceMode::LocalOnly;
    ModeConditions conditions;
    // From the provider's status answer; only read in ApiKey mode.
    QStringList providerCapabilities;
    Connectivity connectivity;
};

// What the user can do right now. Computed only by compute(); no other code
// compares the mode to decide what is offered. A row that is false hides or
// refuses that feature; per-item rules (is this song remote, is a setting on)
// stay with the feature.
struct Capabilities {
    Q_GADGET
    // Read in QML as App.capabilities.<row>.
    Q_PROPERTY(bool searchOnline MEMBER searchOnline FINAL)
    Q_PROPERTY(bool browseCatalogue MEMBER browseCatalogue FINAL)
    Q_PROPERTY(bool chooseRegion MEMBER chooseRegion FINAL)
    Q_PROPERTY(bool showCatalogueHome MEMBER showCatalogueHome FINAL)
    Q_PROPERTY(bool showOnlineMixes MEMBER showOnlineMixes FINAL)
    Q_PROPERTY(bool showLyrics MEMBER showLyrics FINAL)
    Q_PROPERTY(bool playRadio MEMBER playRadio FINAL)
    Q_PROPERTY(bool streamRemoteSongs MEMBER streamRemoteSongs FINAL)
    Q_PROPERTY(bool prefetchUpcomingSongs MEMBER prefetchUpcomingSongs FINAL)
    Q_PROPERTY(bool downloadSongs MEMBER downloadSongs FINAL)
    Q_PROPERTY(bool castToDevices MEMBER castToDevices FINAL)
    Q_PROPERTY(bool createShareLinks MEMBER createShareLinks FINAL)
    Q_PROPERTY(bool openOnlineArtistPages MEMBER openOnlineArtistPages FINAL)
    Q_PROPERTY(bool openOnlineAlbumPages MEMBER openOnlineAlbumPages FINAL)
    Q_PROPERTY(bool loadArtistPictures MEMBER loadArtistPictures FINAL)
    Q_PROPERTY(bool importFromLink MEMBER importFromLink FINAL)
    Q_PROPERTY(bool followArtistsOnServer MEMBER followArtistsOnServer FINAL)
    Q_PROPERTY(bool browseServerArtistList MEMBER browseServerArtistList FINAL)
    Q_PROPERTY(bool editServerPlaylists MEMBER editServerPlaylists FINAL)
    Q_PROPERTY(bool likeOnServer MEMBER likeOnServer FINAL)
    Q_PROPERTY(bool recordServerHistory MEMBER recordServerHistory FINAL)
    Q_PROPERTY(bool syncLibrary MEMBER syncLibrary FINAL)
    Q_PROPERTY(bool importLocalHistory MEMBER importLocalHistory FINAL)
    Q_PROPERTY(bool showAccountProfile MEMBER showAccountProfile FINAL)
    Q_PROPERTY(bool lookUpPresenceArtwork MEMBER lookUpPresenceArtwork FINAL)
    Q_PROPERTY(bool chooseLowQualityStreams MEMBER chooseLowQualityStreams FINAL)
    Q_PROPERTY(bool lowQualityOnThisConnection MEMBER lowQualityOnThisConnection FINAL)
    Q_PROPERTY(bool controlOtherDevices MEMBER controlOtherDevices FINAL)

public:
    // Search results from the provider or the account service, beside the
    // local ones.
    bool searchOnline = false;
    // Catalogue shelves, discovery, charts and catalogue pages.
    bool browseCatalogue = false;
    // Pick the catalogue region.
    bool chooseRegion = false;
    // The catalogue section of Home.
    bool showCatalogueHome = false;
    // Mixes made by the provider or the account service on Home.
    bool showOnlineMixes = false;
    bool showLyrics = false;
    // Keep playing related songs when the queue ends.
    bool playRadio = false;
    // Play songs that have no downloaded file.
    bool streamRemoteSongs = false;
    // Fetch the next songs into the stream cache ahead of time.
    bool prefetchUpcomingSongs = false;
    // Download remote songs for offline listening.
    bool downloadSongs = false;
    // Play to a Cast device. Never in ApiKey mode: the device would receive
    // addresses that carry the key.
    bool castToDevices = false;
    // Links to songs, lists and artists, made by the account service.
    bool createShareLinks = false;
    // Artist and album pages from the remote source instead of the local
    // library.
    bool openOnlineArtistPages = false;
    bool openOnlineAlbumPages = false;
    // Artist pictures on artist pages.
    bool loadArtistPictures = false;
    // Import an album or playlist from a pasted link.
    bool importFromLink = false;
    // Account library kept on the service.
    bool followArtistsOnServer = false;
    bool browseServerArtistList = false;
    bool editServerPlaylists = false;
    bool likeOnServer = false;
    bool recordServerHistory = false;
    // History, likes, preferences and library synchronised with the service.
    bool syncLibrary = false;
    // Offer to bring the local listening history into the account.
    bool importLocalHistory = false;
    // The profile button shows the account's name and picture.
    bool showAccountProfile = false;
    // The presence feature asks the provider for cover pictures.
    bool lookUpPresenceArtwork = false;
    // The data saver setting is offered.
    bool chooseLowQualityStreams = false;
    // Streams are asked for in low quality when that setting is on.
    bool lowQualityOnThisConnection = false;
    // The live link to the account's other devices: see and control the one
    // that plays, and move playback between them. Needs a confirmed session
    // and a network.
    bool controlOtherDevices = false;

    [[nodiscard]] static Capabilities compute(const CapabilityInputs& inputs);

    friend bool operator==(const Capabilities&, const Capabilities&) = default;
};

} // namespace lmp::core::mode

Q_DECLARE_METATYPE(lmp::core::mode::Capabilities)
