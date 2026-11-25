#pragma once

#include <QList>
#include <QObject>
#include <QSet>
#include <QString>
#include <QHash>
#include <QStringList>
#include <QJsonObject>
#include <QTimer>
#include <QVariantList>
#include <QtQml/qqmlregistration.h>

#include <functional>
#include <optional>

#include "core/account/WelcomeModel.h"
#include "core/async/RequestScope.h"
#include "core/async/Result.h"

namespace lmp::core {
class SettingsStore;
namespace account {
class AccountClient;
class AccountSessionService;
class MusicSyncService;
} // namespace account
namespace catalog {
class CatalogService;
}
namespace db {
class AccountRepository;
}
} // namespace lmp::core

namespace lmp::ui {

// The welcome flow for a new account (App.welcome), as the reference client
// shows it: the languages the listener listens to, then at least three
// artists to follow, then the account is marked as welcomed. It opens once,
// after the first library sync of an account that never finished it and
// holds no music; a Debug build can also open it with --show-welcome.
//
// GUI thread only.
class WelcomeViewModel final : public QObject {
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Provided by App.welcome")

    Q_PROPERTY(bool visible READ visible NOTIFY visibleChanged FINAL)
    // "languages" or "artists".
    Q_PROPERTY(QString step READ step NOTIFY changed FINAL)
    // The flow can be closed without finishing (opened later, or by hand).
    Q_PROPERTY(bool closable READ closable NOTIFY changed FINAL)
    // One map per language: code, name, nativeName, selected.
    Q_PROPERTY(QVariantList languages READ languages NOTIFY changed FINAL)
    // One map per artist: key, title, artworkUrl, picked.
    Q_PROPERTY(QVariantList artists READ artists NOTIFY changed FINAL)
    Q_PROPERTY(bool canContinue READ canContinue NOTIFY changed FINAL)
    // How many more artists Continue needs.
    Q_PROPERTY(int missing READ missing NOTIFY changed FINAL)
    Q_PROPERTY(bool busy READ busy NOTIFY changed FINAL)
    Q_PROPERTY(QString status READ status NOTIFY changed FINAL)
    // The words of the step (welcome-flow.ts): the eyebrow, title and
    // sentence, the footer's hint and its button.
    Q_PROPERTY(QString eyebrow READ eyebrow NOTIFY changed FINAL)
    Q_PROPERTY(QString title READ title NOTIFY changed FINAL)
    Q_PROPERTY(QString subtitle READ subtitle NOTIFY changed FINAL)
    Q_PROPERTY(QString hint READ hint NOTIFY changed FINAL)
    Q_PROPERTY(QString nextLabel READ nextLabel NOTIFY changed FINAL)
    // Why the artist minimum is lifted, or "".
    Q_PROPERTY(QString notice READ notice NOTIFY changed FINAL)
    // The languages grouped as Settings groups them ({id, label, languages:
    // [{code, name, nativeName, selected}]}), filtered by languageQuery.
    Q_PROPERTY(QVariantList languageGroups READ languageGroups NOTIFY changed FINAL)
    Q_PROPERTY(QString languageQuery READ languageQuery WRITE setLanguageQuery NOTIFY changed FINAL)
    Q_PROPERTY(bool languagesLoading READ languagesLoading NOTIFY changed FINAL)
    Q_PROPERTY(bool languagesFailed READ languagesFailed NOTIFY changed FINAL)
    // The artist search: what is typed, "idle", "loading", "ready" or
    // "error", and the artists found ({key, title, artworkUrl, picked}).
    Q_PROPERTY(QString artistQuery READ artistQuery WRITE setArtistQuery NOTIFY changed FINAL)
    Q_PROPERTY(QString searchStatus READ searchStatus NOTIFY changed FINAL)
    Q_PROPERTY(QVariantList searchResults READ searchResults NOTIFY changed FINAL)
    // The footer's picked faces (the newest five, {key, title, artworkUrl}),
    // how many older picks are counted as "+N", and rings still to fill.
    Q_PROPERTY(QVariantList trayFaces READ trayFaces NOTIFY changed FINAL)
    Q_PROPERTY(int trayMore READ trayMore NOTIFY changed FINAL)

public:
    struct Parts {
        core::account::AccountClient& client;
        core::account::AccountSessionService& session;
        core::account::MusicSyncService& sync;
        core::db::AccountRepository& accounts;
        core::SettingsStore& settings;
        // The artist search; without it the step offers none.
        core::catalog::CatalogService* catalog = nullptr;
        // The region's name for "Suggested for <country>".
        std::function<QString()> regionName = {};
    };

    // Everything in `parts` outlives this object.
    explicit WelcomeViewModel(Parts parts, QObject* parent = nullptr);

    [[nodiscard]] bool visible() const { return m_visible; }
    [[nodiscard]] QString step() const { return m_step; }
    [[nodiscard]] bool closable() const { return m_mode == core::account::WelcomeMode::Tune; }
    [[nodiscard]] QVariantList languages() const;
    [[nodiscard]] QVariantList artists() const;
    [[nodiscard]] bool canContinue() const;
    [[nodiscard]] int missing() const;
    [[nodiscard]] bool busy() const { return m_busy; }
    [[nodiscard]] QString status() const { return m_status; }
    [[nodiscard]] QString eyebrow() const;
    [[nodiscard]] QString title() const;
    [[nodiscard]] QString subtitle() const;
    [[nodiscard]] QString hint() const;
    [[nodiscard]] QString nextLabel() const;
    [[nodiscard]] QString notice() const;
    [[nodiscard]] QVariantList languageGroups() const;
    [[nodiscard]] QString languageQuery() const { return m_languageQuery; }
    void setLanguageQuery(const QString& query);
    [[nodiscard]] bool languagesLoading() const { return m_busy && m_catalog.isEmpty(); }
    [[nodiscard]] bool languagesFailed() const { return m_languagesFailed; }
    [[nodiscard]] QString artistQuery() const { return m_artistQuery; }
    void setArtistQuery(const QString& query);
    [[nodiscard]] QString searchStatus() const { return m_searchStatus; }
    [[nodiscard]] QVariantList searchResults() const;
    [[nodiscard]] QVariantList trayFaces() const;
    [[nodiscard]] int trayMore() const;

    // Opens the flow at its first step. `firstRun` gates the app with the
    // minimums; otherwise it can be closed.
    void open(bool firstRun);
    // Settings' "Tune your recommendations": the flow, closable.
    Q_INVOKABLE void openTune() { open(false); }
    Q_INVOKABLE void toggleLanguage(const QString& code);
    Q_INVOKABLE void toggleArtist(const QString& key);
    // The next step, or finishing on the last.
    Q_INVOKABLE void next();
    Q_INVOKABLE void back();
    Q_INVOKABLE void close();
    // The languages again, after they failed or the region changed.
    Q_INVOKABLE void reloadLanguages();
    // The groups again, the choices kept (the region list arrived or changed).
    Q_INVOKABLE void regroup() { emit changed(); }
    Q_INVOKABLE void reloadArtists() { loadArtists(); }

signals:
    void visibleChanged();
    void changed();

private:
    void followOwner(const QString& ownerId);
    void checkAfterSync();
    void loadLanguages();
    void loadArtists();
    void finish();
    [[nodiscard]] QString region() const;
    void setStatus(const QString& status);
    void runSearch();
    // A follow or unfollow came back.
    void settleFollow(const QString& key, bool pick, const core::Result<void>& result);
    // addSimilar: a few artists like the picked one join the grid after it.
    void addSimilar(const QString& key);
    // The step's preferences went to the service; `done` runs once they did.
    void saveThen(const QJsonObject& body, const QString& failure, const std::function<void()>& done);
    [[nodiscard]] QVariantMap artistMap(const core::account::WelcomeArtist& artist) const;

    core::account::AccountClient& m_client;
    core::account::AccountSessionService& m_session;
    core::account::MusicSyncService& m_sync;
    core::db::AccountRepository& m_accounts;
    core::SettingsStore& m_settings;
    core::catalog::CatalogService* m_catalogService = nullptr;
    std::function<QString()> m_regionName;

    bool m_visible = false;
    core::account::WelcomeMode m_mode = core::account::WelcomeMode::FirstRun;
    QString m_step;
    QList<core::account::LanguageInfo> m_catalog;
    QStringList m_selectedLanguages;
    QList<core::account::WelcomeArtist> m_grid;
    QSet<QString> m_picked;
    // Picks in the order made, for the footer's faces.
    QStringList m_pickOrder;
    // The public id of an artist followed here, to undo the follow.
    QHash<QString, QString> m_followIds;
    std::optional<core::account::RegionLanguageTable> m_regions;
    QString m_languageQuery;
    bool m_languagesFailed = false;
    bool m_saving = false;
    QString m_artistQuery;
    QString m_searchStatus = QStringLiteral("idle");
    QList<core::account::WelcomeArtist> m_searchResults;
    QTimer m_searchDelay;
    core::RequestScope m_searches;
    // Keys with a follow change on its way.
    QSet<QString> m_pending;
    core::account::ArtistWaiver m_waiver = core::account::ArtistWaiver::None;
    bool m_busy = false;
    QString m_status;
    // The account the flow was opened for (empty while its session was still
    // being checked).
    QString m_owner;
    // Accounts already checked this run.
    QSet<QString> m_checkedOwners;
    core::RequestScope m_loads;
};

} // namespace lmp::ui
