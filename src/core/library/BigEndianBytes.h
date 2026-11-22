#pragma once

#include <cstdint>

namespace lmp::core::artwork {

// Big-endian integer reads for the media container parsers. The caller has
// already checked that enough bytes are available.

[[nodiscard]] inline std::uint32_t readBigEndian32(const std::uint8_t* bytes) noexcept
{
    return (static_cast<std::uint32_t>(bytes[0]) << 24) | (static_cast<std::uint32_t>(bytes[1]) << 16)
        | (static_cast<std::uint32_t>(bytes[2]) << 8) | static_cast<std::uint32_t>(bytes[3]);
}

[[nodiscard]] inline std::uint32_t readBigEndian24(const std::uint8_t* bytes) noexcept
{
    return (static_cast<std::uint32_t>(bytes[0]) << 16) | (static_cast<std::uint32_t>(bytes[1]) << 8)
        | static_cast<std::uint32_t>(bytes[2]);
}

[[nodiscard]] inline std::uint64_t readBigEndian64(const std::uint8_t* bytes) noexcept
{
    return (static_cast<std::uint64_t>(readBigEndian32(bytes)) << 32) | readBigEndian32(bytes + 4);
}

} // namespace lmp::core::artwork
