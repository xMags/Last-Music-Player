#include "ui/navigation/Navigator.h"

#include <QtGlobal>

namespace lmp::ui {

Navigator::Navigator(QObject* parent)
    : QObject(parent)
{
    m_entries.append(Entry{Route(Route::Kind::Home), 0, {}});
}

// Page view models are children of the navigator and go with it.
Navigator::~Navigator() = default;

Route Navigator::current() const
{
    return m_entries.constLast().route;
}

QString Navigator::currentTitle() const
{
    const QString title = current().param(QStringLiteral("title"));
    return title.isEmpty() ? titleFor(current().kind()) : title;
}

qreal Navigator::currentScrollOffset() const
{
    return m_entries.constLast().scrollOffset;
}

QObject* Navigator::currentPage() const
{
    return m_entries.constLast().page.data();
}

void Navigator::push(const Route& route)
{
    m_entries.append(Entry{route, 0, {}});
    m_returning = false;
    emit currentChanged();
    emit currentPageChanged();
}

void Navigator::replace(const Route& route)
{
    Entry& entry = m_entries.last();
    releasePage(entry);
    entry = Entry{route, 0, {}};
    m_returning = false;
    emit currentChanged();
    emit currentPageChanged();
}

void Navigator::back()
{
    if (!canGoBack()) {
        return;
    }
    releasePage(m_entries.last());
    m_entries.removeLast();
    m_returning = true;
    emit currentChanged();
    emit currentPageChanged();
}

void Navigator::openView(int kind)
{
    const auto routeKind = static_cast<Route::Kind>(kind);
    const Route route(routeKind);
    if (!route.isMainView()) {
        return;
    }
    push(route);
}

QString Navigator::currentFocusName() const
{
    return m_entries.isEmpty() ? QString() : m_entries.constLast().focusName;
}

int Navigator::currentFocusIndex() const
{
    return m_entries.isEmpty() ? 0 : m_entries.constLast().focusIndex;
}

void Navigator::setCurrentFocus(const QString& name, int index)
{
    if (m_entries.isEmpty()) {
        return;
    }
    m_entries.last().focusName = name;
    m_entries.last().focusIndex = qMax(0, index);
}

void Navigator::setCurrentScrollOffset(qreal offset)
{
    m_entries.last().scrollOffset = qMax<qreal>(0, offset);
}

void Navigator::setCurrentPage(QObject* page)
{
    Entry& entry = m_entries.last();
    if (entry.page == page) {
        return;
    }
    releasePage(entry);
    if (page != nullptr) {
        page->setParent(this);
    }
    entry.page = page;
    emit currentPageChanged();
}

QString Navigator::titleFor(Route::Kind kind)
{
    switch (kind) {
    case Route::Kind::Home:
        return tr("Home");
    case Route::Kind::Browse:
        return tr("Browse");
    case Route::Kind::Library:
        return tr("Library");
    case Route::Kind::Settings:
        return tr("Settings");
    case Route::Kind::Offline:
        return tr("Offline");
    case Route::Kind::CatalogDetail:
    case Route::Kind::SourceAlbum:
        return tr("Album");
    case Route::Kind::LibraryArtist:
    case Route::Kind::SourceArtist:
        return tr("Artist");
    case Route::Kind::ExternalPlaylist:
        return tr("Playlist");
    case Route::Kind::Shared:
        return tr("Shared");
    case Route::Kind::ArtistSection:
        return tr("Artist");
    case Route::Kind::Shelf:
        return tr("Collection");
    case Route::Kind::Charts:
    case Route::Kind::Chart:
        return tr("Charts");
    case Route::Kind::Collection:
        return tr("Collection");
    }
    return {};
}

void Navigator::setCurrentParam(const QString& name, const QString& value)
{
    m_entries.last().route.setParam(name, value);
}

void Navigator::releasePage(Entry& entry)
{
    if (entry.page) {
        entry.page->deleteLater();
    }
    entry.page = nullptr;
}

} // namespace lmp::ui
