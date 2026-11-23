#include "ui/catalog/CatalogArtistViewModel.h"

#include "core/account/AccountClient.h"
#include "core/account/AccountSessionService.h"
#include "core/account/LibraryArtistContract.h"
#include "core/catalog/CatalogClient.h"
#include "core/catalog/CatalogService.h"
#include "core/library/TrackActions.h"
#include "core/model/ArtworkPlaceholder.h"
#include "ui/catalog/CatalogRoutes.h"
#include "ui/navigation/Navigator.h"
#include "ui/navigation/Route.h"
#include "ui/player/CoverSource.h"
#include "ui/share/ShareController.h"
#include "ui/tracks/TrackMenuController.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLatin1StringView>
#include <QVariantMap>

namespace lmp::ui {

namespace catalog = core::catalog;
namespace routes = catalog_routes;

CatalogArtistViewModel::CatalogArtistViewModel(Parts parts, QObject* parent)
    : QObject(parent)
    , m_catalog(parts.catalog)
    , m_client(parts.client)
    , m_accountClient(parts.accountClient)
    , m_session(parts.session)
    , m_actions(parts.actions)
    , m_trackMenu(parts.trackMenu)
    , m_navigator(parts.navigator)
    , m_share(parts.share)
    , m_loader(parts.catalog)
{
    connect(&m_navigator, &Navigator::currentChanged, this, &CatalogArtistViewModel::followNavigator);
    connect(&m_actions, &core::TrackActions::likedChanged, this, [this](const QString& sourceKey, bool liked) {
        m_topSongs.setLiked(sourceKey, liked);
    });
}

bool CatalogArtistViewModel::handles(const Route& route)
{
    return route.kind() == Route::Kind::SourceArtist
        || (route.kind() == Route::Kind::CatalogDetail && route.param(QStringLiteral("type")) == QLatin1StringView("artist"));
}

void CatalogArtistViewModel::followNavigator()
{
    if (handles(m_navigator.current())) {
        load(m_navigator.current(), false);
    }
}

void CatalogArtistViewModel::retry()
{
    if (handles(m_navigator.current())) {
        load(m_navigator.current(), true);
    }
}

void CatalogArtistViewModel::load(const Route& route, bool force)
{
    const std::uint64_t request = ++m_request;
    catalog::ArtistPageTarget target;
    if (route.kind() == Route::Kind::CatalogDetail) {
        const QString id = route.param(QStringLiteral("id"));
        target.identity = core::account::LibraryArtistIdentity{id, {}, route.param(QStringLiteral("title")),
                                                               m_catalog.tags().catalog};
        target.catalogArtistId = id;
    } else {
        target.identity = routes::identityFromRoute(route);
        target.catalogArtistId = route.param(QStringLiteral("catalogArtistId"));
    }
    m_loading = true;
    m_error.clear();
    m_detail.reset();
    m_pendingName = route.param(QStringLiteral("title")).isEmpty() ? target.identity.name : route.param(QStringLiteral("title"));
    m_allTopSongs.clear();
    m_topSongs.setTracks({});
    m_showingAll = false;
    m_followedId.clear();
    m_followBusy = false;
    for (CatalogCardModel* model : std::as_const(m_sectionModels)) {
        model->setItems({});
    }
    emit changed();
    m_loader.load(target, force).then(this, [this, request](const core::Result<catalog::ArtistPageResult>& result) {
        if (request != m_request) {
            return;
        }
        m_loading = false;
        if (!result) {
            m_error = tr("This artist could not be loaded. Try again.");
            emit changed();
            return;
        }
        show(result.value());
    });
}

void CatalogArtistViewModel::show(const catalog::ArtistPageResult& result)
{
    m_detail = result.detail;
    m_target = result.effectiveTarget;
    m_allTopSongs.clear();
    for (const catalog::Shelf& section : m_detail->sections) {
        if (section.id == QLatin1StringView("top-songs")) {
            for (const catalog::CatalogItem& item : section.items) {
                if (item.track) {
                    m_allTopSongs.append(*item.track);
                }
            }
            continue;
        }
        sectionCards(section.id)->setItems(section.items);
    }
    showTopSongs();
    emit changed();
    readFollow();
}

void CatalogArtistViewModel::showTopSongs()
{
    m_topSongs.setTracks(m_showingAll ? m_allTopSongs : m_allTopSongs.mid(0, TopSongsShown));
}

void CatalogArtistViewModel::setShowingAll(bool showingAll)
{
    if (showingAll == m_showingAll) {
        return;
    }
    m_showingAll = showingAll;
    showTopSongs();
    emit changed();
}

QString CatalogArtistViewModel::info() const
{
    if (!m_detail) {
        return {};
    }
    return m_detail->item.genreNames.mid(0, 3).join(QStringLiteral(" · "));
}

QUrl CatalogArtistViewModel::artworkUrl() const
{
    if (!m_detail || m_detail->item.artworkUrl.isEmpty()) {
        return {};
    }
    core::Track cover;
    cover.artworkUrl = m_detail->item.artworkUrl;
    cover.sourceKind = QStringLiteral("remote");
    return CoverSource::forTrack(cover).displayUrl();
}

QString CatalogArtistViewModel::artworkLabel() const
{
    return core::ArtworkPlaceholder::tileLabelFor(name());
}

QVariantList CatalogArtistViewModel::sections() const
{
    QVariantList list;
    if (!m_detail) {
        return list;
    }
    for (const catalog::Shelf& section : m_detail->sections) {
        if (section.id == QLatin1StringView("top-songs") || section.items.isEmpty()) {
            continue;
        }
        // The latest release is a single card; every other section opens in full.
        list.append(QVariantMap{{QStringLiteral("key"), section.id},
                                {QStringLiteral("title"), section.title},
                                {QStringLiteral("link"), section.id != QLatin1StringView("latest-release")
                                                             && catalog::parseArtistSectionId(section.id).has_value()}});
    }
    return list;
}

CatalogCardModel* CatalogArtistViewModel::sectionCards(const QString& key)
{
    CatalogCardModel*& model = m_sectionModels[key];
    if (model == nullptr) {
        model = new CatalogCardModel(this);
    }
    return model;
}

void CatalogArtistViewModel::openSectionCard(const QString& key, int row)
{
    CatalogCardModel* model = sectionCards(key);
    const catalog::CatalogItem* item = model->itemAt(row);
    if (item == nullptr) {
        return;
    }
    if (item->track) {
        m_actions.play(*item->track, model->tracks());
        return;
    }
    if (const std::optional<Route> route = routes::routeForItem(*item)) {
        m_navigator.push(*route);
    }
}

void CatalogArtistViewModel::seeAllSection(const QString& key)
{
    if (!catalog::parseArtistSectionId(key) || !m_detail) {
        return;
    }
    QString title;
    for (const catalog::Shelf& section : m_detail->sections) {
        if (section.id == key) {
            title = section.title;
        }
    }
    QMap<QString, QString> params{{QStringLiteral("section"), key},
                                  {QStringLiteral("title"), title},
                                  {QStringLiteral("artistName"), name()},
                                  {QStringLiteral("provider"), m_target.identity.provider},
                                  {QStringLiteral("name"), m_target.identity.name}};
    if (!m_target.identity.catalogId.isEmpty()) {
        params.insert(QStringLiteral("catalogId"), m_target.identity.catalogId);
    }
    if (!m_target.identity.sourceUrl.isEmpty()) {
        params.insert(QStringLiteral("sourceUrl"), m_target.identity.sourceUrl);
    }
    if (!m_target.catalogArtistId.isEmpty()) {
        params.insert(QStringLiteral("catalogArtistId"), m_target.catalogArtistId);
    }
    m_navigator.push(Route(Route::Kind::ArtistSection, params));
}

void CatalogArtistViewModel::play()
{
    for (const core::Track& track : m_allTopSongs) {
        if (m_actions.canPlay(track)) {
            m_actions.play(track, m_allTopSongs);
            return;
        }
    }
}

void CatalogArtistViewModel::shuffle()
{
    m_actions.shufflePlay(m_allTopSongs);
}

void CatalogArtistViewModel::playSong(int row)
{
    if (row >= 0 && row < m_topSongs.count()) {
        m_actions.play(m_topSongs.trackAt(row), m_allTopSongs);
    }
}

void CatalogArtistViewModel::likeSong(int row)
{
    if (row >= 0 && row < m_topSongs.count()) {
        [[maybe_unused]] const std::optional<bool> liked = m_actions.toggleLike(m_topSongs.trackAt(row));
    }
}

void CatalogArtistViewModel::prepareSongMenu(int row)
{
    if (row >= 0 && row < m_topSongs.count()) {
        m_trackMenu.prepare(m_topSongs.trackAt(row), m_allTopSongs);
    }
}

bool CatalogArtistViewModel::canFollow() const
{
    const core::account::LibraryArtistIdentity& identity = m_target.identity;
    return m_detail.has_value() && !identity.provider.isEmpty()
        && (!identity.catalogId.isEmpty() || !identity.sourceUrl.isEmpty());
}

void CatalogArtistViewModel::readFollow()
{
    const std::optional<core::account::AccountOperationContext> context = m_session.captureOperation();
    if (!context || !canFollow()) {
        return;
    }
    const std::uint64_t request = m_request;
    m_client.artistFollow(context->bearerSession, m_target.identity)
        .then(this, [this, request](const core::Result<QByteArray>& answer) {
            if (request != m_request || !answer) {
                return;
            }
            const QJsonValue artist = QJsonDocument::fromJson(answer.value()).object().value(QLatin1StringView("artist"));
            const core::account::LibraryArtistPage page = core::account::parseLibraryArtistPage(
                QJsonDocument(QJsonObject{{QStringLiteral("artists"), QJsonArray{artist}}}).toJson(QJsonDocument::Compact));
            m_followedId = !page.artists.isEmpty() && page.artists.first().followed ? page.artists.first().id : QString();
            emit changed();
        });
}

void CatalogArtistViewModel::toggleFollow()
{
    const std::optional<core::account::AccountOperationContext> context = m_session.captureOperation();
    if (!context || !canFollow() || m_followBusy) {
        return;
    }
    m_followBusy = true;
    emit changed();
    const std::uint64_t request = m_request;
    const auto finish = [this, request](const core::Result<void>& result) {
        if (request != m_request) {
            return;
        }
        m_followBusy = false;
        if (!result) {
            emit changed();
            return;
        }
        readFollow();
    };
    if (followed()) {
        m_accountClient.setArtistFollowed(context->bearerSession, m_followedId, false).then(this, finish);
        return;
    }
    m_accountClient.followArtistByIdentity(context->bearerSession, m_target.identity,
                                           m_detail ? m_detail->item.artworkUrl : QString())
        .then(this, finish);
}

void CatalogArtistViewModel::share()
{
    if (m_share == nullptr || !canShare()) {
        return;
    }
    core::account::LibraryArtistIdentity identity = m_target.identity;
    if (identity.name.trimmed().isEmpty()) {
        identity.name = name();
    }
    m_share->share({.kind = core::share::ShareKind::Artist,
                    .artworkUrl = m_detail ? m_detail->item.artworkUrl : QString(),
                    .artist = identity},
                   tr("Could not share this artist."));
}

void CatalogArtistViewModel::openArtistOf(const core::Track& track)
{
    // The song's own artist identity (artistPageTargetForTrack); the loader
    // resolves a name to the catalogue's artist when it can.
    const QString name = !track.primaryArtistName.trimmed().isEmpty() ? track.primaryArtistName.trimmed()
                                                                      : track.artist.trimmed();
    QString provider = track.sourceTag.trimmed();
    if (provider.isEmpty() && !m_catalog.tags().stream.isEmpty()) {
        provider = m_catalog.tags().stream.first();
    }
    const QString catalogId = !track.primaryArtistCatalogId.trimmed().isEmpty() ? track.primaryArtistCatalogId.trimmed()
                                                                                : track.artistCatalogId.trimmed();
    QMap<QString, QString> params{{QStringLiteral("provider"), provider},
                                  {QStringLiteral("name"), name},
                                  {QStringLiteral("title"), name}};
    if (!catalogId.isEmpty()) {
        params.insert(QStringLiteral("catalogId"), catalogId);
    }
    m_navigator.push(Route(Route::Kind::SourceArtist, params));
}

} // namespace lmp::ui
