#pragma once

#include <QAnyStringView>
#include <QJsonObject>
#include <QMutex>
#include <QObject>
#include <QString>

namespace lmp::core {

// The user's settings: one JSON object in Settings.json, shared with installed
// earlier versions. Values keep their JSON types (string, bool, number), the
// file is UTF-8 without a byte order mark, and every change is saved at once
// through an atomic replace.
//
// Reads are lenient in the same way as before: a bool may also be stored as a
// non-zero number or the text "true", "True" or "1"; a number may be stored as
// text. A file that cannot be parsed is moved aside as
// "Settings.json.corrupt-YYYYMMDDTHHMMSS" (UTC), and after that nothing is
// written until a real setting is set, so one bad start cannot replace every
// saved setting with an empty file.
//
// Thread-safe: every member may be called from any thread. `changed` is
// emitted on the calling thread after the lock is released.
class SettingsStore : public QObject {
    Q_OBJECT

public:
    static constexpr int CurrentSchemaVersion = 1;
    static constexpr double DefaultVolume = 0.7;

    explicit SettingsStore(QString filePath, QObject* parent = nullptr);

    [[nodiscard]] QString filePath() const { return m_filePath; }

    // The value when it is stored as a string, otherwise `fallback`.
    [[nodiscard]] QString string(QAnyStringView key, const QString& fallback = {}) const;
    [[nodiscard]] bool boolean(QAnyStringView key, bool fallback = false) const;
    [[nodiscard]] int integer(QAnyStringView key, int fallback = 0) const;
    [[nodiscard]] double number(QAnyStringView key, double fallback = 0.0) const;
    [[nodiscard]] bool contains(QAnyStringView key) const;

    // Each setter saves at once. The value stays in memory even when the save
    // fails, as before; trySetString and remove put the old value back instead.
    void setString(QAnyStringView key, const QString& value);
    [[nodiscard]] bool trySetString(QAnyStringView key, const QString& value);
    void setBoolean(QAnyStringView key, bool value);
    void setInteger(QAnyStringView key, int value);
    void setNumber(QAnyStringView key, double value);
    // True when the key is gone from the saved file (or was never there).
    bool remove(QAnyStringView key);

    // Volume, clamped to 0 to 1.
    [[nodiscard]] double volume() const;
    void setVolume(double volume);

    // Reads the file again, dropping what is in memory.
    void load();
    // Writes what is in memory.
    void save();
    // Leaves only SchemaVersion, saved, and deletes the quarantined copies and
    // any leftover temporary file. False when the save or a deletion failed;
    // a failed save keeps the previous settings in memory.
    [[nodiscard]] bool reset();

signals:
    // A setting changed; an empty key after reset() or load() means all of them.
    void changed(const QString& key);

private:
    void ensureLoaded() const;
    [[nodiscard]] bool persist();
    [[nodiscard]] QJsonValue lookup(QAnyStringView key) const;
    void insertAndPersist(const QString& key, const QJsonValue& value);
    void quarantineCorruptFile() const;
    [[nodiscard]] bool removeRecoveryFiles() const;

    QString m_filePath;
    mutable QMutex m_mutex;
    mutable QJsonObject m_json;
    mutable bool m_loaded = false;
    // Set when the last load moved an unparseable file aside; cleared by the
    // first save that carries a real setting.
    mutable bool m_recoveredFromCorruption = false;
    double m_volume = DefaultVolume;
};

} // namespace lmp::core
