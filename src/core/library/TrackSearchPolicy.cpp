#include "core/library/TrackSearchPolicy.h"

#include "core/text/CaseFold.h"

#include <QLatin1StringView>
#include <QSet>

#include <algorithm>
#include <array>

namespace lmp::core {

namespace {

constexpr std::array<QLatin1StringView, 12> CuratedLandingGenres{
    QLatin1StringView("Indie"), QLatin1StringView("Classical"), QLatin1StringView("Electronic"),
    QLatin1StringView("Ghazal"), QLatin1StringView("Jazz"), QLatin1StringView("Lo-fi"),
    QLatin1StringView("Pop"), QLatin1StringView("Hip-Hop"), QLatin1StringView("Rock"),
    QLatin1StringView("Chill"), QLatin1StringView("Bollywood"), QLatin1StringView("Dance"),
};

// Folded labels that media metadata often carries in its genre field but that
// do not name a genre (generic words and video category names).
constexpr std::array<QLatin1StringView, 27> NonGenreLabels{
    QLatin1StringView("song"),
    QLatin1StringView("music"),
    QLatin1StringView("audio"),
    QLatin1StringView("track"),
    QLatin1StringView("people & blogs"),
    QLatin1StringView("entertainment"),
    QLatin1StringView("news & politics"),
    QLatin1StringView("education"),
    QLatin1StringView("film & animation"),
    QLatin1StringView("gaming"),
    QLatin1StringView("howto & style"),
    QLatin1StringView("science & technology"),
    QLatin1StringView("sports"),
    QLatin1StringView("comedy"),
    QLatin1StringView("travel & events"),
    QLatin1StringView("autos & vehicles"),
    QLatin1StringView("pets & animals"),
    QLatin1StringView("nonprofits & activism"),
    QLatin1StringView("trailers"),
    QLatin1StringView("shows"),
    QLatin1StringView("movies"),
    QLatin1StringView("remote"),
    QLatin1StringView("other"),
    QLatin1StringView("various"),
    QLatin1StringView("various artists"),
    QLatin1StringView("unknown"),
    QLatin1StringView("unknown genre"),
};

bool isUsefulGenre(const QString& foldedName)
{
    return !foldedName.isEmpty() && foldedName != QLatin1StringView("uncategorized")
        && std::find(NonGenreLabels.begin(), NonGenreLabels.end(), foldedName) == NonGenreLabels.end();
}

} // namespace

QList<BrowseLandingCategory> browseLandingCategories(const QList<QString>& rankedGenres)
{
    QList<BrowseLandingCategory> categories;
    categories.reserve(BrowseLandingCategoryLimit);
    QSet<QString> usedNames;

    const auto append = [&categories, &usedNames](const QString& rawName, bool curated) {
        if (categories.size() >= BrowseLandingCategoryLimit) {
            return;
        }
        QString name = rawName.trimmed();
        const QString folded = foldCaseAscii(name);
        if (!isUsefulGenre(folded) || usedNames.contains(folded)) {
            return;
        }
        usedNames.insert(folded);
        categories.append({std::move(name), curated});
    };

    for (const QString& genre : rankedGenres) {
        append(genre, false);
        if (categories.size() >= BrowseLandingCategoryLimit) {
            return categories;
        }
    }

    for (const QLatin1StringView genre : CuratedLandingGenres) {
        append(QString(genre), true);
    }
    return categories;
}

} // namespace lmp::core
