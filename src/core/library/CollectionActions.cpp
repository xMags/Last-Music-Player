#include "core/library/CollectionActions.h"

#include "core/account/AccountLibraryEditor.h"

#include <optional>

#include "core/db/CollectionRepository.h"
#include "core/library/UserDataOperationGate.h"

namespace lmp::core {

namespace {

Failure cleanupRunning()
{
    return Failure::of(FailureKind::Cancelled, QStringLiteral("User data is being cleaned up"));
}

} // namespace

CollectionActions::CollectionActions(db::CollectionRepository& collections, UserDataOperationGate& gate,
                                     QObject* parent)
    : QObject(parent)
    , m_collections(collections)
    , m_gate(gate)
{
}

QString CollectionActions::cleanName(const QString& name, const QString& fallback)
{
    const QString trimmed = name.simplified().left(MaximumNameLength).trimmed();
    return trimmed.isEmpty() ? fallback : trimmed;
}

Result<QString> CollectionActions::createPlaylist(const QString& name)
{
    const std::optional<UserDataOperationGate::Lease> lease = m_gate.tryEnter();
    if (!lease) {
        return cleanupRunning();
    }
    const Result<qint64> created = m_collections.createPlaylist(cleanName(name, QStringLiteral("Untitled Playlist")));
    if (!created) {
        return created.failure();
    }
    const QString key = m_collections.playlistKey(created.value());
    if (key.isEmpty()) {
        return Failure::of(FailureKind::Storage, QStringLiteral("The new playlist was not found"));
    }
    emit playlistsChanged();
    return key;
}

void CollectionActions::setAccountEditor(account::AccountLibraryEditor* editor)
{
    m_account = editor;
    if (m_account != nullptr) {
        connect(m_account, &account::AccountLibraryEditor::playlistsChanged, this, &CollectionActions::playlistsChanged);
    }
}

bool CollectionActions::createsOnAccount() const
{
    return m_account != nullptr && m_account->isActive();
}

QFuture<Result<QString>> CollectionActions::createAccountPlaylist(const QString& name)
{
    if (!createsOnAccount()) {
        return QtFuture::makeReadyValueFuture(Result<QString>(Failure::of(FailureKind::NotAvailable)));
    }
    return m_account->createPlaylist(cleanName(name, QStringLiteral("Untitled Playlist")));
}

Result<void> CollectionActions::renamePlaylist(const QString& playlistKey, const QString& name)
{
    if (playlistKey.isEmpty()) {
        return Failure::of(FailureKind::NotAvailable);
    }
    if (account::AccountLibraryEditor::isAccountPlaylistKey(playlistKey)) {
        if (m_account == nullptr || !m_account->isActive()) {
            return Failure::of(FailureKind::NotAvailable);
        }
        // Shown at once; the editor reports a refusal.
        const QString title = cleanName(name, QStringLiteral("Untitled Playlist"));
        (void)m_account->renamePlaylist(playlistKey, title);
        emit playlistRenamed(playlistKey, title);
        return Result<void>::success();
    }
    const std::optional<UserDataOperationGate::Lease> lease = m_gate.tryEnter();
    if (!lease) {
        return cleanupRunning();
    }
    const QString title = cleanName(name, QStringLiteral("Untitled Playlist"));
    const Result<void> renamed = m_collections.renamePlaylist(playlistKey, title);
    if (renamed) {
        emit playlistRenamed(playlistKey, title);
        emit playlistsChanged();
    }
    return renamed;
}

Result<void> CollectionActions::deletePlaylist(const QString& playlistKey)
{
    if (playlistKey.isEmpty()) {
        return Failure::of(FailureKind::NotAvailable);
    }
    if (account::AccountLibraryEditor::isAccountPlaylistKey(playlistKey)) {
        if (m_account == nullptr || !m_account->isActive()) {
            return Failure::of(FailureKind::NotAvailable);
        }
        (void)m_account->deletePlaylist(playlistKey);
        emit collectionDeleted(QStringLiteral("playlist"), playlistKey);
        return Result<void>::success();
    }
    const std::optional<UserDataOperationGate::Lease> lease = m_gate.tryEnter();
    if (!lease) {
        return cleanupRunning();
    }
    const Result<void> deleted = m_collections.deletePlaylist(playlistKey);
    if (deleted) {
        emit collectionDeleted(QStringLiteral("playlist"), playlistKey);
        emit playlistsChanged();
    }
    return deleted;
}

Result<QString> CollectionActions::createAlbum(const QString& name)
{
    const std::optional<UserDataOperationGate::Lease> lease = m_gate.tryEnter();
    if (!lease) {
        return cleanupRunning();
    }
    const Result<qint64> created =
        m_collections.createAlbumCollection(cleanName(name, QStringLiteral("Untitled Album")));
    if (!created) {
        return created.failure();
    }
    const QString key = m_collections.albumKey(created.value());
    if (key.isEmpty()) {
        return Failure::of(FailureKind::Storage, QStringLiteral("The new album was not found"));
    }
    emit albumsChanged();
    return key;
}

Result<void> CollectionActions::deleteAlbum(const QString& albumKey)
{
    if (albumKey.isEmpty()) {
        return Failure::of(FailureKind::NotAvailable);
    }
    const std::optional<UserDataOperationGate::Lease> lease = m_gate.tryEnter();
    if (!lease) {
        return cleanupRunning();
    }
    const Result<void> deleted = m_collections.deleteAlbumCollection(albumKey);
    if (deleted) {
        emit collectionDeleted(QStringLiteral("album-collection"), albumKey);
        emit albumsChanged();
    }
    return deleted;
}

} // namespace lmp::core
