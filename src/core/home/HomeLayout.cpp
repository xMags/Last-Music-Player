#include "core/home/HomeLayout.h"

#include <QJsonArray>
#include <QJsonObject>
#include <QLatin1StringView>
#include <QSet>

namespace lmp::core::home {

namespace {

const QHash<QString, QStringList>& tierShelves()
{
    static const QHash<QString, QStringList> table{
        {QStringLiteral("you"), {QStringLiteral("mixes"), QStringLiteral("listenAgain")}},
        {QStringLiteral("new"),
         {QStringLiteral("newSongs"), QStringLiteral("newAlbums"), QStringLiteral("biggestHits"),
          QStringLiteral("popularArtists"), QStringLiteral("topSongs"), QStringLiteral("topAlbums"),
          QStringLiteral("topPlaylists")}},
        {QStringLiteral("library"),
         {QStringLiteral("mostPlayed"), QStringLiteral("liked"), QStringLiteral("recentlyAdded")}},
        {QStringLiteral("explore"), {QStringLiteral("moods"), QStringLiteral("genreCharts"), QStringLiteral("cityCharts")}},
    };
    return table;
}

[[nodiscard]] bool isTierKey(const QJsonValue& value)
{
    return value.isString() && tierShelves().contains(value.toString());
}

[[nodiscard]] QList<HomeLayoutShelf> listedShelves(const QString& tier, const QJsonValue& raw)
{
    QList<HomeLayoutShelf> shelves;
    QSet<QString> seen;
    for (const QJsonValue& entry : raw.toArray()) {
        if (!entry.isObject()) {
            continue;
        }
        const QJsonObject shelf = entry.toObject();
        const QJsonValue key = shelf.value(QLatin1StringView("key"));
        // A shelf never leaves its tier; listed elsewhere it keeps its own place.
        if (!key.isString() || homeShelfTier(key.toString()) != tier || seen.contains(key.toString())) {
            continue;
        }
        seen.insert(key.toString());
        const QJsonValue enabled = shelf.value(QLatin1StringView("enabled"));
        shelves.append({key.toString(), normalizeLayoutTitle(shelf.value(QLatin1StringView("title"))),
                        !(enabled.isBool() && !enabled.toBool())});
    }
    return shelves;
}

[[nodiscard]] QList<HomeLayoutShelf> completeShelves(const QString& tier, const QJsonValue& raw, bool phone)
{
    QList<HomeLayoutShelf> shelves = listedShelves(tier, raw);
    QSet<QString> seen;
    for (const HomeLayoutShelf& shelf : shelves) {
        seen.insert(shelf.key);
    }
    for (const QString& key : homeTierShelves(tier, phone)) {
        if (!seen.contains(key)) {
            shelves.append({key, std::nullopt, true});
        }
    }
    return shelves;
}

// The tiers of a layout object, each read by `shelvesOf`; nothing when the
// value is not an object with a "tiers" list.
template <typename ShelvesOf>
[[nodiscard]] std::optional<QList<HomeLayoutTier>> readTiers(const QJsonValue& value, ShelvesOf shelvesOf)
{
    const QJsonValue tiersValue = value.toObject().value(QLatin1StringView("tiers"));
    if (!value.isObject() || !tiersValue.isArray()) {
        return std::nullopt;
    }
    QList<HomeLayoutTier> tiers;
    QSet<QString> seen;
    for (const QJsonValue& entry : tiersValue.toArray()) {
        const QJsonObject tier = entry.toObject();
        const QJsonValue key = tier.value(QLatin1StringView("key"));
        if (!entry.isObject() || !isTierKey(key) || seen.contains(key.toString())) {
            continue;
        }
        seen.insert(key.toString());
        tiers.append({key.toString(), normalizeLayoutTitle(tier.value(QLatin1StringView("title"))),
                      shelvesOf(key.toString(), tier.value(QLatin1StringView("shelves")))});
    }
    return tiers;
}

} // namespace

const QStringList& homeTierOrder()
{
    static const QStringList order{QStringLiteral("you"), QStringLiteral("new"), QStringLiteral("library"),
                                   QStringLiteral("explore")};
    return order;
}

QStringList homeTierShelves(const QString& tier, bool phone)
{
    if (phone && tier == QLatin1StringView("you")) {
        return {QStringLiteral("listenAgain"), QStringLiteral("mixes")};
    }
    return tierShelves().value(tier);
}

QString homeShelfTier(const QString& shelf)
{
    for (auto it = tierShelves().cbegin(); it != tierShelves().cend(); ++it) {
        if (it.value().contains(shelf)) {
            return it.key();
        }
    }
    return {};
}

std::optional<QString> normalizeLayoutTitle(const QJsonValue& value)
{
    if (!value.isString()) {
        return std::nullopt;
    }
    const QString title = value.toString().trimmed();
    if (title.isEmpty() || title.toUcs4().size() > HomeLayoutTitleMax) {
        return std::nullopt;
    }
    return title;
}

HomeLayout defaultHomeLayout(bool phone)
{
    HomeLayout layout;
    for (const QString& tier : homeTierOrder()) {
        HomeLayoutTier entry{tier, std::nullopt, {}};
        for (const QString& shelf : homeTierShelves(tier, phone)) {
            entry.shelves.append({shelf, std::nullopt, true});
        }
        layout.tiers.append(std::move(entry));
    }
    return layout;
}

std::optional<HomeLayout> listedHomeLayout(const QJsonValue& value)
{
    std::optional<QList<HomeLayoutTier>> tiers = readTiers(value, listedShelves);
    if (!tiers) {
        return std::nullopt;
    }
    return HomeLayout{std::move(*tiers)};
}

std::optional<HomeLayout> readHomeLayout(const QJsonValue& value, bool phone)
{
    std::optional<QList<HomeLayoutTier>> tiers =
        readTiers(value, [phone](const QString& tier, const QJsonValue& raw) { return completeShelves(tier, raw, phone); });
    if (!tiers) {
        return std::nullopt;
    }
    QSet<QString> seen;
    for (const HomeLayoutTier& tier : *tiers) {
        seen.insert(tier.key);
    }
    for (const QString& key : homeTierOrder()) {
        if (!seen.contains(key)) {
            tiers->append({key, std::nullopt, completeShelves(key, QJsonValue(), phone)});
        }
    }
    return HomeLayout{std::move(*tiers)};
}

HomeLayout normalizeHomeLayout(const QJsonValue& value, bool phone)
{
    return readHomeLayout(value, phone).value_or(defaultHomeLayout(phone));
}

QJsonValue homeLayoutJson(const HomeLayout& layout)
{
    const auto title = [](const std::optional<QString>& value) { return value ? QJsonValue(*value) : QJsonValue(); };
    QJsonArray tiers;
    for (const HomeLayoutTier& tier : layout.tiers) {
        QJsonArray shelves;
        for (const HomeLayoutShelf& shelf : tier.shelves) {
            shelves.append(QJsonObject{{QStringLiteral("key"), shelf.key},
                                       {QStringLiteral("title"), title(shelf.title)},
                                       {QStringLiteral("enabled"), shelf.enabled}});
        }
        tiers.append(QJsonObject{{QStringLiteral("key"), tier.key},
                                 {QStringLiteral("title"), title(tier.title)},
                                 {QStringLiteral("shelves"), shelves}});
    }
    return QJsonObject{{QStringLiteral("tiers"), tiers}};
}

HomeLayoutView::HomeLayoutView(HomeLayout layout)
    : m_layout(std::move(layout))
{
    for (const HomeLayoutTier& tier : m_layout.tiers) {
        m_tierTitles.insert(tier.key, tier.title);
        for (const HomeLayoutShelf& shelf : tier.shelves) {
            m_shelves.insert(shelf.key, shelf);
        }
    }
}

QString HomeLayoutView::tierTitle(const QString& tier, const QString& fallback) const
{
    return m_tierTitles.value(tier).value_or(fallback);
}

QString HomeLayoutView::shelfTitle(const QString& shelf, const QString& fallback) const
{
    return shelfTitleOverride(shelf).value_or(fallback);
}

std::optional<QString> HomeLayoutView::shelfTitleOverride(const QString& shelf) const
{
    const auto found = m_shelves.constFind(shelf);
    return found != m_shelves.cend() ? found->title : std::nullopt;
}

bool HomeLayoutView::shelfEnabled(const QString& shelf) const
{
    const auto found = m_shelves.constFind(shelf);
    return found == m_shelves.cend() || found->enabled;
}

} // namespace lmp::core::home
