#include "core/library/ArtworkByteSource.h"

#include <QIODevice>

#include <cstring>
#include <new>
#include <stdexcept>

namespace lmp::core::artwork {

ArtworkByteSource::ArtworkByteSource(QByteArrayView bytes) noexcept
    : m_bytes(bytes)
    , m_size(static_cast<std::uint64_t>(bytes.size()))
{
}

ArtworkByteSource::ArtworkByteSource(QIODevice& device) noexcept
    : m_device(&device)
{
    const qint64 deviceSize = device.size();
    m_size = deviceSize > 0 ? static_cast<std::uint64_t>(deviceSize) : 0;
}

bool ArtworkByteSource::contains(std::uint64_t offset, std::uint64_t count) const noexcept
{
    return offset <= m_size && count <= m_size - offset;
}

bool ArtworkByteSource::read(std::uint64_t offset, std::uint8_t* buffer, std::size_t count) noexcept
{
    if (!contains(offset, count)) {
        return false;
    }
    return count == 0 || readCore(offset, buffer, count);
}

bool ArtworkByteSource::readInto(std::uint64_t offset, std::uint64_t count, std::uint64_t maxCount,
                                 std::vector<std::uint8_t>& out) noexcept
{
    if (count > maxCount || !contains(offset, count)) {
        return false;
    }
    try {
        out.resize(static_cast<std::size_t>(count));
    } catch (const std::bad_alloc&) {
        return false;
    } catch (const std::length_error&) {
        return false;
    }
    return read(offset, out.data(), out.size());
}

bool ArtworkByteSource::readCore(std::uint64_t offset, std::uint8_t* buffer, std::size_t count) noexcept
{
    // contains() has bounded offset and count by m_size, which came from a
    // qsizetype or qint64, so both convert back without loss.
    if (m_device == nullptr) {
        std::memcpy(buffer, m_bytes.data() + static_cast<qsizetype>(offset), count);
        return true;
    }
    if (!m_device->seek(static_cast<qint64>(offset))) {
        return false;
    }
    // QIODevice reads into char; the buffer is plain bytes of the same size.
    const qint64 wanted = static_cast<qint64>(count);
    return m_device->read(reinterpret_cast<char*>(buffer), wanted) == wanted;
}

} // namespace lmp::core::artwork
