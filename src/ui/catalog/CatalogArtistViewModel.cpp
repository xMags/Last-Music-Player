#include "ui/catalog/CatalogArtistViewModel.h"

#include <optional>

#include "core/catalog/CatalogPresentation.h"

#include "core/account/AccountClient.h"
#include "core/account/AccountSessionService.h"
#include "core/account/LibraryArtistContract.h"
#include "core/catalog/CatalogClient.h"
#include "core/catalog/CatalogService.h"
#include "core/library/TrackActions.h"
#include "ui/catalog/CatalogRoutes.h"
#include "ui/navigation/Navigator.h"
#include "ui/navigation/Route.h"
#include "ui/player/CoverSource.h"
#include "ui/share/ShareController.h"
#include "core/library/ArtistFollowService.h"
#include "ui/tracks/TrackMenuController.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLatin1StringView>

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
    , m_providerPages(std::move(parts.providerPages))
    , m_providerArtist(std::move(parts.providerArtist))
    , m_follows(parts.follows)
    , m_loader(parts.catalog)
    , m_singleSongs({.catalog = parts.catalog, .providerAlbum = std::move(parts.providerAlbum)})
{
    connect(&m_navigator, &Navigator::currentChanged, this, &CatalogArtistViewModel::followNavigator);
    if (m_follows != nullptr) {
        connect(m_follows, &core::ArtistFollowService::followChanged, this, &CatalogArtistViewModel::changed);
    }
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
    // Another page (or this one again): a single still being looked up must
    // neither play nor open its page now.
    m_singleSongs.forget();
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
    readSections();
    emit changed();
    m_fromProvider = m_providerPages && m_providerPages() && m_providerArtist;
    if (m_fromProvider) {
        m_providerArtist(target.identity)
            .then(this, [this, request, target](const core::Result<catalog::Detail>& detail) {
                if (request != m_request) {
                    return;
                }
                m_loading = false;
                if (!detail) {
                    m_error = tr("This artist could not be loaded. Try again.");
                    emit changed();
                    return;
                }
                show({detail.value(), target});
            });
        return;
    }
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
    readSections();
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
    const core::catalog::CatalogItem& item = m_detail->item;
    QStringList parts{item.releaseDate.left(4), item.genreNames.value(0), item.recordLabel, item.contentRating};
    parts.removeAll(QString());
    return parts.join(QStringLiteral(" · "));
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

void CatalogArtistViewModel::readSections()
{
    QList<ArtistSectionListModel::Section> rows;
    if (m_detail) {
        for (const catalog::Shelf& section : m_detail->sections) {
            if (section.id == QLatin1StringView("top-songs") || section.items.isEmpty()) {
                continue;
            }
            // The latest release is a single card; every other section opens in full.
            rows.append({.key = section.id,
                         .title = section.title,
                         .pill = catalog::shelfPillLabel(section.id, section.type),
                         .link = section.id != QLatin1StringView("latest-release")
                             && catalog::parseArtistSectionId(section.id).has_value()});
        }
    }
    m_sectionList.setSections(rows);
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
        m_singleSongs.forget();
        m_actions.play(*item->track, model->tracks());
        return;
    }
    // A single plays as its one song; anything else opens its page.
    if (SingleSongLookup::isCandidate(*item, key == QLatin1StringView("singles"))) {
        const catalog::CatalogItem release = *item;
        (void)m_singleSongs.lookUp(release, [this, release](const std::optional<core::Track>& song) {
            if (song) {
                m_actions.play(*song, {*song});
                return;
            }
            openRelease(release);
        });
        return;
    }
    openRelease(*item);
}

void CatalogArtistViewModel::openRelease(const catalog::CatalogItem& item)
{
    if (const std::optional<Route> route = routes::routeForItem(item)) {
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
    m_singleSongs.forget();
    for (const core::Track& track : m_allTopSongs) {
        if (m_actions.canPlay(track)) {
            m_actions.play(track, m_allTopSongs);
            return;
        }
    }
}

void CatalogArtistViewModel::shuffle()
{
    m_singleSongs.forget();
    m_actions.shufflePlay(m_allTopSongs);
}

void CatalogArtistViewModel::playSong(int row)
{
    m_singleSongs.forget();
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

namespace {

[[nodiscard]] core::FollowableArtist followable(const core::account::LibraryArtistIdentity& identity,
                                                const QString& fallbackName, const QString& artworkUrl)
{
    return {{}, {identity.catalogId, identity.sourceUrl, identity.name.isEmpty() ? fallbackName : identity.name,
                 identity.provider},
            artworkUrl};
}

} // namespace

bool CatalogArtistViewModel::followed() const
{
    if (m_fromProvider) {
        return m_follows != nullptr && m_detail.has_value()
            && m_follows->isFollowed(followable(m_target.identity, name(), m_detail->item.artworkUrl));
    }
    return !m_followedId.isEmpty();
}

bool CatalogArtistViewModel::canFollow() const
{
    if (m_fromProvider) {
        return m_follows != nullptr && m_detail.has_value()
            && m_follows->canFollow(followable(m_target.identity, name(), m_detail->item.artworkUrl));
    }
    const core::account::LibraryArtistIdentity& identity = m_target.identity;
    return m_detail.has_value() && !identity.provider.isEmpty()
        && (!identity.catalogId.isEmpty() || !identity.sourceUrl.isEmpty());
}

void CatalogArtistViewModel::readFollow()
{
    if (m_fromProvider) {
        return;
    }
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
    if (m_fromProvider) {
        if (!canFollow() || m_followBusy) {
            return;
        }
        m_followBusy = true;
        emit changed();
        const std::uint64_t request = m_request;
        m_follows->setFollowed(followable(m_target.identity, name(), m_detail->item.artworkUrl), !followed())
            .then(this, [this, request](const core::Result<bool>&) {
                if (request == m_request) {
                    m_followBusy = false;
                    emit changed();
                }
            });
        return;
    }
    const std::optional<core::account::AccountOperationContext> context = m_session.captureOperation();
    if (!context || !canFollow() || m_followBusy) {
        return;
    }
    m_followBusy = true;
    emit changed();
    const std::uint64_t request = m_request;
    // The write's own answer sets the button; the read after it only
    // confirms, so a failed read cannot leave it busy or showing the old state.
    // `followedId`: the id the write named (empty: unfollowed), or nothing
    // when it named none and the read after it says.
    const auto finish = [this, request](bool done, const std::optional<QString>& followedId) {
        if (request != m_request) {
            return;
        }
        m_followBusy = false;
        if (done && followedId) {
            m_followedId = *followedId;
        }
        emit changed();
        if (done) {
            readFollow();
        }
    };
    if (followed()) {
        m_accountClient.setArtistFollowed(context->bearerSession, m_followedId, false)
            .then(this, [finish](const core::Result<void>& result) {
                finish(static_cast<bool>(result), std::optional<QString>(QString()));
            });
        return;
    }
    m_accountClient.followArtistByIdentity(context->bearerSession, m_target.identity,
                                           m_detail ? m_detail->item.artworkUrl : QString())
        .then(this, [finish](const core::Result<QString>& result) {
            const bool named = result && !result.value().isEmpty();
            finish(static_cast<bool>(result), named ? std::optional<QString>(result.value()) : std::nullopt);
        });
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
