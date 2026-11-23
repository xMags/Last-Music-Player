#include "core/catalog/ArtistPageLoader.h"

#include "core/catalog/CatalogService.h"

#include <QPromise>
#include <QTimer>

#include <algorithm>
#include <memory>

namespace lmp::core::catalog {

namespace {

[[nodiscard]] bool isNumericId(const QString& id)
{
    const QString trimmed = id.trimmed();
    return !trimmed.isEmpty() && std::all_of(trimmed.cbegin(), trimmed.cend(), [](QChar character) {
        return character >= QLatin1Char('0') && character <= QLatin1Char('9');
    });
}

// `future`, or a Timeout failure after `limit`, whichever comes first.
template <typename T>
[[nodiscard]] QFuture<Result<T>> limited(QFuture<Result<T>> future, QObject* context, std::chrono::milliseconds limit)
{
    auto promise = std::make_shared<QPromise<Result<T>>>();
    auto settled = std::make_shared<bool>(false);
    promise->start();
    const auto settle = [promise, settled](Result<T> value) {
        if (*settled) {
            return;
        }
        *settled = true;
        promise->addResult(std::move(value));
        promise->finish();
    };
    future.then(context, [settle](const Result<T>& value) { settle(value); });
    QTimer::singleShot(limit, context, [settle]() {
        settle(Failure::of(FailureKind::Timeout, QStringLiteral("The music catalog took too long to answer.")));
    });
    return promise->future();
}

} // namespace

ArtistPageLoader::ArtistPageLoader(CatalogService& catalog, QObject* parent)
    : QObject(parent)
    , m_catalog(catalog)
{
}

QString ArtistPageLoader::directCatalogId(const ArtistPageTarget& target) const
{
    if (isNumericId(target.catalogArtistId)) {
        return target.catalogArtistId.trimmed();
    }
    if (m_catalog.tags().isCatalogTag(target.identity.provider) && isNumericId(target.identity.catalogId)) {
        return target.identity.catalogId.trimmed();
    }
    return {};
}

QFuture<Result<ArtistPageResult>> ArtistPageLoader::catalogue(const QString& id, bool force)
{
    return limited(m_catalog.detail(ResourceType::Artist, id, force), this, CallLimit)
        .then(this, [this, id](const Result<Detail>& detail) -> Result<ArtistPageResult> {
            if (!detail) {
                return detail.failure();
            }
            ArtistPageTarget effective;
            effective.identity = account::LibraryArtistIdentity{id, {}, detail.value().item.title, m_catalog.tags().catalog};
            effective.catalogArtistId = id;
            return ArtistPageResult{detail.value(), effective};
        });
}

QFuture<Result<ArtistPageResult>> ArtistPageLoader::provider(const ArtistPageTarget& target, bool force)
{
    // A catalogue artist is known to the provider by name only.
    account::LibraryArtistIdentity identity = target.identity;
    if (m_catalog.tags().isCatalogTag(identity.provider) && isNumericId(identity.catalogId)) {
        identity.catalogId.clear();
        identity.sourceUrl.clear();
    }
    return limited(m_catalog.providerArtist(identity, force), this, CallLimit)
        .then(this, [identity](const Result<Detail>& detail) -> Result<ArtistPageResult> {
            if (!detail) {
                return detail.failure();
            }
            return ArtistPageResult{detail.value(), ArtistPageTarget{identity, {}}};
        });
}

QFuture<Result<ArtistPageResult>> ArtistPageLoader::load(const ArtistPageTarget& target, bool force)
{
    const QString direct = directCatalogId(target);
    if (!direct.isEmpty()) {
        return catalogue(direct, force).then(this, [this, target, force](const Result<ArtistPageResult>& result) {
            // A provider page is still useful when the catalogue's fails.
            return result ? QtFuture::makeReadyValueFuture(result) : provider(target, force);
        }).unwrap();
    }
    return limited(m_catalog.resolveArtist(target.identity.name), this, CallLimit)
        .then(this,
              [this, target, force](const Result<std::optional<CatalogItem>>& resolved) {
                  const bool found = resolved && resolved.value() && resolved.value()->type == ResourceType::Artist
                      && isNumericId(resolved.value()->catalogId);
                  if (!found) {
                      return provider(target, force);
                  }
                  return catalogue(resolved.value()->catalogId, force)
                      .then(this, [this, target, force](const Result<ArtistPageResult>& result) {
                          return result ? QtFuture::makeReadyValueFuture(result) : provider(target, force);
                      })
                      .unwrap();
              })
        .unwrap();
}

} // namespace lmp::core::catalog
