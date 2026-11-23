#pragma once

#include <QHash>
#include <QJsonValue>
#include <QList>
#include <QString>
#include <QStringList>

#include <optional>

// Home's tiers and shelves, and the layout the account service may send with
// the library (`homeLayout`): it can reorder tiers, reorder shelves within a
// tier, rename them and hide shelves, but never move a shelf to another tier.
// Pure functions, no I/O.
namespace lmp::core::home {

// The tiers in their default order: "you", "new", "library", "explore".
[[nodiscard]] const QStringList& homeTierOrder();
// The built-in shelves of a tier, in order (empty for an unknown tier). The
// phone form puts Listen Again ahead of the mixes.
[[nodiscard]] QStringList homeTierShelves(const QString& tier, bool phone = false);
// The tier a shelf belongs to; empty for an unknown shelf.
[[nodiscard]] QString homeShelfTier(const QString& shelf);

// The longest title a layout may set.
inline constexpr int HomeLayoutTitleMax = 60;

struct HomeLayoutShelf {
    QString key;
    // Replaces the shelf's own title when set.
    std::optional<QString> title;
    bool enabled = true;

    friend bool operator==(const HomeLayoutShelf&, const HomeLayoutShelf&) = default;
};

struct HomeLayoutTier {
    QString key;
    std::optional<QString> title;
    QList<HomeLayoutShelf> shelves;

    friend bool operator==(const HomeLayoutTier&, const HomeLayoutTier&) = default;
};

struct HomeLayout {
    QList<HomeLayoutTier> tiers;

    friend bool operator==(const HomeLayout&, const HomeLayout&) = default;
};

// A non-empty trimmed string of at most 60 characters (counted as code
// points), else nothing.
[[nodiscard]] std::optional<QString> normalizeLayoutTitle(const QJsonValue& value);

// Every tier and shelf in default order, shown, with its own title.
[[nodiscard]] HomeLayout defaultHomeLayout(bool phone = false);
// What a layout the service sent lists: known tiers and shelves, checked,
// without repeats, in its order, nothing filled in. Nothing when there is
// none or it is not an object with a "tiers" list.
[[nodiscard]] std::optional<HomeLayout> listedHomeLayout(const QJsonValue& value);
// A complete layout from what the service sent: what it leaves out keeps its
// built-in place after the listed ones. Nothing as listedHomeLayout.
[[nodiscard]] std::optional<HomeLayout> readHomeLayout(const QJsonValue& value, bool phone = false);
// Always a complete layout; the built-in one when nothing usable was sent.
[[nodiscard]] HomeLayout normalizeHomeLayout(const QJsonValue& value, bool phone = false);
// The layout as JSON, as the service sends it.
[[nodiscard]] QJsonValue homeLayoutJson(const HomeLayout& layout);

// Lookups over a complete layout.
class HomeLayoutView {
public:
    explicit HomeLayoutView(HomeLayout layout);

    [[nodiscard]] const QList<HomeLayoutTier>& tiers() const { return m_layout.tiers; }
    [[nodiscard]] QString tierTitle(const QString& tier, const QString& fallback) const;
    [[nodiscard]] QString shelfTitle(const QString& shelf, const QString& fallback) const;
    [[nodiscard]] std::optional<QString> shelfTitleOverride(const QString& shelf) const;
    // A shelf the layout does not name shows.
    [[nodiscard]] bool shelfEnabled(const QString& shelf) const;

private:
    HomeLayout m_layout;
    QHash<QString, std::optional<QString>> m_tierTitles;
    QHash<QString, HomeLayoutShelf> m_shelves;
};

} // namespace lmp::core::home
