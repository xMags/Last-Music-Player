#pragma once

#include <QObject>
#include <QString>
#include <QTimer>

#include <chrono>

namespace lmp::core {
class LibraryScanner;
class PlayerController;
class SettingsStore;
class UserDataOperationGate;
} // namespace lmp::core

namespace lmp::core::account {
class AccountSessionService;
class SecretStore;
} // namespace lmp::core::account

namespace lmp::core::db {
class TrackRepository;
}

namespace lmp::core::mode {
class ModeService;
}

namespace lmp::core {

// "Delete all data", as the current app's clean-up does it
// (MainWindow.Settings.cpp WipeAllData_Click): admissions to user data close
// (a second clean-up is refused), the scan and any sign-in stop, the player
// forgets its song and queue, and once every running data operation has
// finished (at most 30 seconds; otherwise it stops and changes nothing) it
// removes the signed-in session and the API key, returns to songs on this PC,
// deletes the saved playback state, clears the library database, removes the
// cover cache and resets every setting. Music files are never touched.
// Admissions reopen at the end, however it ended.
//
// GUI thread only.
class UserDataCleanup : public QObject {
    Q_OBJECT

public:
    enum class Outcome {
        Done,
        // Some part could not be cleared (the others were).
        Partly,
        AlreadyRunning,
        // A running operation did not finish in time; nothing was cleared.
        StillBusy,
    };
    Q_ENUM(Outcome)

    struct Parts {
        UserDataOperationGate& gate;
        LibraryScanner& scanner;
        PlayerController& player;
        db::TrackRepository& tracks;
        SettingsStore& settings;
        account::SecretStore& secrets;
        account::AccountSessionService& session;
        mode::ModeService& mode;
        // The saved playback state file and the cover cache folder.
        QString stateFile;
        QString coverFolder;
        // How long running operations may take to finish (the current app's 30 seconds).
        std::chrono::milliseconds idleTimeout{std::chrono::seconds(30)};
    };

    // Everything in `parts` outlives this object.
    explicit UserDataCleanup(Parts parts, QObject* parent = nullptr);

    // Starts a clean-up; finished() reports how it ended, always later (on
    // the event loop), except AlreadyRunning, which is reported at once.
    void start();
    [[nodiscard]] bool running() const { return m_running; }

signals:
    void runningChanged();
    // `outcome` and, for Partly, what could not be cleared.
    void finished(lmp::core::UserDataCleanup::Outcome outcome, const QString& detail);
    // The data is gone: views read again.
    void cleared();

private:
    void waitForIdle();
    void clearEverything();
    void finish(Outcome outcome, const QString& detail);

    Parts m_parts;
    QTimer m_poll;
    std::chrono::steady_clock::time_point m_deadline;
    bool m_running = false;
};

} // namespace lmp::core
