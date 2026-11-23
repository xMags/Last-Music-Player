#pragma once

#include "core/async/Result.h"
#include "core/model/ArtistIdentity.h"

#include <QFuture>
#include <QHash>
#include <QLatin1StringView>
#include <QObject>
#include <QSet>
#include <QString>

namespace lmp::core {

class UserDataOperationGate;
namespace account {
class AccountClient;
class AccountSessionService;
} // namespace account
namespace db {
class FollowedArtistRepository;
}
namespace mode {
class ModeService;
}

// An artist someone can follow: the service's public id (Account mode) and
// the identity the list on this PC is keyed by (artistIdentityKey).
struct FollowableArtist {
    QString publicId;
    ArtistIdentity identity;
    QString artworkUrl;
};

// Which artists the user follows, kept where earlier versions kept them: on
// the account service in Account mode (by the artist's public id), in the
// list on this PC otherwise (scope "local"). API key mode keeps its own list
// once API key mode arrives; until then following is not offered there.
//
// The service's follow state is learnt from the artist lists it sends
// (rememberAccountArtist) and from changes made here.
//
// Threading: GUI thread only.
class ArtistFollowService final : public QObject {
    Q_OBJECT

public:
    // The scope key of the list on this PC.
    static constexpr QLatin1StringView LocalScope{"local"};

    struct Dependencies {
        account::AccountClient& client;
        account::AccountSessionService& session;
        mode::ModeService& mode;
        db::FollowedArtistRepository& followed;
        UserDataOperationGate& gate;
    };

    // Every dependency must outlive this object.
    explicit ArtistFollowService(const Dependencies& dependencies, QObject* parent = nullptr);

    // Following is possible for this artist here: Account mode needs its
    // public id; on this PC it needs an identity.
    [[nodiscard]] bool canFollow(const FollowableArtist& artist) const;
    [[nodiscard]] bool isFollowed(const FollowableArtist& artist) const;
    // The new state once it is stored; NotAvailable when following is not
    // possible, the call's failure otherwise.
    [[nodiscard]] QFuture<Result<bool>> setFollowed(const FollowableArtist& artist, bool followed);

    // What a service artist list said about an artist.
    void rememberAccountArtist(const QString& publicId, bool followed);

signals:
    // `key` is the public id in Account mode, else the identity key.
    void followChanged(const QString& key, bool followed);

private:
    [[nodiscard]] bool accountMode() const;

    Dependencies m_deps;
    QHash<QString, bool> m_accountFollowed;
    // Public ids with a change on its way, so a second press waits.
    QSet<QString> m_pending;
};

} // namespace lmp::core
