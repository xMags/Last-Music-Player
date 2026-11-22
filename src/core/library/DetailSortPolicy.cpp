#include "core/library/DetailSortPolicy.h"

#include "core/text/CaseFold.h"

#include <QLatin1StringView>

#include <algorithm>

namespace lmp::core {

namespace {

bool containsFolded(const QString& field, const QString& needle)
{
    return foldCaseAscii(field).contains(needle);
}

} // namespace

bool kindSupportsCuratedOrder(QStringView detailKind)
{
    const QString kind = foldCaseAscii(detailKind);
    return kind == QLatin1StringView("playlist") || kind == QLatin1StringView("auto-playlist")
        || kind == QLatin1StringView("album-collection");
}

DetailSort defaultDetailSort(bool supportsCuratedOrder)
{
    return supportsCuratedOrder ? DetailSort::Curated : DetailSort::DateAdded;
}

DetailSort parseDetailSort(QStringView tag, bool supportsCuratedOrder)
{
    const QString folded = foldCaseAscii(tag);
    if (folded == QLatin1StringView("title")) {
        return DetailSort::Title;
    }
    if (folded == QLatin1StringView("artist")) {
        return DetailSort::Artist;
    }
    if (folded == QLatin1StringView("duration")) {
        return DetailSort::Duration;
    }
    if (folded == QLatin1StringView("dateadded")) {
        return DetailSort::DateAdded;
    }
    if (folded == QLatin1StringView("custom") && supportsCuratedOrder) {
        return DetailSort::Curated;
    }
    return defaultDetailSort(supportsCuratedOrder);
}

QString detailSortQueryValue(DetailSort sort)
{
    switch (sort) {
    case DetailSort::Title:
        return QStringLiteral("Title");
    case DetailSort::Artist:
        return QStringLiteral("Artist");
    case DetailSort::Duration:
        return QStringLiteral("Duration");
    case DetailSort::DateAdded:
        return QStringLiteral("DateAdded");
    case DetailSort::Curated:
        break;
    }
    return {};
}

QString detailSortLabel(DetailSort sort)
{
    switch (sort) {
    case DetailSort::Title:
        return QStringLiteral("Title");
    case DetailSort::Artist:
        return QStringLiteral("Artist");
    case DetailSort::Duration:
        return QStringLiteral("Duration");
    case DetailSort::DateAdded:
        return QStringLiteral("Recently added");
    case DetailSort::Curated:
        break;
    }
    return QStringLiteral("Custom order");
}

QList<qsizetype> detailTrackOrder(const QList<DetailTrackSortKey>& keys, QStringView query, DetailSort sort)
{
    const QString needle = foldCaseAscii(query);
    QList<qsizetype> order;
    order.reserve(keys.size());
    for (qsizetype index = 0; index < keys.size(); ++index) {
        const DetailTrackSortKey& key = keys[index];
        if (needle.isEmpty() || containsFolded(key.title, needle) || containsFolded(key.artist, needle)
            || containsFolded(key.album, needle)) {
            order.append(index);
        }
    }

    if (sort == DetailSort::Curated || sort == DetailSort::DateAdded) {
        // Catalogue pages have no date-added value. DateAdded is accepted as
        // source order so a stale tag cannot scramble them.
        return order;
    }

    std::stable_sort(order.begin(), order.end(), [&keys, sort](qsizetype left, qsizetype right) {
        const DetailTrackSortKey& leftKey = keys[left];
        const DetailTrackSortKey& rightKey = keys[right];
        if (sort == DetailSort::Duration && leftKey.durationSeconds != rightKey.durationSeconds) {
            return leftKey.durationSeconds > rightKey.durationSeconds;
        }
        if (sort == DetailSort::Artist) {
            const QString leftArtist = foldCaseAscii(leftKey.artist);
            const QString rightArtist = foldCaseAscii(rightKey.artist);
            if (leftArtist != rightArtist) {
                return leftArtist < rightArtist;
            }
        }
        return foldCaseAscii(leftKey.title) < foldCaseAscii(rightKey.title);
    });
    return order;
}

} // namespace lmp::core
