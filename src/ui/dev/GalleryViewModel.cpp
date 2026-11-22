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
