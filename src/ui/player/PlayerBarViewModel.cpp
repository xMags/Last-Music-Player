#include "ui/player/PlayerBarViewModel.h"

#include "core/model/DisplayTitle.h"

#include "core/app/AppIdentity.h"
#include "core/connect/ConnectSync.h"
#include "core/library/TrackActions.h"
#include "core/model/CatalogSourceKey.h"
#include "core/net/RemoteUrlPolicy.h"
#include "core/model/ArtworkPlaceholder.h"
#include "core/playback/PlayerController.h"
#include "core/playback/QueueTrackIdentity.h"
#include "ui/player/CoverSource.h"

#include <QtGlobal>

#include <algorithm>
#include <cmath>

namespace lmp::ui {

namespace {

using State = core::PlayerController::State;

} // namespace

PlayerBarViewModel::PlayerBarViewModel(core::PlayerController& player, core::connect::ConnectSync* link,
                                       core::TrackActions* actions, QObject* parent)
    : QObject(parent)
    , m_player(player)
    , m_actions(actions)
    , m_connect(link)
{
    // A like or unlike anywhere (a list's heart, a menu, a sync) moves this heart too.
    if (m_actions != nullptr) {
        connect(m_actions, &core::TrackActions::likedChanged, this, &PlayerBarViewModel::refreshLiked);
    }
    connect(&m_player, &core::PlayerController::currentTrackChanged, this, &PlayerBarViewModel::onTrackChanged);
    connect(&m_player, &core::PlayerController::stateChanged, this, &PlayerBarViewModel::stateChanged);
    connect(&m_player, &core::PlayerController::shuffleChanged, this, &PlayerBarViewModel::stateChanged);
    connect(&m_player, &core::PlayerController::repeatModeChanged, this, &PlayerBarViewModel::stateChanged);
    connect(&m_player, &core::PlayerController::positionChanged, this, &PlayerBarViewModel::positionChanged);
    connect(&m_player, &core::PlayerController::durationChanged, this, &PlayerBarViewModel::positionChanged);
    connect(&m_player, &core::PlayerController::volumeChanged, this, &PlayerBarViewModel::volumeChanged);
    connect(&m_player, &core::PlayerController::mutedChanged, this, &PlayerBarViewModel::volumeChanged);
    if (m_connect != nullptr) {
        connect(m_connect, &core::connect::ConnectSync::remoteChanged, this, [this]() {
            onTrackChanged();
            emit stateChanged();
            emit positionChanged();
            emit volumeChanged();
        });
        connect(m_connect, &core::connect::ConnectSync::remotePositionChanged, this,
                &PlayerBarViewModel::positionChanged);
    }

    if (const core::Track* track = shownTrack()) {
        m_shownIdentity = core::queueTrackIdentity(*track);
        m_artworkUrl = CoverSource::forTrack(*track).displayUrl();
        m_artworkId = CoverSource::placeholderIdFor(*track);
        m_liked = likedNow(track);
    }
    m_shownTitle = displayTitle();
    m_shownArtist = displayArtist();
}

void PlayerBarViewModel::onTrackChanged()
{
    // The controller already holds the new song; what was shown is kept here.
    const QString previousTitle = m_shownTitle;
    const QString previousArtist = m_shownArtist;
    m_shownTitle = displayTitle();
    m_shownArtist = displayArtist();
    const core::Track* track = shownTrack();
    const QString identity = track ? core::queueTrackIdentity(*track) : QString();
    const bool liked = likedNow(track);
    // Only a new song moves; a redraw of the same one (or the launch
    // restore, which opens nothing) does not.
    const bool newSong = track && identity != m_shownIdentity && (remote() || m_player.state() != State::Idle);

    m_shownIdentity = identity;
    if (newSong) {
        m_previousTitle = previousTitle;
        m_previousArtist = previousArtist;
    }
    m_artworkUrl = track ? CoverSource::forTrack(*track).displayUrl() : QUrl();
    m_artworkId = track ? CoverSource::placeholderIdFor(*track) : QString();
    const bool likedMoved = liked != m_liked;
    m_liked = liked;
    emit trackChanged();
    if (likedMoved) {
        emit stateChanged();
    }
    if (newSong) {
        emit songChanged();
    }
}

const core::Track* PlayerBarViewModel::shownTrack() const
{
    if (remote()) {
        const std::optional<core::Track>& track = m_connect->remote().track;
        return track ? &*track : nullptr;
    }
    const std::optional<core::Track>& track = m_player.currentTrack();
    return track ? &*track : nullptr;
}

bool PlayerBarViewModel::remote() const
{
    return m_connect != nullptr && m_connect->isRemote();
}

QString PlayerBarViewModel::playingOn() const
{
    return remote() ? m_connect->remote().deviceLabel : QString();
}

bool PlayerBarViewModel::hasTrack() const
{
    return shownTrack() != nullptr;
}

QString PlayerBarViewModel::title() const
{
    // Cleaned for display (core::displayTitle), as every surface shows it.
    const core::Track* track = shownTrack();
    return track ? core::displayTitle(track->title) : QString();
}

QString PlayerBarViewModel::artist() const
{
    const core::Track* track = shownTrack();
    return track ? track->artist : QString();
}

QString PlayerBarViewModel::displayTitle() const
{
    return hasTrack() ? title() : tr("Search for something");
}

QString PlayerBarViewModel::displayArtist() const
{
    return hasTrack() ? artist() : core::AppIdentity::displayName();
}

QString PlayerBarViewModel::artworkLabel() const
{
    return core::ArtworkPlaceholder::tileLabelFor(title());
}

bool PlayerBarViewModel::playing() const
{
    if (remote()) {
        return m_connect->remote().playing;
    }
    const State state = m_player.state();
    return state == State::Playing || state == State::Loading;
}

bool PlayerBarViewModel::shuffle() const
{
    if (remote()) {
        return m_connect->remote().shuffle;
    }
    return m_player.shuffle() == core::Shuffle::On;
}

int PlayerBarViewModel::repeatMode() const
{
    if (remote()) {
        return m_connect->remote().repeatMode;
    }
    return static_cast<int>(m_player.repeatMode());
}

qreal PlayerBarViewModel::elapsedSeconds() const
{
    const double seconds = remote() ? m_connect->remotePosition() : m_player.positionSeconds();
    return std::isfinite(seconds) ? std::max(0.0, seconds) : 0.0;
}

qreal PlayerBarViewModel::totalSeconds() const
{
    const double seconds = remote() ? m_connect->remote().durationSeconds : m_player.durationSeconds();
    return std::isfinite(seconds) ? std::max(0.0, seconds) : 0.0;
}

qreal PlayerBarViewModel::progress() const
{
    const qreal total = totalSeconds();
    return total > 0 ? std::clamp<qreal>(elapsedSeconds() / total, 0, 1) : 0;
}

int PlayerBarViewModel::volume() const
{
    if (remote()) {
        return std::clamp(m_connect->remote().volume, 0, 100);
    }
    return qRound(std::clamp(m_player.volume(), 0.0, 1.0) * 100);
}

bool PlayerBarViewModel::muted() const
{
    return !remote() && m_player.muted();
}

qreal PlayerBarViewModel::volumeRatio() const
{
    return muted() ? 0.0 : volume() / 100.0;
}

void PlayerBarViewModel::togglePlay()
{
    if (remote()) {
        m_connect->remoteTogglePlay();
        return;
    }
    m_player.togglePlay();
}

void PlayerBarViewModel::previous()
{
    if (remote()) {
        m_connect->remotePrevious();
        return;
    }
    m_player.previous();
}

void PlayerBarViewModel::next()
{
    if (remote()) {
        m_connect->remoteNext();
        return;
    }
    m_player.next();
}

void PlayerBarViewModel::toggleShuffle()
{
    if (remote()) {
        m_connect->remoteSetShuffle(!shuffle());
        return;
    }
    m_player.setShuffle(shuffle() ? core::Shuffle::Off : core::Shuffle::On);
}

void PlayerBarViewModel::cycleRepeat()
{
    if (remote()) {
        m_connect->remoteSetRepeatMode((repeatMode() + 1) % 3);
        return;
    }
    m_player.setRepeatMode(static_cast<core::RepeatMode>((repeatMode() + 1) % 3));
}

QString PlayerBarViewModel::downloadAddress() const
{
    const core::Track* track = shownTrack();
    if (track == nullptr || remote()) {
        return {};
    }
    return core::net::isHttpUrl(track->sourceUrl) ? track->sourceUrl : QString();
}

void PlayerBarViewModel::refreshLiked()
{
    const bool liked = likedNow(shownTrack());
    if (liked != m_liked) {
        m_liked = liked;
        emit stateChanged();
    }
}

bool PlayerBarViewModel::likedNow(const core::Track* track) const
{
    if (track == nullptr) {
        return false;
    }
    // Another device's song is that device's to like: its own field says.
    if (m_actions == nullptr || remote()) {
        return track->liked;
    }
    return m_actions->isLiked(*track);
}

void PlayerBarViewModel::toggleLike()
{
    const core::Track* track = shownTrack();
    if (track == nullptr || remote()) {
        return;
    }
    if (m_actions == nullptr) {
        m_liked = !m_liked;
        emit stateChanged();
        return;
    }
    // The heart moves with likedChanged; a refused like leaves it as it was.
    if (const std::optional<bool> liked = m_actions->toggleLike(*track); liked && *liked != m_liked) {
        m_liked = *liked;
        emit stateChanged();
    }
}

void PlayerBarViewModel::toggleMute()
{
    if (remote()) {
        // The protocol has no mute: this sends silence, or a middle level back.
        m_connect->remoteSetVolume(volume() > 0 ? 0 : 50);
        return;
    }
    m_player.setMuted(!m_player.muted());
}

void PlayerBarViewModel::share()
{
    if (hasTrack()) {
        emit shareRequested();
    }
}

void PlayerBarViewModel::seekToRatio(qreal ratio)
{
    const qreal total = totalSeconds();
    if (!std::isfinite(ratio) || total <= 0) {
        return;
    }
    const qreal seconds = std::clamp<qreal>(ratio, 0, 1) * total;
    if (remote()) {
        m_connect->remoteSeek(seconds);
        return;
    }
    m_player.seek(seconds);
}

void PlayerBarViewModel::setVolumeRatio(qreal ratio)
{
    if (!std::isfinite(ratio)) {
        return;
    }
    // The slider steps in whole percent, as the design's 0 to 100 range does.
    const int percent = qRound(std::clamp<qreal>(ratio, 0, 1) * 100);
    if (remote()) {
        m_connect->remoteSetVolume(percent);
        return;
    }
    m_player.setVolume(percent / 100.0);
}

QString PlayerBarViewModel::seekLabelAt(qreal ratio) const
{
    const qreal total = totalSeconds();
    if (!hasTrack() || total <= 0) {
        return {};
    }
    return formatTime(std::clamp<qreal>(ratio, 0, 1) * total);
}

QString PlayerBarViewModel::volumeLabelAt(qreal ratio) const
{
    // The value a press there sets, rounded to the slider's step of 1.
    return QStringLiteral("%1%").arg(qRound(std::clamp<qreal>(ratio, 0, 1) * 100));
}

QString PlayerBarViewModel::formatTime(qreal seconds)
{
    const qint64 safe = std::isfinite(seconds) ? static_cast<qint64>(std::floor(std::max<qreal>(0, seconds))) : 0;
    return QStringLiteral("%1:%2").arg(safe / 60).arg(safe % 60, 2, 10, QLatin1Char('0'));
}

} // namespace lmp::ui
