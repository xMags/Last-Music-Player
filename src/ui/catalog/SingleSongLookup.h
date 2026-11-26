#pragma once

#include "core/account/LibraryArtistContract.h"
#include "core/async/Result.h"
#include "core/catalog/CatalogModels.h"
#include "core/model/Track.h"

#include <QFuture>
#include <QObject>
#include <QString>
#include <QtGlobal>

#include <functional>
#include <optional>

namespace lmp::core::catalog {
class CatalogService;
}

namespace lmp::ui {

// The one song of a single on an artist's Singles & EPs shelf. A single there
// plays as the song it is instead of opening its release page: a click loads
// the release (through the same cached calls its page uses) and, when it
// holds exactly one song, hands that song back; any other answer (more songs,
// none, a failed load) means "open the page as before".
//
// One lookup at a time: a new one supersedes the one in flight, a repeat of
// the item already being looked up starts no second load (its answer goes to
// the latest caller), and forget() (the listener moved to another page)
// drops it, so a late answer neither plays nor navigates.
//
// GUI thread only.
class SingleSongLookup final : public QObject {
    Q_OBJECT

public:
    using ProviderAlbum = std::function<QFuture<core::Result<core::catalog::Detail>>(
        const core::account::LibraryArtistIdentity&, const QString&)>;
    // The song, or nothing when the release page should open.
    using Done = std::function<void(std::optional<core::Track>)>;

    struct Parts {
        core::catalog::CatalogService& catalog;
        // Loads a release that has a provider identity the way its page
        // would (the provider's own call or the catalogue service's, per
        // mode). Without one, the catalogue service's call.
        ProviderAlbum providerAlbum = {};
    };

    // Everything in `parts` outlives this object.
    explicit SingleSongLookup(Parts parts, QObject* parent = nullptr);

    // Whether a click on `item` looks for its one song first: a release (an
    // album item) not marked EP that is marked single or sits on a shelf
    // whose releases may be singles without the mark (`onSinglesShelf`: the
    // artist's singles shelf, whose releases saved before releases were
    // marked have none, or the latest release, never marked).
    [[nodiscard]] static bool isCandidate(const core::catalog::CatalogItem& item, bool onSinglesShelf);

    // Loads `item`'s release and calls `done` with its only song, or with
    // nothing. `done` runs later, on this object's thread, and only while no
    // later lookup or forget() came first. Returns false when `item` is the
    // one already being looked up: no second load starts, and `done` takes
    // the place of the earlier caller's.
    bool lookUp(const core::catalog::CatalogItem& item, Done done);
    // Drops the lookup in flight, if any: its answer is ignored.
    void forget();
    [[nodiscard]] bool busy() const { return !m_pendingKey.isEmpty(); }

private:
    [[nodiscard]] QFuture<core::Result<core::catalog::Detail>> release(const core::catalog::CatalogItem& item) const;

    Parts m_parts;
    quint64 m_generation = 0;
    QString m_pendingKey;
    Done m_done;
};

} // namespace lmp::ui
