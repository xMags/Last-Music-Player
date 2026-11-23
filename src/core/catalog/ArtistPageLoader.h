#pragma once

#include "core/async/Result.h"
#include "core/catalog/CatalogModels.h"

#include <QFuture>
#include <QObject>

#include <chrono>

namespace lmp::core::catalog {

class CatalogService;

struct ArtistPageResult {
    Detail detail;
    // The source that produced the page, for its sections' routes.
    ArtistPageTarget effectiveTarget;
};

// One artist page, as the reference client composes it: the catalogue's own
// artist when the target names one (a numeric catalogue id) or when the name
// resolves to one, else the provider's page by the target's identity (an
// identity that is only a catalogue artist keeps just its name and tag, the
// form the provider can serve). Each call has CallLimit; a catalogue call
// that fails or runs out of time falls through to the provider.
//
// GUI thread only.
class ArtistPageLoader final : public QObject {
    Q_OBJECT

public:
    static constexpr std::chrono::seconds CallLimit{5};

    // `catalog` outlives this object.
    explicit ArtistPageLoader(CatalogService& catalog, QObject* parent = nullptr);

    [[nodiscard]] QFuture<Result<ArtistPageResult>> load(const ArtistPageTarget& target, bool force = false);

    // The catalogue artist id the target names directly, else empty.
    [[nodiscard]] QString directCatalogId(const ArtistPageTarget& target) const;

private:
    [[nodiscard]] QFuture<Result<ArtistPageResult>> catalogue(const QString& id, bool force);
    [[nodiscard]] QFuture<Result<ArtistPageResult>> provider(const ArtistPageTarget& target, bool force);

    CatalogService& m_catalog;
};

} // namespace lmp::core::catalog
