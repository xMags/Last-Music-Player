#include "core/downloads/PendingExtrasStore.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLatin1StringView>
#include <QSaveFile>

#include <utility>

namespace lmp::core::downloads {

namespace {

constexpr QLatin1StringView kSuffix{".json"};
// A cover and lyrics are far below this; anything bigger is not ours.
constexpr qint64 kMaxFileBytes = 16 * 1024 * 1024;

[[nodiscard]] QString keyOf(const QString& audioPath)
{
    const QString normal = QDir::cleanPath(QFileInfo(audioPath).absoluteFilePath()).toCaseFolded();
    return QString::fromLatin1(QCryptographicHash::hash(normal.toUtf8(), QCryptographicHash::Sha1).toHex());
}

} // namespace

PendingExtrasStore::PendingExtrasStore(QString folder)
    : m_folder(std::move(folder))
{
}

QString PendingExtrasStore::fileFor(const QString& audioPath) const
{
    return QDir(m_folder).filePath(keyOf(audioPath) + kSuffix);
}

bool PendingExtrasStore::save(const QString& audioPath, const SongFileExtras& extras) const
{
    if (!QDir().mkpath(m_folder)) {
        return false;
    }
    QJsonArray synced;
    for (const lyrics::LyricLine& line : extras.syncedLyrics) {
        synced.append(QJsonObject{{QStringLiteral("timeMs"), static_cast<double>(line.timeMs)},
                                  {QStringLiteral("text"), line.text}});
    }
    const QJsonObject root{
        {QStringLiteral("song"), QFileInfo(audioPath).fileName()},
        {QStringLiteral("title"), extras.title},
        {QStringLiteral("artist"), extras.artist},
        {QStringLiteral("album"), extras.album},
        {QStringLiteral("cover"), QString::fromLatin1(extras.cover.toBase64())},
        {QStringLiteral("plain"), extras.plainLyrics},
        {QStringLiteral("synced"), synced},
    };
    QSaveFile file(fileFor(audioPath));
    if (!file.open(QIODevice::WriteOnly)) {
        return false;
    }
    file.write(QJsonDocument(root).toJson(QJsonDocument::Compact));
    return file.commit();
}

std::optional<SongFileExtras> PendingExtrasStore::load(const QString& audioPath) const
{
    QFile file(fileFor(audioPath));
    if (!file.open(QIODevice::ReadOnly) || file.size() > kMaxFileBytes) {
        return std::nullopt;
    }
    const QJsonDocument document = QJsonDocument::fromJson(file.readAll());
    if (!document.isObject()) {
        return std::nullopt;
    }
    const QJsonObject root = document.object();
    SongFileExtras extras;
    extras.title = root.value(QLatin1StringView("title")).toString();
    extras.artist = root.value(QLatin1StringView("artist")).toString();
    extras.album = root.value(QLatin1StringView("album")).toString();
    extras.cover = QByteArray::fromBase64(root.value(QLatin1StringView("cover")).toString().toLatin1());
    extras.plainLyrics = root.value(QLatin1StringView("plain")).toString();
    for (const QJsonValue& value : root.value(QLatin1StringView("synced")).toArray()) {
        const QJsonObject line = value.toObject();
        extras.syncedLyrics.append(lyrics::LyricLine{
            static_cast<qint64>(line.value(QLatin1StringView("timeMs")).toDouble()),
            line.value(QLatin1StringView("text")).toString()});
    }
    return extras;
}

bool PendingExtrasStore::contains(const QString& audioPath) const
{
    return QFileInfo::exists(fileFor(audioPath));
}

void PendingExtrasStore::remove(const QString& audioPath) const
{
    QFile::remove(fileFor(audioPath));
}

void PendingExtrasStore::keepOnly(const QSet<QString>& audioPaths) const
{
    QSet<QString> wanted;
    for (const QString& path : audioPaths) {
        wanted.insert(keyOf(path) + kSuffix);
    }
    const QStringList kept = QDir(m_folder).entryList({QStringLiteral("*") + kSuffix}, QDir::Files);
    for (const QString& name : kept) {
        if (!wanted.contains(name)) {
            QFile::remove(QDir(m_folder).filePath(name));
        }
    }
}

} // namespace lmp::core::downloads
