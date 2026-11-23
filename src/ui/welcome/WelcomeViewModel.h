#pragma once

#include <QList>
#include <QObject>
#include <QSet>
#include <QString>
#include <QStringList>
#include <QVariantList>
#include <QtQml/qqmlregistration.h>

#include <optional>

#include "core/account/WelcomeModel.h"
#include "core/async/RequestScope.h"

namespace lmp::core {
class SettingsStore;
namespace account {
class AccountClient;
class AccountSessionService;
class MusicSyncService;
} // namespace account
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

public:
    struct Parts {
        core::account::AccountClient& client;
        core::account::AccountSessionService& session;
        core::account::MusicSyncService& sync;
        core::db::AccountRepository& accounts;
        core::SettingsStore& settings;
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

    // Opens the flow at its first step. `firstRun` gates the app with the
    // minimums; otherwise it can be closed.
    void open(bool firstRun);
    Q_INVOKABLE void toggleLanguage(const QString& code);
    Q_INVOKABLE void toggleArtist(const QString& key);
    // The next step, or finishing on the last.
    Q_INVOKABLE void next();
    Q_INVOKABLE void back();
    Q_INVOKABLE void close();

signals:
    void visibleChanged();
    void changed();

private:
    void checkAfterSync();
    void loadLanguages();
    void loadArtists();
    void finish();
    [[nodiscard]] QString region() const;
    void setStatus(const QString& status);

    core::account::AccountClient& m_client;
    core::account::AccountSessionService& m_session;
    core::account::MusicSyncService& m_sync;
    core::db::AccountRepository& m_accounts;
    core::SettingsStore& m_settings;

    bool m_visible = false;
    core::account::WelcomeMode m_mode = core::account::WelcomeMode::FirstRun;
    QString m_step;
    QList<core::account::LanguageInfo> m_catalog;
    QStringList m_selectedLanguages;
    QList<core::account::WelcomeArtist> m_grid;
    QSet<QString> m_picked;
    // Keys with a follow change on its way.
    QSet<QString> m_pending;
    core::account::ArtistWaiver m_waiver = core::account::ArtistWaiver::None;
    bool m_busy = false;
    QString m_status;
    // Accounts already checked this run.
    QSet<QString> m_checkedOwners;
    core::RequestScope m_loads;
};

} // namespace lmp::ui
