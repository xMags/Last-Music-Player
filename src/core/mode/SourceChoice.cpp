#include "core/mode/SourceChoice.h"

namespace lmp::core::mode {

bool shouldOfferSourceChoice(const SourceChoiceInputs& inputs)
{
    if (inputs.choiceSeen) {
        return false;
    }
    if (inputs.mode != SourceMode::LocalOnly) {
        return false;
    }
    // Shown before and left open: the mode was stored by that launch.
    if (inputs.choiceSeenStored) {
        return true;
    }
    return !inputs.modeStoredBefore && !inputs.libraryHasSongs && !inputs.sessionStored;
}

} // namespace lmp::core::mode
