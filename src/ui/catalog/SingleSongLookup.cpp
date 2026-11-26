#include "ui/catalog/SingleSongLookup.h"

#include "core/catalog/CatalogService.h"

#include <utility>

namespace lmp::ui {

namespace {

// Tells releases apart while one is being looked up.
[[nodiscard]] QString releaseKey(const core::catalog::CatalogItem& item)
{
    if (item.collectionTarget) {
        const core::account::LibraryArtistIdentity& identity = *item.collectionTarget;
        return QStringLiteral("provider\n") + identity.provider + QLatin1Char('\n') + identity.catalogId
            + QLatin1Char('\n') + identity.sourceUrl + QLatin1Char('\n') + identity.name;
    }
    return QStringLiteral("catalog\n") + item.catalogId;
}

} // namespace

SingleSongLookup::SingleSongLookup(Parts parts, QObject* parent)
    : QObject(parent)
    , m_parts(std::move(parts))
{
}

bool SingleSongLookup::isCandidate(const core::catalog::CatalogItem& item, bool onSinglesShelf)
{
    if (item.type != core::catalog::ResourceType::Album || item.track
        || item.releaseKind == core::catalog::ReleaseKind::Ep) {
        return false;
    }
    if (!item.collectionTarget && item.catalogId.isEmpty()) {
        return false;
    }
    return item.releaseKind == core::catalog::ReleaseKind::Single || onSinglesShelf;
}

bool SingleSongLookup::lookUp(const core::catalog::CatalogItem& item, Done done)
{
    const QString key = releaseKey(item);
    m_done = std::move(done);
    if (key == m_pendingKey) {
        return false;
    }
    const quint64 generation = ++m_generation;
    m_pendingKey = key;
    release(item).then(this, [this, generation](const core::Result<core::catalog::Detail>& detail) {
        if (generation != m_generation) {
            return;
        }
        m_pendingKey.clear();
        const Done done = std::exchange(m_done, Done{});
        if (!done) {
            return;
        }
        if (detail && detail.value().tracks.size() == 1) {
            done(detail.value().tracks.constFirst());
            return;
        }
        done(std::nullopt);
    });
    return true;
}

void SingleSongLookup::forget()
{
    ++m_generation;
    m_pendingKey.clear();
    m_done = {};
}

QFuture<core::Result<core::catalog::Detail>> SingleSongLookup::release(const core::catalog::CatalogItem& item) const
{
    // The same calls the release page makes (CatalogPageViewModel); the
    // catalogue service keeps its answers, so a page opened after a "more
    // than one song" answer does not wait for the same call again.
    if (item.collectionTarget) {
        if (m_parts.providerAlbum) {
            return m_parts.providerAlbum(*item.collectionTarget, {});
        }
        return m_parts.catalog.providerAlbum(*item.collectionTarget, {});
    }
    return m_parts.catalog.detail(core::catalog::ResourceType::Album, item.catalogId);
}

} // namespace lmp::ui
