#pragma once

#include "core/cast/CastAudioPlayer.h"
#include "core/cast/CastDiscovery.h"
#include "core/playback/IPlaybackSink.h"
#include "core/playback/TrackSourceResolver.h"

#include <QFuture>
#include <QList>
#include <QObject>
#include <QString>

#include <functional>
#include <memory>
#include <optional>

namespace lmp::platform {
class IDnsServiceBrowser;
} // namespace lmp::platform

namespace lmp::core {
class PlayerController;
} // namespace lmp::core

namespace lmp::core::cast {

class CastSession;
class ICastTransport;

// Casting, as the earlier versions offered it: scan the network for cast
// devices (3 s), connect to one, and hand the song that plays over to it at
// the point it stood. Only an account-issued stream address is ever sent to
// a device: API key mode is refused (the address would carry the key), and
// so are songs on this PC. A song that cannot be cast later on brings
// playback back to this PC and ends the session.
//
// GUI thread. The scan runs on the thread pool; shutdown() waits for it.
class CastController final : public QObject {
    Q_OBJECT

public:
    enum class Phase { Idle, Scanning, Connecting, Casting };
    Q_ENUM(Phase)

    struct Dependencies {
        platform::IDnsServiceBrowser& browser;
        PlayerController& player;
        // Account mode's stream addresses (AccountTrackSourceResolver).
        TrackSourceResolver& accountResolver;
        // The castToDevices capability.
        std::function<bool()> castAllowed;
        // Picks the refusal message.
        std::function<bool()> apiKeyMode;
        std::function<std::unique_ptr<ICastTransport>()> makeTransport;
        int scanTimeoutMs = 3000;
    };

    // Every dependency must outlive this object.
    explicit CastController(Dependencies dependencies, QObject* parent = nullptr);
    ~CastController() override;

    [[nodiscard]] Phase phase() const noexcept { return m_phase; }
    [[nodiscard]] const QList<CastDevice>& devices() const noexcept { return m_devices; }
    [[nodiscard]] QString deviceName() const { return m_device ? m_device->name : QString(); }

    // Why the song that plays cannot be cast, or empty when it can.
    [[nodiscard]] QString refusalForCurrent() const;
    [[nodiscard]] bool accepts(const Track& track) const;

    void scan();
    void connectTo(const CastDevice& device);
    // Ends casting; the song carries on here if it was playing.
    void stopCasting();
    // The castToDevices capability changed (mode, sign-out, network).
    void refreshAllowed();
    // Ends the session and waits for a running scan. Called at quit.
    void shutdown();

signals:
    void phaseChanged();
    void devicesChanged();
    // A one-line message for the listener (a refusal or a lost device).
    void notice(const QString& text);

private:
    class Sink;
    class Resolver;

    void setPhase(Phase phase);
    void onReady();
    void onFailed(const QString& reason);
    void onClosed(const QString& reason);
    void onSinkRefused(const Track& track);
    void endSession();
    [[nodiscard]] QString refusalFor(const std::optional<Track>& track) const;

    Dependencies m_deps;
    CastAudioPlayer m_audio;
    std::unique_ptr<Resolver> m_resolver;
    std::unique_ptr<Sink> m_sink;
    std::unique_ptr<CastSession> m_session;
    std::optional<CastDevice> m_device;
    QList<CastDevice> m_devices;
    QFuture<QList<CastDevice>> m_scan;
    Phase m_phase = Phase::Idle;
    bool m_shuttingDown = false;
};

} // namespace lmp::core::cast
