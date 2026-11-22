#include "ui/player/PlayerBarViewModel.h"

#include "core/app/AppIdentity.h"

#include <QtGlobal>

#include <algorithm>
#include <cmath>

namespace lmp::ui {

PlayerBarViewModel::PlayerBarViewModel(QObject* parent)
    : QObject(parent)
{
}

void PlayerBarViewModel::setTitle(const QString& title)
{
    if (title == m_title) {
        return;
    }
    m_title = title;
    emit trackChanged();
}

void PlayerBarViewModel::setArtist(const QString& artist)
{
    if (artist == m_artist) {
        return;
    }
    m_artist = artist;
    emit trackChanged();
}

void PlayerBarViewModel::setArtworkUrl(const QUrl& url)
{
    if (url == m_artworkUrl) {
        return;
    }
    m_artworkUrl = url;
    emit trackChanged();
}

void PlayerBarViewModel::setArtworkId(const QString& id)
{
    if (id == m_artworkId) {
        return;
    }
    m_artworkId = id;
    emit trackChanged();
}

void PlayerBarViewModel::setLiked(bool liked)
{
    if (liked == m_liked) {
        return;
    }
    m_liked = liked;
    emit stateChanged();
}

void PlayerBarViewModel::setPlaying(bool playing)
{
    if (playing == m_playing) {
        return;
    }
    m_playing = playing;
    emit stateChanged();
}

void PlayerBarViewModel::setShuffle(bool shuffle)
{
    if (shuffle == m_shuffle) {
        return;
    }
    m_shuffle = shuffle;
    emit stateChanged();
}

void PlayerBarViewModel::setRepeatMode(int mode)
{
    const int bounded = std::clamp(mode, 0, 2);
    if (bounded == m_repeatMode) {
        return;
    }
    m_repeatMode = bounded;
    emit stateChanged();
}

void PlayerBarViewModel::setElapsedSeconds(qreal seconds)
{
    const qreal bounded = std::isfinite(seconds) ? std::max<qreal>(0, seconds) : 0;
    if (bounded == m_elapsed) {
        return;
    }
    m_elapsed = bounded;
    emit positionChanged();
}

void PlayerBarViewModel::setTotalSeconds(qreal seconds)
{
    const qreal bounded = std::isfinite(seconds) ? std::max<qreal>(0, seconds) : 0;
    if (bounded == m_total) {
        return;
    }
    m_total = bounded;
    emit positionChanged();
}

QString PlayerBarViewModel::displayTitle() const
{
    return hasTrack() ? m_title : tr("Search for something");
}

QString PlayerBarViewModel::displayArtist() const
{
    return hasTrack() ? m_artist : core::AppIdentity::displayName();
}

qreal PlayerBarViewModel::progress() const
{
    return m_total > 0 ? std::clamp<qreal>(m_elapsed / m_total, 0, 1) : 0;
}

void PlayerBarViewModel::setVolume(int volume)
{
    const int bounded = std::clamp(volume, 0, 100);
    if (bounded == m_volume) {
        return;
    }
    m_volume = bounded;
    emit volumeChanged();
}

void PlayerBarViewModel::setMuted(bool muted)
{
    if (muted == m_muted) {
        return;
    }
    m_muted = muted;
    emit volumeChanged();
}

void PlayerBarViewModel::togglePlay()
{
    setPlaying(!m_playing);
}

void PlayerBarViewModel::previous()
{
    emit previousRequested();
}

void PlayerBarViewModel::next()
{
    emit nextRequested();
}

void PlayerBarViewModel::toggleShuffle()
{
    setShuffle(!m_shuffle);
}

void PlayerBarViewModel::cycleRepeat()
{
    setRepeatMode((m_repeatMode + 1) % 3);
}

void PlayerBarViewModel::toggleLike()
{
    setLiked(!m_liked);
}

void PlayerBarViewModel::toggleMute()
{
    setMuted(!m_muted);
}

void PlayerBarViewModel::share()
{
    emit shareRequested();
}

void PlayerBarViewModel::seekToRatio(qreal ratio)
{
    setElapsedSeconds(std::clamp<qreal>(ratio, 0, 1) * m_total);
}

void PlayerBarViewModel::setVolumeRatio(qreal ratio)
{
    setVolume(qRound(std::clamp<qreal>(ratio, 0, 1) * 100));
}

QString PlayerBarViewModel::seekLabelAt(qreal ratio) const
{
    if (!hasTrack() || m_total <= 0) {
        return {};
    }
    return formatTime(std::clamp<qreal>(ratio, 0, 1) * m_total);
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
