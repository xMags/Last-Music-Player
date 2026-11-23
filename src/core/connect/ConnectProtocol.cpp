#include "core/connect/ConnectProtocol.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonValue>
#include <QRandomGenerator>
#include <QRegularExpression>

#include <algorithm>
#include <cmath>

namespace lmp::core::connect {

namespace {

[[nodiscard]] double finiteNumber(const QJsonValue& value)
{
    if (!value.isDouble()) {
        return 0.0;
    }
    const double number = value.toDouble();
    return std::isfinite(number) ? number : 0.0;
}

[[nodiscard]] std::optional<qint64> epochOf(const QJsonObject& object)
{
    const QJsonValue value = object.value(QLatin1StringView("epoch"));
    if (!value.isDouble() || !std::isfinite(value.toDouble())) {
        return std::nullopt;
    }
    return static_cast<qint64>(value.toDouble());
}

[[nodiscard]] QList<QJsonObject> trackList(const QJsonValue& value, qsizetype limit)
{
    QList<QJsonObject> tracks;
    const QJsonArray array = value.toArray();
    for (const QJsonValue& entry : array) {
        if (tracks.size() >= limit) {
            break;
        }
        if (entry.isObject()) {
            tracks.append(entry.toObject());
        }
    }
    return tracks;
}

[[nodiscard]] QJsonArray trackArray(const QList<QJsonObject>& tracks)
{
    QJsonArray array;
    for (const QJsonObject& track : tracks) {
        array.append(track);
    }
    return array;
}

[[nodiscard]] QList<Device> parseDevices(const QJsonValue& value)
{
    QList<Device> devices;
    const QJsonArray array = value.toArray();
    for (const QJsonValue& entry : array) {
        const QJsonObject object = entry.toObject();
        const QJsonValue id = object.value(QLatin1StringView("deviceId"));
        const QJsonValue name = object.value(QLatin1StringView("name"));
        if (!id.isString() || !name.isString()) {
            continue;
        }
        const QString kind = object.value(QLatin1StringView("kind")).toString();
        Device device;
        device.deviceId = id.toString();
        device.name = name.toString().left(60);
        device.kind = kind == QLatin1StringView("windows")   ? DeviceKind::Windows
                      : kind == QLatin1StringView("android") ? DeviceKind::Android
                                                             : DeviceKind::Web;
        device.canPlay = object.value(QLatin1StringView("canPlay")) != QJsonValue(false);
        device.active = object.value(QLatin1StringView("active")) == QJsonValue(true);
        devices.append(device);
    }
    return devices;
}

[[nodiscard]] Session parseSession(const QJsonValue& value)
{
    const QJsonObject object = value.toObject();
    Session session;
    const QJsonValue active = object.value(QLatin1StringView("activeDeviceId"));
    session.activeDeviceId = active.isString() ? active.toString() : QString();
    const QJsonValue state = object.value(QLatin1StringView("state"));
    if (state.isObject()) {
        session.state = parsePlayback(state.toObject());
    }
    session.ageMs = static_cast<qint64>(std::max(0.0, finiteNumber(object.value(QLatin1StringView("ageMs")))));
    return session;
}

[[nodiscard]] QByteArray compact(const QJsonObject& object)
{
    return QJsonDocument(object).toJson(QJsonDocument::Compact);
}

} // namespace

std::optional<Playback> parsePlayback(const QJsonObject& object)
{
    Playback state;
    const QJsonValue track = object.value(QLatin1StringView("track"));
    state.track = track.isObject() ? track.toObject() : QJsonObject();
    state.playing = object.value(QLatin1StringView("playing")) == QJsonValue(true);
    state.positionSeconds = std::max(0.0, finiteNumber(object.value(QLatin1StringView("positionSec"))));
    state.durationSeconds = std::max(0.0, finiteNumber(object.value(QLatin1StringView("durationSec"))));
    state.volume = static_cast<int>(std::lround(std::clamp(finiteNumber(object.value(QLatin1StringView("volume"))), 0.0, 100.0)));
    state.shuffle = object.value(QLatin1StringView("shuffle")) == QJsonValue(true);
    const double repeat = finiteNumber(object.value(QLatin1StringView("repeatMode")));
    state.repeatMode = repeat == 1.0 ? 1 : repeat == 2.0 ? 2 : 0;
    state.upcoming = trackList(object.value(QLatin1StringView("upcoming")), UpcomingLimit);
    const QJsonValue cast = object.value(QLatin1StringView("castDevice"));
    state.castDevice = cast.isString() ? cast.toString().left(80) : QString();
    return state;
}

std::optional<Command> parseCommand(const QJsonObject& object)
{
    using Type = Command::Type;
    const QString type = object.value(QLatin1StringView("type")).toString();
    Command command;
    if (type == QLatin1StringView("play")) {
        command.type = Type::Play;
    } else if (type == QLatin1StringView("pause")) {
        command.type = Type::Pause;
    } else if (type == QLatin1StringView("toggle")) {
        command.type = Type::Toggle;
    } else if (type == QLatin1StringView("next")) {
        command.type = Type::Next;
    } else if (type == QLatin1StringView("previous")) {
        command.type = Type::Previous;
    } else if (type == QLatin1StringView("seek")) {
        const QJsonValue position = object.value(QLatin1StringView("positionSec"));
        if (!position.isDouble() || !std::isfinite(position.toDouble())) {
            return std::nullopt;
        }
        command.type = Type::Seek;
        command.positionSeconds = std::max(0.0, position.toDouble());
    } else if (type == QLatin1StringView("volume")) {
        const QJsonValue value = object.value(QLatin1StringView("value"));
        if (!value.isDouble() || !std::isfinite(value.toDouble())) {
            return std::nullopt;
        }
        command.type = Type::Volume;
        command.value = static_cast<int>(std::lround(std::clamp(value.toDouble(), 0.0, 100.0)));
    } else if (type == QLatin1StringView("shuffle")) {
        command.type = Type::Shuffle;
        command.enabled = object.value(QLatin1StringView("enabled")) == QJsonValue(true);
    } else if (type == QLatin1StringView("repeat")) {
        const double mode = finiteNumber(object.value(QLatin1StringView("mode")));
        if (mode != 0.0 && mode != 1.0 && mode != 2.0) {
            return std::nullopt;
        }
        command.type = Type::Repeat;
        command.value = static_cast<int>(mode);
    } else if (type == QLatin1StringView("playTracks")) {
        command.type = Type::PlayTracks;
        command.tracks = trackList(object.value(QLatin1StringView("tracks")), TrackLimit);
        if (command.tracks.isEmpty()) {
            return std::nullopt;
        }
        const double index = std::trunc(finiteNumber(object.value(QLatin1StringView("index"))));
        command.value = static_cast<int>(std::clamp(index, 0.0, static_cast<double>(command.tracks.size() - 1)));
        command.enabled = object.value(QLatin1StringView("shuffle")) == QJsonValue(true);
    } else if (type == QLatin1StringView("playUpcoming")) {
        const QJsonValue position = object.value(QLatin1StringView("position"));
        const double number = position.toDouble(-1.0);
        if (!position.isDouble() || number < 0.0 || number != std::trunc(number) || number > 1e6) {
            return std::nullopt;
        }
        command.type = Type::PlayUpcoming;
        command.value = static_cast<int>(number);
    } else if (type == QLatin1StringView("setUpcoming")) {
        command.type = Type::SetUpcoming;
        command.tracks = trackList(object.value(QLatin1StringView("tracks")), TrackLimit);
    } else if (type == QLatin1StringView("enqueue")) {
        command.type = Type::Enqueue;
        command.tracks = trackList(object.value(QLatin1StringView("tracks")), TrackLimit);
        command.enabled = object.value(QLatin1StringView("next")) == QJsonValue(true);
        if (command.tracks.isEmpty()) {
            return std::nullopt;
        }
    } else {
        return std::nullopt;
    }
    return command;
}

ServerMessage parseServerMessage(const QByteArray& text)
{
    ServerMessage message;
    if (text.size() > MaxMessageBytes) {
        return message;
    }
    QJsonParseError error{};
    const QJsonDocument document = QJsonDocument::fromJson(text, &error);
    if (error.error != QJsonParseError::NoError || !document.isObject()) {
        return message;
    }
    const QJsonObject object = document.object();
    const QString type = object.value(QLatin1StringView("type")).toString();
    message.epoch = epochOf(object);
    using Type = ServerMessage::Type;
    if (type == QLatin1StringView("welcome")) {
        message.type = Type::Welcome;
        message.devices = parseDevices(object.value(QLatin1StringView("devices")));
        message.session = parseSession(object.value(QLatin1StringView("session")));
    } else if (type == QLatin1StringView("devices")) {
        message.type = Type::Devices;
        message.devices = parseDevices(object.value(QLatin1StringView("devices")));
    } else if (type == QLatin1StringView("session")) {
        message.type = Type::Session;
        message.session = parseSession(object.value(QLatin1StringView("session")));
    } else if (type == QLatin1StringView("command")) {
        message.command = parseCommand(object.value(QLatin1StringView("command")).toObject());
        message.type = message.command ? Type::Command : Type::Ignored;
    } else if (type == QLatin1StringView("takeover")) {
        const QJsonValue state = object.value(QLatin1StringView("state"));
        if (state.isObject()) {
            message.type = Type::Takeover;
            message.state = parsePlayback(state.toObject());
            message.play = object.value(QLatin1StringView("play")) != QJsonValue(false);
        }
    } else if (type == QLatin1StringView("stop")) {
        message.type = Type::Stop;
    } else if (type == QLatin1StringView("error")) {
        message.type = Type::Error;
        message.errorCode = object.value(QLatin1StringView("code")).toString().left(64);
        message.errorMessage = object.value(QLatin1StringView("message")).toString().left(300);
    }
    return message;
}

QJsonObject playbackJson(const Playback& state)
{
    QJsonObject object;
    object.insert(QLatin1StringView("track"), state.track.isEmpty() ? QJsonValue(QJsonValue::Null) : QJsonValue(state.track));
    object.insert(QLatin1StringView("playing"), state.playing);
    object.insert(QLatin1StringView("positionSec"), std::max(0.0, state.positionSeconds));
    object.insert(QLatin1StringView("durationSec"), std::max(0.0, state.durationSeconds));
    object.insert(QLatin1StringView("volume"), std::clamp(state.volume, 0, 100));
    object.insert(QLatin1StringView("shuffle"), state.shuffle);
    object.insert(QLatin1StringView("repeatMode"), std::clamp(state.repeatMode, 0, 2));
    object.insert(QLatin1StringView("upcoming"), trackArray(state.upcoming.mid(0, UpcomingLimit)));
    object.insert(QLatin1StringView("castDevice"),
                  state.castDevice.isEmpty() ? QJsonValue(QJsonValue::Null) : QJsonValue(state.castDevice));
    return object;
}

QJsonObject commandJson(const Command& command)
{
    using Type = Command::Type;
    QJsonObject object;
    const auto type = [&object](const char* name) { object.insert(QLatin1StringView("type"), QLatin1StringView(name)); };
    switch (command.type) {
    case Type::Play:
        type("play");
        break;
    case Type::Pause:
        type("pause");
        break;
    case Type::Toggle:
        type("toggle");
        break;
    case Type::Next:
        type("next");
        break;
    case Type::Previous:
        type("previous");
        break;
    case Type::Seek:
        type("seek");
        object.insert(QLatin1StringView("positionSec"), std::max(0.0, command.positionSeconds));
        break;
    case Type::Volume:
        type("volume");
        object.insert(QLatin1StringView("value"), std::clamp(command.value, 0, 100));
        break;
    case Type::Shuffle:
        type("shuffle");
        object.insert(QLatin1StringView("enabled"), command.enabled);
        break;
    case Type::Repeat:
        type("repeat");
        object.insert(QLatin1StringView("mode"), std::clamp(command.value, 0, 2));
        break;
    case Type::PlayTracks:
        type("playTracks");
        object.insert(QLatin1StringView("tracks"), trackArray(command.tracks.mid(0, TrackLimit)));
        object.insert(QLatin1StringView("index"), std::max(0, command.value));
        object.insert(QLatin1StringView("shuffle"), command.enabled);
        break;
    case Type::PlayUpcoming:
        type("playUpcoming");
        object.insert(QLatin1StringView("position"), std::max(0, command.value));
        break;
    case Type::SetUpcoming:
        type("setUpcoming");
        object.insert(QLatin1StringView("tracks"), trackArray(command.tracks.mid(0, TrackLimit)));
        break;
    case Type::Enqueue:
        type("enqueue");
        object.insert(QLatin1StringView("tracks"), trackArray(command.tracks.mid(0, TrackLimit)));
        object.insert(QLatin1StringView("next"), command.enabled);
        break;
    }
    return object;
}

QByteArray helloMessage(const QString& deviceId, const QString& name)
{
    QJsonObject object;
    object.insert(QLatin1StringView("type"), QLatin1StringView("hello"));
    object.insert(QLatin1StringView("protocol"), ProtocolVersion);
    object.insert(QLatin1StringView("deviceId"), deviceId);
    const QString trimmed = name.trimmed().left(60);
    object.insert(QLatin1StringView("name"), trimmed.isEmpty() ? QStringLiteral("Windows PC") : trimmed);
    object.insert(QLatin1StringView("kind"), QLatin1StringView("windows"));
    object.insert(QLatin1StringView("canPlay"), true);
    return compact(object);
}

QByteArray stateMessage(qint64 epoch, const Playback& state)
{
    QJsonObject object;
    object.insert(QLatin1StringView("type"), QLatin1StringView("state"));
    object.insert(QLatin1StringView("epoch"), epoch);
    object.insert(QLatin1StringView("state"), playbackJson(state));
    return compact(object);
}

QByteArray commandMessage(const Command& command)
{
    QJsonObject object;
    object.insert(QLatin1StringView("type"), QLatin1StringView("command"));
    object.insert(QLatin1StringView("command"), commandJson(command));
    return compact(object);
}

QByteArray transferMessage(const QString& toDeviceId, bool play)
{
    QJsonObject object;
    object.insert(QLatin1StringView("type"), QLatin1StringView("transfer"));
    object.insert(QLatin1StringView("to"), toDeviceId);
    object.insert(QLatin1StringView("play"), play);
    return compact(object);
}

QByteArray releaseMessage()
{
    QJsonObject object;
    object.insert(QLatin1StringView("type"), QLatin1StringView("release"));
    return compact(object);
}

bool isValidDeviceId(const QString& deviceId)
{
    static const QRegularExpression pattern(QStringLiteral("^[A-Za-z0-9_-]{8,64}$"));
    return pattern.match(deviceId).hasMatch();
}

QString newDeviceId()
{
    QByteArray bytes(16, Qt::Uninitialized);
    for (char& byte : bytes) {
        byte = static_cast<char>(QRandomGenerator::system()->bounded(256));
    }
    return QString::fromLatin1(bytes.toHex());
}

std::chrono::milliseconds reconnectDelay(int attempt, double random, std::chrono::milliseconds minimum)
{
    const int exponent = std::clamp(attempt, 0, 5);
    const qint64 base = std::min<qint64>(MaxBackoff.count(), qint64{1000} << exponent);
    const double jitter = 0.75 + std::clamp(random, 0.0, 1.0) * 0.5;
    const auto delay = static_cast<qint64>(std::llround(static_cast<double>(base) * jitter));
    return std::max(minimum, std::chrono::milliseconds(delay));
}

std::optional<QUrl> socketUrl(const QString& apiOrigin, const QString& path, const QString& ticket)
{
    static const QRegularExpression pathPattern(QStringLiteral("^/v1/[A-Za-z0-9/_-]+$"));
    if (ticket.isEmpty() || ticket.size() > 256 || !pathPattern.match(path).hasMatch() || path.contains(QLatin1StringView("//"))) {
        return std::nullopt;
    }
    QUrl url(apiOrigin);
    if (!url.isValid() || url.host().isEmpty() || !url.path().isEmpty() || url.hasQuery() || url.hasFragment()) {
        return std::nullopt;
    }
    if (url.scheme() == QLatin1StringView("https")) {
        url.setScheme(QStringLiteral("wss"));
    } else if (url.scheme() == QLatin1StringView("http")) {
        url.setScheme(QStringLiteral("ws"));
    } else {
        return std::nullopt;
    }
    url.setPath(path);
    url.setQuery(QStringLiteral("ticket=") + QString::fromLatin1(QUrl::toPercentEncoding(ticket)));
    return url;
}

double positionAt(const Playback& state, qint64 ageMs, qint64 elapsedMs)
{
    if (!state.playing) {
        return state.positionSeconds;
    }
    const double moved =
        state.positionSeconds + static_cast<double>(std::max<qint64>(0, ageMs) + std::max<qint64>(0, elapsedMs)) / 1000.0;
    return state.durationSeconds > 0.0 ? std::min(state.durationSeconds, moved) : moved;
}

} // namespace lmp::core::connect
