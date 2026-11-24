#include "ui/library/CollectionEditor.h"

#include "core/library/LinkImportService.h"

#include <utility>

#include "core/library/CollectionActions.h"
#include "ui/navigation/Navigator.h"
#include "ui/navigation/Route.h"
#include "ui/toasts/ToastQueue.h"

namespace lmp::ui {

CollectionEditor::CollectionEditor(core::CollectionActions& actions, Navigator& navigator, ToastQueue& toasts,
                                   QObject* parent)
    : QObject(parent)
    , m_actions(actions)
    , m_navigator(navigator)
    , m_toasts(toasts)
{
    connect(&m_actions, &core::CollectionActions::collectionDeleted, this, &CollectionEditor::leaveDeleted);
}

int CollectionEditor::maximumNameLength()
{
    return core::CollectionActions::MaximumNameLength;
}

void CollectionEditor::requestCreatePlaylist()
{
    m_pending = Pending::CreatePlaylist;
    // The reference's prompt (playlist-menu.ts promptCreatePlaylist).
    m_promptTitle = tr("Create playlist");
    m_promptLabel = tr("Playlist name");
    m_promptPlaceholder = tr("Road trip, night drive, work focus...");
    m_promptSubmit = tr("Create");
    m_promptValue.clear();
    emit promptChanged();
    emit promptRequested();
}

void CollectionEditor::requestCreateAlbum()
{
    m_pending = Pending::CreateAlbum;
    // The current app's (LibraryCreateAlbum_Click).
    m_promptTitle = tr("Create album");
    m_promptLabel = tr("Album name");
    m_promptPlaceholder = tr("New album");
    m_promptSubmit = tr("Create");
    m_promptValue.clear();
    emit promptChanged();
    emit promptRequested();
}

int CollectionEditor::promptMaximumLength() const
{
    return m_pending == Pending::ImportLink ? MaximumLinkLength : maximumNameLength();
}

void CollectionEditor::requestImportLink(const QString& kind)
{
    if (m_importer == nullptr) {
        return;
    }
    m_pending = Pending::ImportLink;
    // The current app's (LibraryImportAlbum_Click, LibraryImportPlaylist_Click).
    const bool playlist = kind == QLatin1StringView("playlist");
    m_promptTitle = playlist ? tr("Import playlist") : tr("Import album");
    m_promptLabel = playlist ? tr("Playlist link") : tr("Music API album link");
    m_promptPlaceholder = playlist ? tr("Paste playlist link") : tr("Paste album link");
    m_promptSubmit = tr("Import");
    m_promptValue.clear();
    emit promptChanged();
    emit promptRequested();
}

void CollectionEditor::requestRename(const QString& playlistKey, const QString& title)
{
    if (playlistKey.isEmpty()) {
        return;
    }
    m_pending = Pending::Rename;
    m_key = playlistKey;
    m_title = title;
    m_promptTitle = tr("Rename playlist");
    m_promptLabel = tr("Playlist name");
    m_promptPlaceholder.clear();
    m_promptSubmit = tr("Rename");
    m_promptValue = title;
    emit promptChanged();
    emit promptRequested();
}

void CollectionEditor::requestDelete(const QString& kind, const QString& key, const QString& title)
{
    const bool album = kind == QLatin1StringView("album-collection");
    if (key.isEmpty() || (!album && kind != QLatin1StringView("playlist"))) {
        return;
    }
    m_pending = Pending::Delete;
    m_kind = kind;
    m_key = key;
    m_title = title;
    // The current app's wording (LibraryPlaylistDelete_Click, LibraryAlbumDelete_Click).
    m_confirmTitle = album ? tr("Delete album?") : tr("Delete playlist?");
    m_confirmText = album ? tr("This removes the album collection. Songs stay in your library.")
                          : tr("This removes the playlist. Songs stay in your library.");
    m_confirmLabel = tr("Delete");
    emit confirmChanged();
    emit confirmRequested();
}

bool CollectionEditor::submitPrompt(const QString& value)
{
    const Pending pending = std::exchange(m_pending, Pending::None);
    switch (pending) {
    case Pending::CreatePlaylist: {
        if (m_actions.createsOnAccount()) {
            // Made on the account; the page opens once the service made it.
            const QString title = core::CollectionActions::cleanName(value, tr("Untitled Playlist"));
            m_actions.createAccountPlaylist(value).then(this, [this, title](const core::Result<QString>& key) {
                if (!key) {
                    return; // the account editor reported it
                }
                m_toasts.show(tr("Created \"%1\"").arg(title));
                m_navigator.push(Route(Route::Kind::Collection, {{QStringLiteral("kind"), QStringLiteral("playlist")},
                                                                 {QStringLiteral("key"), key.value()},
                                                                 {QStringLiteral("title"), title}}));
            });
            return true;
        }
        const core::Result<QString> key = m_actions.createPlaylist(value);
        if (!key) {
            m_toasts.show(tr("Could not create the playlist."), true);
            // The prompt stays open for another try.
            m_pending = pending;
            return false;
        }
        const QString title = core::CollectionActions::cleanName(value, tr("Untitled Playlist"));
        m_toasts.show(tr("Created \"%1\"").arg(title));
        // The current app opens the new playlist.
        m_navigator.push(Route(Route::Kind::Collection, {{QStringLiteral("kind"), QStringLiteral("playlist")},
                                                         {QStringLiteral("key"), key.value()},
                                                         {QStringLiteral("title"), title}}));
        return true;
    }
    case Pending::ImportLink: {
        if (m_importer == nullptr || value.trimmed().isEmpty()) {
            return true;
        }
        m_toasts.show(tr("Importing..."));
        m_importer->importLink(value).then(this, [this](const core::Result<core::ImportOutcome>& outcome) {
            if (!outcome) {
                m_toasts.show(outcome.failure().kind == core::FailureKind::NotAvailable
                                  ? tr("Importing needs a connected music source.")
                                  : tr("Import failed: %1").arg(outcome.failure().detail),
                              true);
                return;
            }
            m_toasts.show(outcome.value().kind == QLatin1StringView("playlist")
                              ? tr("Imported playlist with %n songs", nullptr, outcome.value().songCount)
                              : tr("Imported %n songs", nullptr, outcome.value().songCount));
            m_navigator.push(Route(Route::Kind::Collection, {{QStringLiteral("kind"), outcome.value().kind},
                                                             {QStringLiteral("key"), outcome.value().key},
                                                             {QStringLiteral("title"), outcome.value().title}}));
        });
        return true;
    }
    case Pending::CreateAlbum: {
        const core::Result<QString> key = m_actions.createAlbum(value);
        if (!key) {
            m_toasts.show(tr("Could not create the album."), true);
            // The prompt stays open for another try.
            m_pending = pending;
            return false;
        }
        m_toasts.show(tr("Created \"%1\"").arg(core::CollectionActions::cleanName(value, tr("Untitled Album"))));
        return true;
    }
    case Pending::Rename: {
        const QString title = core::CollectionActions::cleanName(value, tr("Untitled Playlist"));
        if (title == m_title) {
            return true;
        }
        if (!m_actions.renamePlaylist(m_key, value)) {
            m_toasts.show(tr("Could not rename the playlist."), true);
            // The prompt stays open for another try.
            m_pending = pending;
            return false;
        }
        m_toasts.show(tr("Renamed to \"%1\"").arg(title));
        return true;
    }
    case Pending::Delete:
    case Pending::None:
        break;
    }
    return false;
}

void CollectionEditor::confirm()
{
    if (std::exchange(m_pending, Pending::None) != Pending::Delete) {
        return;
    }
    const bool album = m_kind == QLatin1StringView("album-collection");
    const core::Result<void> deleted = album ? m_actions.deleteAlbum(m_key) : m_actions.deletePlaylist(m_key);
    if (!deleted) {
        m_toasts.show(album ? tr("Could not delete the album.") : tr("Could not delete the playlist."), true);
        return;
    }
    m_toasts.show(tr("Deleted \"%1\"").arg(m_title));
}

void CollectionEditor::leaveDeleted(const QString& kind, const QString& key)
{
    // A deleted collection's page has nothing left to show.
    const Route route = m_navigator.current();
    if (route.kind() != Route::Kind::Collection || route.param(QStringLiteral("kind")) != kind
        || route.param(QStringLiteral("key")) != key) {
        return;
    }
    if (m_navigator.canGoBack()) {
        m_navigator.back();
    } else {
        m_navigator.openView(static_cast<int>(Route::Kind::Library));
    }
}

} // namespace lmp::ui
