#include "pch.h"
#include "Backend/HistoryImport.h"

#include <windows.h>
#include <bcrypt.h>

#include <array>
#include <cstdint>
#include <string>
#include <vector>

#pragma comment(lib, "Bcrypt.lib")

namespace LastMusicPlayer::Backend
{
    namespace
    {
        std::vector<std::uint8_t> Utf8(std::wstring const& value)
        {
            if (value.empty())
            {
                return {};
            }
            auto size = ::WideCharToMultiByte(
                CP_UTF8,
                WC_ERR_INVALID_CHARS,
                value.data(),
                static_cast<int>(value.size()),
                nullptr,
                0,
                nullptr,
                nullptr);
            if (size <= 0)
            {
                return {};
            }
            std::vector<std::uint8_t> result(static_cast<std::size_t>(size));
            if (::WideCharToMultiByte(
                CP_UTF8,
                WC_ERR_INVALID_CHARS,
                value.data(),
                static_cast<int>(value.size()),
                reinterpret_cast<char*>(result.data()),
                size,
                nullptr,
                nullptr) != size)
            {
                return {};
            }
            return result;
        }

        std::array<std::uint8_t, 32> Sha256(std::vector<std::uint8_t> const& input)
        {
            BCRYPT_ALG_HANDLE algorithm{};
            winrt::check_nt(::BCryptOpenAlgorithmProvider(
                &algorithm,
                BCRYPT_SHA256_ALGORITHM,
                nullptr,
                0));

            DWORD objectLength{};
            DWORD resultLength{};
            auto status = ::BCryptGetProperty(
                algorithm,
                BCRYPT_OBJECT_LENGTH,
                reinterpret_cast<PUCHAR>(&objectLength),
                sizeof(objectLength),
                &resultLength,
                0);
            if (status < 0)
            {
                ::BCryptCloseAlgorithmProvider(algorithm, 0);
                winrt::check_nt(status);
            }

            std::vector<std::uint8_t> object(objectLength);
            BCRYPT_HASH_HANDLE hash{};
            status = ::BCryptCreateHash(
                algorithm,
                &hash,
                object.data(),
                static_cast<ULONG>(object.size()),
                nullptr,
                0,
                0);
            if (status >= 0)
            {
                status = ::BCryptHashData(
                    hash,
                    const_cast<PUCHAR>(input.data()),
                    static_cast<ULONG>(input.size()),
                    0);
            }

            std::array<std::uint8_t, 32> digest{};
            if (status >= 0)
            {
                status = ::BCryptFinishHash(hash, digest.data(), static_cast<ULONG>(digest.size()), 0);
            }
            if (hash)
            {
                ::BCryptDestroyHash(hash);
            }
            ::BCryptCloseAlgorithmProvider(algorithm, 0);
            winrt::check_nt(status);
            return digest;
        }
    }

    std::wstring CreateHistoryImportEventId(
        std::wstring const& accountId,
        std::wstring const& sourceKey)
    {
        if (accountId.empty() || sourceKey.empty())
        {
            return {};
        }

        auto input = Utf8(accountId + L"\n" + sourceKey);
        if (input.empty())
        {
            return {};
        }

        auto digest = Sha256(input);
        digest[6] = static_cast<std::uint8_t>((digest[6] & 0x0f) | 0x50);
        digest[8] = static_cast<std::uint8_t>((digest[8] & 0x3f) | 0x80);

        wchar_t value[37]{};
        swprintf_s(
            value,
            L"%02x%02x%02x%02x-%02x%02x-%02x%02x-%02x%02x-%02x%02x%02x%02x%02x%02x",
            digest[0], digest[1], digest[2], digest[3],
            digest[4], digest[5], digest[6], digest[7],
            digest[8], digest[9], digest[10], digest[11],
            digest[12], digest[13], digest[14], digest[15]);
        return value;
    }
}
