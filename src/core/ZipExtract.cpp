#include "core/ZipExtract.h"

#include <algorithm>
#include <limits>
#include <zlib.h>

namespace
{

constexpr std::uint32_t kLocalHeaderSignature = 0x04034b50;
constexpr std::uint16_t kMethodStored = 0;
constexpr std::uint16_t kMethodDeflate = 8;
constexpr std::size_t kOutputChunk = 64 * 1024;
// zlib counts bytes in a 32-bit uInt.
constexpr std::size_t kMaxSlice = std::size_t{1} << 30;

std::uint64_t readLe(const std::vector<char>& bytes, std::size_t at, std::size_t width)
{
    std::uint64_t value = 0;
    for (std::size_t i = 0; i < width; ++i)
    {
        value |= static_cast<std::uint64_t>(static_cast<unsigned char>(bytes[at + i])) << (8 * i);
    }
    return value;
}

} // namespace

ZipEntrySupport zipEntrySupport(const ZipEntry& entry)
{
    if (entry.isDirectory)
        return ZipEntrySupport::Directory;
    if (entry.encrypted)
        return ZipEntrySupport::Encrypted;
    if (entry.compressionMethod != kMethodStored && entry.compressionMethod != kMethodDeflate)
        return ZipEntrySupport::UnsupportedMethod;
    return ZipEntrySupport::Supported;
}

std::optional<std::uint64_t> zipEntryDataOffset(const std::vector<char>& localHeader,
                                                std::uint64_t localHeaderOffset)
{
    if (localHeader.size() < kZipLocalHeaderFixedSize ||
        readLe(localHeader, 0, 4) != kLocalHeaderSignature)
    {
        return std::nullopt;
    }
    const std::uint64_t nameLength = readLe(localHeader, 26, 2);
    const std::uint64_t extraLength = readLe(localHeader, 28, 2);
    const std::uint64_t headerSize = kZipLocalHeaderFixedSize + nameLength + extraLength;
    if (localHeaderOffset > (std::numeric_limits<std::uint64_t>::max)() - headerSize)
        return std::nullopt;
    return localHeaderOffset + headerSize;
}

struct ZipEntryInflater::State
{
    std::uint16_t method = 0;
    std::uint64_t compressedSize = 0;
    std::uint64_t uncompressedSize = 0;
    std::uint32_t expectedCrc = 0;
    Sink sink;

    z_stream stream{};
    bool streamOpen = false;
    bool streamEnded = false;
    std::vector<unsigned char> output;

    std::uint64_t compressedSeen = 0;
    std::uint64_t produced = 0;
    uLong runningCrc = 0;
    ZipExtractStatus status = ZipExtractStatus::Pending;

    ~State()
    {
        if (streamOpen)
            inflateEnd(&stream);
    }

    bool fail(ZipExtractStatus why)
    {
        status = why;
        return false;
    }

    // size is at most kMaxSlice.
    bool emit(const char* data, std::size_t size)
    {
        if (size == 0)
            return true;
        // Checked before the sink sees anything, so a directory that understates the
        // size cannot be used to write more than it declared.
        if (size > uncompressedSize - produced)
            return fail(ZipExtractStatus::SizeMismatch);
        runningCrc =
            ::crc32(runningCrc, reinterpret_cast<const Bytef*>(data), static_cast<uInt>(size));
        produced += size;
        if (!sink(data, size))
            return fail(ZipExtractStatus::SinkFailed);
        return true;
    }

    bool inflateSlice(const char* data, std::size_t size)
    {
        if (streamEnded)
            return fail(ZipExtractStatus::CorruptData);
        stream.next_in = reinterpret_cast<Bytef*>(const_cast<char*>(data));
        stream.avail_in = static_cast<uInt>(size);
        for (;;)
        {
            stream.next_out = output.data();
            stream.avail_out = static_cast<uInt>(output.size());
            const int rc = inflate(&stream, Z_NO_FLUSH);
            if (rc != Z_OK && rc != Z_STREAM_END && rc != Z_BUF_ERROR)
                return fail(ZipExtractStatus::CorruptData);
            if (!emit(reinterpret_cast<const char*>(output.data()),
                      output.size() - stream.avail_out))
            {
                return false;
            }
            if (rc == Z_STREAM_END)
            {
                streamEnded = true;
                return stream.avail_in == 0 || fail(ZipExtractStatus::CorruptData);
            }
            if (stream.avail_in == 0 && stream.avail_out != 0)
                return true;
        }
    }
};

ZipEntryInflater::ZipEntryInflater(const ZipEntry& entry, Sink sink)
    : mState(std::make_unique<State>())
{
    State& s = *mState;
    s.method = entry.compressionMethod;
    s.compressedSize = entry.compressedSize;
    s.uncompressedSize = entry.uncompressedSize;
    s.expectedCrc = entry.crc;
    s.sink = std::move(sink);

    if (zipEntrySupport(entry) != ZipEntrySupport::Supported)
    {
        s.status = ZipExtractStatus::Unsupported;
        return;
    }
    if (s.method == kMethodDeflate)
    {
        s.output.resize(kOutputChunk);
        // Negative window bits: zip stores raw deflate, without zlib's header and trailer.
        if (inflateInit2(&s.stream, -MAX_WBITS) != Z_OK)
        {
            s.status = ZipExtractStatus::CorruptData;
            return;
        }
        s.streamOpen = true;
    }
}

ZipEntryInflater::~ZipEntryInflater() = default;

bool ZipEntryInflater::feed(const char* data, std::size_t size)
{
    State& s = *mState;
    if (s.status != ZipExtractStatus::Pending)
        return false;
    if (size > s.compressedSize - s.compressedSeen)
        return s.fail(ZipExtractStatus::SizeMismatch);
    s.compressedSeen += size;

    while (size > 0)
    {
        const std::size_t slice = (std::min)(size, kMaxSlice);
        const bool ok =
            s.method == kMethodStored ? s.emit(data, slice) : s.inflateSlice(data, slice);
        if (!ok)
            return false;
        data += slice;
        size -= slice;
    }
    return true;
}

ZipExtractStatus ZipEntryInflater::finish()
{
    State& s = *mState;
    if (s.status != ZipExtractStatus::Pending)
        return s.status;
    if (s.compressedSeen < s.compressedSize)
        s.fail(ZipExtractStatus::Truncated);
    // Some writers store an empty file as method 8 with no deflate bytes at all.
    else if (s.method == kMethodDeflate && !s.streamEnded && s.compressedSize != 0)
        s.fail(ZipExtractStatus::CorruptData);
    else if (s.produced != s.uncompressedSize)
        s.fail(ZipExtractStatus::SizeMismatch);
    else if (s.runningCrc != s.expectedCrc)
        s.fail(ZipExtractStatus::CrcMismatch);
    else
        s.status = ZipExtractStatus::Ok;
    return s.status;
}

ZipExtractStatus ZipEntryInflater::status() const
{
    return mState->status;
}
