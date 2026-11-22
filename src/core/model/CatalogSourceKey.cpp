#include "core/model/CatalogSourceKey.h"

#include "core/net/RemoteUrlPolicy.h"
#include "core/text/CaseFold.h"

#include <QLatin1StringView>

namespace lmp::core {

QString catalogSourceKey(const Track& track)
{
    const bool remote = track.isRemote() || net::isHttpUrl(track.filePath);
    if (!remote) {
        return track.filePath.isEmpty() ? QString() : QLatin1StringView("local|") + foldCaseAscii(track.filePath);
    }

    QString origin = foldCaseAscii(track.origin);
    if (origin.isEmpty()) {
        origin = QStringLiteral("provider");
    }
    const QString& address = track.sourceUrl.isEmpty() ? track.filePath : track.sourceUrl;
    if (address.isEmpty()) {
        return {};
    }
    return QLatin1StringView("remote|") + origin + QLatin1Char('|') + address;
}

} // namespace lmp::core
