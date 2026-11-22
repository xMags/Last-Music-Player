// The commands of the Windows audio player and the state they drive. The
// player events arrive here through WindowsAudioPlayerEvents.cpp.

#include "platform/windows/WindowsAudioPlayer.h"

#include "platform/windows/WindowsAudioPlayerNative.h"

#include <QMetaObject>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <optional>

namespace lmp::platform::windows {

namespace {

namespace wf = winrt::Windows::Foundation;
namespace wmc = winrt::Windows::Media::Core;
namespace wmp = winrt::Windows::Media::Playback;

using media::runPlayerCall;
using media::toTimeSpan;

} // namespace

quint64 WindowsAudioPlayer::open(const QUrl& source, bool autoplay, double startAtSeconds)
{
    const quint64 loadId = ++m_loadId;
    m_positionTimer.stop();
    m_state = State::Loading;
    m_opened = false;
    m_wantsPlay = autoplay;
    m_startAtSeconds = std::isfinite(startAtSeconds) ? std::max(0.0, startAtSeconds) : 0.0;
    m_durationSeconds = 0.0;

    // Posted rather than emitted, so the caller holds the id before any
    // event names it. Dropped if another open comes first.
    const bool posted = QMetaObject::invokeMethod(
        this,
        [this, loadId]() {
            if (loadId == m_loadId) {
                emit stateChanged(loadId, State::Loading);
            }
        },
        Qt::QueuedConnection);
    static_cast<void>(posted); // this object's own thread always has a queue

    // A start position is applied before playing, so play waits for it.
    openSource(loadId, source, autoplay && m_startAtSeconds <= 0.0);
    return loadId;
}

void WindowsAudioPlayer::openSource(quint64 loadId, const QUrl& source, bool playNow)
{
    if (!m_native->player) {
        failLater(loadId, ErrorKind::Unknown, QStringLiteral("the media player is not available"));
        return;
    }
    // What was playing belongs to an earlier load now, even if this one
    // fails before its source replaces it.
    const std::optional<QString> paused = runPlayerCall("Pause", [this]() { m_native->player.Pause(); });
    if (paused) {
        failLater(loadId, ErrorKind::Unknown, *paused);
        return;
    }
    const std::optional<QString> address = media::playerAddress(source);
    if (!address) {
        failLater(loadId, ErrorKind::SourceNotSupported, QStringLiteral("unsupported source address"));
        return;
    }
    const std::optional<QString> failure = runPlayerCall("Open", [this, loadId, &address, playNow]() {
        const wf::Uri uri(winrt::hstring(address->toStdWString()));
        const wmc::MediaSource mediaSource = wmc::MediaSource::CreateFromUri(uri);
        mediaSource.CustomProperties().Insert(media::LoadIdProperty,
                                              winrt::box_value(static_cast<std::uint64_t>(loadId)));
        const wmp::MediaPlayer& player = m_native->player;
        player.Source(mediaSource);
        if (playNow) {
            player.Play();
        }
    });
    if (failure) {
        failLater(loadId, ErrorKind::SourceNotSupported, *failure);
    }
}

void WindowsAudioPlayer::play()
{
    if (!hasActiveLoad()) {
        return;
    }
    m_wantsPlay = true;
    if (!m_opened && m_startAtSeconds > 0.0) {
        return; // finishOpen() starts playback after the start position
    }
    const quint64 loadId = m_loadId;
    if (m_state == State::Ended) {
        // Played to the end: start again from the beginning.
        const std::optional<QString> rewind = runPlayerCall(
            "Rewind", [this]() { m_native->session.Position(toTimeSpan(0.0)); });
        if (rewind) {
            fail(ErrorKind::Unknown, *rewind);
            return;
        }
        emit positionChanged(loadId, 0.0);
        if (loadId != m_loadId) {
            return;
        }
    }
    const std::optional<QString> failure = runPlayerCall("Play", [this]() { m_native->player.Play(); });
    if (failure) {
        fail(ErrorKind::Unknown, *failure);
    }
}

void WindowsAudioPlayer::pause()
{
    if (!hasActiveLoad()) {
        return;
    }
    m_wantsPlay = false;
    const std::optional<QString> failure = runPlayerCall("Pause", [this]() { m_native->player.Pause(); });
    if (failure) {
        fail(ErrorKind::Unknown, *failure);
        return;
    }
    if (m_state == State::Playing) {
        m_positionTimer.stop();
        reportPosition();
        setState(State::Paused);
    }
}

void WindowsAudioPlayer::seek(double seconds)
{
    if (!hasActiveLoad() || !std::isfinite(seconds) || seconds < 0.0) {
        return;
    }
    if (!m_opened) {
        m_startAtSeconds = seconds; // applied once the source opens
        return;
    }
    const double target = m_durationSeconds > 0.0 ? std::min(seconds, m_durationSeconds) : seconds;
    bool seekable = false;
    const std::optional<QString> failure = runPlayerCall("Seek", [this, target, &seekable]() {
        seekable = m_native->session.CanSeek();
        if (seekable) {
            m_native->session.Position(toTimeSpan(target));
        }
    });
    if (failure) {
        fail(ErrorKind::Unknown, *failure);
        return;
    }
    if (!seekable) {
        return;
    }
    const quint64 loadId = m_loadId;
    emit positionChanged(loadId, target);
    if (loadId == m_loadId && m_state == State::Ended) {
        setState(State::Paused);
    }
}

void WindowsAudioPlayer::stop()
{
    if (m_loadId == 0 || m_state == State::Idle) {
        return;
    }
    m_positionTimer.stop();
    m_opened = false;
    m_wantsPlay = false;
    m_startAtSeconds = 0.0;
    m_durationSeconds = 0.0;
    if (m_native->player) {
        const std::optional<QString> failure = runPlayerCall("Stop", [this]() {
            m_native->player.Pause();
            m_native->player.Source(nullptr);
        });
        // The load is over either way; the next open replaces the source.
        static_cast<void>(failure);
    }
    setState(State::Idle);
}

void WindowsAudioPlayer::setVolume(double volume)
{
    if (!std::isfinite(volume) || !m_native->player) {
        return;
    }
    const double clamped = std::clamp(volume, 0.0, 1.0);
    const std::optional<QString> failure =
        runPlayerCall("Volume", [this, clamped]() { m_native->player.Volume(clamped); });
    // The volume has no load to fail; the next call tries again.
    static_cast<void>(failure);
}

void WindowsAudioPlayer::handleOpened(quint64 loadId, double durationSeconds)
{
    if (loadId != m_loadId || m_state != State::Loading) {
        return;
    }
    finishOpen(durationSeconds);
}

void WindowsAudioPlayer::handleSessionState(quint64 loadId, SessionState state, double positionSeconds)
{
    if (loadId != m_loadId || !hasActiveLoad()) {
        return;
    }
    if (!m_opened) {
        // Playing can be reported before the opened event is handled.
        if (state != SessionState::Playing) {
            return;
        }
        finishOpen(sessionDuration());
        if (loadId != m_loadId || !m_opened) {
            return;
        }
    }
    switch (state) {
    case SessionState::Playing:
        if (!m_wantsPlay) {
            return; // left over from before a pause; its Paused follows
        }
        setState(State::Playing);
        if (loadId == m_loadId && m_state == State::Playing) {
            emit positionChanged(loadId, positionSeconds);
            m_positionTimer.start();
        }
        return;
    case SessionState::Paused:
        if (m_state != State::Playing) {
            return;
        }
        // At the very end the session pauses just before it reports the
        // end; Ended follows, so this pause is not reported.
        if (m_durationSeconds > 0.0 && positionSeconds >= m_durationSeconds - 0.5) {
            return;
        }
        m_wantsPlay = false;
        m_positionTimer.stop();
        emit positionChanged(loadId, positionSeconds);
        if (loadId == m_loadId) {
            setState(State::Paused);
        }
        return;
    case SessionState::Opening:
    case SessionState::Buffering:
    case SessionState::None:
        return; // buffering while playing still counts as playing
    }
}

void WindowsAudioPlayer::handleDuration(quint64 loadId, double durationSeconds)
{
    if (loadId == m_loadId && m_opened && hasActiveLoad()) {
        updateDuration(durationSeconds);
    }
}

void WindowsAudioPlayer::handleEnded(quint64 loadId, double durationSeconds)
{
    if (loadId != m_loadId || !m_opened || !hasActiveLoad() || m_state == State::Ended) {
        return;
    }
    m_wantsPlay = false;
    m_positionTimer.stop();
    updateDuration(durationSeconds);
    if (loadId != m_loadId) {
        return;
    }
    emit positionChanged(loadId, m_durationSeconds);
    if (loadId == m_loadId) {
        setState(State::Ended);
    }
}

void WindowsAudioPlayer::handleFailed(quint64 loadId, ErrorKind kind, const QString& detail)
{
    if (loadId == m_loadId && hasActiveLoad()) {
        fail(kind, detail);
    }
}

void WindowsAudioPlayer::finishOpen(double durationSeconds)
{
    const quint64 loadId = m_loadId;
    m_opened = true;
    updateDuration(durationSeconds);
    if (loadId != m_loadId) {
        return;
    }

    double start = 0.0;
    const bool playAfterStart = m_startAtSeconds > 0.0 && m_wantsPlay;
    if (m_startAtSeconds > 0.0) {
        start = m_startAtSeconds;
        // Never start on the very last second, as the earlier versions did.
        if (m_durationSeconds > 1.0 && start > m_durationSeconds - 1.0) {
            start = m_durationSeconds - 1.0;
        }
        const std::optional<QString> failure = runPlayerCall("Start position", [this, &start]() {
            if (m_native->session.CanSeek()) {
                m_native->session.Position(toTimeSpan(start));
            } else {
                start = 0.0;
            }
        });
        if (failure) {
            fail(ErrorKind::Unknown, *failure);
            return;
        }
    }
    m_startAtSeconds = 0.0;

    emit positionChanged(loadId, start);
    if (loadId != m_loadId) {
        return;
    }
    setState(State::Paused);
    if (loadId != m_loadId || !playAfterStart || !m_wantsPlay) {
        return;
    }
    const std::optional<QString> failure = runPlayerCall("Play", [this]() { m_native->player.Play(); });
    if (failure) {
        fail(ErrorKind::Unknown, *failure);
    }
}

void WindowsAudioPlayer::failLater(quint64 loadId, ErrorKind kind, const QString& detail)
{
    // Queued behind the Loading report of the same load.
    const bool posted = QMetaObject::invokeMethod(
        this,
        [this, loadId, kind, detail]() {
            if (loadId == m_loadId && hasActiveLoad()) {
                fail(kind, detail);
            }
        },
        Qt::QueuedConnection);
    static_cast<void>(posted); // this object's own thread always has a queue
}

void WindowsAudioPlayer::fail(ErrorKind kind, const QString& detail)
{
    const quint64 loadId = m_loadId;
    m_positionTimer.stop();
    m_wantsPlay = false;
    // Set first, so a listener that asks sees the failure.
    m_state = State::Error;
    emit errorOccurred(loadId, kind, detail);
    if (loadId == m_loadId && m_state == State::Error) {
        emit stateChanged(loadId, State::Error);
    }
}

void WindowsAudioPlayer::updateDuration(double durationSeconds)
{
    if (durationSeconds <= 0.0 || std::abs(durationSeconds - m_durationSeconds) <= 0.01) {
        return;
    }
    m_durationSeconds = durationSeconds;
    emit durationChanged(m_loadId, durationSeconds);
}

void WindowsAudioPlayer::setState(State state)
{
    if (m_state == state) {
        return;
    }
    m_state = state;
    emit stateChanged(m_loadId, state);
}

void WindowsAudioPlayer::reportPosition()
{
    if (hasActiveLoad() && m_opened) {
        emit positionChanged(m_loadId, sessionPosition());
    }
}

bool WindowsAudioPlayer::hasActiveLoad() const
{
    return m_loadId != 0 && m_state != State::Idle && m_state != State::Error;
}

double WindowsAudioPlayer::sessionPosition() const
{
    return m_native->session ? media::positionOf(m_native->session) : 0.0;
}

double WindowsAudioPlayer::sessionDuration() const
{
    return m_native->session ? media::durationOf(m_native->session) : 0.0;
}

} // namespace lmp::platform::windows
