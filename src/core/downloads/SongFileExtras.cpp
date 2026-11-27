#include "core/downloads/SongFileExtras.h"

#include "core/lyrics/LrcText.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QLatin1StringView>
#include <QSaveFile>
#include <QStringList>

#include <fileref.h>
#include <tbytevector.h>
#include <tpropertymap.h>
#include <tstring.h>
#include <tvariant.h>

#include <filesystem>
#include <system_error>

namespace lmp::core::downloads {

namespace {

[[nodiscard]] TagLib::String tagText(const QString& text)
{
    const QByteArray utf8 = text.toUtf8();
    return TagLib::String(utf8.constData(), TagLib::String::UTF8);
}

[[nodiscard]] QString fromTagText(const TagLib::String& text)
{
    return QString::fromStdWString(text.toWString());
}

[[nodiscard]] std::filesystem::path pathOf(const QString& path)
{
    return std::filesystem::path(path.toStdWString());
}

// The MIME type of a cover by its first bytes; empty for formats players do
// not take in every tag (WebP, say).
[[nodiscard]] QString coverMimeType(const QByteArray& bytes)
{
    if (bytes.startsWith(QByteArrayLiteral("\xFF\xD8\xFF"))) {
        return QStringLiteral("image/jpeg");
    }
    if (bytes.startsWith(QByteArrayLiteral("\x89PNG\r\n\x1A\n"))) {
        return QStringLiteral("image/png");
    }
    return {};
}

void setText(TagLib::PropertyMap& properties, const char* key, const QString& value)
{
    if (!value.trimmed().isEmpty()) {
        properties.replace(key, TagLib::StringList(tagText(value)));
    }
}

[[nodiscard]] Result<void> storageFailure(const QString& detail)
{
    return Failure::of(FailureKind::Storage, detail);
}

// Opens `path` as TagLib reads it on this system (wide names on Windows).
[[nodiscard]] TagLib::FileRef openTags(const QString& path)
{
#ifdef _WIN32
    const std::wstring name = path.toStdWString();
    return TagLib::FileRef(name.c_str(), false);
#else
    const QByteArray name = QFile::encodeName(path);
    return TagLib::FileRef(name.constData(), false);
#endif
}

[[nodiscard]] Result<void> tagCopy(const QString& copyPath, const SongFileExtras& extras)
{
    TagLib::FileRef file = openTags(copyPath);
    if (file.isNull()) {
        return Failure::of(FailureKind::NotAvailable, QStringLiteral("this kind of file cannot hold tags"));
    }
    TagLib::PropertyMap properties = file.properties();
    setText(properties, "TITLE", extras.title);
    setText(properties, "ARTIST", extras.artist);
    setText(properties, "ALBUM", extras.album);
    QString words = extras.plainLyrics.trimmed();
    if (words.isEmpty() && !extras.syncedLyrics.isEmpty()) {
        QStringList lines;
        for (const lyrics::LyricLine& line : extras.syncedLyrics) {
            lines.append(line.text);
        }
        words = lines.join(QLatin1Char('\n'));
    }
    setText(properties, "LYRICS", words);
    file.setProperties(properties);
    if (const QString mimeType = coverMimeType(extras.cover); !mimeType.isEmpty()) {
        TagLib::VariantMap picture;
        picture.insert("data", TagLib::ByteVector(extras.cover.constData(), static_cast<unsigned int>(extras.cover.size())));
        picture.insert("mimeType", tagText(mimeType));
        picture.insert("description", TagLib::String());
        picture.insert("pictureType", TagLib::String("Front Cover"));
        file.setComplexProperties("PICTURE", {picture});
    }
    if (!file.save()) {
        return storageFailure(QStringLiteral("the tags could not be written"));
    }
    return Result<void>::success();
}

[[nodiscard]] Result<void> writeLrc(const QString& audioPath, const QList<lyrics::LyricLine>& lines)
{
    const QString text = lyrics::formatLrc(lines);
    if (text.isEmpty()) {
        return Result<void>::success();
    }
    QSaveFile file(lrcPathFor(audioPath));
    if (!file.open(QIODevice::WriteOnly)) {
        return storageFailure(file.errorString());
    }
    // With its byte order mark: players on Windows otherwise read UTF-8
    // lyrics in the system code page.
    file.write(QByteArrayLiteral("\xEF\xBB\xBF"));
    file.write(text.toUtf8());
    if (!file.commit()) {
        return storageFailure(file.errorString());
    }
    return Result<void>::success();
}

} // namespace

QString lrcPathFor(const QString& audioPath)
{
    const QFileInfo info(audioPath);
    return info.dir().filePath(info.completeBaseName() + QStringLiteral(".lrc"));
}

Result<void> writeSongFileExtras(const QString& audioPath, const SongFileExtras& extras)
{
    if (!QFileInfo(audioPath).isFile()) {
        return Failure::of(FailureKind::NotFound, QStringLiteral("the song's file is gone"));
    }
    // The copy keeps the extension: it tells TagLib the kind of file.
    const QFileInfo audio(audioPath);
    const QString copyPath =
        audio.dir().filePath(audio.completeBaseName() + QStringLiteral(".lmp-tagging.") + audio.suffix());
    QFile::remove(copyPath);
    if (!QFile::copy(audioPath, copyPath)) {
        return storageFailure(QStringLiteral("the song could not be copied"));
    }
    if (const Result<void> tagged = tagCopy(copyPath, extras); !tagged) {
        QFile::remove(copyPath);
        return tagged;
    }
    // Deleted meanwhile: the copy must not bring it back.
    if (!QFileInfo(audioPath).isFile()) {
        QFile::remove(copyPath);
        return Failure::of(FailureKind::NotFound, QStringLiteral("the song's file is gone"));
    }
    std::error_code error;
    std::filesystem::rename(pathOf(copyPath), pathOf(audioPath), error);
    if (error) {
        QFile::remove(copyPath);
        return storageFailure(QString::fromStdString(error.message()));
    }
    return writeLrc(audioPath, extras.syncedLyrics);
}

std::optional<lyrics::LyricsResult> readSavedLyrics(const QString& audioPath)
{
    if (audioPath.isEmpty()) {
        return std::nullopt;
    }
    QFile lrc(lrcPathFor(audioPath));
    if (lrc.open(QIODevice::ReadOnly)) {
        // Lyrics are small; anything past a megabyte is not a lyrics file.
        QString text = QString::fromUtf8(lrc.read(1024 * 1024));
        if (text.startsWith(QChar(0xFEFF))) {
            text.remove(0, 1);
        }
        const lyrics::LyricsResult saved = lyrics::lyricsFromSavedText(text);
        if (!saved.synced.isEmpty()) {
            return saved;
        }
    }
    if (!QFileInfo(audioPath).isFile()) {
        return std::nullopt;
    }
    const TagLib::FileRef file = openTags(audioPath);
    if (file.isNull()) {
        return std::nullopt;
    }
    const TagLib::StringList words = file.properties().value("LYRICS");
    if (words.isEmpty()) {
        return std::nullopt;
    }
    const lyrics::LyricsResult saved = lyrics::lyricsFromSavedText(fromTagText(words.toString("\n")));
    if (!saved.found) {
        return std::nullopt;
    }
    return saved;
}

} // namespace lmp::core::downloads
