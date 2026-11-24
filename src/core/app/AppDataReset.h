#pragma once

#include <QFuture>
#include <QObject>

namespace lmp::core {

class PlayerController;
class SettingsStore;
class StreamCache;

namespace account {
class AccountSessionService;
class AccountSignOutCoordinator;
} // namespace account

namespace remote {
class ArtworkService;
}

// Settings' "Reset app data": signs out and erases what the app keeps for
// itself on this PC, as the reference client's reset does. It ends the
// session with the service first; when the service cannot be reached the
// session is still forgotten here, since a reset must work offline. Then the
// queue is cleared, the preferences go back to their defaults
// (resetPreferencesToDefaults) and the stream and artwork caches are
// emptied. The music library, downloads and music files stay.
//
// GUI thread only; the future finishes there. A second run while one is
// under way does nothing and finishes at once with false.
class AppDataReset final : public QObject {
    Q_OBJECT

public:
    struct Dependencies {
        account::AccountSignOutCoordinator& signOut;
        account::AccountSessionService& session;
        PlayerController& player;
        SettingsStore& settings;
        StreamCache& streamCache;
        remote::ArtworkService& artwork;
    };

    // Every dependency outlives this object.
    explicit AppDataReset(const Dependencies& dependencies, QObject* parent = nullptr);

    // True once everything was erased; false when a cache could not be
    // emptied (the rest is still done) or a reset was already running.
    [[nodiscard]] QFuture<bool> run();
    [[nodiscard]] bool running() const { return m_running; }

private:
    [[nodiscard]] bool eraseLocalData();

    Dependencies m_deps;
    bool m_running = false;
};

} // namespace lmp::core
