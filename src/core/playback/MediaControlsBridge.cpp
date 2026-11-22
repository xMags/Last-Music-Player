#include "core/playback/MediaControlsBridge.h"

#include <QDir>
#include <QUrl>

#include <optional>

#include "core/settings/SettingKeys.h"

namespace lmp::core {

namespace {

using platform::IMediaControls;
namespace keys = settings::keys;

// The controls are on unless the user turned them off.
constexpr bool EnabledByDefault = true;

} // namespace

MediaControlsBridge::MediaControlsBridge(Services services, QObject* parent)
    : QObject(parent)
    , m_player(services.player)
    , m_controls(services.controls)
    , m_settings(services.settings)
    , m_throttle(services.clock)
{
    connect(&m_settings, &SettingsStore::changed, this, [this](const QString& key) {
        // An empty key means every setting may have changed.
        if (key.isEmpty() || key == keys::WindowsMediaControls) {
            applyEnabled();
        }
    });
    connect(&m_player, &PlayerController::currentTrackChanged, this, &MediaControlsBridge::onTrackChanged);
    connect(&m_player, &PlayerController::stateChanged, this, &MediaControlsBridge::onStateChanged);
    connect(&m_player, &PlayerController::positionChanged, this, [this]() { report(false); });
    connect(&m_player, &PlayerController::durationChanged, this, [this]() { report(false); });
    connect(&m_controls, &IMediaControls::commandReceived, this, &MediaControlsBridge::onCommand);

    applyEnabled();
    onTrackChanged();
}

IMediaControls::PlaybackStatus MediaControlsBridge::statusFor(PlayerController::State state, bool hasTrack) noexcept
{
    if (!hasTrack) {
        return IMediaControls::PlaybackStatus::Closed;
    }
    switch (state) {
    case PlayerController::State::Loading:
        return IMediaControls::PlaybackStatus::Changing;
    case PlayerController::State::Playing:
        return IMediaControls::PlaybackStatus::Playing;
    case PlayerController::State::Paused:
    case PlayerController::State::Idle:
        // Idle with a song is the song shown at launch, which the first Play
        // opens; shown as paused so the play key reaches this app.
        return IMediaControls::PlaybackStatus::Paused;
    case PlayerController::State::Error:
        return IMediaControls::PlaybackStatus::Stopped;
    }
    return IMediaControls::PlaybackStatus::Stopped;
}

QString MediaControlsBridge::artworkFileFor(const Track& track)
{
    // The same order as the cover the player shows: a cover link first, then
    // the picture inside a local file. Remote covers are not passed on.
    const QString artwork = track.artworkUrl.trimmed();
    if (!artwork.isEmpty()) {
        const QUrl url(artwork);
        const QString scheme = url.scheme().toLower();
        if (scheme == QLatin1StringView("http") || scheme == QLatin1StringView("https")) {
            return {};
        }
        if (url.isLocalFile()) {
            return QDir::toNativeSeparators(url.toLocalFile());
        }
        if (QDir::isAbsolutePath(artwork)) {
            return QDir::toNativeSeparators(artwork);
        }
    }
    if (!track.isRemote() && !track.filePath.isEmpty() && QDir::isAbsolutePath(track.filePath)) {
        return QDir::toNativeSeparators(track.filePath);
    }
    return {};
}

void MediaControlsBridge::applyEnabled()
{
    m_controls.setEnabled(m_settings.boolean(keys::WindowsMediaControls, EnabledByDefault));
}

void MediaControlsBridge::onTrackChanged()
{
    const std::optional<Track>& track = m_player.currentTrack();
    platform::MediaControlsMetadata metadata;
    if (track) {
        metadata = {.title = track->title,
                    .artist = track->artist,
                    .album = track->album,
                    .artworkFilePath = artworkFileFor(*track)};
    }
    m_controls.setMetadata(metadata);
    m_status = statusFor(m_player.state(), track.has_value());
    m_controls.setPlaybackStatus(m_status);
    report(true);
}

void MediaControlsBridge::onStateChanged()
{
    const IMediaControls::PlaybackStatus status = statusFor(m_player.state(), m_player.currentTrack().has_value());
    if (status == m_status) {
        return;
    }
    m_status = status;
    m_controls.setPlaybackStatus(status);
    report(true);
}

void MediaControlsBridge::report(bool force)
{
    if (!m_throttle.admit(force)) {
        return;
    }
    if (!m_player.currentTrack()) {
        m_controls.setTimeline(0.0, 0.0);
        return;
    }
    m_controls.setTimeline(m_player.positionSeconds(), m_player.durationSeconds());
}

void MediaControlsBridge::onCommand(IMediaControls::MediaCommand command)
{
    // Play and Pause act only when they would change something, so a key
    // that the system sends as Play never pauses. A song that is loading
    // counts as on its way to playing: the controller only loads to play.
    const PlayerController::State state = m_player.state();
    const bool playing = state == PlayerController::State::Playing;
    const bool startingOrPlaying = playing || state == PlayerController::State::Loading;
    switch (command) {
    case IMediaControls::MediaCommand::Toggle:
        m_player.togglePlay();
        return;
    case IMediaControls::MediaCommand::Play:
        if (!startingOrPlaying) {
            m_player.togglePlay();
        }
        return;
    case IMediaControls::MediaCommand::Pause:
    case IMediaControls::MediaCommand::Stop:
        if (playing) {
            m_player.togglePlay();
        }
        return;
    case IMediaControls::MediaCommand::Next:
        m_player.next();
        return;
    case IMediaControls::MediaCommand::Previous:
        m_player.previous();
        return;
    }
}

} // namespace lmp::core
