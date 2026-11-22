#pragma once

#include "core/model/Track.h"

#include <QList>
#include <QString>
#include <QStringList>
#include <QtGlobal>

// The values the account store reads and writes: the cached copy of an
// account library, its sync bookkeeping and the followed artists.
namespace lmp::core::db {

enum class AccountDataClearMode {
    // The account's data goes and the active context returns to local only
    // if it named this account.
    LeaveInactive,
    // The data goes, then an empty profile is made again and the active
    // context names this account.
    RestoreActiveContext,
};

struct AccountProfileSnapshot {
    QString accountId;
    QString displayName;
    QString username;
    // An address, or a picture inlined as a data URI.
    QString avatarUrl;
    QString planLabel;
    QString updatedAtUtc;
};

struct AccountTrackSnapshot {
    Track track;
    qint64 playCount = 0;
    QString firstPlayedAtUtc;
    QString lastPlayedAtUtc;
    double lastPositionSeconds = 0;
};

struct AccountPlaylistSnapshot {
    QString playlistId;
    QString name;
    QString description;
    QString sourceUrl;
    QString updatedAtUtc;
    // In playlist order.
    QStringList remoteTrackIds;
};

struct AccountLibrarySnapshot {
    AccountProfileSnapshot profile;
    QList<AccountTrackSnapshot> tracks;
    QList<AccountPlaylistSnapshot> playlists;
    QString synchronizedAtUtc;
};

struct AccountSyncState {
    QString accountId;
    QString lastSuccessfulSyncUtc;
    QString lastSafeErrorCode;
    QString legacyHistoryImportState;
};

// One followed artist. The scope key says whose list it is on (an account,
// or an API key server); the identity key is artistIdentityKey of the rest.
struct FollowedArtist {
    QString scopeKey;
    QString identityKey;
    QString catalogId;
    QString sourceUrl;
    QString name;
    QString provider;
    // ISO 8601 UTC, "2026-08-24T00:00:00Z".
    QString addedAtUtc;
};

} // namespace lmp::core::db
