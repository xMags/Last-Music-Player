#include "core/catalog/CollectionSourceKey.h"

#include <QList>
#include <QPair>
#include <QSet>
#include <QUrl>
#include <QUrlQuery>

#include <algorithm>

namespace lmp::core::catalog {

QString canonicalSourceKey(const QString& raw)
{
    const QUrl url(raw.trimmed(), QUrl::StrictMode);
    const QString scheme = url.scheme().toLower();
    if (!url.isValid() || (scheme != QLatin1StringView("http") && scheme != QLatin1StringView("https"))
        || url.host().isEmpty()) {
        return {};
    }
    static const QSet<QString> tracking{QStringLiteral("si"),      QStringLiteral("feature"), QStringLiteral("pt"),
                                        QStringLiteral("nd"),      QStringLiteral("context"), QStringLiteral("ref"),
                                        QStringLiteral("src")};
    QList<QPair<QString, QString>> params;
    for (const auto& [key, value] : QUrlQuery(url).queryItems(QUrl::FullyDecoded)) {
        const QString folded = key.toLower();
        if (tracking.contains(folded) || folded.startsWith(QLatin1StringView("utm_"))) {
            continue;
        }
        params.append({key, value});
    }
    std::stable_sort(params.begin(), params.end(),
                     [](const auto& a, const auto& b) { return QString::localeAwareCompare(a.first, b.first) < 0; });
    QUrlQuery sorted;
    sorted.setQueryItems(params);
    QString path = url.path(QUrl::FullyEncoded);
    while (path.endsWith(QLatin1Char('/'))) {
        path.chop(1);
    }
    const QString query = params.isEmpty() ? QString() : QLatin1Char('?') + sorted.toString(QUrl::FullyEncoded);
    return url.host().toLower() + path + query;
}

} // namespace lmp::core::catalog
