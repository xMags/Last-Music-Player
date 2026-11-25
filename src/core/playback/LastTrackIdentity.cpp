#include "core/playback/LastTrackIdentity.h"

#include "core/model/CatalogSourceKey.h"
#include "core/net/RemoteUrlPolicy.h"
#include "core/text/CaseFold.h"

#include <QChar>
#include <QLatin1StringView>

namespace lmp::core {

namespace {

constexpr QLatin1StringView kRemoteIdPrefix{"remote-id:"};
constexpr QLatin1StringView kSourcePrefix{"source:"};

} // namespace

QString canonicalQueueText(const QString& text)
{
    const QString lowered = foldCaseAscii(text);
    QString canonical;
    canonical.reserve(lowered.size());
    // Starts true so leading separators are dropped.
    bool previousWasSpace = true;
    for (const QChar unit : lowered) {
        if (unit.isLetterOrNumber()) {
            canonical.append(unit);
            previousWasSpace = false;
        } else if (!previousWasSpace) {
            canonical.append(QLatin1Char(' '));
            previousWasSpace = true;
        }
    }
    while (canonical.endsWith(QLatin1Char(' '))) {
        canonical.chop(1);
    }
    return canonical;
}

QString homeHistoryKey(const Track& track)
{
    const bool remote = track.isRemote() || net::isHttpUrl(track.filePath) || net::isHttpUrl(track.sourceUrl);
    if (!remote) {
        return QLatin1StringView("local|") + track.filePath + QLatin1Char('|') + canonicalQueueText(track.title)
            + QLatin1Char('|') + canonicalQueueText(track.artist);
    }
    if (!track.remoteId.isEmpty()) {
        return QLatin1StringView("remote-id|") + track.remoteId;
    }
    if (!track.sourceUrl.isEmpty()) {
        return QLatin1StringView("remote-url|") + track.sourceUrl;
    }
    const QString title = canonicalQueueText(track.title);
    if (title.isEmpty()) {
        return QLatin1StringView("remote-url|") + track.filePath;
    }
    return QLatin1StringView("remote|") + title + QLatin1Char('|') + canonicalQueueText(track.artist);
}

QString legacyCatalogSourceKey(const Track& track)
{
    const QString key = catalogSourceKey(track);
    return key.isEmpty() ? homeHistoryKey(track) : key;
}

QString lastTrackValue(const Track& track)
{
    // An account song first by its id: a downloaded one also carries its
    // file, which the library's copy of the song does not.
    if (track.isRemote() && !track.remoteId.isEmpty()) {
        return kRemoteIdPrefix + track.remoteId;
    }
    if (!track.filePath.isEmpty() && !net::isHttpUrl(track.filePath)) {
        return track.filePath;
    }
    if (!track.remoteId.isEmpty()) {
        return kRemoteIdPrefix + track.remoteId;
    }
    return kSourcePrefix + legacyCatalogSourceKey(track);
}

bool matchesLastTrackValue(const Track& candidate, const QString& lastTrack)
{
    if (lastTrack.isEmpty()) {
        return false;
    }
    if (candidate.filePath == lastTrack) {
        return true;
    }
    if (lastTrack.startsWith(kRemoteIdPrefix)) {
        return candidate.remoteId == QStringView(lastTrack).sliced(kRemoteIdPrefix.size());
    }
    if (lastTrack.startsWith(kSourcePrefix)) {
        return legacyCatalogSourceKey(candidate) == QStringView(lastTrack).sliced(kSourcePrefix.size());
    }
    return false;
}

} // namespace lmp::core
