#pragma once

#include <QHash>
#include <QObject>

#include <optional>
#include <QString>
#include <QTimer>
#include <QtQml/qqmlregistration.h>

#include "core/account/LibraryArtistContract.h"
#include "core/async/RequestScope.h"
#include "core/model/ArtistIdentity.h"
#include "ui/library/CollectionCardModel.h"

namespace lmp::core {
class ArtistFollowService;
class SettingsStore;
namespace account {
class AccountClient;
class AccountSessionService;
} // namespace account
namespace mode {
class ModeService;
}
} // namespace lmp::core

namespace lmp::ui {

class Navigator;
class ToastQueue;

// The Library's Artists tab in Account mode (App.accountArtists): the
// account service's artist list, 50 at a time with "Load more", searched by
// name 250 ms after typing stops, as earlier versions showed it. Outside
// Account mode `active` is false and the tab shows the artists of the
// library on this PC instead.
//
// A newer search or reload drops the answers of older ones; an answer for
// another account or mode is dropped too.
//
// GUI thread only.
class AccountArtistsViewModel final : public QObject {
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Provided by App.accountArtists")

    Q_PROPERTY(bool active READ active NOTIFY activeChanged FINAL)
    Q_PROPERTY(lmp::ui::CollectionCardModel* artists READ artists CONSTANT FINAL)
    Q_PROPERTY(bool loading READ loading NOTIFY stateChanged FINAL)
    Q_PROPERTY(bool hasMore READ hasMore NOTIFY stateChanged FINAL)
    // A problem in words, or empty.
    Q_PROPERTY(QString status READ status NOTIFY stateChanged FINAL)
    Q_PROPERTY(QString query READ query WRITE setQuery NOTIFY queryChanged FINAL)
    // Which artists: "default" (Top), "followed" or "all".
    Q_PROPERTY(QString view READ view WRITE setView NOTIFY queryChanged FINAL)
    // "12 artists": the service's total, else how many are listed; empty
    // while the first page loads.
    Q_PROPERTY(QString countText READ countText NOTIFY stateChanged FINAL)

public:
    struct Parts {
        core::account::AccountClient& client;
        core::account::AccountSessionService& session;
        core::mode::ModeService& mode;
        core::ArtistFollowService& follows;
        Navigator& navigator;
        // Says how a Follow went; may be missing.
        ToastQueue* toasts = nullptr;
        // Keeps the view between runs; may be missing.
        core::SettingsStore* settings = nullptr;
    };

    // How long typing must pause before the search is sent.
    static constexpr int SearchDelayMs = 250;

    // Everything in `parts` outlives this object.
    explicit AccountArtistsViewModel(Parts parts, QObject* parent = nullptr);

    [[nodiscard]] bool active() const { return m_active; }
    [[nodiscard]] CollectionCardModel* artists() { return &m_cards; }
    [[nodiscard]] bool loading() const { return m_loading; }
    [[nodiscard]] bool hasMore() const { return !m_nextCursor.isEmpty(); }
    [[nodiscard]] QString status() const { return m_status; }
    [[nodiscard]] QString query() const { return m_query; }
    void setQuery(const QString& query);
    [[nodiscard]] QString view() const { return m_view; }
    void setView(const QString& view);
    [[nodiscard]] QString countText() const;

    // The first page again (the tab opens, the library changed).
    Q_INVOKABLE void reload();
    Q_INVOKABLE void loadMore();
    // Opens the artist's page.
    Q_INVOKABLE void openCard(int row);
    // The card's Follow / Unfollow; a toast says how it went.
    Q_INVOKABLE void toggleFollow(int row);

signals:
    void activeChanged();
    void stateChanged();
    void queryChanged();

private:
    void onModeChanged();
    void request(bool append);
    void apply(const core::account::LibraryArtistPage& page, bool append);
    [[nodiscard]] static QString captionFor(const core::account::LibraryArtist& artist);

    core::account::AccountClient& m_client;
    core::account::AccountSessionService& m_session;
    core::mode::ModeService& m_mode;
    core::ArtistFollowService& m_follows;
    Navigator& m_navigator;
    ToastQueue* m_toasts = nullptr;
    core::SettingsStore* m_settings = nullptr;

    CollectionCardModel m_cards;
    // The public id of each card, by row.
    QList<QString> m_ids;
    core::RequestScope m_requests;
    QTimer m_searchDelay;
    QString m_view = QStringLiteral("default");
    // The listed artists' identities, by row (for following).
    QList<core::ArtistIdentity> m_identities;
    std::optional<qint64> m_totalCount;
    QString m_query;
    QString m_nextCursor;
    QString m_status;
    bool m_loading = false;
    bool m_active = false;
};

} // namespace lmp::ui
