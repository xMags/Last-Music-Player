#include "core/playback/PlaybackStateSnapshot.h"

#include "core/net/RemoteUrlPolicy.h"
#include "core/text/StrictUtf8.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QJsonValue>
#include <QLatin1StringView>

#include <algorithm>
#include <limits>

namespace lmp::core {

namespace {

// File keys, unchanged from earlier versions, plus the added QueueSnapshot.
constexpr QLatin1StringView kLastTrack{"LastTrack"};
constexpr QLatin1StringView kVolume{"Volume"};
constexpr QLatin1StringView kHomePlaySequence{"HomePlaySequence"};
constexpr QLatin1StringView kHomeHistory{"HomeHistory"};
constexpr QLatin1StringView kQueueSnapshot{"QueueSnapshot"};
constexpr QLatin1StringView kPlayOrder{"playOrder"};
constexpr QLatin1StringView kCurrentTrack{"currentTrack"};

constexpr QLatin1StringView kRemote{"remote"};

// Reads named values the way earlier versions did: a missing key gives the
// fallback, a key holding another type (null included) spoils the whole
// file. Once spoiled, every later read returns its fallback.
class StrictReader {
public:
    explicit StrictReader(const QJsonObject& object)
        : m_object(object)
    {
    }

    [[nodiscard]] QString string(QLatin1StringView key, const QString& fallback = {})
    {
        const QJsonValue value = m_object.value(key);
        if (value.isUndefined()) {
            return fallback;
        }
        m_ok = m_ok && value.isString();
        return value.isString() ? value.toString() : fallback;
    }

    [[nodiscard]] double number(QLatin1StringView key, double fallback)
    {
        const QJsonValue value = m_object.value(key);
        if (value.isUndefined()) {
            return fallback;
        }
        m_ok = m_ok && value.isDouble();
        return value.isDouble() ? value.toDouble() : fallback;
    }

    [[nodiscard]] bool boolean(QLatin1StringView key, bool fallback)
    {
        const QJsonValue value = m_object.value(key);
        if (value.isUndefined()) {
            return fallback;
        }
        m_ok = m_ok && value.isBool();
        return value.isBool() ? value.toBool() : fallback;
    }

    [[nodiscard]] bool ok() const noexcept { return m_ok; }

private:
    const QJsonObject& m_object;
    bool m_ok = true;
};

// max(0, value) converted to an unsigned count, clamped to its range.
template <typename Unsigned>
[[nodiscard]] Unsigned nonNegativeCount(double value)
{
    constexpr double upper = static_cast<double>(std::numeric_limits<Unsigned>::max());
    const double bounded = std::max(0.0, value);
    return bounded >= upper ? std::numeric_limits<Unsigned>::max() : static_cast<Unsigned>(bounded);
}

// One old home history entry: only these keys were ever read from it.
[[nodiscard]] std::optional<LegacyHistoryEntry> historyEntryFromJson(const QJsonObject& object)
{
    StrictReader reader(object);
    LegacyHistoryEntry entry;
    Track& track = entry.track;
    track.title = reader.string(QLatin1StringView("title"));
    track.artist = reader.string(QLatin1StringView("artist"));
    track.album = reader.string(QLatin1StringView("album"));
    track.genre = reader.string(QLatin1StringView("genre"));
    track.filePath = reader.string(QLatin1StringView("filePath"));
    track.artworkUrl = reader.string(QLatin1StringView("artworkUrl"));
    track.dateAdded = reader.string(QLatin1StringView("dateAdded"));
    track.durationText = reader.string(QLatin1StringView("duration"));
    const QString derivedKind = net::isHttpUrl(track.filePath) ? QString(kRemote) : QStringLiteral("local");
    track.sourceKind = reader.string(QLatin1StringView("sourceKind"), derivedKind);
    track.origin = reader.string(QLatin1StringView("provider"));
    track.sourceUrl = reader.string(QLatin1StringView("sourceUrl"));
    const auto labelFor = [](const QString& kind) {
        return kind == kRemote ? QStringLiteral("Remote") : QStringLiteral("Local");
    };
    track.sourceLabel = reader.string(QLatin1StringView("sourceLabel"), labelFor(track.sourceKind));
    entry.key = reader.string(QLatin1StringView("key"));
    track.durationSeconds = reader.number(QLatin1StringView("durationSeconds"), 0.0);
    track.dateAddedSortKey = reader.number(QLatin1StringView("dateAddedSortKey"), 0.0);
    track.liked = reader.boolean(QLatin1StringView("isLiked"), false);
    entry.playCount = nonNegativeCount<quint32>(reader.number(QLatin1StringView("playCount"), 0.0));
    entry.lastPlayedOrder = nonNegativeCount<quint64>(reader.number(QLatin1StringView("lastPlayedOrder"), 0.0));
    if (!reader.ok()) {
        return std::nullopt;
    }
    // A stored empty kind or label is replaced as well, as the old track
    // builder did.
    if (track.sourceKind.isEmpty()) {
        track.sourceKind = derivedKind;
    }
    if (track.sourceLabel.isEmpty()) {
        track.sourceLabel = labelFor(track.sourceKind);
    }
    return entry;
}

[[nodiscard]] std::optional<SavedQueue> savedQueueFromJson(const QJsonValue& value)
{
    if (!value.isObject()) {
        return std::nullopt;
    }
    const QJsonObject object = value.toObject();
    const QJsonValue current = object.value(kCurrentTrack);
    if (!current.isObject()) {
        return std::nullopt;
    }
    SavedQueue saved;
    saved.queue = QueueSnapshot::fromJson(object);
    saved.currentTrack = Track::fromSnapshotJson(current.toObject());
    saved.playOrder = object.value(kPlayOrder).toBool(false);
    return saved;
}

} // namespace

QByteArray serializePlaybackState(const PlaybackState& state)
{
    QJsonObject object;
    object.insert(kLastTrack, state.lastTrack);
    object.insert(kVolume, state.volume);
    if (state.queue) {
        QJsonObject queue = state.queue->queue.toJson();
        queue.insert(kPlayOrder, state.queue->playOrder);
        queue.insert(kCurrentTrack, state.queue->currentTrack.toSnapshotJson());
        object.insert(kQueueSnapshot, queue);
    }
    return QJsonDocument(object).toJson(QJsonDocument::Compact);
}

std::optional<PlaybackState> parsePlaybackState(const QByteArray& bytes)
{
    if (!text::decodeUtf8Strict(bytes)) {
        return std::nullopt;
    }
    QJsonParseError error{};
    const QJsonDocument document = QJsonDocument::fromJson(bytes, &error);
    if (error.error != QJsonParseError::NoError || !document.isObject()) {
        return std::nullopt;
    }
    const QJsonObject object = document.object();

    StrictReader reader(object);
    PlaybackState state;
    state.lastTrack = reader.string(kLastTrack);
    state.volume = reader.number(kVolume, PlaybackState::DefaultVolume);
    state.homePlaySequence = nonNegativeCount<quint64>(reader.number(kHomePlaySequence, 0.0));
    if (!reader.ok()) {
        return std::nullopt;
    }

    const QJsonValue history = object.value(kHomeHistory);
    if (history.isArray()) {
        const QJsonArray entries = history.toArray();
        state.homeHistory.reserve(entries.size());
        for (const QJsonValue& value : entries) {
            if (!value.isObject()) {
                continue;
            }
            std::optional<LegacyHistoryEntry> entry = historyEntryFromJson(value.toObject());
            if (!entry) {
                return std::nullopt;
            }
            state.homeHistory.append(std::move(*entry));
        }
    }

    state.queue = savedQueueFromJson(object.value(kQueueSnapshot));
    return state;
}

} // namespace lmp::core
