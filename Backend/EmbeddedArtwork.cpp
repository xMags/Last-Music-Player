#include "pch.h"
#include "Backend/EmbeddedArtwork.h"

#include <array>
#include <cstring>
#include <fstream>
#include <ios>
#include <string>
#include <utility>
#include <vector>

namespace LastMusicPlayer::Backend
{
    namespace
    {
        // Every parser here reads attacker-controlled length fields. The shared
        // rule is that a length is only ever used after it has been checked
        // against the bytes actually available in its enclosing scope, so a
        // hand-crafted file can make us stop early but never over-read or ask
        // for an unbounded allocation.

        // Guards against pathological metadata that would otherwise spin: a real
        // tag holds tens of frames, a real FLAC stream a handful of blocks.
        constexpr int kMaxId3Frames = 4096;
        constexpr int kMaxFlacBlocks = 1024;
        constexpr int kMaxMp4AtomsPerLevel = 4096;
        constexpr int kMaxMp4Depth = 8;

        // An ID3v2 tag large enough to hold the biggest picture we accept, plus
        // room for the text frames sharing the tag.
        constexpr std::uint64_t kMaxId3TagBytes = kMaxEmbeddedArtworkBytes + (1u * 1024u * 1024u);

        // ID3v2 APIC picture types. Front cover wins; anything else is only used
        // when the file carries no front cover at all.
        constexpr std::uint8_t kPictureTypeFrontCover = 0x03;

        // Random access over the metadata regions of a media file. Audio files
        // routinely run to tens of megabytes, so the parsers seek to the tag and
        // read only what they need instead of buffering the whole file.
        class ByteSource
        {
        public:
            virtual ~ByteSource() = default;

            ByteSource(ByteSource const&) = delete;
            ByteSource& operator=(ByteSource const&) = delete;

            [[nodiscard]] virtual std::uint64_t Size() const noexcept = 0;

            // Reads exactly `size` bytes. Fails without touching `buffer` when
            // the requested range is not entirely inside the source.
            [[nodiscard]] bool Read(std::uint64_t offset, std::uint8_t* buffer, std::size_t size) noexcept
            {
                if (!Contains(offset, size))
                {
                    return false;
                }
                return size == 0 || ReadCore(offset, buffer, size);
            }

            [[nodiscard]] bool ReadInto(
                std::uint64_t offset,
                std::uint64_t size,
                std::uint64_t maxSize,
                std::vector<std::uint8_t>& out) noexcept
            {
                if (size > maxSize || !Contains(offset, size))
                {
                    return false;
                }
                try
                {
                    out.resize(static_cast<std::size_t>(size));
                }
                catch (...)
                {
                    return false;
                }
                return Read(offset, out.data(), out.size());
            }

            [[nodiscard]] bool Contains(std::uint64_t offset, std::uint64_t size) const noexcept
            {
                auto total = Size();
                return offset <= total && size <= total - offset;
            }

        protected:
            ByteSource() = default;

            [[nodiscard]] virtual bool ReadCore(std::uint64_t offset, std::uint8_t* buffer, std::size_t size) noexcept = 0;
        };

        class FileByteSource final : public ByteSource
        {
        public:
            explicit FileByteSource(std::filesystem::path const& path)
                : m_stream(path, std::ios::binary)
            {
                if (!m_stream)
                {
                    return;
                }
                m_stream.seekg(0, std::ios::end);
                auto end = m_stream.tellg();
                if (end < 0)
                {
                    m_stream.setstate(std::ios::failbit);
                    return;
                }
                m_size = static_cast<std::uint64_t>(end);
            }

            [[nodiscard]] bool IsOpen() const noexcept { return m_stream.good() || m_stream.eof(); }

            [[nodiscard]] std::uint64_t Size() const noexcept override { return m_size; }

        protected:
            [[nodiscard]] bool ReadCore(std::uint64_t offset, std::uint8_t* buffer, std::size_t size) noexcept override
            {
                try
                {
                    m_stream.clear();
                    m_stream.seekg(static_cast<std::streamoff>(offset), std::ios::beg);
                    if (!m_stream)
                    {
                        return false;
                    }
                    m_stream.read(reinterpret_cast<char*>(buffer), static_cast<std::streamsize>(size));
                    return m_stream.gcount() == static_cast<std::streamsize>(size);
                }
                catch (...)
                {
                    return false;
                }
            }

        private:
            std::ifstream m_stream;
            std::uint64_t m_size{ 0 };
        };

        class MemoryByteSource final : public ByteSource
        {
        public:
            MemoryByteSource(std::uint8_t const* bytes, std::size_t count) noexcept
                : m_bytes(bytes), m_count(bytes ? count : 0)
            {
            }

            [[nodiscard]] std::uint64_t Size() const noexcept override { return m_count; }

        protected:
            [[nodiscard]] bool ReadCore(std::uint64_t offset, std::uint8_t* buffer, std::size_t size) noexcept override
            {
                std::memcpy(buffer, m_bytes + static_cast<std::size_t>(offset), size);
                return true;
            }

        private:
            std::uint8_t const* m_bytes;
            std::size_t m_count;
        };

        std::uint32_t ReadBigEndian32(std::uint8_t const* bytes) noexcept
        {
            return (static_cast<std::uint32_t>(bytes[0]) << 24)
                | (static_cast<std::uint32_t>(bytes[1]) << 16)
                | (static_cast<std::uint32_t>(bytes[2]) << 8)
                | static_cast<std::uint32_t>(bytes[3]);
        }

        std::uint32_t ReadBigEndian24(std::uint8_t const* bytes) noexcept
        {
            return (static_cast<std::uint32_t>(bytes[0]) << 16)
                | (static_cast<std::uint32_t>(bytes[1]) << 8)
                | static_cast<std::uint32_t>(bytes[2]);
        }

        std::uint64_t ReadBigEndian64(std::uint8_t const* bytes) noexcept
        {
            return (static_cast<std::uint64_t>(ReadBigEndian32(bytes)) << 32)
                | ReadBigEndian32(bytes + 4);
        }

        // ID3v2 sizes are "syncsafe": seven significant bits per byte so the
        // encoded size can never contain a false MPEG frame sync.
        bool TryReadSyncSafe32(std::uint8_t const* bytes, std::uint32_t& value) noexcept
        {
            std::uint32_t result = 0;
            for (int index = 0; index < 4; ++index)
            {
                if ((bytes[index] & 0x80u) != 0)
                {
                    return false;
                }
                result = (result << 7) | bytes[index];
            }
            value = result;
            return true;
        }

        bool LooksLikeId3FrameId(std::uint8_t const* bytes, std::size_t length) noexcept
        {
            for (std::size_t index = 0; index < length; ++index)
            {
                auto character = bytes[index];
                auto upper = character >= 'A' && character <= 'Z';
                auto digit = character >= '0' && character <= '9';
                if (!upper && !digit)
                {
                    return false;
                }
            }
            return true;
        }

        // MP4 atom types are four printable characters, and unlike ID3 frame
        // identifiers they are usually lowercase and may start with 0xA9 ("©").
        bool LooksLikeMp4AtomType(std::uint8_t const* bytes) noexcept
        {
            for (int index = 0; index < 4; ++index)
            {
                auto character = bytes[index];
                if (character != 0xA9 && (character < 0x20 || character > 0x7E))
                {
                    return false;
                }
            }
            return true;
        }

        bool StartsWith(
            std::vector<std::uint8_t> const& bytes,
            std::size_t offset,
            char const* signature,
            std::size_t length) noexcept
        {
            if (offset > bytes.size() || bytes.size() - offset < length)
            {
                return false;
            }
            return std::memcmp(bytes.data() + offset, signature, length) == 0;
        }

        // The picture payload is untrusted input on its way to an image decoder
        // and to disk, so it has to look like an image the platform can actually
        // read. This also rejects ID3's "-->" frames, whose payload is a URL.
        std::wstring SniffImageMimeType(std::vector<std::uint8_t> const& bytes)
        {
            if (StartsWith(bytes, 0, "\xFF\xD8\xFF", 3))
            {
                return L"image/jpeg";
            }
            if (StartsWith(bytes, 0, "\x89PNG\r\n\x1A\n", 8))
            {
                return L"image/png";
            }
            if (StartsWith(bytes, 0, "GIF87a", 6) || StartsWith(bytes, 0, "GIF89a", 6))
            {
                return L"image/gif";
            }
            if (StartsWith(bytes, 0, "BM", 2))
            {
                return L"image/bmp";
            }
            if (StartsWith(bytes, 0, "RIFF", 4) && StartsWith(bytes, 8, "WEBP", 4))
            {
                return L"image/webp";
            }
            if (StartsWith(bytes, 0, "II\x2A\x00", 4) || StartsWith(bytes, 0, "MM\x00\x2A", 4))
            {
                return L"image/tiff";
            }
            if (StartsWith(bytes, 0, "\x00\x00\x01\x00", 4))
            {
                return L"image/x-icon";
            }
            return {};
        }

        // A picture candidate plus the container's declared type, so a front
        // cover can displace an earlier "other" picture from the same file.
        struct PictureCandidate
        {
            EmbeddedArtwork Artwork;
            bool IsFrontCover{ false };
            bool Present{ false };
        };

        void OfferPicture(
            PictureCandidate& best,
            std::vector<std::uint8_t> bytes,
            bool isFrontCover)
        {
            if (bytes.empty() || bytes.size() > kMaxEmbeddedArtworkBytes)
            {
                return;
            }
            auto mimeType = SniffImageMimeType(bytes);
            if (mimeType.empty())
            {
                return;
            }
            if (best.Present && (!isFrontCover || best.IsFrontCover))
            {
                return;
            }

            best.Artwork.Bytes = std::move(bytes);
            best.Artwork.MimeType = std::move(mimeType);
            best.IsFrontCover = isFrontCover;
            best.Present = true;
        }

        // FF 00 pairs are inserted so a tag can never contain a byte sequence an
        // MPEG decoder would mistake for a frame sync. Collapse them back.
        void RemoveUnsynchronisation(std::vector<std::uint8_t>& bytes)
        {
            std::size_t write = 0;
            for (std::size_t read = 0; read < bytes.size(); ++read)
            {
                bytes[write++] = bytes[read];
                if (bytes[read] == 0xFF && read + 1 < bytes.size() && bytes[read + 1] == 0x00)
                {
                    ++read;
                }
            }
            bytes.resize(write);
        }

        // Length of the NUL terminator that ends a string in the given ID3 text
        // encoding, or 0 when the encoding is unknown.
        std::size_t Id3TerminatorSize(std::uint8_t encoding) noexcept
        {
            switch (encoding)
            {
            case 0: // ISO-8859-1
            case 3: // UTF-8
                return 1;
            case 1: // UTF-16 with BOM
            case 2: // UTF-16BE
                return 2;
            default:
                return 0;
            }
        }

        // Returns the offset just past the terminator, or `end` when the string
        // is unterminated (which makes the frame unusable).
        std::size_t SkipId3String(
            std::vector<std::uint8_t> const& bytes,
            std::size_t offset,
            std::size_t end,
            std::size_t terminatorSize) noexcept
        {
            if (terminatorSize == 1)
            {
                for (auto cursor = offset; cursor < end; ++cursor)
                {
                    if (bytes[cursor] == 0x00)
                    {
                        return cursor + 1;
                    }
                }
                return end;
            }

            // UTF-16 terminators are two zero bytes on an even boundary relative
            // to the start of the string.
            for (auto cursor = offset; cursor + 1 < end; cursor += 2)
            {
                if (bytes[cursor] == 0x00 && bytes[cursor + 1] == 0x00)
                {
                    return cursor + 2;
                }
            }
            return end;
        }

        // APIC (ID3v2.3/2.4): encoding, NUL-terminated MIME, picture type,
        // NUL-terminated description, picture bytes.
        void ParseId3ApicFrame(
            std::vector<std::uint8_t> const& body,
            std::size_t offset,
            std::size_t end,
            PictureCandidate& best)
        {
            if (offset >= end)
            {
                return;
            }

            auto terminatorSize = Id3TerminatorSize(body[offset]);
            if (terminatorSize == 0)
            {
                return;
            }
            auto cursor = SkipId3String(body, offset + 1, end, 1);
            if (cursor >= end)
            {
                return;
            }

            auto pictureType = body[cursor];
            ++cursor;
            cursor = SkipId3String(body, cursor, end, terminatorSize);
            if (cursor >= end)
            {
                return;
            }

            OfferPicture(
                best,
                std::vector<std::uint8_t>(body.begin() + cursor, body.begin() + end),
                pictureType == kPictureTypeFrontCover);
        }

        // PIC (ID3v2.2): encoding, three-character image format, picture type,
        // NUL-terminated description, picture bytes.
        void ParseId3PicFrame(
            std::vector<std::uint8_t> const& body,
            std::size_t offset,
            std::size_t end,
            PictureCandidate& best)
        {
            if (offset >= end || end - offset < 5)
            {
                return;
            }

            auto terminatorSize = Id3TerminatorSize(body[offset]);
            if (terminatorSize == 0)
            {
                return;
            }

            auto pictureType = body[offset + 4];
            auto cursor = SkipId3String(body, offset + 5, end, terminatorSize);
            if (cursor >= end)
            {
                return;
            }

            OfferPicture(
                best,
                std::vector<std::uint8_t>(body.begin() + cursor, body.begin() + end),
                pictureType == kPictureTypeFrontCover);
        }

        struct Id3TagLayout
        {
            std::uint8_t MajorVersion{ 0 };
            std::uint8_t Flags{ 0 };
            std::uint32_t BodySize{ 0 };
            std::uint64_t TotalSize{ 0 }; // header + body + optional footer
        };

        bool TryReadId3TagLayout(ByteSource& source, std::uint64_t offset, Id3TagLayout& layout) noexcept
        {
            std::array<std::uint8_t, 10> header{};
            if (!source.Read(offset, header.data(), header.size()))
            {
                return false;
            }
            if (header[0] != 'I' || header[1] != 'D' || header[2] != '3')
            {
                return false;
            }
            if (header[3] < 2 || header[3] > 4 || header[4] == 0xFF)
            {
                return false;
            }

            std::uint32_t bodySize = 0;
            if (!TryReadSyncSafe32(header.data() + 6, bodySize))
            {
                return false;
            }

            layout.MajorVersion = header[3];
            layout.Flags = header[5];
            layout.BodySize = bodySize;
            layout.TotalSize = 10u + bodySize + (((header[5] & 0x10u) != 0) ? 10u : 0u);
            return true;
        }

        void ParseId3Tag(ByteSource& source, Id3TagLayout const& layout, PictureCandidate& best)
        {
            std::vector<std::uint8_t> body;
            if (!source.ReadInto(10, layout.BodySize, kMaxId3TagBytes, body))
            {
                return;
            }

            auto tagUnsynchronised = (layout.Flags & 0x80u) != 0;
            // ID3v2.3 and earlier unsynchronise the tag as a whole, so the frame
            // sizes only make sense after the tag has been decoded. ID3v2.4
            // moved unsynchronisation into the individual frames.
            if (tagUnsynchronised && layout.MajorVersion < 4)
            {
                RemoveUnsynchronisation(body);
            }

            std::size_t cursor = 0;
            if ((layout.Flags & 0x40u) != 0)
            {
                if (body.size() < 4)
                {
                    return;
                }
                if (layout.MajorVersion >= 4)
                {
                    // The v2.4 extended header size is syncsafe and counts itself.
                    std::uint32_t extendedSize = 0;
                    if (!TryReadSyncSafe32(body.data(), extendedSize) || extendedSize > body.size())
                    {
                        return;
                    }
                    cursor = extendedSize;
                }
                else
                {
                    // The v2.3 extended header size excludes its own four bytes.
                    auto extendedSize = ReadBigEndian32(body.data());
                    if (extendedSize > body.size() - 4)
                    {
                        return;
                    }
                    cursor = 4u + extendedSize;
                }
            }

            auto const idSize = layout.MajorVersion == 2 ? std::size_t{ 3 } : std::size_t{ 4 };
            auto const headerSize = layout.MajorVersion == 2 ? std::size_t{ 6 } : std::size_t{ 10 };

            for (int frame = 0; frame < kMaxId3Frames; ++frame)
            {
                if (cursor + headerSize > body.size())
                {
                    return;
                }
                // Padding: the remainder of the tag is zeroed out.
                if (body[cursor] == 0x00)
                {
                    return;
                }
                if (!LooksLikeId3FrameId(body.data() + cursor, idSize))
                {
                    return;
                }

                auto const available = body.size() - cursor - headerSize;
                std::uint64_t frameSize = 0;
                std::uint16_t frameFlags = 0;

                if (layout.MajorVersion == 2)
                {
                    frameSize = ReadBigEndian24(body.data() + cursor + 3);
                }
                else if (layout.MajorVersion == 3)
                {
                    frameSize = ReadBigEndian32(body.data() + cursor + 4);
                    frameFlags = static_cast<std::uint16_t>((body[cursor + 8] << 8) | body[cursor + 9]);
                }
                else
                {
                    auto plainSize = static_cast<std::uint64_t>(ReadBigEndian32(body.data() + cursor + 4));
                    std::uint32_t syncSafeSize = 0;
                    auto syncSafe = TryReadSyncSafe32(body.data() + cursor + 4, syncSafeSize);
                    frameSize = syncSafe ? syncSafeSize : plainSize;
                    frameFlags = static_cast<std::uint16_t>((body[cursor + 8] << 8) | body[cursor + 9]);

                    // Several widely used taggers write ID3v2.4 frame sizes as
                    // plain big-endian. Keep the syncsafe reading only when the
                    // frame it describes is followed by padding, another frame,
                    // or the end of the tag.
                    if (syncSafe && syncSafeSize != plainSize && plainSize <= available)
                    {
                        auto next = cursor + headerSize + syncSafeSize;
                        auto plausible = syncSafeSize <= available
                            && (next + idSize > body.size()
                                || body[next] == 0x00
                                || LooksLikeId3FrameId(body.data() + next, idSize));
                        if (!plausible)
                        {
                            frameSize = plainSize;
                        }
                    }
                }

                if (frameSize > available)
                {
                    return;
                }

                auto frameStart = cursor;
                auto contentStart = frameStart + headerSize;
                auto contentEnd = contentStart + static_cast<std::size_t>(frameSize);
                cursor = contentEnd;

                auto isPictureFrame = layout.MajorVersion == 2
                    ? std::memcmp(body.data() + frameStart, "PIC", 3) == 0
                    : std::memcmp(body.data() + frameStart, "APIC", 4) == 0;
                if (!isPictureFrame)
                {
                    continue;
                }

                std::vector<std::uint8_t> content(
                    body.begin() + contentStart,
                    body.begin() + contentEnd);

                if (layout.MajorVersion == 3)
                {
                    // Compressed or encrypted frames need codecs this reader
                    // deliberately does not carry.
                    if ((frameFlags & 0x0080u) != 0 || (frameFlags & 0x0040u) != 0)
                    {
                        continue;
                    }
                    if ((frameFlags & 0x0020u) != 0)
                    {
                        if (content.empty())
                        {
                            continue;
                        }
                        content.erase(content.begin());
                    }
                }
                else if (layout.MajorVersion >= 4)
                {
                    if ((frameFlags & 0x0008u) != 0 || (frameFlags & 0x0004u) != 0)
                    {
                        continue;
                    }
                    if (tagUnsynchronised || (frameFlags & 0x0002u) != 0)
                    {
                        RemoveUnsynchronisation(content);
                    }
                    if ((frameFlags & 0x0001u) != 0)
                    {
                        if (content.size() < 4)
                        {
                            continue;
                        }
                        content.erase(content.begin(), content.begin() + 4);
                    }
                }

                if (layout.MajorVersion == 2)
                {
                    ParseId3PicFrame(content, 0, content.size(), best);
                }
                else
                {
                    ParseId3ApicFrame(content, 0, content.size(), best);
                }

                if (best.Present && best.IsFrontCover)
                {
                    return;
                }
            }
        }

        // FLAC METADATA_BLOCK_PICTURE: picture type, length-prefixed MIME type
        // and description, four fixed geometry fields, then length-prefixed data.
        void ParseFlacPictureBlock(std::vector<std::uint8_t> const& block, PictureCandidate& best)
        {
            constexpr std::size_t kFixedFields = 4 + 4 + 4 + 4 + 4 + 4 + 4;
            if (block.size() < kFixedFields)
            {
                return;
            }

            auto pictureType = ReadBigEndian32(block.data());
            std::size_t cursor = 4;

            auto readLengthPrefixed = [&block, &cursor](std::size_t& length) noexcept
            {
                if (block.size() - cursor < 4)
                {
                    return false;
                }
                auto declared = ReadBigEndian32(block.data() + cursor);
                cursor += 4;
                if (declared > block.size() - cursor)
                {
                    return false;
                }
                length = declared;
                return true;
            };

            std::size_t mimeLength = 0;
            if (!readLengthPrefixed(mimeLength))
            {
                return;
            }
            auto mimeStart = cursor;
            cursor += mimeLength;

            std::size_t descriptionLength = 0;
            if (!readLengthPrefixed(descriptionLength))
            {
                return;
            }
            cursor += descriptionLength;

            // width, height, colour depth, indexed colour count
            if (block.size() - cursor < 16)
            {
                return;
            }
            cursor += 16;

            std::size_t dataLength = 0;
            if (!readLengthPrefixed(dataLength))
            {
                return;
            }

            // "-->" in the MIME field means the payload is a URL, not a picture.
            if (mimeLength == 3 && std::memcmp(block.data() + mimeStart, "-->", 3) == 0)
            {
                return;
            }

            OfferPicture(
                best,
                std::vector<std::uint8_t>(block.begin() + cursor, block.begin() + cursor + dataLength),
                pictureType == kPictureTypeFrontCover);
        }

        void ParseFlacMetadata(ByteSource& source, std::uint64_t offset, PictureCandidate& best)
        {
            auto cursor = offset + 4; // past the "fLaC" marker
            for (int index = 0; index < kMaxFlacBlocks; ++index)
            {
                std::array<std::uint8_t, 4> header{};
                if (!source.Read(cursor, header.data(), header.size()))
                {
                    return;
                }

                auto isLast = (header[0] & 0x80u) != 0;
                auto blockType = static_cast<std::uint8_t>(header[0] & 0x7Fu);
                auto blockSize = ReadBigEndian24(header.data() + 1);
                cursor += 4;

                if (blockType == 6)
                {
                    std::vector<std::uint8_t> block;
                    if (!source.ReadInto(cursor, blockSize, kMaxId3TagBytes, block))
                    {
                        return;
                    }
                    ParseFlacPictureBlock(block, best);
                    if (best.Present && best.IsFrontCover)
                    {
                        return;
                    }
                }

                if (isLast || !source.Contains(cursor, blockSize))
                {
                    return;
                }
                cursor += blockSize;
            }
        }

        // MP4 `data` atom under `covr`: a four-byte type indicator whose low byte
        // names the image format, four reserved bytes, then the picture.
        void ParseMp4CoverData(ByteSource& source, std::uint64_t offset, std::uint64_t size, PictureCandidate& best)
        {
            if (size <= 8)
            {
                return;
            }
            std::vector<std::uint8_t> payload;
            if (!source.ReadInto(offset + 8, size - 8, kMaxEmbeddedArtworkBytes, payload))
            {
                return;
            }
            // MP4 carries no picture-type notion, so a cover atom is always the
            // front cover as far as selection is concerned.
            OfferPicture(best, std::move(payload), true);
        }

        void ParseMp4Atoms(
            ByteSource& source,
            std::uint64_t offset,
            std::uint64_t end,
            int depth,
            PictureCandidate& best);

        // `meta` is a full box: four version/flags bytes precede its children.
        // Some writers omit them, so fall back when the first child looks wrong.
        std::uint64_t Mp4MetaChildOffset(ByteSource& source, std::uint64_t offset, std::uint64_t end) noexcept
        {
            std::array<std::uint8_t, 8> header{};
            if (offset + 4 <= end && source.Read(offset + 4, header.data(), header.size()))
            {
                auto atomSize = ReadBigEndian32(header.data());
                if (atomSize >= 8 && LooksLikeMp4AtomType(header.data() + 4))
                {
                    return offset + 4;
                }
            }
            return offset;
        }

        void ParseMp4Atoms(
            ByteSource& source,
            std::uint64_t offset,
            std::uint64_t end,
            int depth,
            PictureCandidate& best)
        {
            if (depth > kMaxMp4Depth)
            {
                return;
            }

            auto cursor = offset;
            for (int index = 0; index < kMaxMp4AtomsPerLevel && cursor + 8 <= end; ++index)
            {
                std::array<std::uint8_t, 16> header{};
                if (!source.Read(cursor, header.data(), 8))
                {
                    return;
                }

                std::uint64_t atomSize = ReadBigEndian32(header.data());
                std::uint64_t headerSize = 8;
                if (atomSize == 1)
                {
                    if (!source.Read(cursor, header.data(), header.size()))
                    {
                        return;
                    }
                    atomSize = ReadBigEndian64(header.data() + 8);
                    headerSize = 16;
                }
                else if (atomSize == 0)
                {
                    atomSize = end - cursor; // extends to the end of its parent
                }

                if (atomSize < headerSize || atomSize > end - cursor)
                {
                    return;
                }

                auto const* type = header.data() + 4;
                auto childStart = cursor + headerSize;
                auto childEnd = cursor + atomSize;

                if (std::memcmp(type, "moov", 4) == 0
                    || std::memcmp(type, "udta", 4) == 0
                    || std::memcmp(type, "ilst", 4) == 0
                    || std::memcmp(type, "covr", 4) == 0)
                {
                    ParseMp4Atoms(source, childStart, childEnd, depth + 1, best);
                }
                else if (std::memcmp(type, "meta", 4) == 0)
                {
                    ParseMp4Atoms(source, Mp4MetaChildOffset(source, childStart, childEnd), childEnd, depth + 1, best);
                }
                else if (std::memcmp(type, "data", 4) == 0)
                {
                    ParseMp4CoverData(source, childStart, atomSize - headerSize, best);
                }

                if (best.Present && best.IsFrontCover)
                {
                    return;
                }
                cursor = childEnd;
            }
        }

        std::optional<EmbeddedArtwork> ReadFromSource(ByteSource& source)
        {
            PictureCandidate best;

            std::uint64_t audioStart = 0;
            Id3TagLayout layout{};
            if (TryReadId3TagLayout(source, 0, layout))
            {
                ParseId3Tag(source, layout, best);
                if (best.Present && best.IsFrontCover)
                {
                    return std::move(best.Artwork);
                }
                audioStart = layout.TotalSize;
            }

            // A FLAC stream can sit behind an ID3v2 tag even though the format
            // does not call for one, so probe both the file start and the byte
            // just past any tag we found.
            std::array<std::uint64_t, 2> probes{ 0, audioStart };
            auto const probeCount = audioStart == 0 ? std::size_t{ 1 } : probes.size();

            std::array<std::uint8_t, 12> signature{};
            for (std::size_t index = 0; index < probeCount; ++index)
            {
                if (!source.Read(probes[index], signature.data(), signature.size()))
                {
                    continue;
                }
                if (std::memcmp(signature.data(), "fLaC", 4) == 0)
                {
                    ParseFlacMetadata(source, probes[index], best);
                }
                else if (std::memcmp(signature.data() + 4, "ftyp", 4) == 0)
                {
                    ParseMp4Atoms(source, probes[index], source.Size(), 0, best);
                }
                if (best.Present && best.IsFrontCover)
                {
                    break;
                }
            }

            if (!best.Present)
            {
                return std::nullopt;
            }
            return std::move(best.Artwork);
        }
    }

    std::optional<EmbeddedArtwork> TryReadEmbeddedArtwork(std::filesystem::path const& path) noexcept
    {
        try
        {
            FileByteSource source{ path };
            if (!source.IsOpen() || source.Size() == 0)
            {
                return std::nullopt;
            }
            return ReadFromSource(source);
        }
        catch (...)
        {
            return std::nullopt;
        }
    }

    std::optional<EmbeddedArtwork> TryReadEmbeddedArtwork(
        std::uint8_t const* fileBytes,
        std::size_t byteCount) noexcept
    {
        try
        {
            if (!fileBytes || byteCount == 0)
            {
                return std::nullopt;
            }
            MemoryByteSource source{ fileBytes, byteCount };
            return ReadFromSource(source);
        }
        catch (...)
        {
            return std::nullopt;
        }
    }

    std::wstring ArtworkFileExtension(std::wstring const& mimeType)
    {
        if (mimeType == L"image/png")
        {
            return L".png";
        }
        if (mimeType == L"image/gif")
        {
            return L".gif";
        }
        if (mimeType == L"image/bmp")
        {
            return L".bmp";
        }
        if (mimeType == L"image/webp")
        {
            return L".webp";
        }
        if (mimeType == L"image/tiff")
        {
            return L".tif";
        }
        if (mimeType == L"image/x-icon")
        {
            return L".ico";
        }
        return L".jpg";
    }
}
