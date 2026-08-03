#include "pch.h"

#include "Backend/EmbeddedArtwork.h"

#include <cstdint>
#include <exception>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace artwork = LastMusicPlayer::Backend;

namespace
{
    void Expect(bool condition, char const* message)
    {
        if (!condition)
        {
            throw std::runtime_error(message);
        }
    }

    std::optional<artwork::EmbeddedArtwork> Read(std::vector<std::uint8_t> const& file)
    {
        return artwork::TryReadEmbeddedArtwork(file.data(), file.size());
    }

    void Append(std::vector<std::uint8_t>& target, std::vector<std::uint8_t> const& source)
    {
        target.insert(target.end(), source.begin(), source.end());
    }

    void AppendAscii(std::vector<std::uint8_t>& target, char const* text)
    {
        for (auto cursor = text; *cursor != '\0'; ++cursor)
        {
            target.push_back(static_cast<std::uint8_t>(*cursor));
        }
    }

    void AppendBigEndian32(std::vector<std::uint8_t>& target, std::uint32_t value)
    {
        target.push_back(static_cast<std::uint8_t>((value >> 24) & 0xFFu));
        target.push_back(static_cast<std::uint8_t>((value >> 16) & 0xFFu));
        target.push_back(static_cast<std::uint8_t>((value >> 8) & 0xFFu));
        target.push_back(static_cast<std::uint8_t>(value & 0xFFu));
    }

    void AppendBigEndian24(std::vector<std::uint8_t>& target, std::uint32_t value)
    {
        target.push_back(static_cast<std::uint8_t>((value >> 16) & 0xFFu));
        target.push_back(static_cast<std::uint8_t>((value >> 8) & 0xFFu));
        target.push_back(static_cast<std::uint8_t>(value & 0xFFu));
    }

    void AppendSyncSafe32(std::vector<std::uint8_t>& target, std::uint32_t value)
    {
        target.push_back(static_cast<std::uint8_t>((value >> 21) & 0x7Fu));
        target.push_back(static_cast<std::uint8_t>((value >> 14) & 0x7Fu));
        target.push_back(static_cast<std::uint8_t>((value >> 7) & 0x7Fu));
        target.push_back(static_cast<std::uint8_t>(value & 0x7Fu));
    }

    // A minimal but signature-correct JPEG. `marker` distinguishes payloads so a
    // test can prove which of several pictures in a file was selected.
    std::vector<std::uint8_t> MakeJpeg(std::uint8_t marker, std::size_t size = 64)
    {
        std::vector<std::uint8_t> bytes{ 0xFF, 0xD8, 0xFF, 0xE0 };
        bytes.resize(size, marker);
        return bytes;
    }

    std::vector<std::uint8_t> MakePng()
    {
        std::vector<std::uint8_t> bytes{ 0x89, 'P', 'N', 'G', '\r', '\n', 0x1A, '\n' };
        bytes.resize(48, 0x11);
        return bytes;
    }

    // APIC/PIC payload: encoding, format, picture type, description, picture.
    std::vector<std::uint8_t> MakeApicContent(
        char const* mimeType,
        std::uint8_t pictureType,
        std::vector<std::uint8_t> const& picture,
        std::uint8_t encoding = 0,
        char const* description = "cover")
    {
        std::vector<std::uint8_t> content{ encoding };
        AppendAscii(content, mimeType);
        content.push_back(0x00);
        content.push_back(pictureType);
        AppendAscii(content, description);
        content.push_back(0x00);
        if (encoding == 1 || encoding == 2)
        {
            content.push_back(0x00);
        }
        Append(content, picture);
        return content;
    }

    std::vector<std::uint8_t> MakePicContent(
        char const* format,
        std::uint8_t pictureType,
        std::vector<std::uint8_t> const& picture)
    {
        std::vector<std::uint8_t> content{ 0x00 };
        AppendAscii(content, format);
        content.push_back(pictureType);
        AppendAscii(content, "cover");
        content.push_back(0x00);
        Append(content, picture);
        return content;
    }

    enum class FrameSizeEncoding
    {
        SyncSafe,
        BigEndian
    };

    std::vector<std::uint8_t> MakeFrame(
        char const* id,
        std::vector<std::uint8_t> const& content,
        std::uint8_t majorVersion,
        FrameSizeEncoding sizeEncoding = FrameSizeEncoding::SyncSafe,
        std::uint16_t frameFlags = 0)
    {
        std::vector<std::uint8_t> frame;
        AppendAscii(frame, id);
        if (majorVersion == 2)
        {
            AppendBigEndian24(frame, static_cast<std::uint32_t>(content.size()));
        }
        else
        {
            if (majorVersion >= 4 && sizeEncoding == FrameSizeEncoding::SyncSafe)
            {
                AppendSyncSafe32(frame, static_cast<std::uint32_t>(content.size()));
            }
            else
            {
                AppendBigEndian32(frame, static_cast<std::uint32_t>(content.size()));
            }
            frame.push_back(static_cast<std::uint8_t>((frameFlags >> 8) & 0xFFu));
            frame.push_back(static_cast<std::uint8_t>(frameFlags & 0xFFu));
        }
        Append(frame, content);
        return frame;
    }

    std::vector<std::uint8_t> MakeId3File(
        std::uint8_t majorVersion,
        std::vector<std::uint8_t> const& frames,
        std::uint8_t tagFlags = 0,
        std::vector<std::uint8_t> const& trailer = {})
    {
        std::vector<std::uint8_t> file;
        AppendAscii(file, "ID3");
        file.push_back(majorVersion);
        file.push_back(0x00);
        file.push_back(tagFlags);
        AppendSyncSafe32(file, static_cast<std::uint32_t>(frames.size()));
        Append(file, frames);
        Append(file, trailer);
        return file;
    }

    std::vector<std::uint8_t> MakeFlacPictureBlock(
        std::uint32_t pictureType,
        char const* mimeType,
        std::vector<std::uint8_t> const& picture)
    {
        std::vector<std::uint8_t> block;
        AppendBigEndian32(block, pictureType);
        AppendBigEndian32(block, static_cast<std::uint32_t>(std::string{ mimeType }.size()));
        AppendAscii(block, mimeType);
        AppendBigEndian32(block, 0); // empty description
        AppendBigEndian32(block, 500);
        AppendBigEndian32(block, 500);
        AppendBigEndian32(block, 24);
        AppendBigEndian32(block, 0);
        AppendBigEndian32(block, static_cast<std::uint32_t>(picture.size()));
        Append(block, picture);
        return block;
    }

    void AppendFlacBlock(
        std::vector<std::uint8_t>& file,
        std::uint8_t blockType,
        bool isLast,
        std::vector<std::uint8_t> const& block)
    {
        file.push_back(static_cast<std::uint8_t>(blockType | (isLast ? 0x80u : 0x00u)));
        AppendBigEndian24(file, static_cast<std::uint32_t>(block.size()));
        Append(file, block);
    }

    std::vector<std::uint8_t> MakeAtom(char const* type, std::vector<std::uint8_t> const& payload)
    {
        std::vector<std::uint8_t> atom;
        AppendBigEndian32(atom, static_cast<std::uint32_t>(payload.size() + 8));
        AppendAscii(atom, type);
        Append(atom, payload);
        return atom;
    }

    std::vector<std::uint8_t> MakeMp4File(
        std::vector<std::uint8_t> const& picture,
        std::uint32_t dataTypeIndicator,
        bool metaCarriesVersionFlags)
    {
        std::vector<std::uint8_t> dataPayload;
        AppendBigEndian32(dataPayload, dataTypeIndicator);
        AppendBigEndian32(dataPayload, 0); // locale
        Append(dataPayload, picture);

        auto covr = MakeAtom("covr", MakeAtom("data", dataPayload));
        auto ilst = MakeAtom("ilst", covr);

        std::vector<std::uint8_t> metaPayload;
        if (metaCarriesVersionFlags)
        {
            AppendBigEndian32(metaPayload, 0);
        }
        Append(metaPayload, ilst);

        auto moov = MakeAtom("moov", MakeAtom("udta", MakeAtom("meta", metaPayload)));

        std::vector<std::uint8_t> ftypPayload;
        AppendAscii(ftypPayload, "M4A ");
        AppendBigEndian32(ftypPayload, 0);

        std::vector<std::uint8_t> file = MakeAtom("ftyp", ftypPayload);
        Append(file, moov);
        return file;
    }

    void TestId3v23Apic()
    {
        auto picture = MakeJpeg(0xAA);
        auto frames = MakeFrame("APIC", MakeApicContent("image/jpeg", 0x03, picture), 3);
        auto result = Read(MakeId3File(3, frames));

        Expect(result.has_value(), "ID3v2.3 APIC front cover was not found");
        Expect(result->Bytes == picture, "ID3v2.3 APIC picture bytes were altered");
        Expect(result->MimeType == L"image/jpeg", "ID3v2.3 APIC media type was not detected");
    }

    void TestId3v24SyncSafeAndTrailingAudio()
    {
        auto picture = MakeJpeg(0xBB, 300);
        auto frames = MakeFrame("APIC", MakeApicContent("image/jpeg", 0x03, picture), 4);
        // Real files continue into audio frames after the tag; the parser must
        // stop at the declared tag size rather than reading past it.
        std::vector<std::uint8_t> audio(2048, 0xFF);
        auto result = Read(MakeId3File(4, frames, 0, audio));

        Expect(result.has_value(), "ID3v2.4 APIC front cover was not found");
        Expect(result->Bytes == picture, "ID3v2.4 APIC picture bytes were altered");
    }

    void TestId3v24PlainBigEndianFrameSize()
    {
        // Several widely used taggers write v2.4 frame sizes as plain
        // big-endian. A size above 0x7F then decodes far too small as syncsafe.
        auto picture = MakeJpeg(0xCC, 400);
        auto content = MakeApicContent("image/jpeg", 0x03, picture);
        Expect(content.size() > 0x7F, "test frame is too small to exercise the syncsafe fallback");

        auto frames = MakeFrame("APIC", content, 4, FrameSizeEncoding::BigEndian);
        auto result = Read(MakeId3File(4, frames));

        Expect(result.has_value(), "ID3v2.4 frame with a plain big-endian size was not recovered");
        Expect(result->Bytes == picture, "ID3v2.4 big-endian sized frame produced the wrong bytes");
    }

    // Inserting 0x00 after every 0xFF is a superset of the ID3 rule and decodes
    // identically, which keeps the fixtures independent of the encoder's choice
    // of which 0xFF bytes actually needed escaping.
    std::vector<std::uint8_t> Unsynchronise(std::vector<std::uint8_t> const& bytes)
    {
        std::vector<std::uint8_t> encoded;
        for (auto byte : bytes)
        {
            encoded.push_back(byte);
            if (byte == 0xFF)
            {
                encoded.push_back(0x00);
            }
        }
        return encoded;
    }

    void TestId3v23Unsynchronisation()
    {
        // A JPEG whose payload contains 0xFF bytes, which the tagger has to
        // escape so no byte pair inside the tag can pass for an MPEG frame sync.
        std::vector<std::uint8_t> picture{ 0xFF, 0xD8, 0xFF, 0xE0, 0x00, 0x10, 0xFF, 0xFE, 0x01, 0x02 };
        auto frames = MakeFrame("APIC", MakeApicContent("image/jpeg", 0x03, picture), 3);

        // Through ID3v2.3 a tagger renders the frames first and then
        // unsynchronises the finished block, so the frame sizes still describe
        // the decoded bytes while only the tag header counts encoded bytes.
        auto result = Read(MakeId3File(3, Unsynchronise(frames), 0x80));

        Expect(result.has_value(), "unsynchronised ID3v2.3 APIC was not found");
        Expect(result->Bytes == picture, "unsynchronisation was not reversed");
    }

    void TestId3v24FrameLevelUnsynchronisation()
    {
        std::vector<std::uint8_t> picture{ 0xFF, 0xD8, 0xFF, 0xE0, 0x22, 0xFF, 0x33 };

        // ID3v2.4 moved unsynchronisation into the frame, and there the size
        // field counts the encoded bytes.
        auto encoded = Unsynchronise(MakeApicContent("image/jpeg", 0x03, picture));
        auto frames = MakeFrame("APIC", encoded, 4, FrameSizeEncoding::SyncSafe, 0x0002);
        auto result = Read(MakeId3File(4, frames));

        Expect(result.has_value(), "frame-level unsynchronised ID3v2.4 APIC was not found");
        Expect(result->Bytes == picture, "frame-level unsynchronisation was not reversed");
    }

    void TestId3v24DataLengthIndicator()
    {
        auto picture = MakeJpeg(0xDD);
        auto content = MakeApicContent("image/jpeg", 0x03, picture);

        std::vector<std::uint8_t> withIndicator;
        AppendSyncSafe32(withIndicator, static_cast<std::uint32_t>(content.size()));
        Append(withIndicator, content);

        auto frames = MakeFrame("APIC", withIndicator, 4, FrameSizeEncoding::SyncSafe, 0x0001);
        auto result = Read(MakeId3File(4, frames));

        Expect(result.has_value(), "ID3v2.4 frame with a data length indicator was not parsed");
        Expect(result->Bytes == picture, "the data length indicator was not skipped");
    }

    void TestId3v22Pic()
    {
        auto picture = MakeJpeg(0xEE);
        auto frames = MakeFrame("PIC", MakePicContent("JPG", 0x03, picture), 2);
        auto result = Read(MakeId3File(2, frames));

        Expect(result.has_value(), "ID3v2.2 PIC frame was not found");
        Expect(result->Bytes == picture, "ID3v2.2 PIC picture bytes were altered");
    }

    void TestFrontCoverWinsOverEarlierPicture()
    {
        auto back = MakeJpeg(0x11);
        auto front = MakePng();

        std::vector<std::uint8_t> frames;
        Append(frames, MakeFrame("APIC", MakeApicContent("image/jpeg", 0x04, back), 3));
        Append(frames, MakeFrame("APIC", MakeApicContent("image/png", 0x03, front), 3));

        auto result = Read(MakeId3File(3, frames));

        Expect(result.has_value(), "no picture was selected from a multi-picture tag");
        Expect(result->Bytes == front, "a back cover was preferred over the front cover");
        Expect(result->MimeType == L"image/png", "the selected picture reported the wrong media type");
    }

    void TestNonFrontCoverIsStillUsable()
    {
        auto other = MakeJpeg(0x77);
        auto frames = MakeFrame("APIC", MakeApicContent("image/jpeg", 0x00, other), 3);
        auto result = Read(MakeId3File(3, frames));

        Expect(result.has_value(), "a tag whose only picture is not a front cover produced nothing");
        Expect(result->Bytes == other, "the fallback picture bytes were altered");
    }

    void TestUnicodeDescriptionIsSkipped()
    {
        auto picture = MakeJpeg(0x55);
        // UTF-16 descriptions terminate with two zero bytes on an even boundary.
        std::vector<std::uint8_t> content{ 0x01 };
        AppendAscii(content, "image/jpeg");
        content.push_back(0x00);
        content.push_back(0x03);
        content.push_back(0xFF);
        content.push_back(0xFE); // BOM
        content.push_back('c');
        content.push_back(0x00);
        content.push_back(0x00);
        content.push_back(0x00); // terminator
        Append(content, picture);

        auto result = Read(MakeId3File(3, MakeFrame("APIC", content, 3)));

        Expect(result.has_value(), "APIC with a UTF-16 description was not parsed");
        Expect(result->Bytes == picture, "the UTF-16 description was not skipped correctly");
    }

    void TestUrlPayloadIsRejected()
    {
        std::vector<std::uint8_t> url;
        AppendAscii(url, "http://example.test/cover.jpg");
        auto frames = MakeFrame("APIC", MakeApicContent("-->", 0x03, url), 3);

        Expect(!Read(MakeId3File(3, frames)).has_value(), "a URL picture reference was accepted as image bytes");
    }

    void TestNonImagePayloadIsRejected()
    {
        std::vector<std::uint8_t> payload(64, 0x42);
        auto frames = MakeFrame("APIC", MakeApicContent("image/jpeg", 0x03, payload), 3);

        Expect(!Read(MakeId3File(3, frames)).has_value(), "a payload with no image signature was accepted");
    }

    void TestOversizedAndTruncatedInputsAreRejected()
    {
        auto picture = MakeJpeg(0x99);
        auto content = MakeApicContent("image/jpeg", 0x03, picture);

        // A frame that claims far more bytes than the tag actually holds.
        std::vector<std::uint8_t> lyingFrame;
        AppendAscii(lyingFrame, "APIC");
        AppendBigEndian32(lyingFrame, 0x00FFFFFFu);
        lyingFrame.push_back(0x00);
        lyingFrame.push_back(0x00);
        Append(lyingFrame, content);
        Expect(!Read(MakeId3File(3, lyingFrame)).has_value(), "a frame claiming more bytes than the tag holds was accepted");

        // A tag header that claims more body than the file contains.
        auto truncated = MakeId3File(3, MakeFrame("APIC", content, 3));
        truncated.resize(truncated.size() / 2);
        Expect(!Read(truncated).has_value(), "a truncated tag was accepted");

        // A tag whose declared size overruns the file entirely.
        std::vector<std::uint8_t> overrun;
        AppendAscii(overrun, "ID3");
        overrun.push_back(0x03);
        overrun.push_back(0x00);
        overrun.push_back(0x00);
        AppendSyncSafe32(overrun, 0x0FFFFFFFu);
        Expect(!Read(overrun).has_value(), "a tag whose size overruns the file was accepted");

        Expect(!artwork::TryReadEmbeddedArtwork(nullptr, 0).has_value(), "a null buffer was accepted");
        Expect(!Read({}).has_value(), "an empty buffer was accepted");
        Expect(!Read({ 'I', 'D', '3' }).has_value(), "a three byte file was accepted");
    }

    void TestFlacPictureBlock()
    {
        auto picture = MakeJpeg(0x31, 200);

        std::vector<std::uint8_t> file;
        AppendAscii(file, "fLaC");
        AppendFlacBlock(file, 0, false, std::vector<std::uint8_t>(34, 0x00)); // STREAMINFO
        AppendFlacBlock(file, 4, false, std::vector<std::uint8_t>(16, 0x00)); // VORBIS_COMMENT
        AppendFlacBlock(file, 6, true, MakeFlacPictureBlock(3, "image/jpeg", picture));

        auto result = Read(file);

        Expect(result.has_value(), "FLAC PICTURE block was not found");
        Expect(result->Bytes == picture, "FLAC picture bytes were altered");
        Expect(result->MimeType == L"image/jpeg", "FLAC picture media type was not detected");
    }

    void TestFlacBehindId3Tag()
    {
        auto picture = MakeJpeg(0x32, 120);

        std::vector<std::uint8_t> stream;
        AppendAscii(stream, "fLaC");
        AppendFlacBlock(stream, 0, false, std::vector<std::uint8_t>(34, 0x00));
        AppendFlacBlock(stream, 6, true, MakeFlacPictureBlock(3, "image/jpeg", picture));

        // An ID3v2 tag carrying no picture, followed by the real FLAC stream.
        auto textFrame = MakeFrame("TIT2", std::vector<std::uint8_t>{ 0x00, 'S', 'o', 'n', 'g' }, 3);
        auto result = Read(MakeId3File(3, textFrame, 0, stream));

        Expect(result.has_value(), "FLAC picture behind an ID3v2 tag was not found");
        Expect(result->Bytes == picture, "FLAC picture behind an ID3v2 tag produced the wrong bytes");
    }

    void TestFlacUrlPictureIsRejected()
    {
        std::vector<std::uint8_t> url;
        AppendAscii(url, "http://example.test/cover.jpg");

        std::vector<std::uint8_t> file;
        AppendAscii(file, "fLaC");
        AppendFlacBlock(file, 6, true, MakeFlacPictureBlock(3, "-->", url));

        Expect(!Read(file).has_value(), "a FLAC URL picture reference was accepted as image bytes");
    }

    void TestMp4CoverAtom()
    {
        auto picture = MakeJpeg(0x41, 256);
        auto result = Read(MakeMp4File(picture, 13, true));

        Expect(result.has_value(), "MP4 covr atom was not found");
        Expect(result->Bytes == picture, "MP4 cover bytes were altered");
        Expect(result->MimeType == L"image/jpeg", "MP4 cover media type was not detected");
    }

    void TestMp4MetaWithoutVersionFlags()
    {
        auto picture = MakePng();
        auto result = Read(MakeMp4File(picture, 14, false));

        Expect(result.has_value(), "MP4 covr atom under a meta box with no version flags was not found");
        Expect(result->Bytes == picture, "MP4 cover bytes were altered when meta carried no version flags");
        Expect(result->MimeType == L"image/png", "MP4 PNG cover media type was not detected");
    }

    void TestMp4LyingAtomSizeIsRejected()
    {
        auto file = MakeMp4File(MakeJpeg(0x42, 128), 13, true);
        // Overstate the moov atom size so it runs past the end of the file.
        auto moovStart = MakeAtom("ftyp", std::vector<std::uint8_t>(8, 0x00)).size();
        Expect(file.size() > moovStart + 4, "MP4 fixture is smaller than expected");
        file[moovStart] = 0x7F;
        file[moovStart + 1] = 0xFF;

        Expect(!Read(file).has_value(), "an MP4 atom size running past the file was accepted");
    }

    void TestArtworkFileExtension()
    {
        Expect(artwork::ArtworkFileExtension(L"image/png") == L".png", "PNG extension mapping is wrong");
        Expect(artwork::ArtworkFileExtension(L"image/webp") == L".webp", "WebP extension mapping is wrong");
        Expect(artwork::ArtworkFileExtension(L"image/jpeg") == L".jpg", "JPEG extension mapping is wrong");
        Expect(artwork::ArtworkFileExtension(L"") == L".jpg", "unknown media types should fall back to .jpg");
    }
}

int wmain()
{
    try
    {
        TestId3v23Apic();
        TestId3v24SyncSafeAndTrailingAudio();
        TestId3v24PlainBigEndianFrameSize();
        TestId3v23Unsynchronisation();
        TestId3v24FrameLevelUnsynchronisation();
        TestId3v24DataLengthIndicator();
        TestId3v22Pic();
        TestFrontCoverWinsOverEarlierPicture();
        TestNonFrontCoverIsStillUsable();
        TestUnicodeDescriptionIsSkipped();
        TestUrlPayloadIsRejected();
        TestNonImagePayloadIsRejected();
        TestOversizedAndTruncatedInputsAreRejected();
        TestFlacPictureBlock();
        TestFlacBehindId3Tag();
        TestFlacUrlPictureIsRejected();
        TestMp4CoverAtom();
        TestMp4MetaWithoutVersionFlags();
        TestMp4LyingAtomSizeIsRejected();
        TestArtworkFileExtension();
        std::wcout << L"EmbeddedArtworkTests passed" << std::endl;
        return 0;
    }
    catch (std::exception const& error)
    {
        std::cerr << "EmbeddedArtworkTests failed: " << error.what() << std::endl;
        return 1;
    }
    catch (...)
    {
        std::cerr << "EmbeddedArtworkTests failed with an unknown exception" << std::endl;
        return 1;
    }
}
