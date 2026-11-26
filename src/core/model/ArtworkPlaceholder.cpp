#include "core/model/ArtworkPlaceholder.h"

#include <algorithm>

#include <QChar>
#include <QStringList>

#include <cstdint>

namespace lmp::core {

int ArtworkPlaceholder::gradientIndexForId(const QString& id, int gradientCount)
{
    if (gradientCount <= 0) {
        return 0;
    }
    std::uint32_t hash = 0;
    for (const QChar unit : id) {
        // Unsigned arithmetic wraps at 32 bits, like `>>> 0` does.
        hash = hash * 31U + unit.unicode();
    }
    return static_cast<int>(hash % static_cast<std::uint32_t>(gradientCount));
}

namespace {

// The first character of `text` whole: a character outside the basic plane
// (most emoji) is two UTF-16 units, and half of one draws as a box.
[[nodiscard]] QString firstCharacter(const QString& text)
{
    if (text.size() >= 2 && text.at(0).isHighSurrogate() && text.at(1).isLowSurrogate()) {
        return text.left(2);
    }
    return text.left(1);
}

} // namespace

QString ArtworkPlaceholder::initialsFor(const QString& name)
{
    const QStringList words = name.simplified().split(QLatin1Char(' '), Qt::SkipEmptyParts);
    if (words.isEmpty()) {
        return QStringLiteral("L");
    }
    // The first two words' first characters. Taken before upper-casing: a
    // letter that upper-cases to two (as the sharp s does) still counts as one.
    QString initials;
    for (qsizetype index = 0; index < std::min<qsizetype>(2, words.size()); ++index) {
        initials.append(firstCharacter(words.at(index)));
    }
    return initials.toUpper();
}

} // namespace lmp::core
