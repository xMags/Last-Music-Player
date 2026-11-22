#include "ui/player/FullscreenPlayerViewModel.h"

#include <QLatin1StringView>
#include <QList>
#include <QStringList>

#include "core/playback/PlayerController.h"
#include "core/settings/SettingKeys.h"
#include "core/settings/SettingsStore.h"
#include "ui/shell/ShellLayoutViewModel.h"
#include "ui/toasts/ToastQueue.h"

namespace lmp::ui {

namespace {

namespace keys = core::settings::keys;

constexpr QLatin1StringView kLyrics{"lyrics"};
constexpr QLatin1StringView kQueue{"queue"};
constexpr QLatin1StringView kNone{"none"};

} // namespace

FullscreenPlayerViewModel::FullscreenPlayerViewModel(core::PlayerController& player, core::SettingsStore& settings,
                                                     ShellLayoutViewModel& layout, ToastQueue& toasts,
                                                     QObject* parent)
    : QObject(parent)
    , m_player(player)
    , m_settings(settings)
    , m_layout(layout)
    , m_toasts(toasts)
    , m_panel(panelFromSetting(settings.string(keys::FullscreenPanel, QString(kLyrics))))
{
    connect(&m_layout, &ShellLayoutViewModel::changed, this, &FullscreenPlayerViewModel::panelChanged);
    connect(&m_player, &core::PlayerController::currentTrackChanged, this, [this]() {
        if (!m_player.currentTrack()) {
            setOpen(false);
        }
        emit trackChanged();
    });
    connect(&m_player, &core::PlayerController::queueChanged, this, &FullscreenPlayerViewModel::refreshUpcoming);
    refreshUpcoming();
}

bool FullscreenPlayerViewModel::panelFits() const
{
    return m_layout.windowWidth() > SidePanelHiddenMaxWidth;
}

QString FullscreenPlayerViewModel::albumLine() const
{
    const std::optional<core::Track>& track = m_player.currentTrack();
    if (!track) {
        return {};
    }
    QStringList parts;
    const QString album = track->album.trimmed();
    if (!album.isEmpty()) {
        parts.append(album);
    }
    if (track->year > 0) {
        parts.append(QString::number(track->year));
    }
    return parts.join(QStringLiteral(" · "));
}

void FullscreenPlayerViewModel::openPlayer()
{
    if (!m_player.currentTrack()) {
        m_toasts.show(tr("Play something first."));
        return;
    }
    setOpen(true);
}

void FullscreenPlayerViewModel::close()
{
    setOpen(false);
}

void FullscreenPlayerViewModel::toggle()
{
    if (m_open) {
        close();
    } else {
        openPlayer();
    }
}

void FullscreenPlayerViewModel::pickLyrics()
{
    pick(Panel::Lyrics);
}

void FullscreenPlayerViewModel::pickQueue()
{
    pick(Panel::Queue);
}

FullscreenPlayerViewModel::Panel FullscreenPlayerViewModel::panelFromSetting(const QString& value)
{
    if (value == kQueue) {
        return Panel::Queue;
    }
    if (value == kNone) {
        return Panel::None;
    }
    return Panel::Lyrics;
}

QString FullscreenPlayerViewModel::settingForPanel(Panel panel)
{
    switch (panel) {
    case Panel::Queue:
        return QString(kQueue);
    case Panel::None:
        return QString(kNone);
    case Panel::Lyrics:
        break;
    }
    return QString(kLyrics);
}

void FullscreenPlayerViewModel::pick(Panel panel)
{
    const Panel next = m_panel == panel ? Panel::None : panel;
    m_panel = next;
    m_settings.setString(keys::FullscreenPanel, settingForPanel(next));
    emit panelChanged();
}

void FullscreenPlayerViewModel::setOpen(bool open)
{
    if (open == m_open) {
        return;
    }
    m_open = open;
    emit openChanged();
}

void FullscreenPlayerViewModel::refreshUpcoming()
{
    QStringList lines;
    const QList<core::QueueEntry> entries = m_player.visibleQueue();
    lines.reserve(entries.size());
    for (const core::QueueEntry& entry : entries) {
        lines.append(entry.track.artist.isEmpty()
                         ? entry.track.title
                         : QStringLiteral("%1 · %2").arg(entry.track.title, entry.track.artist));
    }
    if (lines == m_upcoming.stringList()) {
        return;
    }
    m_upcoming.setStringList(lines);
    emit upcomingChanged();
}

} // namespace lmp::ui
