#include "platform/windows/WindowsTagReader.h"

#include <QDir>
#include <QFileInfo>

#include <windows.h>
#include <objbase.h>

#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Storage.FileProperties.h>
#include <winrt/Windows.Storage.Streams.h>
#include <winrt/Windows.Storage.h>

#include <array>
#include <cstdint>

#include "core/library/EmbeddedArtwork.h"

namespace lmp::platform::windows {

namespace {

namespace storage = winrt::Windows::Storage;

// Each worker thread joins the multi-threaded apartment once. A thread
// already in another apartment keeps it; blocking .get() is still allowed
// there because worker threads are never single-threaded UI apartments.
void enterApartment()
{
    thread_local const bool entered = []() {
        const HRESULT result = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
        return SUCCEEDED(result) || result == RPC_E_CHANGED_MODE;
    }();
    (void)entered;
}

storage::StorageFile openFile(const QString& filePath)
{
    const QString native = QDir::toNativeSeparators(QFileInfo(filePath).absoluteFilePath());
    return storage::StorageFile::GetFileFromPathAsync(winrt::hstring(reinterpret_cast<const wchar_t*>(native.utf16())))
        .get();
}

QString fromHstring(const winrt::hstring& text)
{
    return QString::fromWCharArray(text.c_str(), static_cast<qsizetype>(text.size()));
}

QDateTime fromDateTime(const winrt::Windows::Foundation::DateTime& value)
{
    // Windows counts 100 ns ticks from 1601; zero means unknown.
    if (value.time_since_epoch().count() == 0) {
        return {};
    }
    const std::time_t seconds = winrt::clock::to_time_t(value);
    return QDateTime::fromSecsSinceEpoch(static_cast<qint64>(seconds), QTimeZone::UTC);
}

} // namespace

core::Result<TrackTags> WindowsTagReader::read(const QString& filePath)
{
    enterApartment();
    storage::StorageFile file{nullptr};
    storage::FileProperties::MusicProperties music{nullptr};
    try {
        file = openFile(filePath);
        music = file.Properties().GetMusicPropertiesAsync().get();
    } catch (const winrt::hresult_error&) {
        return core::Failure::of(core::FailureKind::Storage, QStringLiteral("The file's properties could not be read"));
    }

    // Each property on its own, as earlier versions did: one that throws
    // leaves the others.
    TrackTags tags;
    try {
        tags.title = fromHstring(music.Title());
    } catch (const winrt::hresult_error&) {
    }
    try {
        tags.artist = fromHstring(music.Artist());
    } catch (const winrt::hresult_error&) {
    }
    try {
        tags.album = fromHstring(music.Album());
    } catch (const winrt::hresult_error&) {
    }
    try {
        const auto genres = music.Genre();
        if (genres && genres.Size() > 0) {
            tags.genre = fromHstring(genres.GetAt(0));
        }
    } catch (const winrt::hresult_error&) {
    }
    try {
        tags.durationSeconds = static_cast<double>(music.Duration().count()) / 10'000'000.0;
    } catch (const winrt::hresult_error&) {
    }
    try {
        tags.created = fromDateTime(file.DateCreated());
        tags.modified = fromDateTime(file.GetBasicPropertiesAsync().get().DateModified());
    } catch (const winrt::hresult_error&) {
    }
    return tags;
}

std::optional<QByteArray> WindowsTagReader::thumbnail(const QString& filePath)
{
    enterApartment();
    // SingleItem serves the large previews; MusicView is smaller, so it is
    // the second choice. No resize option, so the shell may hand back its
    // next larger cached size.
    constexpr std::uint32_t requestedPixels = 1024;
    constexpr std::array modes{storage::FileProperties::ThumbnailMode::SingleItem,
                               storage::FileProperties::ThumbnailMode::MusicView};
    storage::StorageFile file{nullptr};
    try {
        file = openFile(filePath);
    } catch (const winrt::hresult_error&) {
        return std::nullopt;
    }
    for (const auto mode : modes) {
        try {
            const auto picture =
                file.GetThumbnailAsync(mode, requestedPixels, storage::FileProperties::ThumbnailOptions::None).get();
            if (!picture || picture.Type() != storage::FileProperties::ThumbnailType::Image || picture.Size() == 0
                || picture.Size() > core::MaxEmbeddedArtworkBytes) {
                continue;
            }
            const auto total = static_cast<std::uint32_t>(picture.Size());
            storage::Streams::DataReader reader(picture.GetInputStreamAt(0));
            if (reader.LoadAsync(total).get() != total) {
                continue;
            }
            QByteArray bytes(static_cast<qsizetype>(total), Qt::Uninitialized);
            reader.ReadBytes(winrt::array_view<std::uint8_t>(reinterpret_cast<std::uint8_t*>(bytes.data()),
                                                             reinterpret_cast<std::uint8_t*>(bytes.data()) + total));
            return bytes;
        } catch (const winrt::hresult_error&) {
        }
    }
    return std::nullopt;
}

} // namespace lmp::platform::windows
