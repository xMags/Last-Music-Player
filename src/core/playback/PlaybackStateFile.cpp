#include "core/playback/PlaybackStateFile.h"

#include "core/library/UserDataOperationGate.h"
#include "core/playback/PlaybackStateRestore.h"

#include <QByteArray>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QLoggingCategory>
#include <QMutex>
#include <QMutexLocker>
#include <QSaveFile>
#include <QtConcurrent/QtConcurrentRun>

#include <utility>

namespace lmp::core {

namespace {

Q_LOGGING_CATEGORY(lcStateFile, "lmp.playback.statefile")

// The temporary name earlier versions wrote through; removed with the file.
constexpr QLatin1StringView kLegacyTempSuffix{".tmp"};

bool writeAtomically(const QString& filePath, const QByteArray& payload)
{
    if (!QDir().mkpath(QFileInfo(filePath).absolutePath())) {
        return false;
    }
    QSaveFile file(filePath);
    if (!file.open(QIODevice::WriteOnly)) {
        return false;
    }
    if (file.write(payload) != payload.size()) {
        file.cancelWriting();
        return false;
    }
    return file.commit();
}

bool removeIfPresent(const QString& path)
{
    return !QFileInfo::exists(path) || QFile::remove(path);
}

} // namespace

struct PlaybackStateFile::Shared {
    QMutex fileLock;
    // Guarded by fileLock; bumped by every write and by clear().
    quint64 version = 0;
};

PlaybackStateFile::PlaybackStateFile(QString filePath, UserDataOperationGate& gate, Executor executor)
    : m_filePath(std::move(filePath))
    , m_gate(gate)
    , m_shared(std::make_shared<Shared>())
    , m_executor(std::move(executor))
{
    // One thread keeps the jobs in the order they were queued.
    m_worker.setMaxThreadCount(1);
    m_worker.setObjectName(QStringLiteral("PlaybackStateFile"));
}

PlaybackStateFile::~PlaybackStateFile()
{
    flush();
}

bool PlaybackStateFile::write(const PlaybackState& state)
{
    std::optional<UserDataOperationGate::Lease> lease = m_gate.tryEnter();
    if (!lease) {
        return false;
    }
    quint64 version = 0;
    {
        const QMutexLocker locker(&m_shared->fileLock);
        version = ++m_shared->version;
    }
    // Held by the job until it has written, then released with it.
    auto heldLease = std::make_shared<UserDataOperationGate::Lease>(std::move(*lease));
    std::function<void()> job = [shared = m_shared, path = m_filePath, payload = serializePlaybackState(state),
                                 version, heldLease]() {
        const QMutexLocker locker(&shared->fileLock);
        if (shared->version != version) {
            return; // a newer write (or a clear) came after this one
        }
        if (!writeAtomically(path, payload)) {
            qCWarning(lcStateFile) << "Could not write the playback state file.";
        }
    };
    if (m_executor) {
        m_executor(std::move(job));
    } else {
        m_worker.start(std::move(job));
    }
    return true;
}

QFuture<std::optional<PlaybackState>> PlaybackStateFile::readAsync()
{
    return QtConcurrent::run(&m_worker, [path = m_filePath]() { return readNow(path); });
}

bool PlaybackStateFile::clear()
{
    const QMutexLocker locker(&m_shared->fileLock);
    ++m_shared->version;
    const bool fileRemoved = removeIfPresent(m_filePath);
    const bool tempRemoved = removeIfPresent(m_filePath + kLegacyTempSuffix);
    return fileRemoved && tempRemoved;
}

void PlaybackStateFile::flush()
{
    m_worker.waitForDone();
}

std::optional<PlaybackState> PlaybackStateFile::readNow(const QString& filePath)
{
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly)) {
        return std::nullopt;
    }
    const QByteArray bytes = file.readAll();
    if (file.error() != QFileDevice::NoError) {
        return std::nullopt;
    }
    std::optional<PlaybackState> state = parsePlaybackState(bytes);
    if (!state) {
        if (!bytes.isEmpty()) {
            qCWarning(lcStateFile) << "The playback state file could not be read; starting without it.";
        }
        return std::nullopt;
    }
    for (LegacyHistoryEntry& entry : state->homeHistory) {
        entry.localFileMissing = isLocalFileMissing(entry.track);
    }
    return state;
}

} // namespace lmp::core
