#include "pch.h"
#include "Backend/Pkce.h"

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
        winrt::hstring Base64Url(std::uint8_t const* bytes, std::size_t size)
        {
            static constexpr wchar_t alphabet[] =
                L"ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_";
            std::wstring encoded;
            encoded.reserve((size * 4 + 2) / 3);

            std::size_t index{};
            while (index + 3 <= size)
            {
                auto value = (static_cast<std::uint32_t>(bytes[index]) << 16)
                    | (static_cast<std::uint32_t>(bytes[index + 1]) << 8)
                    | static_cast<std::uint32_t>(bytes[index + 2]);
                encoded.push_back(alphabet[(value >> 18) & 0x3f]);
                encoded.push_back(alphabet[(value >> 12) & 0x3f]);
                encoded.push_back(alphabet[(value >> 6) & 0x3f]);
                encoded.push_back(alphabet[value & 0x3f]);
                index += 3;
            }

            auto remaining = size - index;
            if (remaining == 1)
            {
                auto value = static_cast<std::uint32_t>(bytes[index]) << 16;
                encoded.push_back(alphabet[(value >> 18) & 0x3f]);
                encoded.push_back(alphabet[(value >> 12) & 0x3f]);
            }
            else if (remaining == 2)
            {
                auto value = (static_cast<std::uint32_t>(bytes[index]) << 16)
                    | (static_cast<std::uint32_t>(bytes[index + 1]) << 8);
                encoded.push_back(alphabet[(value >> 18) & 0x3f]);
                encoded.push_back(alphabet[(value >> 12) & 0x3f]);
                encoded.push_back(alphabet[(value >> 6) & 0x3f]);
            }
            return winrt::hstring{ encoded };
        }

        std::vector<std::uint8_t> AsciiBytes(winrt::hstring const& value)
        {
            std::vector<std::uint8_t> bytes;
            bytes.reserve(value.size());
            for (auto character : value)
            {
                if (character > 0x7f)
                {
                    throw winrt::hresult_invalid_argument(L"PKCE verifier must be ASCII.");
                }
                bytes.push_back(static_cast<std::uint8_t>(character));
            }
            return bytes;
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

        template<std::size_t Size>
        std::array<std::uint8_t, Size> RandomBytes()
        {
            std::array<std::uint8_t, Size> bytes{};
            winrt::check_nt(::BCryptGenRandom(
                nullptr,
                bytes.data(),
                static_cast<ULONG>(bytes.size()),
                BCRYPT_USE_SYSTEM_PREFERRED_RNG));
            return bytes;
        }
    }

    winrt::hstring CreatePkceChallenge(winrt::hstring const& verifier)
    {
        if (verifier.size() < 43 || verifier.size() > 128)
        {
            throw winrt::hresult_invalid_argument(L"PKCE verifier length is invalid.");
        }
        auto digest = Sha256(AsciiBytes(verifier));
        return Base64Url(digest.data(), digest.size());
    }

    PkceTransaction CreatePkceTransaction()
    {
        auto verifierBytes = RandomBytes<32>();
        auto stateBytes = RandomBytes<24>();

        PkceTransaction transaction;
        transaction.Verifier = Base64Url(verifierBytes.data(), verifierBytes.size());
        transaction.Challenge = CreatePkceChallenge(transaction.Verifier);
        transaction.State = Base64Url(stateBytes.data(), stateBytes.size());
        return transaction;
    }
}
