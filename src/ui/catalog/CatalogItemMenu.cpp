#include "ui/catalog/CatalogItemMenu.h"

#include "core/catalog/CatalogService.h"
#include "core/library/TrackActions.h"
#include "core/share/ShareLinks.h"
#include "ui/catalog/CatalogCardModel.h"
#include "ui/catalog/CatalogRoutes.h"
#include "ui/navigation/Navigator.h"
#include "ui/share/ShareController.h"
#include "ui/toasts/ToastQueue.h"

namespace lmp::ui {

namespace catalog = core::catalog;

CatalogItemMenu::CatalogItemMenu(Parts parts, QObject* parent)
    : QObject(parent)
    , m_catalog(parts.catalog)
    , m_actions(parts.actions)
    , m_navigator(parts.navigator)
    , m_toasts(parts.toasts)
    , m_share(parts.share)
    , m_canDownload(std::move(parts.canDownload))
    , m_download(std::move(parts.download))
    , m_providerAlbum(std::move(parts.providerAlbum))
{
}

bool CatalogItemMenu::prepare(CatalogCardModel* cards, int row)
{
    const catalog::CatalogItem* item = cards != nullptr ? cards->itemAt(row) : nullptr;
    if (item == nullptr || item->track || item->type == catalog::ResourceType::Song
        || item->type == catalog::ResourceType::Unknown) {
        return false;
    }
    m_item = *item;
    ++m_request;
    const bool sharing = m_share != nullptr && m_share->available();
    QList<MenuItemListModel::Item> items;
    if (item->type == catalog::ResourceType::Artist) {
        // An artist has no song list to queue from here.
        items.append({.id = QStringLiteral("details"), .label = tr("Go to artist"), .iconName = QStringLiteral("info")});
        if (sharing && item->artistTarget) {
            items.append({.id = QStringLiteral("share"), .label = tr("Share"), .iconName = QStringLiteral("share"),
                          .separatorBefore = true});
        }
        m_menu.setItems(std::move(items));
        return true;
    }
    items = {{.id = QStringLiteral("play"), .label = tr("Play now"), .iconName = QStringLiteral("play")},
             {.id = QStringLiteral("next"), .label = tr("Play next"), .iconName = QStringLiteral("play-next")},
             {.id = QStringLiteral("queue"), .label = tr("Add to queue"), .iconName = QStringLiteral("queue-add")},
             {.id = QStringLiteral("shuffle"), .label = tr("Shuffle"), .iconName = QStringLiteral("shuffle")}};
    // Its songs load only when an item needs them, so Download is always offered.
    if (m_download && m_canDownload && m_canDownload()) {
        items.append({.id = QStringLiteral("download"), .label = tr("Download"), .iconName = QStringLiteral("download"),
                      .separatorBefore = true});
    }
    if (sharing) {
        items.append({.id = QStringLiteral("share"), .label = tr("Share"), .iconName = QStringLiteral("share"),
                      .separatorBefore = true});
    }
    items.append({.id = QStringLiteral("details"), .label = tr("View details"), .iconName = QStringLiteral("info"),
                  .separatorBefore = !sharing});
    m_menu.setItems(std::move(items));
    return true;
}

void CatalogItemMenu::trigger(const QString& itemId)
{
    if (!m_item) {
        return;
    }
    const catalog::CatalogItem item = *m_item;
    if (itemId == QLatin1StringView("details")) {
        if (const std::optional<Route> route = catalog_routes::routeForItem(item)) {
            m_navigator.push(*route);
        }
        return;
    }
    if (itemId == QLatin1StringView("share") && item.type == catalog::ResourceType::Artist && item.artistTarget) {
        if (m_share != nullptr) {
            core::account::LibraryArtistIdentity identity = item.artistTarget->identity;
            if (identity.name.trimmed().isEmpty()) {
                identity.name = item.title;
            }
            m_share->share({.kind = core::share::ShareKind::Artist, .artworkUrl = item.artworkUrl, .artist = identity},
                           tr("Could not share this artist."));
        }
        return;
    }
    withTracks([this, itemId, item](const QList<core::Track>& tracks) {
        if (itemId == QLatin1StringView("play")) {
            for (const core::Track& track : tracks) {
                if (m_actions.canPlay(track)) {
                    m_actions.play(track, tracks);
                    break;
                }
            }
        } else if (itemId == QLatin1StringView("next")) {
            // Each goes right after the current song, so the list goes in backwards.
            for (auto it = tracks.crbegin(); it != tracks.crend(); ++it) {
                m_actions.playNext(*it);
            }
        } else if (itemId == QLatin1StringView("queue")) {
            m_actions.addAllToQueue(tracks);
        } else if (itemId == QLatin1StringView("shuffle")) {
            m_actions.shufflePlay(tracks);
        } else if (itemId == QLatin1StringView("download") && m_download) {
            (void)m_download(item.title, item.subtitle, item.artworkUrl, tracks);
        } else if (itemId == QLatin1StringView("share") && m_share != nullptr) {
            m_share->share({.kind = core::share::ShareKind::List,
                            .title = item.title,
                            .subtitle = item.subtitle,
                            .artworkUrl = item.artworkUrl,
                            .tracks = tracks},
                           tr("Could not share this playlist."));
        }
    });
}

void CatalogItemMenu::withTracks(const std::function<void(const QList<core::Track>&)>& use)
{
    if (!m_item) {
        return;
    }
    const catalog::CatalogItem item = *m_item;
    QFuture<core::Result<catalog::Detail>> load;
    if (item.collectionTarget && m_providerAlbum) {
        load = m_providerAlbum(*item.collectionTarget, item.subtitle);
    } else if (!item.externalPlaylistId.isEmpty()) {
        load = m_catalog.externalPlaylist(item.externalPlaylistId);
    } else if (!item.catalogId.isEmpty()) {
        load = m_catalog.detail(item.type, item.catalogId);
    } else {
        (void)m_toasts.show(tr("That didn't work. Try again."), true);
        return;
    }
    const quint64 request = m_request;
    load.then(this, [this, request, use](const core::Result<catalog::Detail>& detail) {
        if (request != m_request) {
            return;
        }
        if (!detail || detail.value().tracks.isEmpty()) {
            (void)m_toasts.show(tr("That didn't work. Try again."), true);
            return;
        }
        use(detail.value().tracks);
    });
}

} // namespace lmp::ui
