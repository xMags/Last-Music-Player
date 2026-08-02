#include "pch.h"
#include "Backend/CatalogParser.h"

#include <winrt/Windows.Data.Json.h>

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

            item.CatalogId = StringValue(object, L"catalogId");
            item.Title = FirstString(object, { L"title", L"name" });
            item.Subtitle = FirstString(object, { L"artistName", L"curatorName" });
            item.AlbumName = StringValue(object, L"albumName");
            item.ArtworkUrl = StringValue(object, L"artworkUrl");
            item.SourceUrl = StringValue(object, L"sourceUrl");
            item.Provider = StringValue(object, L"provider");
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
                storefront.Code = StringValue(object, L"code");
                storefront.Name = StringValue(object, L"name");
                if (storefront.Code.empty())
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

    CatalogDiscovery ParseCatalogDiscovery(winrt::hstring const& json) noexcept
    {
        CatalogDiscovery discovery;
        try
        {
            auto root = ParseObject(json);
            if (!root)
            {
                return discovery;
            }
            discovery.Storefront = StringValue(root, L"storefront");
            discovery.FetchedAt = StringValue(root, L"fetchedAt");

            auto shelves = ArrayValue(root, L"shelves");
            if (!shelves)
            {
                return discovery;
            }
            for (std::uint32_t index = 0; index < shelves.Size(); ++index)
            {
                auto object = ObjectAt(shelves, index);
                if (!object)
                {
                    continue;
                }
                CatalogShelf shelf;
                shelf.Id = StringValue(object, L"id");
                shelf.Title = StringValue(object, L"title");
                shelf.ItemType = ParseCatalogResourceType(StringValue(object, L"resourceType"));
                shelf.Items = ParseItems(ArrayValue(object, L"items"), shelf.ItemType);

                // `chart` is what turns a shelf's "See all" into a paged chart
                // request. A shelf without one is still a valid shelf.
                if (auto chart = ObjectValue(object, L"chart"))
                {
                    shelf.ChartId = StringValue(chart, L"id");
                    shelf.ChartType = StringValue(chart, L"resourceType");
                }

                if (shelf.Items.empty())
                {
                    continue;
                }
                if (shelf.Title.empty())
                {
                    shelf.Title = L"More music";
                }
                discovery.Shelves.push_back(std::move(shelf));
            }
        }
        catch (...)
        {
            discovery.Shelves.clear();
        }
        return discovery;
    }

    CatalogChartPage ParseCatalogChartPage(winrt::hstring const& json) noexcept
    {
        CatalogChartPage page;
        try
        {
            auto root = ParseObject(json);
            if (!root)
            {
                return page;
            }
            page.Storefront = StringValue(root, L"storefront");
            page.Type = StringValue(root, L"type");
            page.Id = StringValue(root, L"id");
            page.Title = StringValue(root, L"title");
            page.ItemType = ParseCatalogResourceType(StringValue(root, L"resourceType"));
            if (page.ItemType == CatalogResourceType::Unknown)
            {
                page.ItemType = ParseCatalogResourceType(page.Type);
            }
            page.Items = ParseItems(ArrayValue(root, L"items"), page.ItemType);

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
            page.Items.clear();
            page.HasNextOffset = false;
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
