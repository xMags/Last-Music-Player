#include "pch.h"
#include "Backend/RecentSearchStore.h"

#include <algorithm>
#include <cwctype>

namespace LastMusicPlayer::Backend
{
    namespace
    {
        std::wstring Trim(std::wstring value)
        {
            auto const isSpace = [](wchar_t character)
            {
                return std::iswspace(character) != 0;
            };
            value.erase(value.begin(), std::find_if_not(value.begin(), value.end(), isSpace));
            value.erase(std::find_if_not(value.rbegin(), value.rend(), isSpace).base(), value.end());
            return value;
        }

        std::wstring Fold(std::wstring value)
        {
            std::transform(value.begin(), value.end(), value.begin(),
                [](wchar_t character)
                {
                    return static_cast<wchar_t>(std::towlower(character));
                });
            return value;
        }
    }

    std::vector<std::wstring> RecordRecentSearch(
        std::vector<std::wstring> const& history,
        std::wstring query,
        std::size_t limit)
    {
        query = Trim(std::move(query));
        if (query.empty() || limit == 0)
        {
            return limit == 0 ? std::vector<std::wstring>{} : history;
        }

        std::vector<std::wstring> result;
        result.reserve((std::min)(limit, history.size() + 1));
        result.push_back(query);
        auto const foldedQuery = Fold(query);
        for (auto const& existing : history)
        {
            if (result.size() >= limit)
            {
                break;
            }
            auto cleaned = Trim(existing);
            if (cleaned.empty() || Fold(cleaned) == foldedQuery)
            {
                continue;
            }
            result.push_back(std::move(cleaned));
        }
        return result;
    }

    std::wstring EncodeRecentSearches(std::vector<std::wstring> const& history)
    {
        std::wstring encoded;
        for (std::size_t index = 0; index < history.size(); ++index)
        {
            if (index > 0)
            {
                encoded.push_back(L'\n');
            }
            for (auto const character : history[index])
            {
                if (character == L'\\')
                {
                    encoded += L"\\\\";
                }
                else if (character == L'\n' || character == L'\r')
                {
                    encoded += L"\\n";
                }
                else
                {
                    encoded.push_back(character);
                }
            }
        }
        return encoded;
    }

    std::vector<std::wstring> DecodeRecentSearches(
        std::wstring const& encoded,
        std::size_t limit)
    {
        std::vector<std::wstring> result;
        std::wstring current;
        bool escaped = false;
        auto append = [&]()
        {
            auto cleaned = Trim(std::move(current));
            current.clear();
            if (!cleaned.empty() && result.size() < limit)
            {
                result.push_back(std::move(cleaned));
            }
        };

        for (auto const character : encoded)
        {
            if (escaped)
            {
                current.push_back(character == L'n' ? L'\n' : character);
                escaped = false;
                continue;
            }
            if (character == L'\\')
            {
                escaped = true;
                continue;
            }
            if (character == L'\n')
            {
                append();
                if (result.size() >= limit)
                {
                    return result;
                }
                continue;
            }
            current.push_back(character);
        }
        if (escaped)
        {
            current.push_back(L'\\');
        }
        append();
        return result;
    }
}
