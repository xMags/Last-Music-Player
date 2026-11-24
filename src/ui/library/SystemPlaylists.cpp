#include "ui/library/SystemPlaylists.h"

#include <QCoreApplication>

namespace lmp::ui {

namespace {

core::db::TrackQuery queryFor(const char* filter, const char* sort)
{
    core::db::TrackQuery query;
    query.filter = QString::fromLatin1(filter);
    query.sort = QString::fromLatin1(sort);
    return query;
}

QString text(const char* source)
{
    return QCoreApplication::translate("SystemPlaylists", source);
}

} // namespace

const QList<SystemPlaylist>& systemPlaylists()
{
    static const QList<SystemPlaylist> playlists{
        {QStringLiteral("smart-liked"), text("Liked Songs"), text("Everything you have liked"),
         QStringLiteral("heart-fill"), QColor(0x00, 0xB8, 0xD4), QColor(0x00, 0x8D, 0xA8), queryFor("Liked", "DateAdded")},
        {QStringLiteral("smart-most"), text("Most Played"), text("Your most-played tracks"), QStringLiteral("flame"),
         QColor(0x8B, 0x7B, 0xE8), QColor(0x64, 0x55, 0xD6), queryFor("Most", "MostPlayed")},
        {QStringLiteral("smart-recent"), text("Recently Added"), text("Your newest library additions"),
         QStringLiteral("clock-plus"), QColor(0xF3, 0xA9, 0x3B), QColor(0xD7, 0x7B, 0x18), queryFor("All", "DateAdded")},
    };
    return playlists;
}

std::optional<SystemPlaylist> systemPlaylist(const QString& key)
{
    for (const SystemPlaylist& playlist : systemPlaylists()) {
        if (playlist.key == key) {
            return playlist;
        }
    }
    return std::nullopt;
}

} // namespace lmp::ui
