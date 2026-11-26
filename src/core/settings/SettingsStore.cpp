#include "core/settings/SettingsStore.h"

#include "core/settings/SettingKeys.h"
#include "core/text/StrictUtf8.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QJsonValue>
#include <QMutexLocker>
#include <QSaveFile>

#include <algorithm>
#include <cerrno>
#include <climits>
#include <cmath>
#include <cwchar>
#include <optional>
#include <string>
#include <utility>

namespace lmp::core {

namespace {

// Reads a whole number from the start of `text` the way std::stoi did: leading
// white space, an optional sign and digits, ignoring anything after them.
// Nothing when there are no digits or the value does not fit in an int.
std::optional<int> leadingInt(const QString& text)
{
    const std::wstring wide = text.toStdWString();
    const wchar_t* begin = wide.c_str();
    wchar_t* end = nullptr;
    errno = 0;
    const long value = std::wcstol(begin, &end, 10);
    if (end == begin || errno == ERANGE || value < INT_MIN || value > INT_MAX) {
        return std::nullopt;
    }
    return static_cast<int>(value);
}

// The same for a number, the way std::stod did (which also accepts "inf",
// "nan" and hexadecimal forms, and rejects values out of range).
std::optional<double> leadingDouble(const QString& text)
{
    const std::wstring wide = text.toStdWString();
    const wchar_t* begin = wide.c_str();
    wchar_t* end = nullptr;
    errno = 0;
    const double value = std::wcstod(begin, &end);
    if (end == begin || errno == ERANGE) {
        return std::nullopt;
    }
    return value;
}

// A settings object holds a real setting when it has any key besides SchemaVersion.
bool hasUserKeys(const QJsonObject& object)
{
    const QStringList names = object.keys();
    return std::any_of(names.cbegin(), names.cend(), [](const QString& name) {
        return name != settings::keys::SchemaVersion;
    });
}

QJsonObject withSchemaVersion(QJsonObject object)
{
    if (!object.contains(settings::keys::SchemaVersion)) {
        object.insert(settings::keys::SchemaVersion, SettingsStore::CurrentSchemaVersion);
    }
    return object;
}

} // namespace

SettingsStore::SettingsStore(QString filePath, QObject* parent)
    : QObject(parent)
    , m_filePath(std::move(filePath))
{
    m_writer.setMaxThreadCount(1);
}

SettingsStore::~SettingsStore()
{
    // A setting changed just before exit is still saved.
    flush();
}

void SettingsStore::ensureLoaded() const
{
    if (m_loaded) {
        return;
    }

    QJsonObject object;
    bool recovered = false;
    QByteArray bytes;
    QFile file(m_filePath);
    if (file.open(QIODevice::ReadOnly)) {
        bytes = file.readAll();
        file.close();
    }
    // Text that is not valid UTF-8 was read as no text at all, and so is an
    // empty file: both start from empty settings without a quarantine.
    if (!bytes.isEmpty() && text::decodeUtf8Strict(bytes).has_value()) {
        QJsonParseError error{};
        const QJsonDocument document = QJsonDocument::fromJson(bytes, &error);
        if (error.error == QJsonParseError::NoError && document.isObject()) {
            object = document.object();
        } else {
            recovered = true;
            quarantineCorruptFile();
        }
    }

    m_json = withSchemaVersion(object);
    m_recoveredFromCorruption = recovered;
    m_loaded = true;
}

void SettingsStore::quarantineCorruptFile() const
{
    if (!QFileInfo::exists(m_filePath)) {
        return;
    }
    const QString stamp = QDateTime::currentDateTimeUtc().toString(QStringLiteral("yyyyMMdd'T'HHmmss"));
    const QString target = m_filePath + QStringLiteral(".corrupt-") + stamp;
    // Best effort: the unreadable bytes are kept for inspection when possible.
    // A copy quarantined earlier in the same second is replaced, as the
    // earlier rename replaced it.
    if (QFileInfo::exists(target)) {
        QFile::remove(target);
    }
    QFile::rename(m_filePath, target);
}

bool SettingsStore::persist()
{
    if (m_recoveredFromCorruption && !hasUserKeys(m_json)) {
        return false;
    }
    if (!writeVersion(m_json, ++m_version)) {
        return false;
    }
    m_recoveredFromCorruption = false;
    return true;
}

bool SettingsStore::writeVersion(const QJsonObject& json, quint64 version)
{
    const QMutexLocker locker(&m_writeMutex);
    if (version <= m_writtenVersion) {
        return true; // a newer save got there first
    }
    const QFileInfo info(m_filePath);
    if (!QDir().mkpath(info.absolutePath())) {
        return false;
    }
    QSaveFile file(m_filePath);
    if (!file.open(QIODevice::WriteOnly)) {
        return false;
    }
    const QByteArray text = QJsonDocument(json).toJson(QJsonDocument::Compact);
    if (file.write(text) != text.size()) {
        file.cancelWriting();
        return false;
    }
    if (!file.commit()) {
        return false;
    }
    m_writtenVersion = version;
    return true;
}

void SettingsStore::writeLatest()
{
    QJsonObject json;
    quint64 version = 0;
    {
        const QMutexLocker locker(&m_mutex);
        if (m_recoveredFromCorruption && !hasUserKeys(m_json)) {
            return;
        }
        json = m_json;
        version = m_version;
    }
    if (!writeVersion(json, version)) {
        return; // kept in memory; the next save carries it
    }
    const QMutexLocker locker(&m_mutex);
    if (hasUserKeys(json)) {
        m_recoveredFromCorruption = false;
    }
}

void SettingsStore::setStringInBackground(QAnyStringView key, const QString& value)
{
    const QString name = key.toString();
    {
        const QMutexLocker locker(&m_mutex);
        ensureLoaded();
        m_json.insert(name, value);
        ++m_version;
    }
    m_writer.start([this]() { writeLatest(); });
    emit changed(name);
}

void SettingsStore::flush()
{
    m_writer.waitForDone();
    writeLatest();
}

QJsonValue SettingsStore::lookup(QAnyStringView key) const
{
    ensureLoaded();
    return m_json.value(key.toString());
}

bool SettingsStore::contains(QAnyStringView key) const
{
    const QMutexLocker locker(&m_mutex);
    ensureLoaded();
    return m_json.contains(key.toString());
}

QString SettingsStore::string(QAnyStringView key, const QString& fallback) const
{
    const QMutexLocker locker(&m_mutex);
    const QJsonValue value = lookup(key);
    return value.isString() ? value.toString() : fallback;
}

bool SettingsStore::boolean(QAnyStringView key, bool fallback) const
{
    const QMutexLocker locker(&m_mutex);
    const QJsonValue value = lookup(key);
    if (value.isBool()) {
        return value.toBool();
    }
    if (value.isDouble()) {
        return value.toDouble() != 0.0;
    }
    if (value.isString()) {
        const QString text = value.toString();
        return text == QLatin1StringView("true") || text == QLatin1StringView("True")
            || text == QLatin1StringView("1");
    }
    return fallback;
}

int SettingsStore::integer(QAnyStringView key, int fallback) const
{
    const QMutexLocker locker(&m_mutex);
    const QJsonValue value = lookup(key);
    if (value.isDouble()) {
        // Truncated toward zero like the earlier cast; out of range falls back.
        const double number = value.toDouble();
        if (!std::isfinite(number) || number <= double(INT_MIN) - 1.0 || number >= double(INT_MAX) + 1.0) {
            return fallback;
        }
        return static_cast<int>(number);
    }
    if (value.isString()) {
        return leadingInt(value.toString()).value_or(fallback);
    }
    return fallback;
}

double SettingsStore::number(QAnyStringView key, double fallback) const
{
    const QMutexLocker locker(&m_mutex);
    const QJsonValue value = lookup(key);
    if (value.isDouble()) {
        return value.toDouble();
    }
    if (value.isString()) {
        return leadingDouble(value.toString()).value_or(fallback);
    }
    return fallback;
}

void SettingsStore::insertAndPersist(const QString& key, const QJsonValue& value)
{
    {
        const QMutexLocker locker(&m_mutex);
        ensureLoaded();
        m_json.insert(key, value);
        // As before, the value stays in memory even when the save fails.
        static_cast<void>(persist());
    }
    emit changed(key);
}

void SettingsStore::setString(QAnyStringView key, const QString& value)
{
    static_cast<void>(trySetString(key, value));
}

bool SettingsStore::trySetString(QAnyStringView key, const QString& value)
{
    const QString name = key.toString();
    bool saved = false;
    {
        const QMutexLocker locker(&m_mutex);
        ensureLoaded();
        const bool hadPrevious = m_json.contains(name);
        const QJsonValue previous = m_json.value(name);
        m_json.insert(name, value);
        saved = persist();
        if (!saved) {
            if (hadPrevious) {
                m_json.insert(name, previous);
            } else {
                m_json.remove(name);
            }
        }
    }
    if (saved) {
        emit changed(name);
    }
    return saved;
}

void SettingsStore::setBoolean(QAnyStringView key, bool value)
{
    insertAndPersist(key.toString(), value);
}

void SettingsStore::setInteger(QAnyStringView key, int value)
{
    insertAndPersist(key.toString(), value);
}

void SettingsStore::setNumber(QAnyStringView key, double value)
{
    insertAndPersist(key.toString(), value);
}

bool SettingsStore::remove(QAnyStringView key)
{
    const QString name = key.toString();
    {
        const QMutexLocker locker(&m_mutex);
        ensureLoaded();
        if (!m_json.contains(name)) {
            return true;
        }
        const QJsonValue previous = m_json.value(name);
        m_json.remove(name);
        if (!persist()) {
            m_json.insert(name, previous);
            return false;
        }
    }
    emit changed(name);
    return true;
}

double SettingsStore::volume() const
{
    double current = DefaultVolume;
    {
        const QMutexLocker locker(&m_mutex);
        current = m_volume;
    }
    return number(settings::keys::Volume, current);
}

void SettingsStore::setVolume(double volume)
{
    const double bounded = std::clamp(volume, 0.0, 1.0);
    {
        const QMutexLocker locker(&m_mutex);
        m_volume = bounded;
    }
    setNumber(settings::keys::Volume, bounded);
}

void SettingsStore::load()
{
    // A background save still running would land after the file is read.
    m_writer.waitForDone();
    {
        const QMutexLocker locker(&m_mutex);
        m_loaded = false;
        ensureLoaded();
    }
    emit changed(QString());
}

void SettingsStore::save()
{
    const QMutexLocker locker(&m_mutex);
    ensureLoaded();
    static_cast<void>(persist());
}

bool SettingsStore::reset()
{
    m_writer.waitForDone();
    bool removed = false;
    {
        const QMutexLocker locker(&m_mutex);
        const QJsonObject previousJson = m_json;
        const bool previousLoaded = m_loaded;
        const bool previousRecovered = m_recoveredFromCorruption;
        const double previousVolume = m_volume;

        m_json = withSchemaVersion(QJsonObject());
        m_loaded = true;
        m_recoveredFromCorruption = false;
        m_volume = DefaultVolume;

        if (!persist()) {
            m_json = previousJson;
            m_loaded = previousLoaded;
            m_recoveredFromCorruption = previousRecovered;
            m_volume = previousVolume;
            return false;
        }
        removed = removeRecoveryFiles();
    }
    emit changed(QString());
    return removed;
}

bool SettingsStore::removeRecoveryFiles() const
{
    const QFileInfo info(m_filePath);
    QDir folder = info.absoluteDir();
    if (!folder.exists()) {
        return true;
    }
    const QString prefix = info.fileName() + QStringLiteral(".corrupt-");
    const QFileInfoList entries =
        folder.entryInfoList(QDir::Files | QDir::Dirs | QDir::Hidden | QDir::System | QDir::NoDotAndDotDot);
    for (const QFileInfo& entry : entries) {
        if (!entry.fileName().startsWith(prefix)) {
            continue;
        }
        const bool gone = entry.isDir() ? QDir(entry.absoluteFilePath()).removeRecursively()
                                        : QFile::remove(entry.absoluteFilePath());
        if (!gone) {
            return false;
        }
    }
    // The temporary file earlier versions wrote before replacing the real one.
    const QString temporary = m_filePath + QStringLiteral(".tmp");
    return !QFileInfo::exists(temporary) || QFile::remove(temporary);
}

} // namespace lmp::core
