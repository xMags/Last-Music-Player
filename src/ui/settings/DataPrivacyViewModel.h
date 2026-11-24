#pragma once

#include <QObject>
#include <QString>
#include <QtQml/qqmlregistration.h>

#include <optional>

namespace lmp::core {
class AppDataReset;
namespace account {
class AccountClient;
class AccountSessionService;
class MusicSyncService;
} // namespace account
} // namespace lmp::core

namespace lmp::ui {

// Settings' Data and privacy in Account mode (`App.dataPrivacy`;
// views/settings.ts 203-221 and runConfirmedAction): clearing the account's
// listening history on the service, and resetting the app's data on this PC
// (core::AppDataReset). One of them runs at a time; `working` says which,
// and the outcome is the feedback line under the card.
//
// GUI thread only.
class DataPrivacyViewModel final : public QObject {
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Provided by App.dataPrivacy")

    // The clear-history row's description, and whether there is a history.
    Q_PROPERTY(QString historySubtitle READ historySubtitle NOTIFY changed FINAL)
    Q_PROPERTY(bool hasHistory READ hasHistory NOTIFY changed FINAL)
    // "", "history" or "reset".
    Q_PROPERTY(QString working READ working NOTIFY changed FINAL)
    Q_PROPERTY(QString message READ message NOTIFY changed FINAL)
    Q_PROPERTY(bool messageError READ messageError NOTIFY changed FINAL)

public:
    // How many plays the library answer carries (the reference's
    // HistoryLimit); a history this long may hold more.
    static constexpr int HistoryLimit = 300;

    struct Parts {
        core::account::AccountClient& client;
        core::account::AccountSessionService& session;
        core::account::MusicSyncService& sync;
        core::AppDataReset& reset;
    };

    // Everything in `parts` outlives this object.
    explicit DataPrivacyViewModel(const Parts& parts, QObject* parent = nullptr);

    [[nodiscard]] QString historySubtitle() const;
    [[nodiscard]] bool hasHistory() const;
    [[nodiscard]] QString working() const { return m_working; }
    [[nodiscard]] QString message() const { return m_message; }
    [[nodiscard]] bool messageError() const { return m_messageError; }

    // Both run after their confirmation dialog.
    Q_INVOKABLE void clearHistory();
    Q_INVOKABLE void resetAppData();

    // The reference's historySubtitle for `count` synced songs.
    [[nodiscard]] static QString historySubtitleFor(int count);

signals:
    void changed();

private:
    [[nodiscard]] int historyCount() const;
    void finish(const QString& message, bool error);

    Parts m_parts;
    // After a clear, the history reads as empty until a newer library pull
    // (the library revision it was cleared at) says otherwise.
    std::optional<quint64> m_clearedAtRevision;
    QString m_working;
    QString m_message;
    bool m_messageError = false;
};

} // namespace lmp::ui
