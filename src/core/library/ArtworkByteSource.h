#pragma once

#include <QByteArrayView>

#include <cstddef>
#include <cstdint>
#include <vector>

class QIODevice;

namespace lmp::core::artwork {

// Random access to the bytes of a media file, either in memory or through a
// random-access device (an open QFile). Audio files run to tens of
// megabytes, so the parsers read only the ranges they need.
//
// Every parser reads length fields from untrusted input. A length is only
// used after Contains() has checked it against the bytes available, so a
// hand-crafted file can make parsing stop early but never over-read.
//
// Not copyable. Holds a view or a device pointer: the memory or the device
// must outlive the source.
class ArtworkByteSource final {
public:
    explicit ArtworkByteSource(QByteArrayView bytes) noexcept;
    // `device` must be open for reading and not sequential.
    explicit ArtworkByteSource(QIODevice& device) noexcept;

    ArtworkByteSource(const ArtworkByteSource&) = delete;
    ArtworkByteSource& operator=(const ArtworkByteSource&) = delete;

    [[nodiscard]] std::uint64_t size() const noexcept { return m_size; }

    // Reads exactly `count` bytes. Fails without touching `buffer` when the
    // range is not entirely inside the source.
    [[nodiscard]] bool read(std::uint64_t offset, std::uint8_t* buffer, std::size_t count) noexcept;

    // Replaces `out` with `count` bytes from `offset`. Fails when `count` is
    // above `maxCount`, outside the source, or cannot be allocated.
    [[nodiscard]] bool readInto(std::uint64_t offset, std::uint64_t count, std::uint64_t maxCount,
                                std::vector<std::uint8_t>& out) noexcept;

    [[nodiscard]] bool contains(std::uint64_t offset, std::uint64_t count) const noexcept;

private:
    [[nodiscard]] bool readCore(std::uint64_t offset, std::uint8_t* buffer, std::size_t count) noexcept;

    QByteArrayView m_bytes;
    QIODevice* m_device = nullptr;
    std::uint64_t m_size = 0;
};

} // namespace lmp::core::artwork
