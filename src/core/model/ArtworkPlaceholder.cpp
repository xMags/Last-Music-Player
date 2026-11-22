#include "core/model/ArtworkPlaceholder.h"

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

QString ArtworkPlaceholder::initialsFor(const QString& name)
{
    const QStringList words = name.simplified().split(QLatin1Char(' '), Qt::SkipEmptyParts);
    if (words.isEmpty()) {
        return QStringLiteral("L");
    }
    QString initials;
    for (const QString& word : words) {
        initials.append(word.front());
    }
    // Cut before upper-casing: a letter that upper-cases to two (as the sharp
    // s does) still counts as one.
    return initials.left(2).toUpper();
}

QString ArtworkPlaceholder::tileLabelFor(const QString& title)
{
    const QString trimmed = title.trimmed();
    if (trimmed.isEmpty()) {
        return QStringLiteral("M");
    }
    return trimmed.left(1);
}

} // namespace lmp::core
