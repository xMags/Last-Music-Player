#include "pch.h"
#include "Backend/LyricsService.h"
#include "Backend/RemoteMusicService.h"

#include <winrt/Windows.Data.Json.h>

#include <algorithm>
#include <cwctype>

namespace LastMusicPlayer::Backend
{
    namespace
    {
        std::wstring LowercaseTrim(winrt::hstring const& value)
        {
            std::wstring out{ value.c_str() };
            auto notSpace = [](wchar_t ch) { return !std::iswspace(ch); };
            out.erase(out.begin(), std::find_if(out.begin(), out.end(), notSpace));
            out.erase(std::find_if(out.rbegin(), out.rend(), notSpace).base(), out.end());
            std::transform(out.begin(), out.end(), out.begin(),
                [](wchar_t ch) { return static_cast<wchar_t>(std::towlower(ch)); });
            return out;
        }

        LyricsResult ParseLyricsPayloadImpl(winrt::hstring const& payload)
        {
            LyricsResult result;
            if (payload.empty())
            {
                return result;
            }

            winrt::Windows::Data::Json::JsonObject root{ nullptr };
            if (!winrt::Windows::Data::Json::JsonObject::TryParse(payload, root))
            {
                return result;
            }

            auto namedBool = [&](wchar_t const* name) -> bool
            {
                if (!root.HasKey(name)) return false;
                auto value = root.GetNamedValue(name);
                return value.ValueType() == winrt::Windows::Data::Json::JsonValueType::Boolean
                    && value.GetBoolean();
            };
            auto namedString = [&](wchar_t const* name) -> std::wstring
            {
                if (!root.HasKey(name)) return {};
                auto value = root.GetNamedValue(name);
                return value.ValueType() == winrt::Windows::Data::Json::JsonValueType::String
                    ? std::wstring{ value.GetString().c_str() }
                    : std::wstring{};
            };

            result.Found = namedBool(L"found");
            result.Instrumental = namedBool(L"instrumental");
            result.Source = namedString(L"source");
            result.Plain = namedString(L"plain");
            result.TrackName = namedString(L"trackName");
            result.ArtistName = namedString(L"artistName");

            if (root.HasKey(L"synced"))
            {
                auto syncedValue = root.GetNamedValue(L"synced");
                if (syncedValue.ValueType() == winrt::Windows::Data::Json::JsonValueType::Array)
                {
                    auto array = syncedValue.GetArray();
                    result.Synced.reserve(array.Size());
                    for (auto const& item : array)
                    {
                        if (item.ValueType() != winrt::Windows::Data::Json::JsonValueType::Object)
                        {
                            continue;
                        }
                        auto line = item.GetObject();
                        LyricLine entry;
                        if (line.HasKey(L"timeMs"))
                        {
                            auto v = line.GetNamedValue(L"timeMs");
                            if (v.ValueType() == winrt::Windows::Data::Json::JsonValueType::Number)
                            {
                                entry.TimeMs = static_cast<int64_t>(v.GetNumber());
                            }
                        }
                        if (line.HasKey(L"text"))
                        {
                            auto v = line.GetNamedValue(L"text");
                            if (v.ValueType() == winrt::Windows::Data::Json::JsonValueType::String)
                            {
                                entry.Text = std::wstring{ v.GetString().c_str() };
                            }
                        }
                        result.Synced.push_back(std::move(entry));
                    }
                    std::sort(result.Synced.begin(), result.Synced.end(),
                        [](LyricLine const& a, LyricLine const& b) { return a.TimeMs < b.TimeMs; });
                }
            }

            return result;
        }
    }

    LyricsService::LyricsService() = default;

    void LyricsService::SetRemoteMusicService(RemoteMusicService* service)
    {
        if (m_remoteMusicService != service)
        {
            m_remoteMusicService = service;
            ClearCache();
        }
    }

    void LyricsService::ClearCache() noexcept
    {
        m_cache.clear();
        m_cacheOrder.clear();
    }

    winrt::Windows::Foundation::IAsyncOperation<winrt::hstring> LyricsService::FetchPayloadAsync(
        winrt::hstring artist,
        winrt::hstring title,
        winrt::hstring album,
        int64_t durationMs,
        winrt::hstring sourceUrl)
    {
        auto service = m_remoteMusicService;
        if (!service)
        {
            co_return winrt::hstring{};
        }

        auto scope = service->CaptureScope();
        auto key = RemoteScopeCacheKey(scope) + L"|"
            + MakeCacheKey(artist, title, durationMs, sourceUrl);

        winrt::hstring cached;
        if (TryGetCachedPayload(key, cached))
        {
            co_return service->IsCurrent(scope) ? cached : winrt::hstring{};
        }

        winrt::hstring payload;
        try
        {
            payload = co_await service->GetLyricsAsync(
                artist, title, album, durationMs, sourceUrl);
        }
        catch (...)
        {
            co_return winrt::hstring{};
        }

        if (!service->IsCurrent(scope) || payload.empty())
        {
            co_return winrt::hstring{};
        }

        StoreCachedPayload(key, payload);
        co_return payload;
    }

    LyricsResult LyricsService::ParseLyrics(winrt::hstring const& payload)
    {
        return ParseLyricsPayloadImpl(payload);
    }

    int32_t LyricsService::ActiveLineIndex(std::vector<LyricLine> const& synced, int64_t positionMs)
    {
        if (synced.empty() || positionMs < synced.front().TimeMs)
        {
            return -1;
        }
        auto it = std::upper_bound(synced.begin(), synced.end(), positionMs,
            [](int64_t pos, LyricLine const& line) { return pos < line.TimeMs; });
        return static_cast<int32_t>(std::distance(synced.begin(), it) - 1);
    }

    std::wstring LyricsService::MakeCacheKey(
        winrt::hstring const& artist,
        winrt::hstring const& title,
        int64_t durationMs,
        winrt::hstring const& sourceUrl)
    {
        if (!sourceUrl.empty())
        {
            return std::wstring{ L"u|" } + sourceUrl.c_str();
        }
        std::wstring key = LowercaseTrim(artist);
        key.push_back(L'|');
        key.append(LowercaseTrim(title));
        key.push_back(L'|');
        key.append(std::to_wstring(durationMs / 1000));
        return key;
    }

    bool LyricsService::TryGetCachedPayload(std::wstring const& key, winrt::hstring& outPayload) const
    {
        auto it = m_cache.find(key);
        if (it == m_cache.end())
        {
            return false;
        }
        outPayload = it->second;
        return true;
    }

    void LyricsService::StoreCachedPayload(std::wstring const& key, winrt::hstring const& payload)
    {
        if (m_cache.find(key) == m_cache.end())
        {
            m_cacheOrder.push_back(key);
            if (m_cacheOrder.size() > kCacheCapacity)
            {
                auto evicted = m_cacheOrder.front();
                m_cacheOrder.pop_front();
                m_cache.erase(evicted);
            }
        }
        m_cache[key] = payload;
    }
}
