#pragma once
#include <string>
#include <mutex>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Data.Json.h>

namespace LastMusicPlayer::Backend
{
    // Single typed, JSON-backed source of truth for user preferences.
    // Persists to %LOCALAPPDATA%\Last Music Player\Settings.json (the same
    // file the legacy ReadAppSettingString/WriteAppSettingString helpers used,
    // so existing keys load unchanged).
    class SettingsManager
    {
    public:
        SettingsManager() = default;
        ~SettingsManager() = default;

        static constexpr int CurrentSchemaVersion = 1;

        // Volume is persisted under the "Volume" key in Settings.json and
        // written synchronously on every change, so it survives restarts
        // independent of the async app-state save/restore path.
        void SetVolume(double volume);
        double GetVolume() const;

        // Theme is backed by the JSON store under the "Theme" key.
        void SetTheme(const std::wstring& theme);
        std::wstring GetTheme() const;

        // Typed accessors (all backed by Settings.json).
        winrt::hstring GetString(winrt::hstring const& key, winrt::hstring const& def = L"") const;
        void SetString(winrt::hstring const& key, winrt::hstring const& value);

        bool GetBool(winrt::hstring const& key, bool def = false) const;
        void SetBool(winrt::hstring const& key, bool value);

        int GetInt(winrt::hstring const& key, int def = 0) const;
        void SetInt(winrt::hstring const& key, int value);

        double GetDouble(winrt::hstring const& key, double def = 0.0) const;
        void SetDouble(winrt::hstring const& key, double value);

        // Legacy generic key/value API (now backed by Settings.json string values).
        void Set(const std::wstring& key, const std::wstring& value);
        std::wstring Get(const std::wstring& key, const std::wstring& defaultValue = L"") const;
        // Removes a key and returns true only when the sanitized document was
        // durably written. A failed write restores the in-memory value.
        bool Remove(winrt::hstring const& key);

        // Persistence.
        void Load();   // (re)load from disk into memory
        void Save();   // force flush current in-memory state to disk
        [[nodiscard]] bool Reset();  // clear settings and quarantine files durably

    private:
        void EnsureLoaded() const;
        bool Persist();

        mutable std::recursive_mutex m_mutex;
        mutable bool m_loaded{ false };
        mutable winrt::Windows::Data::Json::JsonObject m_json{ nullptr };
        // Set when EnsureLoaded() recovered from an unparseable file. While
        // this is true and the in-memory object has no real keys, Persist()
        // refuses to write — otherwise a single bad startup would replace
        // every saved setting with an empty document.
        mutable bool m_recoveredFromCorruption{ false };
        double m_volume{ 0.7 };
    };
}
