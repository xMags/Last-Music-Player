#include "ui/library/AccountArtistsViewModel.h"

#include "core/account/AccountClient.h"
#include "core/account/AccountSessionService.h"
#include "core/library/ArtistFollowService.h"
#include "core/mode/ModeService.h"
#include "core/model/Track.h"
#include "ui/navigation/Navigator.h"
#include "ui/navigation/Route.h"
#include "ui/player/CoverSource.h"

#include <QSet>

#include <optional>

namespace lmp::ui {

AccountArtistsViewModel::AccountArtistsViewModel(Parts parts, QObject* parent)
    : QObject(parent)
    , m_client(parts.client)
    , m_session(parts.session)
    , m_mode(parts.mode)
    , m_follows(parts.follows)
    , m_navigator(parts.navigator)
{
    m_searchDelay.setSingleShot(true);
    m_searchDelay.setInterval(SearchDelayMs);
    connect(&m_searchDelay, &QTimer::timeout, this, &AccountArtistsViewModel::reload);
    connect(&m_mode, &core::mode::ModeService::changed, this, &AccountArtistsViewModel::onModeChanged);
    connect(&m_session, &core::account::AccountSessionService::ownerChanged, this, &AccountArtistsViewModel::reload);
    connect(&m_follows, &core::ArtistFollowService::followChanged, this, [this](const QString& key, bool) {
        // A follow changes the artist's place in the default view.
        if (m_ids.contains(key)) {
            reload();
        }
    });
    onModeChanged();
}

void AccountArtistsViewModel::onModeChanged()
{
    const bool active = m_mode.mode() == core::mode::SourceMode::Account;
    if (active == m_active) {
        return;
    }
    m_active = active;
    emit activeChanged();
    if (!m_active) {
        m_requests.invalidate();
        m_cards.setCards({});
        m_ids.clear();
        m_nextCursor.clear();
        m_status.clear();
        m_loading = false;
        emit stateChanged();
    }
}

void AccountArtistsViewModel::setQuery(const QString& query)
{
    if (query == m_query) {
        return;
    }
    m_query = query;
    emit queryChanged();
    m_searchDelay.start();
}

void AccountArtistsViewModel::reload()
{
    m_searchDelay.stop();
    request(false);
}

void AccountArtistsViewModel::loadMore()
{
    if (m_loading || m_nextCursor.isEmpty()) {
        return;
    }
    request(true);
}

QString AccountArtistsViewModel::captionFor(const core::account::LibraryArtist& artist)
{
    QString caption = (artist.trackCount == 1 ? tr("1 song") : tr("%1 songs").arg(artist.trackCount))
        + QStringLiteral(" - ")
        + (artist.albumCount == 1 ? tr("1 album") : tr("%1 albums").arg(artist.albumCount));
    if (artist.resolving) {
        caption += QStringLiteral(" - ") + tr("Resolving");
    }
    return caption;
}

void AccountArtistsViewModel::request(bool append)
{
    if (!m_active) {
        return;
    }
    const std::optional<core::account::AccountOperationContext> context = m_session.captureOperation();
    if (!context) {
        return;
    }
    core::account::LibraryArtistListRequest request;
    request.query = m_query.left(core::account::MaxLibraryArtistSearchCharacters);
    request.cursor = append ? m_nextCursor : QString();
    const core::RequestScope::Token token = m_requests.next();
    m_loading = true;
    m_status.clear();
    emit stateChanged();
    m_client.libraryArtists(context->bearerSession, request)
        .then(this, [this, token, append, context = *context](const core::Result<core::account::LibraryArtistPage>& page) {
            if (!m_requests.isCurrent(token) || !m_session.isCurrent(context)) {
                return;
            }
            m_loading = false;
            if (!page) {
                if (page.failure().kind == core::FailureKind::Unauthorized) {
                    m_session.handleUnauthorized(context);
                }
                m_status = tr("Artists are temporarily unavailable.");
            } else if (!page.value().valid) {
                m_status = tr("The artist response was not accepted.");
            } else {
                apply(page.value(), append);
            }
            emit stateChanged();
        });
}

void AccountArtistsViewModel::apply(const core::account::LibraryArtistPage& page, bool append)
{
    QList<CollectionCardModel::Card> cards;
    QList<QString> ids;
    if (append) {
        for (int row = 0; row < m_cards.count(); ++row) {
            cards.append(m_cards.cardAt(row));
        }
        ids = m_ids;
    }
    QSet<QString> seen(ids.cbegin(), ids.cend());
    for (const core::account::LibraryArtist& artist : page.artists) {
        m_follows.rememberAccountArtist(artist.id, artist.followed);
        if (seen.contains(artist.id)) {
            continue;
        }
        seen.insert(artist.id);
        core::Track cover;
        cover.artworkUrl = artist.artworkUrl;
        cover.sourceKind = QStringLiteral("remote");
        cards.append({.kind = QStringLiteral("artist"),
                      .key = artist.name,
                      .title = artist.name,
                      .subtitle = captionFor(artist),
                      .artworkUrl = CoverSource::forTrack(cover).displayUrl()});
        ids.append(artist.id);
    }
    m_cards.setCards(std::move(cards));
    m_ids = std::move(ids);
    m_nextCursor = page.nextCursor;
}

void AccountArtistsViewModel::openCard(int row)
{
    if (row < 0 || row >= m_cards.count()) {
        return;
    }
    const CollectionCardModel::Card card = m_cards.cardAt(row);
    // The page reads the artist's songs in the library by name; the public
    // id lets it follow the artist.
    m_navigator.push(Route(Route::Kind::Collection, {{QStringLiteral("kind"), card.kind},
                                                     {QStringLiteral("key"), card.key},
                                                     {QStringLiteral("title"), card.title},
                                                     {QStringLiteral("artistId"), m_ids.value(row)}}));
}

} // namespace lmp::ui
