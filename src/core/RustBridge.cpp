#include "RustBridge.h"

#ifdef ALPHA_ENABLE_RUST
#include "../../rust/alpha_bridge/alpha_bridge.h"
#endif

#include "Logger.h"
#include "NBT.h"

#include <algorithm>
#include <array>
#include <cstring>

#include <zlib.h>
#include <zstd.h>

namespace {

std::vector<uint8_t> gzipCompressFallback(const std::vector<uint8_t>& input, int level) {
    z_stream strm{};
    strm.zalloc = Z_NULL;
    strm.zfree = Z_NULL;
    strm.opaque = Z_NULL;

    const int actualLevel = level < 0 ? Z_DEFAULT_COMPRESSION : std::clamp(level, 0, 9);
    if (deflateInit2(&strm, actualLevel, Z_DEFLATED, 15 + 16, 8, Z_DEFAULT_STRATEGY) != Z_OK) {
        return {};
    }

    strm.next_in = const_cast<Bytef*>(reinterpret_cast<const Bytef*>(input.data()));
    strm.avail_in = static_cast<uInt>(input.size());

    std::vector<uint8_t> output;
    std::array<uint8_t, 16384> chunk{};

    int ret = Z_OK;
    do {
        strm.next_out = chunk.data();
        strm.avail_out = static_cast<uInt>(chunk.size());
        ret = deflate(&strm, Z_FINISH);
        if (ret != Z_OK && ret != Z_STREAM_END) {
            deflateEnd(&strm);
            return {};
        }

        const size_t produced = chunk.size() - strm.avail_out;
        output.insert(output.end(), chunk.data(), chunk.data() + produced);
    } while (ret != Z_STREAM_END);

    deflateEnd(&strm);
    return output;
}

std::vector<uint8_t> gzipDecompressFallback(const std::vector<uint8_t>& input) {
    z_stream strm{};
    strm.zalloc = Z_NULL;
    strm.zfree = Z_NULL;
    strm.opaque = Z_NULL;
    strm.next_in = const_cast<Bytef*>(reinterpret_cast<const Bytef*>(input.data()));
    strm.avail_in = static_cast<uInt>(input.size());

    if (inflateInit2(&strm, 15 + 16) != Z_OK) {
        return {};
    }

    std::vector<uint8_t> output;
    std::array<uint8_t, 16384> chunk{};

    int ret = Z_OK;
    do {
        strm.next_out = chunk.data();
        strm.avail_out = static_cast<uInt>(chunk.size());
        ret = inflate(&strm, Z_NO_FLUSH);
        if (ret != Z_OK && ret != Z_STREAM_END) {
            inflateEnd(&strm);
            return {};
        }

        const size_t produced = chunk.size() - strm.avail_out;
        output.insert(output.end(), chunk.data(), chunk.data() + produced);
    } while (ret != Z_STREAM_END);

    inflateEnd(&strm);
    return output;
}

std::vector<uint8_t> zstdCompressFallback(const std::vector<uint8_t>& input, int level) {
    const size_t bound = ZSTD_compressBound(input.size());
    std::vector<uint8_t> output(bound);
    const size_t compressed = ZSTD_compress(
        output.data(), output.size(), input.data(), input.size(), std::clamp(level, 1, 22));
    if (ZSTD_isError(compressed)) {
        Logger::severe("zstd compress failed: {}", ZSTD_getErrorName(compressed));
        return {};
    }

    output.resize(compressed);
    return output;
}

std::vector<uint8_t> zstdDecompressFallback(const std::vector<uint8_t>& input) {
    unsigned long long frameSize = ZSTD_getFrameContentSize(input.data(), input.size());
    if (frameSize == ZSTD_CONTENTSIZE_ERROR) {
        Logger::severe("zstd decompress failed: invalid frame");
        return {};
    }

    std::vector<uint8_t> output;
    if (frameSize != ZSTD_CONTENTSIZE_UNKNOWN) {
        output.resize(static_cast<size_t>(frameSize));
        const size_t result = ZSTD_decompress(output.data(), output.size(), input.data(), input.size());
        if (ZSTD_isError(result)) {
            Logger::severe("zstd decompress failed: {}", ZSTD_getErrorName(result));
            return {};
        }
        output.resize(result);
        return output;
    }

    ZSTD_DStream* stream = ZSTD_createDStream();
    if (!stream) {
        return {};
    }

    size_t initResult = ZSTD_initDStream(stream);
    if (ZSTD_isError(initResult)) {
        ZSTD_freeDStream(stream);
        Logger::severe("zstd init stream failed: {}", ZSTD_getErrorName(initResult));
        return {};
    }

    output.reserve(input.size() * 2);
    std::array<uint8_t, 16384> chunk{};
    ZSTD_inBuffer inBuf{input.data(), input.size(), 0};

    while (inBuf.pos < inBuf.size) {
        ZSTD_outBuffer outBuf{chunk.data(), chunk.size(), 0};
        const size_t result = ZSTD_decompressStream(stream, &outBuf, &inBuf);
        if (ZSTD_isError(result)) {
            ZSTD_freeDStream(stream);
            Logger::severe("zstd decompress stream failed: {}", ZSTD_getErrorName(result));
            return {};
        }
        output.insert(output.end(), chunk.data(), chunk.data() + outBuf.pos);
    }

    ZSTD_freeDStream(stream);
    return output;
}

std::vector<uint8_t> encodeLevelDatFallback(const RustBridge::LevelDatData& level) {
    ByteBuffer buf;
    NBTCompound data;
    data.setLong("RandomSeed", level.randomSeed);
    data.setInt("SpawnX", level.spawnX);
    data.setInt("SpawnY", level.spawnY);
    data.setInt("SpawnZ", level.spawnZ);
    data.setLong("Time", level.worldTime);
    data.setLong("SizeOnDisk", level.sizeOnDisk);
    data.setInt("version", level.version);
    data.setString("LevelName", level.levelName);

    NBTCompound root;
    root.setCompound("Data", std::make_shared<NBTCompound>(data));
    root.writeRoot(buf, "");
    return gzipCompressFallback(buf.data, -1);
}

bool decodeLevelDatFallback(const std::vector<uint8_t>& input, RustBridge::LevelDatData& outLevel) {
    std::vector<uint8_t> raw = gzipDecompressFallback(input);
    if (raw.empty()) {
        return false;
    }

    ByteBuffer buf(raw);
    auto root = NBTCompound::readRoot(buf);
    if (!root) {
        return false;
    }

    auto data = root->getCompound("Data");
    if (!data) {
        return false;
    }

    outLevel.randomSeed = data->getLong("RandomSeed");
    outLevel.spawnX = data->getInt("SpawnX");
    outLevel.spawnY = data->getInt("SpawnY");
    outLevel.spawnZ = data->getInt("SpawnZ");
    outLevel.worldTime = data->getLong("Time");
    outLevel.sizeOnDisk = data->getLong("SizeOnDisk");
    outLevel.version = data->getInt("version");
    outLevel.levelName = data->getString("LevelName");
    if (outLevel.levelName.empty()) {
        outLevel.levelName = "world";
    }
    return true;
}

#ifdef ALPHA_ENABLE_RUST
std::vector<uint8_t> fromRustBuffer(AlphaBuffer buffer) {
    if (!buffer.data || buffer.len == 0) {
        alpha_buffer_free(buffer);
        return {};
    }

    std::vector<uint8_t> bytes(buffer.len);
    std::memcpy(bytes.data(), buffer.data, buffer.len);
    alpha_buffer_free(buffer);
    return bytes;
}

#endif

} // namespace

namespace RustBridge {

bool enabled() {
#ifdef ALPHA_ENABLE_RUST
    return true;
#else
    return false;
#endif
}

std::vector<uint8_t> gzipCompress(const std::vector<uint8_t>& input, int level) {
#ifdef ALPHA_ENABLE_RUST
    if (auto output = fromRustBuffer(alpha_gzip_compress(input.data(), input.size(), level)); !output.empty() || input.empty()) {
        return output;
    }
#endif
    return gzipCompressFallback(input, level);
}

std::vector<uint8_t> gzipDecompress(const std::vector<uint8_t>& input) {
#ifdef ALPHA_ENABLE_RUST
    if (auto output = fromRustBuffer(alpha_gzip_decompress(input.data(), input.size())); !output.empty() || input.empty()) {
        return output;
    }
#endif
    return gzipDecompressFallback(input);
}

std::vector<uint8_t> zstdCompress(const std::vector<uint8_t>& input, int level) {
#ifdef ALPHA_ENABLE_RUST
    if (auto output = fromRustBuffer(alpha_zstd_compress(input.data(), input.size(), level)); !output.empty() || input.empty()) {
        return output;
    }
#endif
    return zstdCompressFallback(input, level);
}

std::vector<uint8_t> zstdDecompress(const std::vector<uint8_t>& input) {
#ifdef ALPHA_ENABLE_RUST
    if (auto output = fromRustBuffer(alpha_zstd_decompress(input.data(), input.size())); !output.empty() || input.empty()) {
        return output;
    }
#endif
    return zstdDecompressFallback(input);
}

std::vector<uint8_t> encodeLevelDat(const LevelDatData& level) {
#ifdef ALPHA_ENABLE_RUST
    AlphaLevelDat ffiLevel{
        .random_seed = level.randomSeed,
        .spawn_x = level.spawnX,
        .spawn_y = level.spawnY,
        .spawn_z = level.spawnZ,
        .world_time = level.worldTime,
        .size_on_disk = level.sizeOnDisk,
        .version = level.version,
        .level_name = level.levelName.c_str(),
    };
    if (auto output = fromRustBuffer(alpha_level_dat_encode(&ffiLevel)); !output.empty()) {
        return output;
    }
#endif
    return encodeLevelDatFallback(level);
}

bool decodeLevelDat(const std::vector<uint8_t>& input, LevelDatData& outLevel) {
#ifdef ALPHA_ENABLE_RUST
    AlphaLevelDat ffiLevel{};
    if (alpha_level_dat_decode(input.data(), input.size(), &ffiLevel) != 0) {
        outLevel.randomSeed = ffiLevel.random_seed;
        outLevel.spawnX = ffiLevel.spawn_x;
        outLevel.spawnY = ffiLevel.spawn_y;
        outLevel.spawnZ = ffiLevel.spawn_z;
        outLevel.worldTime = ffiLevel.world_time;
        outLevel.sizeOnDisk = ffiLevel.size_on_disk;
        outLevel.version = ffiLevel.version;
        outLevel.levelName = ffiLevel.level_name ? ffiLevel.level_name : "world";
        alpha_level_dat_free(&ffiLevel);
        return true;
    }
#endif
    return decodeLevelDatFallback(input, outLevel);
}

} // namespace RustBridge
