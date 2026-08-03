#include "pch.h"
#include "Backend/CatalogParser.h"

#include <winrt/Windows.Data.Json.h>

#include <algorithm>

namespace LastMusicPlayer::Backend
{
    using namespace winrt::Windows::Data::Json;

    namespace
    {
        // Every accessor below tolerates a missing key and a value of the wrong
        // JSON type. The payload comes from a service in another repository, so
        // shape drift has to degrade a field rather than fail the whole page.

        winrt::hstring StringValue(JsonObject const& object, wchar_t const* key)
        {
            if (!object || !object.HasKey(key))
            {
                return {};
            }
            auto value = object.GetNamedValue(key);
            return value.ValueType() == JsonValueType::String ? value.GetString() : winrt::hstring{};
        }

        winrt::hstring FirstString(JsonObject const& object, std::initializer_list<wchar_t const*> keys)
        {
            for (auto key : keys)
            {
                auto value = StringValue(object, key);
                if (!value.empty())
                {
                    return value;
                }
            }
            return {};
        }

        double NumberValue(JsonObject const& object, wchar_t const* key, double fallback = 0.0)
        {
            if (!object || !object.HasKey(key))
            {
                return fallback;
            }
            auto value = object.GetNamedValue(key);
            return value.ValueType() == JsonValueType::Number ? value.GetNumber() : fallback;
        }

        bool BooleanValue(JsonObject const& object, wchar_t const* key, bool fallback = false)
        {
            if (!object || !object.HasKey(key))
            {
                return fallback;
            }
            auto value = object.GetNamedValue(key);
            return value.ValueType() == JsonValueType::Boolean ? value.GetBoolean() : fallback;
        }

        JsonObject ObjectValue(JsonObject const& object, wchar_t const* key)
        {
            if (!object || !object.HasKey(key))
            {
                return nullptr;
            }
            auto value = object.GetNamedValue(key);
            return value.ValueType() == JsonValueType::Object ? value.GetObject() : nullptr;
        }

        JsonArray ArrayValue(JsonObject const& object, wchar_t const* key)
        {
            if (!object || !object.HasKey(key))
            {
                return nullptr;
            }
            auto value = object.GetNamedValue(key);
            return value.ValueType() == JsonValueType::Array ? value.GetArray() : nullptr;
        }

        JsonObject ObjectAt(JsonArray const& array, std::uint32_t index)
        {
            auto value = array.GetAt(index);
            return value.ValueType() == JsonValueType::Object ? value.GetObject() : nullptr;
        }

        std::vector<winrt::hstring> StringArray(JsonObject const& object, wchar_t const* key)
        {
            std::vector<winrt::hstring> values;
            auto array = ArrayValue(object, key);
            if (!array)
            {
                return values;
            }
            for (std::uint32_t index = 0; index < array.Size(); ++index)
            {
                auto value = array.GetAt(index);
                if (value.ValueType() == JsonValueType::String && !value.GetString().empty())
                {
                    values.push_back(value.GetString());
                }
            }
            return values;
        }

        JsonObject ParseObject(winrt::hstring const& json)
        {
            JsonObject root{ nullptr };
            if (json.empty() || !JsonObject::TryParse(json, root))
            {
                return nullptr;
            }
            return root;
        }

        winrt::hstring NormalizeStorefrontValue(winrt::hstring const& value)
        {
            std::wstring normalized{ value.c_str() };
            std::transform(normalized.begin(), normalized.end(), normalized.begin(), [](wchar_t character)
            {
                return character >= L'A' && character <= L'Z'
                    ? static_cast<wchar_t>(character + (L'a' - L'A'))
                    : character;
            });
            return winrt::hstring{ normalized };
        }

        bool IsAbsoluteHttpUrl(winrt::hstring const& value) noexcept
        {
            std::wstring normalized{ value.c_str() };
            std::transform(normalized.begin(), normalized.end(), normalized.begin(), [](wchar_t character)
            {
                return character >= L'A' && character <= L'Z'
                    ? static_cast<wchar_t>(character + (L'a' - L'A'))
                    : character;
            });
            return normalized.rfind(L"https://", 0) == 0 || normalized.rfind(L"http://", 0) == 0;
        }

        bool SameChartRef(CatalogChartRef const& left, CatalogChartRef const& right) noexcept
        {
            return left.Storefront == right.Storefront
                && left.Kind == right.Kind
                && left.Id == right.Id
                && left.ResourceType == right.ResourceType;
        }

        std::optional<CatalogChartRef> ParseChartRef(
            JsonObject const& object,
            winrt::hstring const& fallbackStorefront,
            bool requireExplicitStorefront)
        {
            if (!object)
            {
                return std::nullopt;
            }

            auto explicitStorefront = NormalizeStorefrontValue(StringValue(object, L"storefront"));
            if (requireExplicitStorefront && !IsValidCatalogStorefront(explicitStorefront))
            {
                return std::nullopt;
            }

            CatalogChartRef chart;
            chart.Storefront = IsValidCatalogStorefront(explicitStorefront)
                ? explicitStorefront
                : NormalizeStorefrontValue(fallbackStorefront);
            chart.Kind = ParseCatalogChartKind(StringValue(object, L"kind"));
            chart.Id = StringValue(object, L"id");
            chart.ResourceType = ParseCatalogResourceType(StringValue(object, L"resourceType"));
            return IsValidCatalogChartRef(chart)
                ? std::optional<CatalogChartRef>{ std::move(chart) }
                : std::nullopt;
        }

        std::optional<CatalogChartDescriptor> ParseChartDescriptor(
            JsonObject const& object,
            winrt::hstring const& fallbackStorefront,
            bool requireExplicitStorefront)
        {
            auto chart = ParseChartRef(object, fallbackStorefront, requireExplicitStorefront);
            if (!chart)
            {
                return std::nullopt;
            }

            CatalogChartDescriptor descriptor;
            descriptor.Ref = std::move(*chart);
            descriptor.Name = StringValue(object, L"name");
            descriptor.Title = StringValue(object, L"title");
            descriptor.Subtitle = StringValue(object, L"subtitle");
            descriptor.StorefrontName = StringValue(object, L"storefrontName");
            auto artworkUrl = StringValue(object, L"artworkUrl");
            if (IsAbsoluteHttpUrl(artworkUrl))
            {
                descriptor.ArtworkUrl = artworkUrl;
            }
            if (descriptor.Name.empty() || descriptor.Title.empty())
            {
                return std::nullopt;
            }
            return descriptor;
        }

        CatalogItem ParseItem(JsonObject const& object, CatalogResourceType shelfType)
        {
            CatalogItem item;
            if (!object)
            {
                return item;
            }

            // Items inside a typed shelf usually repeat their own resourceType,
            // but the shelf's type is the fallback when they do not.
            item.Type = ParseCatalogResourceType(StringValue(object, L"resourceType"));
            if (item.Type == CatalogResourceType::Unknown)
            {
                item.Type = shelfType;
            }

            item.RemoteId = StringValue(object, L"id");
            item.CatalogId = StringValue(object, L"catalogId");
            item.Title = FirstString(object, { L"title", L"name" });
            item.Subtitle = FirstString(object, { L"artistName", L"curatorName" });
            item.AlbumName = StringValue(object, L"albumName");
            auto artworkUrl = StringValue(object, L"artworkUrl");
            if (IsAbsoluteHttpUrl(artworkUrl))
            {
                item.ArtworkUrl = artworkUrl;
            }
            item.SourceUrl = StringValue(object, L"sourceUrl");
            item.Provider = StringValue(object, L"provider");
            if (item.RemoteId.empty() && !item.CatalogId.empty())
            {
                auto provider = item.Provider.empty() ? winrt::hstring{ L"apple-music" } : item.Provider;
                item.RemoteId = winrt::hstring{
                    std::wstring{ provider.c_str() } + L":" + std::wstring{ item.CatalogId.c_str() }
                };
            }
            item.AlbumCatalogId = StringValue(object, L"albumCatalogId");
            item.ArtistCatalogId = StringValue(object, L"artistCatalogId");
            item.ReleaseDate = StringValue(object, L"releaseDate");
            item.Description = FirstString(object, { L"description", L"editorialNotes" });
            item.GenreNames = StringArray(object, L"genreNames");

            auto durationMs = NumberValue(object, L"durationMs");
            item.DurationMs = durationMs > 0.0 ? static_cast<std::int64_t>(durationMs) : 0;
            auto trackCount = NumberValue(object, L"trackCount");
            item.TrackCount = trackCount > 0.0 ? static_cast<std::int32_t>(trackCount) : 0;
            return item;
        }

        // An item with no title is not renderable, and one with an unrecognized
        // type has no tile to render into. Both are dropped rather than shown
        // as a blank card.
        bool IsUsableItem(CatalogItem const& item)
        {
            return item.Type != CatalogResourceType::Unknown && !item.Title.empty();
        }

        std::vector<CatalogItem> ParseItems(JsonArray const& array, CatalogResourceType shelfType)
        {
            std::vector<CatalogItem> items;
            if (!array)
            {
                return items;
            }
            items.reserve(array.Size());
            for (std::uint32_t index = 0; index < array.Size(); ++index)
            {
                auto item = ParseItem(ObjectAt(array, index), shelfType);
                if (IsUsableItem(item))
                {
                    items.push_back(std::move(item));
                }
            }
            return items;
        }

        std::optional<CatalogChartRef> LegacyShelfChart(
            winrt::hstring const& shelfId,
            CatalogResourceType resourceType,
            winrt::hstring const& storefront)
        {
            winrt::hstring expected;
            switch (resourceType)
            {
            case CatalogResourceType::Song: expected = L"top-songs"; break;
            case CatalogResourceType::Album: expected = L"top-albums"; break;
            case CatalogResourceType::Playlist: expected = L"top-playlists"; break;
            default: return std::nullopt;
            }
            if (shelfId != expected)
            {
                return std::nullopt;
            }
            return CatalogChartRef{ storefront, CatalogChartKind::Global, expected, resourceType };
        }

        std::optional<CatalogShelf> ParseShelf(JsonObject const& object, winrt::hstring const& storefront)
        {
            if (!object)
            {
                return std::nullopt;
            }

            CatalogShelf shelf;
            shelf.Id = StringValue(object, L"id");
            shelf.Title = StringValue(object, L"title");
            shelf.ItemType = ParseCatalogResourceType(StringValue(object, L"resourceType"));
            shelf.Items = ParseItems(ArrayValue(object, L"items"), shelf.ItemType);
            if (shelf.Items.empty() || shelf.ItemType == CatalogResourceType::Unknown)
            {
                return std::nullopt;
            }

            shelf.Chart = ParseChartRef(ObjectValue(object, L"chart"), storefront, false);
            if (!shelf.Chart)
            {
                shelf.Chart = LegacyShelfChart(shelf.Id, shelf.ItemType, storefront);
            }
            if (shelf.Chart && shelf.Chart->ResourceType != shelf.ItemType)
            {
                shelf.Chart.reset();
            }
            if (shelf.Title.empty())
            {
                shelf.Title = L"More music";
            }
            return shelf;
        }

        CatalogCityChartGroup ParseCityChartGroup(JsonObject const& object)
        {
            CatalogCityChartGroup group;
            if (!object)
            {
                return group;
            }
            auto charts = ArrayValue(object, L"charts");
            if (charts)
            {
                for (std::uint32_t index = 0; index < charts.Size(); ++index)
                {
                    auto descriptor = ParseChartDescriptor(ObjectAt(charts, index), {}, true);
                    if (descriptor && descriptor->Ref.Kind == CatalogChartKind::City)
                    {
                        group.Charts.push_back(std::move(*descriptor));
                    }
                }
            }
            group.Partial = BooleanValue(object, L"partial");
            return group;
        }
    }

    CatalogResourceType ParseCatalogResourceType(winrt::hstring const& value) noexcept
    {
        // The service uses singular names for items and plural for chart types.
        if (value == L"song" || value == L"songs") return CatalogResourceType::Song;
        if (value == L"album" || value == L"albums") return CatalogResourceType::Album;
        if (value == L"playlist" || value == L"playlists") return CatalogResourceType::Playlist;
        if (value == L"artist" || value == L"artists") return CatalogResourceType::Artist;
        return CatalogResourceType::Unknown;
    }

    winrt::hstring CatalogResourceTypeName(CatalogResourceType type) noexcept
    {
        switch (type)
        {
        case CatalogResourceType::Song: return L"song";
        case CatalogResourceType::Album: return L"album";
        case CatalogResourceType::Playlist: return L"playlist";
        case CatalogResourceType::Artist: return L"artist";
        default: return L"";
        }
    }

    CatalogChartKind ParseCatalogChartKind(winrt::hstring const& value) noexcept
    {
        if (value == L"global") return CatalogChartKind::Global;
        if (value == L"city") return CatalogChartKind::City;
        if (value == L"genre") return CatalogChartKind::Genre;
        return CatalogChartKind::Unknown;
    }

    winrt::hstring CatalogChartKindName(CatalogChartKind kind) noexcept
    {
        switch (kind)
        {
        case CatalogChartKind::Global: return L"global";
        case CatalogChartKind::City: return L"city";
        case CatalogChartKind::Genre: return L"genre";
        default: return L"";
        }
    }

    bool IsValidCatalogStorefront(winrt::hstring const& storefront) noexcept
    {
        return storefront.size() == 2
            && std::all_of(storefront.begin(), storefront.end(), [](wchar_t character)
            {
                return character >= L'a' && character <= L'z';
            });
    }

    bool IsValidCatalogChartRef(CatalogChartRef const& chart) noexcept
    {
        if (!IsValidCatalogStorefront(chart.Storefront)
            || chart.Kind == CatalogChartKind::Unknown
            || chart.ResourceType == CatalogResourceType::Unknown
            || chart.ResourceType == CatalogResourceType::Artist
            || chart.Id.empty()
            || chart.Id.size() > 160
            || !std::all_of(chart.Id.begin(), chart.Id.end(), [](wchar_t character)
            {
                return (character >= L'a' && character <= L'z')
                    || (character >= L'A' && character <= L'Z')
                    || (character >= L'0' && character <= L'9')
                    || character == L'_'
                    || character == L'-'
                    || character == L'.';
            }))
        {
            return false;
        }
        if (chart.Kind != CatalogChartKind::Global
            && chart.ResourceType != CatalogResourceType::Song)
        {
            return false;
        }
        if (chart.Kind == CatalogChartKind::Global)
        {
            auto expected = winrt::hstring{
                L"top-" + std::wstring{ CatalogResourceTypeName(chart.ResourceType).c_str() } + L"s"
            };
            return chart.Id == expected;
        }
        return true;
    }

    std::vector<CatalogStorefront> ParseCatalogStorefronts(winrt::hstring const& json) noexcept
    {
        std::vector<CatalogStorefront> storefronts;
        try
        {
            auto root = ParseObject(json);
            auto array = ArrayValue(root, L"storefronts");
            if (!array)
            {
                return storefronts;
            }
            storefronts.reserve(array.Size());
            for (std::uint32_t index = 0; index < array.Size(); ++index)
            {
                auto object = ObjectAt(array, index);
                CatalogStorefront storefront;
                storefront.Code = NormalizeStorefrontValue(StringValue(object, L"code"));
                storefront.Name = StringValue(object, L"name");
                if (!IsValidCatalogStorefront(storefront.Code))
                {
                    continue;
                }
                if (storefront.Name.empty())
                {
                    storefront.Name = storefront.Code;
                }
                storefronts.push_back(std::move(storefront));
            }
        }
        catch (...)
        {
            storefronts.clear();
        }
        return storefronts;
    }

    CatalogDiscovery ParseCatalogDiscovery(
        winrt::hstring const& json,
        winrt::hstring const& requestedStorefront) noexcept
    {
        CatalogDiscovery discovery;
        try
        {
            auto root = ParseObject(json);
            if (!root)
            {
                return discovery;
            }
            auto normalizedRequested = NormalizeStorefrontValue(requestedStorefront);
            auto responseStorefront = NormalizeStorefrontValue(StringValue(root, L"storefront"));
            if (IsValidCatalogStorefront(normalizedRequested)
                && IsValidCatalogStorefront(responseStorefront)
                && normalizedRequested != responseStorefront)
            {
                return discovery;
            }
            discovery.Storefront = IsValidCatalogStorefront(responseStorefront)
                ? responseStorefront
                : normalizedRequested;
            if (!IsValidCatalogStorefront(discovery.Storefront))
            {
                return {};
            }
            discovery.StorefrontName = StringValue(root, L"storefrontName");
            discovery.FetchedAt = StringValue(root, L"fetchedAt");
            discovery.Stale = BooleanValue(root, L"stale");

            auto shelves = ArrayValue(root, L"shelves");
            if (shelves)
            {
                for (std::uint32_t index = 0; index < shelves.Size(); ++index)
                {
                    auto shelf = ParseShelf(ObjectAt(shelves, index), discovery.Storefront);
                    if (shelf)
                    {
                        discovery.Shelves.push_back(std::move(*shelf));
                    }
                }
            }

            auto charts = ArrayValue(root, L"charts");
            if (charts)
            {
                for (std::uint32_t index = 0; index < charts.Size(); ++index)
                {
                    auto descriptor = ParseChartDescriptor(
                        ObjectAt(charts, index),
                        discovery.Storefront,
                        false);
                    if (descriptor)
                    {
                        discovery.Charts.push_back(std::move(*descriptor));
                    }
                }
            }

            auto groups = ObjectValue(root, L"cityChartGroups");
            if (groups)
            {
                CatalogCityChartGroups parsedGroups;
                parsedGroups.Indian = ParseCityChartGroup(ObjectValue(groups, L"indian"));
                parsedGroups.International = ParseCityChartGroup(ObjectValue(groups, L"international"));
                discovery.CityChartGroups = std::move(parsedGroups);
            }

            auto mood = ParseShelf(ObjectValue(root, L"moodActivityShelf"), discovery.Storefront);
            if (mood && mood->ItemType == CatalogResourceType::Playlist)
            {
                discovery.MoodActivityShelf = std::move(*mood);
            }
        }
        catch (...)
        {
            discovery = {};
        }
        return discovery;
    }

    CatalogChartPage ParseCatalogChartPage(
        winrt::hstring const& json,
        CatalogChartRef const& requestedChart) noexcept
    {
        CatalogChartPage page;
        try
        {
            if (!IsValidCatalogChartRef(requestedChart))
            {
                return page;
            }
            auto root = ParseObject(json);
            if (!root)
            {
                return page;
            }

            auto responseStorefrontValue = StringValue(root, L"storefront");
            auto responseStorefront = NormalizeStorefrontValue(responseStorefrontValue);
            if (!responseStorefrontValue.empty()
                && (!IsValidCatalogStorefront(responseStorefront)
                    || responseStorefront != requestedChart.Storefront))
            {
                return page;
            }

            auto responseId = StringValue(root, L"id");
            auto expectedGlobalId = winrt::hstring{
                L"global:" + std::wstring{ requestedChart.Id.c_str() }
            };
            if (!responseId.empty()
                && responseId != requestedChart.Id
                && (requestedChart.Kind != CatalogChartKind::Global || responseId != expectedGlobalId))
            {
                return page;
            }

            auto responseType = StringValue(root, L"type");
            if (!responseType.empty()
                && ParseCatalogResourceType(responseType) != requestedChart.ResourceType)
            {
                return page;
            }

            auto descriptor = ParseChartDescriptor(
                ObjectValue(root, L"descriptor"),
                requestedChart.Storefront,
                false);
            if (!descriptor && requestedChart.Kind == CatalogChartKind::Global)
            {
                CatalogChartDescriptor synthesized;
                synthesized.Ref = requestedChart;
                synthesized.Name = StringValue(root, L"title");
                if (synthesized.Name.empty())
                {
                    synthesized.Name = requestedChart.Id;
                }
                synthesized.Title = synthesized.Name;
                synthesized.Subtitle = L"Your music region";
                descriptor = std::move(synthesized);
            }
            if (!descriptor || !SameChartRef(descriptor->Ref, requestedChart))
            {
                return page;
            }

            page.Storefront = requestedChart.Storefront;
            page.Type = CatalogResourceTypeName(requestedChart.ResourceType);
            page.Id = responseId;
            page.Title = StringValue(root, L"title");
            if (page.Title.empty())
            {
                page.Title = descriptor->Title;
            }
            page.ItemType = requestedChart.ResourceType;
            page.Items = ParseItems(ArrayValue(root, L"items"), requestedChart.ResourceType);
            page.Descriptor = std::move(*descriptor);
            page.Stale = BooleanValue(root, L"stale");

            // nextOffset is null on the last page, so a present-and-numeric
            // check is what decides whether paging continues.
            if (root.HasKey(L"nextOffset"))
            {
                auto value = root.GetNamedValue(L"nextOffset");
                if (value.ValueType() == JsonValueType::Number && value.GetNumber() > 0.0)
                {
                    page.HasNextOffset = true;
                    page.NextOffset = static_cast<std::int32_t>(value.GetNumber());
                }
            }
        }
        catch (...)
        {
            page = {};
        }
        return page;
    }

    CatalogResourceDetail ParseCatalogResourceDetail(
        winrt::hstring const& json,
        winrt::hstring const& kind) noexcept
    {
        CatalogResourceDetail detail;
        try
        {
            auto root = ParseObject(json);
            if (!root)
            {
                return detail;
            }
            detail.Storefront = StringValue(root, L"storefront");

            bool isPlaylist = kind == L"playlists" || kind == L"playlist";
            auto header = ObjectValue(root, isPlaylist ? L"playlist" : L"album");
            auto headerType = isPlaylist ? CatalogResourceType::Playlist : CatalogResourceType::Album;
            detail.Resource = ParseItem(header, headerType);
            detail.Tracks = ParseItems(ArrayValue(root, L"tracks"), CatalogResourceType::Song);
        }
        catch (...)
        {
            detail.Tracks.clear();
        }
        return detail;
    }
}
