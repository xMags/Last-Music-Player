// A manual check of an upgrade in place: opens a copy of a data folder (made
// by tools/Copy-UserDataForTest.ps1) the way the application does, runs the
// schema step, and prints numbers only: the schema version before and after,
// the row count of each table, how many settings are stored and the source
// mode. Never song titles, account ids or other contents.
//
// Usage: userdata_probe <copied data folder>
// It refuses the live data folder; the schema step changes what it opens.

#include "core/app/AppPaths.h"
#include "core/db/Database.h"
#include "core/db/Schema.h"
#include "core/db/SqliteConnection.h"
#include "core/db/TrackRepository.h"
#include "core/settings/SettingKeys.h"
#include "core/settings/SettingsStore.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QStringList>
#include <QTextStream>

#include <cstdio>

namespace {

[[nodiscard]] QString canonical(const QString& path)
{
    const QString resolved = QFileInfo(path).canonicalFilePath();
    return QDir::cleanPath(resolved.isEmpty() ? QFileInfo(path).absoluteFilePath() : resolved).toLower();
}

[[nodiscard]] int settingsKeyCount(const QString& file)
{
    QFile settings(file);
    if (!settings.open(QIODevice::ReadOnly)) {
        return -1;
    }
    return static_cast<int>(QJsonDocument::fromJson(settings.readAll()).object().size());
}

[[nodiscard]] QStringList tableNames(lmp::core::db::SqliteConnection& connection)
{
    QStringList names;
    auto statement =
        connection.prepare("SELECT name FROM sqlite_master WHERE type = 'table' AND name NOT LIKE 'sqlite_%' ORDER BY name");
    if (!statement) {
        return names;
    }
    while (true) {
        const lmp::core::Result<bool> row = statement.value().step();
        if (!row || !row.value()) {
            break;
        }
        names.append(statement.value().columnText(0));
    }
    return names;
}

[[nodiscard]] qint64 rowCount(lmp::core::db::SqliteConnection& connection, const QString& table)
{
    // Table names come from sqlite_master; quoted all the same.
    QString quoted = table;
    quoted.replace(QLatin1Char('"'), QStringLiteral("\"\""));
    auto statement = connection.prepare(QStringLiteral("SELECT COUNT(*) FROM \"%1\"").arg(quoted).toUtf8());
    if (!statement) {
        return -1;
    }
    const lmp::core::Result<bool> row = statement.value().step();
    return row && row.value() ? statement.value().columnInt64(0) : -1;
}

} // namespace

int main(int argc, char* argv[])
{
    QCoreApplication app(argc, argv);
    QTextStream out(stdout);
    const QStringList arguments = QCoreApplication::arguments();
    if (arguments.size() != 2) {
        out << "Usage: userdata_probe <copied data folder>\n";
        return 2;
    }
    const QString folder = arguments.at(1);
    const QString live = QDir(qEnvironmentVariable("LOCALAPPDATA")).filePath(QStringLiteral("Last Music Player"));
    if (canonical(folder) == canonical(live)) {
        out << "Refused: that is the live data folder. Run tools/Copy-UserDataForTest.ps1 and probe the copy.\n";
        return 2;
    }
    if (!QFileInfo(QDir(folder).filePath(QStringLiteral("lastmusic.db"))).isFile()) {
        out << "No lastmusic.db in that folder.\n";
        return 2;
    }

    const lmp::core::AppPaths paths(folder);
    lmp::core::db::Database database(paths);
    if (const lmp::core::Result<void> opened = database.open(); !opened) {
        out << "open failed\n";
        return 1;
    }
    lmp::core::db::SqliteConnection& connection = database.connection();
    const int before = lmp::core::db::Schema::userVersion(connection);
    const lmp::core::Result<void> ensured = lmp::core::db::Schema::ensure(connection);
    const int after = lmp::core::db::Schema::userVersion(connection);
    out << "schema version before: " << before << "\n";
    out << "schema step: " << (ensured ? "ok" : "failed") << "\n";
    out << "schema version after: " << after << "\n";
    // The repository the application reads songs through opens on it too.
    lmp::core::db::TrackRepository tracks(database);
    (void)tracks;
    for (const QString& table : tableNames(connection)) {
        out << "rows " << table << ": " << rowCount(connection, table) << "\n";
    }

    lmp::core::SettingsStore settings(paths.settingsFile());
    settings.load();
    out << "settings keys: " << settingsKeyCount(paths.settingsFile()) << "\n";
    const QString mode = settings.string(lmp::core::settings::keys::RemoteAccessMode);
    out << "mode: " << (mode.isEmpty() ? QStringLiteral("(not set)") : mode) << "\n";
    out.flush();
    return ensured ? 0 : 1;
}
