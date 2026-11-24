#include "ui/tracks/TrackMenuBuilder.h"

#include <QCoreApplication>

#include "core/model/SingleRelease.h"

namespace lmp::ui {

namespace {

QString text(const char* source)
{
    return QCoreApplication::translate("TrackMenu", source);
}

} // namespace

QList<MenuItemListModel::Item> TrackMenuBuilder::songMenu(const core::Track& track, const Abilities& abilities,
                                                          const QString& playlistKey)
{
    QList<MenuItemListModel::Item> items;
    if (abilities.play) {
        items.append({.id = QString(Play), .label = text("Play now"), .iconName = QStringLiteral("play")});
        items.append({.id = QString(PlayNext), .label = text("Play next"), .iconName = QStringLiteral("play-next")});
        items.append({.id = QString(Queue), .label = text("Add to queue"), .iconName = QStringLiteral("queue-add")});
    }
    if (abilities.keep) {
        items.append({.id = QString(AddToPlaylist),
                      .label = text("Add to playlist"),
                      .iconName = QStringLiteral("playlist-add"),
                      .separatorBefore = true});
        items.append({.id = QString(Like),
                      .label = track.liked ? text("Remove from Liked Songs") : text("Add to Liked Songs"),
                      .iconName = track.liked ? QStringLiteral("heart-fill") : QStringLiteral("heart")});
    }
    // Download, Remove download, Cancel download or Retry, in a group of
    // its own.
    if (abilities.download == QLatin1StringView("offer")) {
        items.append({.id = QString(Download),
                      .label = text("Download"),
                      .iconName = QStringLiteral("download"),
                      .separatorBefore = true});
    } else if (abilities.download == QLatin1StringView("done")) {
        items.append({.id = QString(RemoveDownload),
                      .label = text("Remove download"),
                      .iconName = QStringLiteral("trash"),
                      .separatorBefore = true});
    } else if (abilities.download == QLatin1StringView("failed")) {
        items.append({.id = QString(RetryDownload),
                      .label = text("Download failed · Retry"),
                      .iconName = QStringLiteral("alert"),
                      .separatorBefore = true});
    } else if (abilities.download == QLatin1StringView("pending")) {
        items.append({.id = QString(RemoveDownload),
                      .label = text("Cancel download"),
                      .iconName = QStringLiteral("download"),
                      .separatorBefore = true});
    }
    if (!playlistKey.isEmpty() && abilities.keep && abilities.moveUp) {
        items.append({.id = QString(MoveUp), .label = text("Move up"), .iconName = QStringLiteral("arrow-up")});
    }
    if (!playlistKey.isEmpty() && abilities.keep && abilities.moveDown) {
        items.append({.id = QString(MoveDown), .label = text("Move down"), .iconName = QStringLiteral("arrow-down")});
    }
    if (!playlistKey.isEmpty() && abilities.keep) {
        items.append({.id = QString(RemoveFromPlaylist),
                      .label = text("Remove from playlist"),
                      .iconName = QStringLiteral("trash"),
                      .danger = true});
    }
    if (abilities.mix) {
        items.append({.id = QString(Mix),
                      .label = text("Start mix"),
                      .iconName = QStringLiteral("radio"),
                      .separatorBefore = true});
    }
    if (abilities.share) {
        items.append({.id = QString(Share),
                      .label = text("Share"),
                      .iconName = QStringLiteral("share"),
                      .separatorBefore = !abilities.mix});
    }
    items.append({.id = QString(Artist),
                  .label = text("Go to artist"),
                  .iconName = QStringLiteral("person"),
                  .separatorBefore = true});
    if (!core::looksLikeSingle(track)) {
        items.append({.id = QString(Album), .label = text("Go to album"), .iconName = QStringLiteral("album")});
    }
    return items;
}

} // namespace lmp::ui
