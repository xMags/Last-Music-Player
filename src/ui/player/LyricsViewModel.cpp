#include "ui/player/LyricsViewModel.h"

#include "core/model/DisplayTitle.h"
#include "core/playback/PlayerController.h"

#include <cmath>

namespace lmp::ui {

namespace lyrics = core::lyrics;

LyricsViewModel::LyricsViewModel(core::PlayerController& player, lyrics::LyricsService& service, QObject* parent)
    : QObject(parent)
    , m_player(player)
    , m_lyrics(service)
{
    m_debounce.setSingleShot(true);
    m_debounce.setInterval(DebounceMs);
    connect(&m_debounce, &QTimer::timeout, this, &LyricsViewModel::load);
    connect(&m_player, &core::PlayerController::currentTrackChanged, this, &LyricsViewModel::onTrackChanged);
    connect(&m_player, &core::PlayerController::positionChanged, this, &LyricsViewModel::followPosition);
    onTrackChanged();
}

QStringList LyricsViewModel::lines() const
{
    QStringList list;
    list.reserve(m_result.synced.size());
    for (const lyrics::LyricLine& line : m_result.synced) {
        list.append(line.text);
    }
    return list;
}

void LyricsViewModel::onTrackChanged()
{
    const std::optional<core::Track>& track = m_player.currentTrack();
    if (!track) {
        ++m_request;
        m_debounce.stop();
        m_songKey.clear();
        m_songTitle.clear();
        m_result = {};
        setState(QStringLiteral("idle"));
        return;
    }
    const QString key = lyrics::LyricsService::songKey(*track);
    // The same song again (a new play of it, a queue change): keep what shows.
    if (key == m_songKey && m_state != QLatin1StringView("idle") && m_state != QLatin1StringView("unavailable")) {
        return;
    }
    ++m_request;
    m_songKey = key;
    m_songTitle = core::displayTitle(track->title);
    m_result = {};
    if (!m_lyrics.available()) {
        setState(QStringLiteral("unavailable"));
        return;
    }
    if (key == m_failedKey) {
        setState(QStringLiteral("failed"));
        return;
    }
    setState(QStringLiteral("loading"));
    m_debounce.start();
}

void LyricsViewModel::load()
{
    const std::optional<core::Track>& track = m_player.currentTrack();
    if (!track || lyrics::LyricsService::songKey(*track) != m_songKey) {
        return;
    }
    const std::uint64_t request = ++m_request;
    const QString key = m_songKey;
    m_lyrics.lyricsFor(*track).then(this, [this, request, key](const core::Result<lyrics::LyricsResult>& answer) {
        if (request != m_request || key != m_songKey) {
            return;
        }
        if (!answer) {
            m_failedKey = key;
            setState(QStringLiteral("failed"));
            return;
        }
        if (m_failedKey == key) {
            m_failedKey.clear();
        }
        show(answer.value());
    });
}

void LyricsViewModel::show(const lyrics::LyricsResult& result)
{
    m_result = result;
    m_activeLine = -1;
    if (result.found && !result.synced.isEmpty()) {
        setState(QStringLiteral("synced"));
        followPosition();
        return;
    }
    if (result.found && !result.plain.trimmed().isEmpty()) {
        setState(QStringLiteral("plain"));
        return;
    }
    setState(result.instrumental ? QStringLiteral("instrumental") : QStringLiteral("missing"));
}

void LyricsViewModel::followPosition()
{
    if (m_state != QLatin1StringView("synced")) {
        return;
    }
    const double seconds = m_player.positionSeconds();
    const qint64 positionMs = std::isfinite(seconds) ? std::llround(seconds * 1000.0) : 0;
    const int index = lyrics::activeLineIndex(m_result.synced, positionMs + LeadMs);
    if (index != m_activeLine) {
        m_activeLine = index;
        emit activeLineChanged();
    }
}

void LyricsViewModel::seekToLine(int index)
{
    if (m_state != QLatin1StringView("synced") || index < 0 || index >= m_result.synced.size()) {
        return;
    }
    m_player.seek(static_cast<double>(m_result.synced.at(index).timeMs) / 1000.0);
}

void LyricsViewModel::retry()
{
    if (!m_player.currentTrack() || !m_lyrics.available()) {
        return;
    }
    m_failedKey.clear();
    ++m_request;
    m_result = {};
    setState(QStringLiteral("loading"));
    m_debounce.stop();
    load();
}

void LyricsViewModel::reload()
{
    m_songKey.clear();
    m_failedKey.clear();
    onTrackChanged();
}

void LyricsViewModel::setState(const QString& state)
{
    m_state = state;
    if (state != QLatin1StringView("synced") && m_activeLine != -1) {
        m_activeLine = -1;
        emit activeLineChanged();
    }
    emit changed();
}

} // namespace lmp::ui
