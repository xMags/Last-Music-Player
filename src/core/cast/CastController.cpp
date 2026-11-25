#include "core/cast/CastController.h"

#include "core/cast/CastSession.h"
#include "core/cast/CastTransport.h"
#include "core/model/DisplayTitle.h"
#include "core/playback/PlayerController.h"

#include <QtConcurrent/QtConcurrentRun>

namespace lmp::core::cast {

// The account resolver, with only web addresses let through.
class CastController::Resolver final : public TrackSourceResolver {
public:
    explicit Resolver(TrackSourceResolver& account) : m_account(account) {}

    [[nodiscard]] QFuture<Result<QUrl>> resolve(const Track& track) override { return checked(m_account.resolve(track)); }
    [[nodiscard]] QFuture<Result<QUrl>> refresh(const Track& track) override { return checked(m_account.refresh(track)); }

private:
    [[nodiscard]] static QFuture<Result<QUrl>> checked(QFuture<Result<QUrl>> future)
    {
        return future.then([](const Result<QUrl>& answer) -> Result<QUrl> {
            if (!answer) {
                return answer;
            }
            const QString scheme = answer.value().scheme();
            if (scheme != QLatin1StringView("https") && scheme != QLatin1StringView("http")) {
                return Failure::of(FailureKind::NotAvailable, QStringLiteral("Only web addresses can be cast."));
            }
            return answer;
        });
    }

    TrackSourceResolver& m_account;
};

class CastController::Sink final : public IPlaybackSink {
public:
    explicit Sink(CastController& owner) : m_owner(owner) {}

    [[nodiscard]] platform::IAudioPlayer& audio() override { return m_owner.m_audio; }
    [[nodiscard]] TrackSourceResolver& resolver() override { return *m_owner.m_resolver; }
    [[nodiscard]] bool accepts(const Track& track) const override { return m_owner.accepts(track); }

private:
    CastController& m_owner;
};

CastController::CastController(Dependencies dependencies, QObject* parent)
    : QObject(parent)
    , m_deps(std::move(dependencies))
    , m_resolver(std::make_unique<Resolver>(m_deps.accountResolver))
    , m_sink(std::make_unique<Sink>(*this))
{
    connect(&m_deps.player, &PlayerController::sinkRefused, this, &CastController::onSinkRefused);
    // What the device shows for each song (the earlier versions sent no
    // picture in Account mode).
    connect(&m_deps.player, &PlayerController::currentTrackChanged, this, [this]() {
        const std::optional<Track>& track = m_deps.player.currentTrack();
        if (track) {
            m_audio.setMetadata(displayTitle(track->title), track->artist, QString());
        }
    });
}

CastController::~CastController()
{
    shutdown();
}

bool CastController::accepts(const Track& track) const
{
    return m_deps.castAllowed() && track.isRemote() && !track.unavailable;
}

QString CastController::refusalFor(const std::optional<Track>& track) const
{
    if (m_deps.apiKeyMode()) {
        return tr("Casting API-key tracks is disabled to keep the API key on this PC.");
    }
    if (!track) {
        return tr("Play an account track before connecting a Cast device.");
    }
    if (!accepts(*track)) {
        return m_deps.castAllowed() ? tr("This track cannot be cast. Play an account track with an available source first.")
                                    : tr("Play an account track before connecting a Cast device.");
    }
    return {};
}

QString CastController::refusalForCurrent() const
{
    return refusalFor(m_deps.player.currentTrack());
}

void CastController::setPhase(Phase phase)
{
    if (m_phase == phase) {
        return;
    }
    m_phase = phase;
    emit phaseChanged();
}

void CastController::scan()
{
    if (m_shuttingDown || m_scanning) {
        return;
    }
    m_scanning = true;
    emit scanningChanged();
    platform::IDnsServiceBrowser* browser = &m_deps.browser;
    const int timeout = m_deps.scanTimeoutMs;
    m_scanStop = std::make_shared<std::atomic<bool>>(false);
    // The flag is shared, so the worker never reads a destroyed one.
    m_scan = QtConcurrent::run([browser, timeout, stop = m_scanStop]() {
        return discoverCastDevices(*browser, timeout, DiscoveryLimits{.stop = stop.get()});
    });
    m_scan.then(this, [this](const QList<CastDevice>& devices) {
        if (m_shuttingDown) {
            return;
        }
        m_devices = devices;
        m_scanning = false;
        emit devicesChanged();
        emit scanningChanged();
    });
}

void CastController::connectTo(const CastDevice& device)
{
    if (m_shuttingDown || m_phase == Phase::Connecting) {
        return;
    }
    if (const QString refusal = refusalForCurrent(); !refusal.isEmpty()) {
        emit notice(refusal);
        return;
    }
    if (m_device && m_device->id == device.id && m_phase == Phase::Casting) {
        return;
    }
    endSession();
    m_device = device;
    m_session = std::make_unique<CastSession>(m_deps.makeTransport());
    connect(m_session.get(), &CastSession::ready, this, &CastController::onReady);
    connect(m_session.get(), &CastSession::failed, this, &CastController::onFailed);
    connect(m_session.get(), &CastSession::closed, this, &CastController::onClosed);
    setPhase(Phase::Connecting);
    m_session->start(device.host, device.port);
}

void CastController::onReady()
{
    // The song may have changed while the device was set up.
    if (const QString refusal = refusalForCurrent(); !refusal.isEmpty()) {
        endSession();
        setPhase(Phase::Idle);
        emit notice(tr("The track changed to one that cannot be cast, so casting was cancelled."));
        return;
    }
    m_audio.setSession(m_session.get());
    if (const std::optional<Track>& track = m_deps.player.currentTrack()) {
        m_audio.setMetadata(displayTitle(track->title), track->artist, QString());
    }
    setPhase(Phase::Casting);
    m_deps.player.setSink(m_sink.get(), PlayerController::SinkHandover::KeepPlaying);
}

void CastController::onFailed(const QString& reason)
{
    const QString name = m_device ? m_device->name : QString();
    endSession();
    setPhase(Phase::Idle);
    QString text = name.isEmpty() ? tr("Could not connect") : tr("Could not connect to %1").arg(name);
    if (!reason.isEmpty()) {
        text += QStringLiteral(": ") + reason;
    }
    emit notice(text);
}

void CastController::onClosed(const QString& reason)
{
    Q_UNUSED(reason)
    const QString name = deviceName();
    // The device is gone: the song stays here, paused where it was; Play
    // opens it on this PC.
    m_deps.player.setSink(nullptr, PlayerController::SinkHandover::Stop);
    endSession();
    setPhase(Phase::Idle);
    emit notice(name.isEmpty() ? tr("Casting ended.") : tr("Casting to %1 ended.").arg(name));
}

void CastController::onSinkRefused(const Track& track)
{
    Q_UNUSED(track)
    if (!m_session) {
        return;
    }
    const bool apiKey = m_deps.apiKeyMode();
    endSession();
    setPhase(Phase::Idle);
    emit notice(apiKey ? tr("Casting API-key tracks is disabled to keep the API key on this PC.")
                       : tr("The track changed to one that cannot be cast, so casting was cancelled."));
}

void CastController::stopCasting()
{
    if (!m_session) {
        return;
    }
    m_deps.player.setSink(nullptr, PlayerController::SinkHandover::KeepPlaying);
    endSession();
    setPhase(Phase::Idle);
}

void CastController::refreshAllowed()
{
    if (m_session && !m_deps.castAllowed()) {
        m_deps.player.setSink(nullptr, PlayerController::SinkHandover::Stop);
        endSession();
        setPhase(Phase::Idle);
    }
}

void CastController::endSession()
{
    if (m_deps.player.usesExternalSink()) {
        m_deps.player.setSink(nullptr, PlayerController::SinkHandover::Stop);
    }
    m_audio.setSession(nullptr);
    if (m_session) {
        m_session->disconnect(this);
        m_session->stop();
        m_session->disconnectFromDevice();
        // This may run inside one of the session's own signals.
        m_session.release()->deleteLater();
    }
    m_device.reset();
}

void CastController::shutdown()
{
    if (m_shuttingDown) {
        return;
    }
    m_shuttingDown = true;
    endSession();
    // The scan stops at its next resolve: the wait is one resolve at most.
    if (m_scanStop) {
        m_scanStop->store(true);
    }
    if (m_scan.isRunning()) {
        m_scan.waitForFinished();
    }
}

} // namespace lmp::core::cast
