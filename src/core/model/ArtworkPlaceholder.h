#pragma once

#include <QString>

namespace lmp::core {

// What a cover shows while it has no picture: one of the artwork gradients,
// chosen from the item's id so the same item always gets the same colours.
struct ArtworkPlaceholder {
    // Index into a list of `gradientCount` gradients for `id`. A string hash
    // over UTF-16 code units, multiplied by 31 and wrapped to 32 bits, as the
    // other clients compute it, so an item looks the same everywhere.
    [[nodiscard]] static int gradientIndexForId(const QString& id, int gradientCount);

    // Up to two upper-case initials of a name: the first character of each
    // word, joined and cut to two. "L" for a blank name.
    [[nodiscard]] static QString initialsFor(const QString& name);
};

} // namespace lmp::core
