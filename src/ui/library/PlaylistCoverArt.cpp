#include "ui/library/PlaylistCoverArt.h"

#include "ui/player/CoverSource.h"

namespace lmp::ui {

PlaylistCoverArt playlistCoverArt(const QList<core::Track>& tracks, bool collageAllowed)
{
    QList<QUrl> covers;
    for (const core::Track& track : tracks) {
        const QUrl url = CoverSource::forLibraryTrack(track).displayUrl();
        if (url.isEmpty() || covers.contains(url)) {
            continue;
        }
        covers.append(url);
        if (covers.size() == 4) {
            break;
        }
    }
    PlaylistCoverArt art;
    if (collageAllowed && covers.size() >= 2) {
        art.style = QStringLiteral("collage");
        if (covers.size() == 2) {
            art.collage = {covers.at(0), covers.at(1), covers.at(1), covers.at(0)};
        } else {
            for (int slot = 0; slot < 4; ++slot) {
                art.collage.append(covers.at(slot % covers.size()));
            }
        }
        return art;
    }
    if (!covers.isEmpty()) {
        art.image = covers.constFirst();
    }
    return art;
}

} // namespace lmp::ui
