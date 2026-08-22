#include "pch.h"
#include "Backend/SettingsManager.h"

#include <windows.h>

#include <filesystem>
#include <fstream>
#include <sstream>
#include <cstdlib>

namespace
{
    namespace json = winrt::Windows::Data::Json;

    std::filesystem::path SettingsFilePath()
    {
        std::filesystem::path base;
        wchar_t* localAppData{};
        size_t length{};
        if (_wdupenv_s(&localAppData, &length, L"LOCALAPPDATA") == 0 && localAppData && *localAppData)
        {
            base = std::filesystem::path{ localAppData } / L"Last Music Player";
        }
        else
        {
            base = std::filesystem::current_path() / L"Last Music Player";
        }
        std::free(localAppData);
        return base / L"Settings.json";
    }

    std::string ToUtf8(std::wstring const& text)
    {
        if (text.empty())
        {
            return {};
        }

        auto required = WideCharToMultiByte(CP_UTF8, 0, text.c_str(), -1, nullptr, 0, nullptr, nullptr);
        if (required <= 1)
        {
            return {};
        }

        std::string utf8(static_cast<size_t>(required - 1), '\0');
        WideCharToMultiByte(CP_UTF8, 0, text.c_str(), -1, utf8.data(), required, nullptr, nullptr);
        return utf8;
    }

    std::wstring FromUtf8(std::string const& value)
    {
        if (value.empty())
        {
            return {};
        }

        auto required = MultiByteToWideChar(CP_UTF8, 0, value.data(), static_cast<int>(value.size()), nullptr, 0);
        if (required <= 0)
        {
            return {};
        }

        std::wstring wide(static_cast<size_t>(required), L'\0');
        MultiByteToWideChar(CP_UTF8, 0, value.data(), static_cast<int>(value.size()), wide.data(), required);
        return wide;
    }

    winrt::hstring ReadSettingsText()
    {
        try
        {
            std::ifstream file{ SettingsFilePath(), std::ios::binary };
            if (!file)
            {
                return {};
            }

            std::ostringstream buffer;
            buffer << file.rdbuf();
            return winrt::hstring{ FromUtf8(buffer.str()) };
        }
        catch (...)
        {
            return {};
        }
    }

    bool WriteSettingsText(winrt::hstring const& value)
    {
        try
        {
            auto path = SettingsFilePath();
            std::filesystem::create_directories(path.parent_path());
            auto tempPath = path;
            tempPath += L".tmp";
            std::ofstream file{ tempPath, std::ios::binary | std::ios::trunc };
            auto utf8 = ToUtf8(std::wstring{ value.c_str() });
            file.write(utf8.data(), static_cast<std::streamsize>(utf8.size()));
            file.close();
            if (!file)
            {
                std::filesystem::remove(tempPath);
                return false;
            }
            if (!::MoveFileExW(
                tempPath.c_str(),
                path.c_str(),
                MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
            {
                std::filesystem::remove(tempPath);
                return false;
            }
            return true;
        }
        catch (...)
        {
            return false;
        }
    }
}

namespace LastMusicPlayer::Backend
{
    namespace
    {
        // Rename Settings.json -> Settings.json.corrupt-<UTC timestamp> so
        // the unparseable bytes are preserved for forensics and so the
        // next launch can't keep reading the same broken file. Ignores
        // errors (best-effort recovery).
        void QuarantineCorruptSettings()
        {
            try
            {
                auto path = SettingsFilePath();
                if (!std::filesystem::exists(path))
                {
                    return;
                }
                SYSTEMTIME now{};
                ::GetSystemTime(&now);
                wchar_t stamp[64]{};
                swprintf_s(stamp, L".corrupt-%04u%02u%02uT%02u%02u%02u",
                    now.wYear, now.wMonth, now.wDay,
                    now.wHour, now.wMinute, now.wSecond);
                auto target = path;
                target += stamp;
                std::error_code ec;
                std::filesystem::rename(path, target, ec);
            }
            catch (...)
            {
            }
        }

        bool RemoveSettingsRecoveryFiles()
        {
            try
            {
                auto settingsPath = SettingsFilePath();
                auto parent = settingsPath.parent_path();
                auto corruptPrefix = settingsPath.filename().wstring() + L".corrupt-";

                std::error_code existsError;
                if (!std::filesystem::exists(parent, existsError))
                {
                    return !existsError;
                }

                for (auto const& entry : std::filesystem::directory_iterator(parent))
                {
                    auto name = entry.path().filename().wstring();
                    if (name.rfind(corruptPrefix, 0) != 0)
                    {
                        continue;
                    }

                    std::error_code removeError;
                    std::filesystem::remove_all(entry.path(), removeError);
                    if (removeError)
                    {
                        return false;
                    }
                }

                std::error_code tempError;
                std::filesystem::remove(settingsPath.wstring() + L".tmp", tempError);
                return !tempError;
            }
            catch (...)
            {
                return false;
            }
        }

        // A "real" payload is one with more than just the SchemaVersion
        // synthetic key. Used to gate writes during corruption recovery —
        // a freshly-loaded empty document should not overwrite a backup
        // until the user actually changes something.
        bool HasUserKeys(json::JsonObject const& obj)
        {
            if (obj == nullptr) return false;
            try
            {
                for (auto const& kv : obj)
                {
                    if (kv.Key() != L"SchemaVersion")
                    {
                        return true;
                    }
                }
            }
            catch (...)
            {
            }
            return false;
        }
    }

    void SettingsManager::EnsureLoaded() const
    {
        if (m_loaded)
        {
            return;
        }

        json::JsonObject obj{ nullptr };
        bool recovered = false;
        auto payload = ReadSettingsText();
        if (!payload.empty())
        {
            try
            {
                obj = json::JsonObject::Parse(payload);
            }
            catch (...)
            {
                obj = nullptr;
                recovered = true;
                // Move the unreadable file aside so a subsequent good
                // write doesn't lose the forensic copy.
                QuarantineCorruptSettings();
            }
        }

        if (obj == nullptr)
        {
            obj = json::JsonObject{};
        }

        if (!obj.HasKey(L"SchemaVersion"))
        {
            obj.Insert(L"SchemaVersion",
                json::JsonValue::CreateNumberValue(static_cast<double>(CurrentSchemaVersion)));
        }

        m_json = obj;
        m_recoveredFromCorruption = recovered;
        m_loaded = true;
    }

    bool SettingsManager::Persist()
    {
        if (m_json == nullptr)
        {
            return false;
        }
        // After a corruption-recovery load, refuse to overwrite until the
        // in-memory object actually carries user keys. Otherwise an app
        // launch that follows a bad write would replace every persisted
        // setting with a SchemaVersion-only document.
        if (m_recoveredFromCorruption && !HasUserKeys(m_json))
        {
            return false;
        }
        if (!WriteSettingsText(m_json.Stringify()))
        {
            return false;
        }
        // Successful write with real keys means we're past the recovery
        // window. Clear the flag so future intentionally empty states work.
        m_recoveredFromCorruption = false;
        return true;
    }

    void SettingsManager::Load()
    {
        std::lock_guard<std::recursive_mutex> guard{ m_mutex };
        m_loaded = false;
        EnsureLoaded();
    }

    void SettingsManager::Save()
    {
        std::lock_guard<std::recursive_mutex> guard{ m_mutex };
        EnsureLoaded();
        Persist();
    }

    bool SettingsManager::Reset()
    {
        std::lock_guard<std::recursive_mutex> guard{ m_mutex };

        auto previousJson = m_json;
        auto previousLoaded = m_loaded;
        auto previousRecovered = m_recoveredFromCorruption;
        auto previousVolume = m_volume;

        auto obj = json::JsonObject{};
        obj.Insert(L"SchemaVersion",
            json::JsonValue::CreateNumberValue(static_cast<double>(CurrentSchemaVersion)));
        m_json = obj;
        m_loaded = true;
        m_recoveredFromCorruption = false;
        m_volume = 0.7;

        if (!Persist())
        {
            m_json = previousJson;
            m_loaded = previousLoaded;
            m_recoveredFromCorruption = previousRecovered;
            m_volume = previousVolume;
            return false;
        }

        return RemoveSettingsRecoveryFiles();
    }

    void SettingsManager::SetVolume(double volume)
    {
        m_volume = (volume < 0.0) ? 0.0 : (volume > 1.0) ? 1.0 : volume;
        SetDouble(L"Volume", m_volume); // persist to Settings.json immediately
    }

    double SettingsManager::GetVolume() const
    {
        return GetDouble(L"Volume", m_volume);
    }

    winrt::hstring SettingsManager::GetString(winrt::hstring const& key, winrt::hstring const& def) const
    {
        std::lock_guard<std::recursive_mutex> guard{ m_mutex };
        EnsureLoaded();
        try
        {
            if (m_json.HasKey(key))
            {
                auto value = m_json.Lookup(key);
                if (value && value.ValueType() == json::JsonValueType::String)
                {
                    return value.GetString();
                }
            }
        }
        catch (...)
        {
        }
        return def;
    }

    void SettingsManager::SetString(winrt::hstring const& key, winrt::hstring const& value)
    {
        std::lock_guard<std::recursive_mutex> guard{ m_mutex };
        EnsureLoaded();
        m_json.Insert(key, json::JsonValue::CreateStringValue(value));
        Persist();
    }

    bool SettingsManager::Remove(winrt::hstring const& key)
    {
        std::lock_guard<std::recursive_mutex> guard{ m_mutex };
        EnsureLoaded();
        if (!m_json.HasKey(key))
        {
            return true;
        }

        auto previous = m_json.Lookup(key);
        m_json.Remove(key);
        if (Persist())
        {
            return true;
        }

        m_json.Insert(key, previous);
        return false;
    }

    bool SettingsManager::GetBool(winrt::hstring const& key, bool def) const
    {
        std::lock_guard<std::recursive_mutex> guard{ m_mutex };
        EnsureLoaded();
        try
        {
            if (m_json.HasKey(key))
            {
                auto value = m_json.Lookup(key);
                if (value)
                {
                    switch (value.ValueType())
                    {
                    case json::JsonValueType::Boolean:
                        return value.GetBoolean();
                    case json::JsonValueType::Number:
                        return value.GetNumber() != 0.0;
                    case json::JsonValueType::String:
                    {
                        auto s = value.GetString();
                        return s == L"true" || s == L"True" || s == L"1";
                    }
                    default:
                        break;
                    }
                }
            }
        }
        catch (...)
        {
        }
        return def;
    }

    void SettingsManager::SetBool(winrt::hstring const& key, bool value)
    {
        std::lock_guard<std::recursive_mutex> guard{ m_mutex };
        EnsureLoaded();
        m_json.Insert(key, json::JsonValue::CreateBooleanValue(value));
        Persist();
    }

    int SettingsManager::GetInt(winrt::hstring const& key, int def) const
    {
        std::lock_guard<std::recursive_mutex> guard{ m_mutex };
        EnsureLoaded();
        try
        {
            if (m_json.HasKey(key))
            {
                auto value = m_json.Lookup(key);
                if (value)
                {
                    if (value.ValueType() == json::JsonValueType::Number)
                    {
                        return static_cast<int>(value.GetNumber());
                    }
                    if (value.ValueType() == json::JsonValueType::String)
                    {
                        return std::stoi(std::wstring{ value.GetString().c_str() });
                    }
                }
            }
        }
        catch (...)
        {
        }
        return def;
    }

    void SettingsManager::SetInt(winrt::hstring const& key, int value)
    {
        std::lock_guard<std::recursive_mutex> guard{ m_mutex };
        EnsureLoaded();
        m_json.Insert(key, json::JsonValue::CreateNumberValue(static_cast<double>(value)));
        Persist();
    }

    double SettingsManager::GetDouble(winrt::hstring const& key, double def) const
    {
        std::lock_guard<std::recursive_mutex> guard{ m_mutex };
        EnsureLoaded();
        try
        {
            if (m_json.HasKey(key))
            {
                auto value = m_json.Lookup(key);
                if (value)
                {
                    if (value.ValueType() == json::JsonValueType::Number)
                    {
                        return value.GetNumber();
                    }
                    if (value.ValueType() == json::JsonValueType::String)
                    {
                        return std::stod(std::wstring{ value.GetString().c_str() });
                    }
                }
            }
        }
        catch (...)
        {
        }
        return def;
    }

    void SettingsManager::SetDouble(winrt::hstring const& key, double value)
    {
        std::lock_guard<std::recursive_mutex> guard{ m_mutex };
        EnsureLoaded();
        m_json.Insert(key, json::JsonValue::CreateNumberValue(value));
        Persist();
    }

    void SettingsManager::Set(const std::wstring& key, const std::wstring& value)
    {
        SetString(winrt::hstring{ key }, winrt::hstring{ value });
    }

    std::wstring SettingsManager::Get(const std::wstring& key, const std::wstring& defaultValue) const
    {
        auto result = GetString(winrt::hstring{ key }, winrt::hstring{ defaultValue });
        return std::wstring{ result.c_str() };
    }

    void SettingsManager::SetTheme(const std::wstring& theme)
    {
        SetString(L"Theme", winrt::hstring{ theme });
    }

    std::wstring SettingsManager::GetTheme() const
    {
        auto result = GetString(L"Theme", L"System");
        return std::wstring{ result.c_str() };
    }
}
