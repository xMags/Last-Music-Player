#include "core/model/ArtistIdentity.h"

#include <QChar>

namespace lmp::core {

QString normalizeArtistName(QStringView value)
{
    QString collapsed;
    collapsed.reserve(value.size());
    bool pendingSpace = false;
    for (const QChar character : value) {
        if (character.isSpace()) {
            pendingSpace = !collapsed.isEmpty();
            continue;
        }
        if (pendingSpace) {
            collapsed.append(QLatin1Char(' '));
            pendingSpace = false;
        }
        collapsed.append(character);
    }
    return collapsed.toLower();
}

QString artistIdentityKey(const ArtistIdentity& artist)
{
    if (!artist.catalogId.isEmpty()) {
        return QStringLiteral("catalog|") + normalizeArtistName(artist.provider) + QLatin1Char('|') + artist.catalogId;
    }
    if (!artist.sourceUrl.isEmpty()) {
        return QStringLiteral("url|") + artist.sourceUrl;
    }
    const QString name = normalizeArtistName(artist.name);
    return name.isEmpty() ? QString() : QStringLiteral("name|") + name;
}

bool sameArtistIdentity(const ArtistIdentity& left, const ArtistIdentity& right)
{
    if (!left.catalogId.isEmpty() || !right.catalogId.isEmpty()) {
        const QString leftProvider = normalizeArtistName(left.provider);
        const QString rightProvider = normalizeArtistName(right.provider);
        return !left.catalogId.isEmpty() && !right.catalogId.isEmpty() && left.catalogId == right.catalogId
            && (leftProvider.isEmpty() || rightProvider.isEmpty() || leftProvider == rightProvider);
    }
    if (!left.sourceUrl.isEmpty() || !right.sourceUrl.isEmpty()) {
        return !left.sourceUrl.isEmpty() && !right.sourceUrl.isEmpty() && left.sourceUrl == right.sourceUrl;
    }
    const QString leftName = normalizeArtistName(left.name);
    return !leftName.isEmpty() && leftName == normalizeArtistName(right.name);
}

} // namespace lmp::core
