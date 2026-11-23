#include "ui/dev/GalleryViewModel.h"

namespace lmp::ui {

GalleryViewModel::GalleryViewModel(QObject* parent)
    : QObject(parent)
{
    m_menuItems.addItem(QStringLiteral("play-next"), tr("Play next"), QStringLiteral("play-next"));
    m_menuItems.addItem(QStringLiteral("queue"), tr("Add to queue"), QStringLiteral("queue-add"));
    m_menuItems.addItem(QStringLiteral("playlist"), tr("Add to playlist"),
                        QStringLiteral("playlist-add"));
    m_menuItems.addItem(QStringLiteral("share"), tr("Share"), QStringLiteral("share"), false, true);
    m_menuItems.addItem(QStringLiteral("unavailable"), tr("Not available here"),
                        QStringLiteral("info"), false, false, false);
    m_menuItems.addItem(QStringLiteral("remove"), tr("Remove from library"),
                        QStringLiteral("trash"), true, true);

    connect(&m_toasts, &ToastQueue::actionTriggered, this,
            [this](const QString& actionId) { report(actionId); });

    const auto sample = [](const QString& title, const QString& artist, const QString& album, const QString& length) {
        core::Track track;
        track.title = title;
        track.artist = artist;
        track.album = album;
        track.durationText = length;
        // Remote songs without artwork: the gradient placeholder shows.
        track.sourceKind = QStringLiteral("remote");
        track.origin = QStringLiteral("gallery");
        track.remoteId = title;
        track.sourceUrl = QStringLiteral("https://example.invalid/gallery/") + title;
        return track;
    };
    QList<core::Track> tracks{
        sample(QStringLiteral("Morning Light (Official Video)"), QStringLiteral("The Sample Band"),
               QStringLiteral("First Album"), QStringLiteral("3:41")),
        sample(QStringLiteral("Second Song"), QStringLiteral("Another Artist"), QStringLiteral("Second Album - Single"),
               QStringLiteral("4:05")),
        sample(QStringLiteral("A Much Longer Song Title That Does Not Fit In One Line"),
               QStringLiteral("Someone With A Long Name"), QStringLiteral("Deluxe Edition"), QStringLiteral("12:30")),
        sample(QStringLiteral("Quiet Hours"), QStringLiteral("Night Owls"), QStringLiteral("Late"),
               QStringLiteral("2:58")),
        sample(QStringLiteral("Gone"), QStringLiteral("Lost Artist"), QStringLiteral("Missing"), QStringLiteral("3:00")),
        sample(QStringLiteral("Echoes"), QStringLiteral("Canyon"), QStringLiteral("Valleys"), QStringLiteral("5:12")),
    };
    tracks[1].liked = true;
    tracks[4].unavailable = true;
    m_sampleTracks.setTracks(std::move(tracks));
    // The second song plays, so its row shows the playing state.
    m_sampleTracks.setPlayingKey(m_sampleTracks.data(m_sampleTracks.index(1), TrackListModel::SourceKeyRole).toString());
}

QStringList GalleryViewModel::selectChoices() const
{
    return {tr("Most recent"), tr("Title"), tr("Artist"), tr("Album")};
}

void GalleryViewModel::showSampleToast()
{
    switch (m_nextSample) {
    case 0:
        m_toasts.show(tr("Added to Liked Songs"));
        break;
    case 1:
        m_toasts.show(tr("Removed from the queue"), false, tr("Undo"), QStringLiteral("undo"));
        break;
    default:
        m_toasts.show(tr("Playback failed: the file could not be opened"), true);
        break;
    }
    m_nextSample = (m_nextSample + 1) % 3;
}

void GalleryViewModel::report(const QString& action)
{
    m_lastAction = action;
    emit lastActionChanged();
}

} // namespace lmp::ui
