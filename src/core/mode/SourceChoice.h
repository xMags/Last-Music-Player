#pragma once

#include "core/mode/SourceMode.h"

namespace lmp::core::mode {

// What decides whether the first-run "Choose your music source" page shows.
// Everything is read at launch, before the mode service stores its first-run
// default.
struct SourceChoiceInputs {
    // The SourceChoiceSeen setting is present; and its value.
    bool choiceSeenStored = false;
    bool choiceSeen = false;
    // RemoteAccessMode held a mode before this launch.
    bool modeStoredBefore = false;
    // The mode the service settled on at launch.
    SourceMode mode = SourceMode::LocalOnly;
    // The library database has songs.
    bool libraryHasSongs = false;
    // An account session is stored.
    bool sessionStored = false;
};

// The page shows once, to someone who never chose a source: no mode was ever
// stored, the default came out as LocalOnly (no provider is set up), the
// library is empty and no account session is stored. Anyone upgrading from
// an earlier version has a stored mode and never sees it. A page that was
// shown but left without a choice (SourceChoiceSeen stored false) shows
// again while the source is still LocalOnly. Once SourceChoiceSeen is true it
// never shows.
[[nodiscard]] bool shouldOfferSourceChoice(const SourceChoiceInputs& inputs);

} // namespace lmp::core::mode
