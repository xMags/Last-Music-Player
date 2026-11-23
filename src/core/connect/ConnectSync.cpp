#include "core/connect/ConnectSync.h"

#include "core/connect/ConnectClient.h"
#include "core/connect/ConnectTracks.h"
#include "core/playback/PlayerController.h"
#include "core/playback/QueueEngine.h"
#include "core/playback/QueueTrackIdentity.h"

#include <QJsonArray>
#include <QJsonDocument>

#include <algorithm>
#include <cmath>
#include <utility>

namespace lmp::core::connect {

namespace {

constexpr std::chrono::milliseconds TickInterval{500};
constexpr std::chrono::milliseconds SeekSendDelay{200};
constexpr std::chrono::milliseconds VolumeSendDelay{150};

[[nodiscard]] std::uint64_t later(std::uint64_t now, std::chrono::milliseconds delay)
{
    return now + static_cast<std::uint64_t>(delay.count());
}

[[nodiscard]] Command simple(Command::Type type)
{
    Command command;
    command.type = type;
    return command;
}

} // namespace

ConnectSync::ConnectSync(Dependencies dependencies, QObject* parent)
    : QObject(parent)
    , m_dependencies(std::move(dependencies))
{
    PlaybackClock& clock = m_dependencies.clock;
    m_publishTimer = clock.createTimer([this]() { publish(); });
    m_heartbeat = clock.createTimer([this]() {
        if (m_dependencies.client.isActive() && localPlaying()) {
            schedulePublish(true);
        }
    });
    m_ticker = clock.createTimer([this]() {
        if (m_remoteMode && m_remote.playing) {
            emit remotePositionChanged();
            m_ticker->start(TickInterval);
        }
    });
    m_seekTimer = clock.createTimer([this]() {
        Command command = simple(Command::Type::Seek);
        command.positionSeconds = m_pendingSeek;
        send(command);
    });
    m_volumeTimer = clock.createTimer([this]() {
        Command command = simple(Command::Type::Volume);
        command.value = m_pendingVolume;
        send(command);
    });

    ConnectClient& client = m_dependencies.client;
    connect(&client, &ConnectClient::sessionChanged, this, &ConnectSync::onSession);
    connect(&client, &ConnectClient::commandReceived, this, &ConnectSync::onCommand);
    connect(&client, &ConnectClient::takeoverReceived, this, &ConnectSync::onTakeover);
    connect(&client, &ConnectClient::stopReceived, this, &ConnectSync::onStop);
    connect(&client, &ConnectClient::errorReceived, this,
            [this](const QString& code, const QString&) { onError(code); });
    connect(&client, &ConnectClient::devicesChanged, this, [this]() {
        if (m_remoteMode) {
            showRemote(m_dependencies.client.session());
        }
    });

    PlayerController& player = m_dependencies.player;
    const auto changed = [this]() { schedulePublish(false); };
    connect(&player, &PlayerController::currentTrackChanged, this, changed);
    connect(&player, &PlayerController::stateChanged, this, changed);
    connect(&player, &PlayerController::positionChanged, this, changed);
    connect(&player, &PlayerController::durationChanged, this, changed);
    connect(&player, &PlayerController::volumeChanged, this, changed);
    connect(&player, &PlayerController::shuffleChanged, this, changed);
    connect(&player, &PlayerController::repeatModeChanged, this, changed);
    connect(&player, &PlayerController::queueChanged, this, changed);
}

ConnectSync::~ConnectSync() = default;

double ConnectSync::remotePosition() const
{
    if (!m_remote.playing) {
        return m_remoteClockPosition;
    }
    const std::uint64_t now = m_dependencies.clock.nowMilliseconds();
    const double moved =
        m_remoteClockPosition + static_cast<double>(now > m_remoteClockAt ? now - m_remoteClockAt : 0) / 1000.0;
    return m_remote.durationSeconds > 0.0 ? std::min(m_remote.durationSeconds, moved) : moved;
}

void ConnectSync::remoteTogglePlay()
{
    if (!m_remoteMode || !m_remote.track) {
        return;
    }
    const bool play = !m_remote.playing;
    send(simple(play ? Command::Type::Play : Command::Type::Pause));
    setRemoteClock(remotePosition(), play);
    emit remoteChanged();
}

void ConnectSync::remoteNext()
{
    if (m_remoteMode) {
        send(simple(Command::Type::Next));
    }
}

void ConnectSync::remotePrevious()
{
    if (m_remoteMode) {
        send(simple(Command::Type::Previous));
    }
}

void ConnectSync::remoteSeek(double seconds)
{
    if (!m_remoteMode || !std::isfinite(seconds)) {
        return;
    }
    double position = std::max(0.0, seconds);
    if (m_remote.durationSeconds > 0.0) {
        position = std::min(m_remote.durationSeconds, position);
    }
    const std::uint64_t now = m_dependencies.clock.nowMilliseconds();
    m_seekHeldUntil = later(now, ControlSettle);
    setRemoteClock(position, m_remote.playing);
    m_pendingSeek = position;
    m_seekTimer->start(SeekSendDelay);
    emit remotePositionChanged();
}

void ConnectSync::remoteSetVolume(int volume)
{
    if (!m_remoteMode) {
        return;
    }
    m_remote.volume = std::clamp(volume, 0, 100);
    m_volumeHeldUntil = later(m_dependencies.clock.nowMilliseconds(), ControlSettle);
    m_pendingVolume = m_remote.volume;
    m_volumeTimer->start(VolumeSendDelay);
    emit remoteChanged();
}

void ConnectSync::remoteSetShuffle(bool shuffle)
{
    if (!m_remoteMode) {
        return;
    }
    Command command = simple(Command::Type::Shuffle);
    command.enabled = shuffle;
    send(command);
    m_remote.shuffle = shuffle;
    emit remoteChanged();
}

void ConnectSync::remoteSetRepeatMode(int repeatMode)
{
    if (!m_remoteMode) {
        return;
    }
    Command command = simple(Command::Type::Repeat);
    command.value = std::clamp(repeatMode, 0, 2);
    send(command);
    m_remote.repeatMode = command.value;
    emit remoteChanged();
}

void ConnectSync::remotePlayUpcoming(int position)
{
    if (!m_remoteMode || position < 0 || position >= m_remote.upcoming.size()) {
        return;
    }
    Command command = simple(Command::Type::PlayUpcoming);
    command.value = position;
    send(command);
}

void ConnectSync::remoteClearUpcoming()
{
    if (m_remoteMode) {
        send(simple(Command::Type::SetUpcoming));
    }
}

void ConnectSync::playOn(const QString& deviceId)
{
    ConnectClient& client = m_dependencies.client;
    if (client.status() != ConnectClient::Status::Open) {
        emit notice(tr("Your other devices are not reachable right now."));
        return;
    }
    if (deviceId == client.deviceId() && !client.isRemote()) {
        return;
    }
    if (!client.isActive() && !client.isRemote()) {
        QHash<QString, Track> reported;
        Playback state = snapshot(reported);
        if (state.track.isEmpty()) {
            emit notice(tr("Play something first, then choose where it plays."));
            return;
        }
        // The claim rule: a paused session may be reported right before a transfer.
        state.playing = false;
        if (client.sendState(state)) {
            m_reported = std::move(reported);
            m_hasReported = true;
        }
    }
    (void)client.transfer(deviceId, true);
}

void ConnectSync::onSession()
{
    ConnectClient& client = m_dependencies.client;
    const Session& session = client.session();
    if (client.isRemote()) {
        m_lastSent.reset();
        showRemote(session);
        return;
    }
    if (m_remoteMode) {
        leaveRemote(true);
    }
    if (client.status() != ConnectClient::Status::Open) {
        return;
    }
    // The server echoes every report as the session; the echo alone must not
    // force another report (the ordinary "anything changed?" check is
    // enough). A full report is forced only when the server may not hold
    // this PC's playback: no active device or no state (a fresh or restarted
    // server), or no report since this PC became active.
    const bool reportedHere = client.isActive() && session.state.has_value() && m_lastSent.has_value();
    schedulePublish(!reportedHere);
}

void ConnectSync::onCommand(const Command& command)
{
    if (m_remoteMode) {
        return;
    }
    PlayerController& player = m_dependencies.player;
    using Type = Command::Type;
    switch (command.type) {
    case Type::Play:
        if (!localPlaying()) {
            player.togglePlay();
        }
        return;
    case Type::Pause:
        if (localPlaying()) {
            player.togglePlay();
        }
        return;
    case Type::Toggle:
        player.togglePlay();
        return;
    case Type::Next:
        player.next();
        return;
    case Type::Previous:
        player.previous();
        return;
    case Type::Seek:
        player.seek(command.positionSeconds);
        return;
    case Type::Volume:
        player.setVolume(static_cast<double>(command.value) / 100.0);
        return;
    case Type::Shuffle:
        player.setShuffle(command.enabled ? Shuffle::On : Shuffle::Off);
        return;
    case Type::Repeat:
        player.setRepeatMode(static_cast<RepeatMode>(std::clamp(command.value, 0, 2)));
        return;
    case Type::PlayTracks: {
        const QList<Track> tracks = tracksFor(command.tracks);
        if (tracks.isEmpty()) {
            return;
        }
        const qsizetype index = std::clamp<qsizetype>(command.value, 0, tracks.size() - 1);
        player.playInContext(tracks.at(index), tracks, command.enabled ? Shuffle::On : Shuffle::Off);
        return;
    }
    case Type::PlayUpcoming:
        player.playVisibleQueueEntry(command.value);
        return;
    case Type::SetUpcoming: {
        if (command.tracks.isEmpty()) {
            player.clearUpcoming();
            return;
        }
        QList<Track> visible;
        const QList<QueueEntry> entries = player.visibleQueue();
        visible.reserve(entries.size());
        for (const QueueEntry& entry : entries) {
            visible.append(entry.track);
        }
        QSet<QString> seen;
        if (m_hasReported) {
            for (const Track& track : std::as_const(m_reported)) {
                seen.insert(queueTrackIdentity(track));
            }
        } else {
            // With no report to go by, the remote saw no more than the first songs.
            for (qsizetype index = 0; index < std::min(visible.size(), UpcomingLimit); ++index) {
                seen.insert(queueTrackIdentity(visible.at(index)));
            }
        }
        QList<Track> desired = tracksFor(command.tracks);
        desired.removeIf([](const Track& track) { return track.unavailable || queueTrackIdentity(track).isEmpty(); });
        player.commitVisibleOrder(QueueEngine::mergeRemoteUpcoming(visible, desired, seen));
        return;
    }
    case Type::Enqueue: {
        QList<Track> tracks = tracksFor(command.tracks);
        tracks.removeIf([](const Track& track) { return track.unavailable; });
        if (tracks.isEmpty()) {
            return;
        }
        if (command.enabled) {
            player.playNext(tracks);
        } else {
            player.addToQueue(tracks);
        }
        return;
    }
    }
}

void ConnectSync::onTakeover(const Playback& state, bool play)
{
    m_lastSent.reset();
    if (m_remoteMode) {
        leaveRemote(false);
    }
    if (!state.track.isEmpty()) {
        const Track track = trackFor(state.track);
        m_dependencies.player.takeOver(track, tracksFor(state.upcoming), state.shuffle ? Shuffle::On : Shuffle::Off,
                                       static_cast<RepeatMode>(std::clamp(state.repeatMode, 0, 2)),
                                       state.positionSeconds, play);
    }
    // Reported in any case: a song that cannot start here is reported paused,
    // and this PC stays the active device.
    schedulePublish(true);
}

void ConnectSync::onStop()
{
    m_lastSent.reset();
    m_dependencies.player.pauseForOtherDevice();
}

void ConnectSync::onError(const QString& code)
{
    QString message;
    if (code == QLatin1StringView("unknown_device")) {
        message = tr("That device is no longer connected.");
    } else if (code == QLatin1StringView("nothing_to_transfer")) {
        message = tr("Nothing is playing to move.");
    } else if (code == QLatin1StringView("no_active_device")) {
        message = tr("Nothing is playing on another device.");
    }
    if (!message.isEmpty()) {
        emit notice(message);
    }
}

void ConnectSync::showRemote(const Session& session)
{
    if (!m_remoteMode) {
        enterRemote();
    }
    const std::uint64_t now = m_dependencies.clock.nowMilliseconds();
    const std::optional<Track> previous = m_remote.track;
    RemoteView view;
    if (session.state && !session.state->track.isEmpty()) {
        view.track = trackFromConnect(session.state->track);
    }
    if (session.state) {
        const Playback& state = *session.state;
        view.upcoming.reserve(state.upcoming.size());
        for (const QJsonObject& object : state.upcoming) {
            view.upcoming.append(trackFromConnect(object));
        }
        view.durationSeconds =
            state.durationSeconds > 0.0 ? state.durationSeconds : (view.track ? view.track->durationSeconds : 0.0);
        view.playing = state.playing && view.track.has_value();
        view.volume = now < m_volumeHeldUntil ? m_remote.volume : state.volume;
        view.shuffle = state.shuffle;
        view.repeatMode = state.repeatMode;
    }
    const std::optional<Device> device = m_dependencies.client.activeDevice();
    const QString name = device ? device->name : tr("another device");
    const QString cast = session.state ? session.state->castDevice : QString();
    view.deviceLabel = cast.isEmpty() ? name : tr("%1 (via %2)").arg(cast, name);

    const bool sameSong = previous && view.track && queueTrackIdentity(*previous) == queueTrackIdentity(*view.track);
    const bool holdPosition = now < m_seekHeldUntil && sameSong;
    const double position = holdPosition ? remotePosition()
                            : session.state
                                ? positionAt(*session.state, session.ageMs,
                                             static_cast<qint64>(now - std::min(now, m_dependencies.client.sessionReceivedAt())))
                                : 0.0;
    m_remote = std::move(view);
    setRemoteClock(position, m_remote.playing);
    emit remoteChanged();
    emit remotePositionChanged();
}

void ConnectSync::enterRemote()
{
    m_remoteMode = true;
    m_publishTimer->stop();
    m_heartbeat->stop();
    // Nothing plays here while another device does; a song that was loading
    // must not start.
    m_dependencies.player.pauseForOtherDevice();
}

void ConnectSync::leaveRemote(bool adopt)
{
    const RemoteView last = m_remote;
    const double position = remotePosition();
    m_remoteMode = false;
    m_remote = RemoteView{};
    m_ticker->stop();
    m_seekTimer->stop();
    m_volumeTimer->stop();
    m_seekHeldUntil = 0;
    m_volumeHeldUntil = 0;
    if (adopt && last.track && !last.track->unavailable) {
        QList<Track> upcoming = last.upcoming;
        upcoming.removeIf([](const Track& track) { return track.unavailable; });
        m_dependencies.player.takeOver(*last.track, upcoming, last.shuffle ? Shuffle::On : Shuffle::Off,
                                       static_cast<RepeatMode>(std::clamp(last.repeatMode, 0, 2)), position, false);
    }
    emit remoteChanged();
    emit remotePositionChanged();
}

void ConnectSync::schedulePublish(bool force)
{
    if (force) {
        m_lastSent.reset();
    }
    if (m_remoteMode || m_publishTimer->isActive()) {
        return;
    }
    // Changes that arrive together (a new song sets several fields) make one report.
    m_publishTimer->start(PublishDelay);
}

void ConnectSync::publish()
{
    ConnectClient& client = m_dependencies.client;
    if (client.status() != ConnectClient::Status::Open || client.isRemote() || m_remoteMode) {
        return;
    }
    QHash<QString, Track> reported;
    const Playback state = snapshot(reported);
    // Only the active device reports; any other only when playback starts
    // here (a claim). A restored paused queue never claims the session.
    if (!client.isActive() && !(state.playing && !state.track.isEmpty())) {
        return;
    }
    const std::uint64_t now = m_dependencies.clock.nowMilliseconds();
    const QString signature = signatureOf(state);
    if (m_lastSent && m_lastSent->signature == signature) {
        const double expected = m_lastSent->positionSeconds
                                + (m_lastSent->playing ? static_cast<double>(now - m_lastSent->at) / 1000.0 : 0.0);
        if (std::abs(state.positionSeconds - expected) <= SeekReportSeconds) {
            return;
        }
    }
    if (!client.sendState(state)) {
        return;
    }
    m_reported = std::move(reported);
    m_hasReported = true;
    m_lastSent = Sent{.signature = signature, .positionSeconds = state.positionSeconds, .at = now, .playing = state.playing};
    m_heartbeat->start(Heartbeat);
}

Playback ConnectSync::snapshot(QHash<QString, Track>& reported) const
{
    const PlayerController& player = m_dependencies.player;
    Playback state;
    const std::optional<Track>& current = player.currentTrack();
    if (current) {
        state.track = connectTrackJson(*current);
    }
    state.playing = current.has_value() && localPlaying();
    state.positionSeconds = std::max(0.0, player.positionSeconds());
    state.durationSeconds = player.durationSeconds() > 0.0 ? player.durationSeconds()
                            : current                      ? current->durationSeconds
                                                           : 0.0;
    state.volume = static_cast<int>(std::lround(std::clamp(player.volume(), 0.0, 1.0) * 100.0));
    state.shuffle = player.shuffle() == Shuffle::On;
    state.repeatMode = static_cast<int>(player.repeatMode());
    reported.clear();
    const QList<QueueEntry> entries = player.visibleQueue();
    for (qsizetype index = 0; index < std::min(entries.size(), UpcomingLimit); ++index) {
        const Track& track = entries.at(index).track;
        const QJsonObject object = connectTrackJson(track);
        state.upcoming.append(object);
        reported.insert(connectTrackId(object), track);
    }
    state.castDevice = m_dependencies.castDeviceName ? m_dependencies.castDeviceName() : QString();
    return state;
}

QString ConnectSync::signatureOf(const Playback& state) const
{
    QJsonArray upcoming;
    for (const QJsonObject& track : state.upcoming) {
        upcoming.append(connectTrackId(track));
    }
    const QJsonArray parts{connectTrackId(state.track),
                           state.playing,
                           static_cast<qint64>(std::llround(state.durationSeconds)),
                           state.volume,
                           state.shuffle,
                           state.repeatMode,
                           state.castDevice,
                           upcoming};
    return QString::fromUtf8(QJsonDocument(parts).toJson(QJsonDocument::Compact));
}

Track ConnectSync::trackFor(const QJsonObject& object) const
{
    const auto found = m_reported.constFind(connectTrackId(object));
    if (found != m_reported.cend()) {
        return found.value();
    }
    const std::optional<Track>& current = m_dependencies.player.currentTrack();
    if (current && connectTrackId(connectTrackJson(*current)) == connectTrackId(object)) {
        return *current;
    }
    return trackFromConnect(object);
}

QList<Track> ConnectSync::tracksFor(const QList<QJsonObject>& objects) const
{
    QList<Track> tracks;
    tracks.reserve(objects.size());
    for (const QJsonObject& object : objects) {
        tracks.append(trackFor(object));
    }
    return tracks;
}

bool ConnectSync::localPlaying() const
{
    return m_dependencies.player.playingOrStarting();
}

void ConnectSync::setRemoteClock(double positionSeconds, bool playing)
{
    m_remoteClockPosition = positionSeconds;
    m_remoteClockAt = m_dependencies.clock.nowMilliseconds();
    m_remote.playing = playing;
    syncTicker();
}

void ConnectSync::syncTicker()
{
    if (!m_remoteMode || !m_remote.playing) {
        m_ticker->stop();
        return;
    }
    if (!m_ticker->isActive()) {
        m_ticker->start(TickInterval);
    }
}

void ConnectSync::send(const Command& command)
{
    (void)m_dependencies.client.sendCommand(command);
}

} // namespace lmp::core::connect
