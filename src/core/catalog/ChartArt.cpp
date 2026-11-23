#include "core/catalog/ChartArt.h"

#include <QHash>
#include <QList>
#include <QPair>

#include <algorithm>

namespace lmp::core::catalog {

namespace {

[[nodiscard]] GenreArt art(const char* motif, const char* tone)
{
    return GenreArt{QString::fromLatin1(motif), QString::fromLatin1(tone)};
}

const QHash<QString, GenreArt>& genreTable()
{
    static const QHash<QString, GenreArt> table{
        {QStringLiteral("bollywood"), art("marquee", "bollywood")},
        {QStringLiteral("soundtrack"), art("marquee", "bollywood")},
        {QStringLiteral("indian classical"), art("sitar", "raga")},
        {QStringLiteral("carnatic"), art("sitar", "raga")},
        {QStringLiteral("hindustani"), art("sitar", "raga")},
        {QStringLiteral("devotional spiritual"), art("lotus", "devotional")},
        {QStringLiteral("devotional"), art("lotus", "devotional")},
        {QStringLiteral("spiritual"), art("lotus", "devotional")},
        {QStringLiteral("bhajan"), art("lotus", "devotional")},
        {QStringLiteral("classical"), art("piano", "classical")},
        {QStringLiteral("opera"), art("violin", "classical")},
        {QStringLiteral("new age"), art("harp", "classical")},
        {QStringLiteral("instrumental"), art("harp", "classical")},
        {QStringLiteral("ambient"), art("harp", "classical")},
        {QStringLiteral("easy listening"), art("harp", "classical")},
        {QStringLiteral("jazz"), art("cymbal", "jazz")},
        {QStringLiteral("blues"), art("cymbal", "jazz")},
        {QStringLiteral("alternative"), art("guitar", "alternative")},
        {QStringLiteral("rock"), art("amp", "alternative")},
        {QStringLiteral("metal"), art("pick", "alternative")},
        {QStringLiteral("punk"), art("pick", "alternative")},
        {QStringLiteral("pop"), art("bubbles", "pop")},
        {QStringLiteral("indian pop"), art("paisley", "indian-pop")},
        {QStringLiteral("k pop"), art("bubbles", "pop")},
        {QStringLiteral("singer songwriter"), art("bubbles", "pop")},
        {QStringLiteral("hip hop rap"), art("cassette", "hiphop")},
        {QStringLiteral("hip hop"), art("cassette", "hiphop")},
        {QStringLiteral("rap"), art("cassette", "hiphop")},
        {QStringLiteral("r b soul"), art("ribbon", "soul")},
        {QStringLiteral("reggae"), art("cassette", "hiphop")},
        {QStringLiteral("electronic"), art("horizon", "electronic")},
        {QStringLiteral("house"), art("horizon", "electronic")},
        {QStringLiteral("techno"), art("horizon", "electronic")},
        {QStringLiteral("dance"), art("spotlights", "dance")},
        {QStringLiteral("christmas"), art("bauble", "christmas")},
        {QStringLiteral("holiday"), art("bauble", "christmas")},
        {QStringLiteral("country"), art("hills", "country")},
        {QStringLiteral("folk"), art("hills", "country")},
        {QStringLiteral("americana"), art("hills", "country")},
        {QStringLiteral("punjabi"), art("dhol", "punjabi")},
        {QStringLiteral("bhangra"), art("dhol", "punjabi")},
        {QStringLiteral("tamil"), art("marquee", "bollywood")},
        {QStringLiteral("telugu"), art("marquee", "bollywood")},
        {QStringLiteral("kannada"), art("marquee", "bollywood")},
        {QStringLiteral("malayalam"), art("marquee", "bollywood")},
        {QStringLiteral("marathi"), art("marquee", "bollywood")},
        {QStringLiteral("bengali"), art("marquee", "bollywood")},
    };
    return table;
}

// The most specific word first, so "Indian Pop" is not swallowed by "Pop".
const QList<QPair<QString, GenreArt>>& genreKeywords()
{
    static const QList<QPair<QString, GenreArt>> keywords{
        {QStringLiteral("christmas"), art("bauble", "christmas")},
        {QStringLiteral("holiday"), art("bauble", "christmas")},
        {QStringLiteral("devotional"), art("lotus", "devotional")},
        {QStringLiteral("spiritual"), art("lotus", "devotional")},
        {QStringLiteral("bhakti"), art("lotus", "devotional")},
        {QStringLiteral("indian classical"), art("sitar", "raga")},
        {QStringLiteral("bollywood"), art("marquee", "bollywood")},
        {QStringLiteral("soundtrack"), art("marquee", "bollywood")},
        {QStringLiteral("hip hop"), art("cassette", "hiphop")},
        {QStringLiteral("rap"), art("cassette", "hiphop")},
        {QStringLiteral("soul"), art("ribbon", "soul")},
        {QStringLiteral("punjabi"), art("dhol", "punjabi")},
        {QStringLiteral("bhangra"), art("dhol", "punjabi")},
        {QStringLiteral("reggae"), art("cassette", "hiphop")},
        {QStringLiteral("electronic"), art("horizon", "electronic")},
        {QStringLiteral("dance"), art("spotlights", "dance")},
        {QStringLiteral("classical"), art("piano", "classical")},
        {QStringLiteral("opera"), art("violin", "classical")},
        {QStringLiteral("new age"), art("harp", "classical")},
        {QStringLiteral("instrumental"), art("harp", "classical")},
        {QStringLiteral("ambient"), art("harp", "classical")},
        {QStringLiteral("jazz"), art("cymbal", "jazz")},
        {QStringLiteral("blues"), art("cymbal", "jazz")},
        {QStringLiteral("metal"), art("pick", "alternative")},
        {QStringLiteral("punk"), art("pick", "alternative")},
        {QStringLiteral("rock"), art("amp", "alternative")},
        {QStringLiteral("alternative"), art("guitar", "alternative")},
        {QStringLiteral("country"), art("hills", "country")},
        {QStringLiteral("folk"), art("hills", "country")},
        {QStringLiteral("pop"), art("bubbles", "pop")},
    };
    return keywords;
}

[[nodiscard]] ChartPalette palette(const char* bg1, const char* bg2, const char* accent, const char* accent2)
{
    return ChartPalette{QString::fromLatin1(bg1), QString::fromLatin1(bg2), QString::fromLatin1(accent),
                        QString::fromLatin1(accent2)};
}

} // namespace

QString chartIdentity(const ChartRef& chart)
{
    return chart.region + QLatin1Char(':') + chartKindName(chart.kind) + QLatin1Char(':')
        + resourceTypeName(chart.resourceType) + QLatin1Char(':') + chart.id;
}

std::uint32_t chartStableHash(const QString& value)
{
    std::uint32_t hash = 2166136261U;
    for (const QChar unit : value) {
        hash ^= unit.unicode();
        hash *= 16777619U;
    }
    return hash;
}

QString normalizeChartName(const QString& value)
{
    const QString decomposed = value.normalized(QString::NormalizationForm_KD);
    QString result;
    bool space = false;
    for (const QChar character : decomposed) {
        // U+0300 to U+036F: the combining marks the reference drops.
        if (character.unicode() >= 0x0300 && character.unicode() <= 0x036F) {
            continue;
        }
        const QChar lower = character.toLower();
        const bool kept = (lower >= QLatin1Char('a') && lower <= QLatin1Char('z'))
            || (lower >= QLatin1Char('0') && lower <= QLatin1Char('9'));
        if (!kept) {
            space = true;
            continue;
        }
        if (space && !result.isEmpty()) {
            result.append(QLatin1Char(' '));
        }
        space = false;
        result.append(lower);
    }
    return result;
}

std::optional<GenreArt> resolveGenreArt(const QString& name)
{
    const QString normalized = normalizeChartName(name);
    if (normalized.isEmpty()) {
        return std::nullopt;
    }
    const auto exact = genreTable().constFind(normalized);
    if (exact != genreTable().cend()) {
        return *exact;
    }
    for (const auto& [keyword, entry] : genreKeywords()) {
        if (normalized.contains(keyword)) {
            return entry;
        }
    }
    return std::nullopt;
}

QString resolveCityLandmark(const QString& name)
{
    static const QHash<QString, QString> aliases{
        {QStringLiteral("mumbai"), QStringLiteral("mumbai")},
        {QStringLiteral("bombay"), QStringLiteral("mumbai")},
        {QStringLiteral("delhi"), QStringLiteral("delhi")},
        {QStringLiteral("new delhi"), QStringLiteral("delhi")},
        {QStringLiteral("bengaluru"), QStringLiteral("bengaluru")},
        {QStringLiteral("bangalore"), QStringLiteral("bengaluru")},
        {QStringLiteral("new york"), QStringLiteral("new-york")},
        {QStringLiteral("new york city"), QStringLiteral("new-york")},
        {QStringLiteral("nyc"), QStringLiteral("new-york")},
        {QStringLiteral("london"), QStringLiteral("london")},
        {QStringLiteral("los angeles"), QStringLiteral("los-angeles")},
        {QStringLiteral("la"), QStringLiteral("los-angeles")},
    };
    return aliases.value(normalizeChartName(name), QStringLiteral("generic"));
}

ChartVisual resolveChartVisual(const ChartDescriptor& chart)
{
    ChartVisual visual;
    visual.identity = chartIdentity(chart.ref);
    const std::uint32_t seed = chartStableHash(visual.identity);
    visual.paletteIndex = static_cast<int>(seed % 6);
    visual.patternIndex = static_cast<int>((seed / 7) % 3);
    visual.kindLabel = chart.ref.kind == ChartKind::City
        ? QStringLiteral("City chart")
        : QStringLiteral("Top ") + resourceTypePlural(chart.ref.resourceType);
    if (chart.ref.kind == ChartKind::City) {
        visual.family = ChartFamily::City;
        visual.visualKey = resolveCityLandmark(chart.name);
        return visual;
    }
    if (chart.ref.kind == ChartKind::Genre) {
        visual.family = ChartFamily::Genre;
        if (const std::optional<GenreArt> genre = resolveGenreArt(chart.name)) {
            visual.visualKey = QStringLiteral("genre-") + genre->motif;
            visual.tone = genre->tone;
        } else {
            visual.visualKey = QStringLiteral("genre-%1").arg(visual.patternIndex);
        }
        return visual;
    }
    // There is only one region card, the way into the whole row: one palette.
    visual.family = ChartFamily::Global;
    visual.visualKey = QStringLiteral("region");
    visual.tone = QStringLiteral("region");
    return visual;
}

QString chartCardSubtitle(const ChartDescriptor& chart, const ChartVisual& visual)
{
    if (visual.family != ChartFamily::City) {
        return {};
    }
    return chart.subtitle.isEmpty() ? chart.title : chart.subtitle;
}

ChartPalette chartPalette(const ChartVisual& visual)
{
    static const QHash<QString, ChartPalette> tones{
        {QStringLiteral("bollywood"), palette("#e0437c", "#45115c", "#ffd166", "#ff7a59")},
        {QStringLiteral("raga"), palette("#6d3fa8", "#21123f", "#f2c86b", "#ff8fa3")},
        {QStringLiteral("devotional"), palette("#f0912a", "#7a1f10", "#ffd98a", "#fff1c7")},
        {QStringLiteral("classical"), palette("#2f4a8c", "#131a3a", "#e8c987", "#9db7ff")},
        {QStringLiteral("jazz"), palette("#c9761f", "#40203f", "#ffcf7a", "#fff0c9")},
        {QStringLiteral("alternative"), palette("#c02f3c", "#241f2e", "#ff9a6c", "#ffd36e")},
        {QStringLiteral("pop"), palette("#e5488f", "#4b2270", "#ffc2e2", "#7ee6ff")},
        {QStringLiteral("indian-pop"), palette("#e8556a", "#6a1f52", "#ffd6a0", "#2ec4b6")},
        {QStringLiteral("hiphop"), palette("#d97a26", "#24242c", "#ffc857", "#ff5d73")},
        {QStringLiteral("electronic"), palette("#1f9bc9", "#251a63", "#7ef0ff", "#ff5fd2")},
        {QStringLiteral("dance"), palette("#8b3fd4", "#1d1a55", "#8ff0e0", "#ff8ad8")},
        {QStringLiteral("christmas"), palette("#17734d", "#10303a", "#ffd9a0", "#ffe7a3")},
        {QStringLiteral("country"), palette("#e0843a", "#3a2340", "#ffd27a", "#ff6b6b")},
        {QStringLiteral("soul"), palette("#8a2c5e", "#1d1030", "#ffcf7a", "#ff7aa8")},
        {QStringLiteral("punjabi"), palette("#f0a12c", "#7a1f3d", "#ffe08a", "#2ec4b6")},
        {QStringLiteral("region"), palette("#0d7f9c", "#102a55", "#8ce8f3", "#ffd36e")},
    };
    // Palette 0 is the card's own colours; the others keep its second accent.
    static const QList<ChartPalette> seeded{
        palette("#087e9b", "#12345b", "#8ce8f3", "#ffd36e"), palette("#d94b68", "#5b246f", "#ffd36e", "#ffd36e"),
        palette("#7457df", "#273068", "#f2a8ff", "#ffd36e"), palette("#138b75", "#16445d", "#9ef2be", "#ffd36e"),
        palette("#db6640", "#72334f", "#ffe08a", "#ffd36e"), palette("#2778ca", "#3e256f", "#8de8ff", "#ffd36e"),
    };
    const auto tone = tones.constFind(visual.tone);
    if (tone != tones.cend()) {
        return *tone;
    }
    return seeded.at(std::clamp(visual.paletteIndex, 0, static_cast<int>(seeded.size()) - 1));
}

} // namespace lmp::core::catalog
