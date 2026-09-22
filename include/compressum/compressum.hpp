#pragma once

/**
 * CompressUM - Original Compression Algorithm
 * Main API Header
 *
 * This is the primary include file for using CompressUM.
 */

#include "config.hpp"
#include "types.hpp"
#include "core/bitstream.hpp"
#include "core/crc32.hpp"
#include "core/mmap.hpp"
#include "core/analyzer.hpp"
#include "compressor/block.hpp"
#include "compressor/parallel.hpp"

#include <vector>
#include <span>
#include <string>
#include <functional>

namespace compressum {

// Forward declarations
namespace entropy {
    class HuffmanEncoder;
    class HuffmanDecoder;
    class RansEncoder;
    class RansDecoder;
}

namespace dict {
    class LZ77Compressor;
}

/**
 * Progress callback type
 * @param bytes_processed Current progress
 * @param total_bytes Total bytes to process
 * @return true to continue, false to cancel
 */
using ProgressCallback = std::function<bool(size_t bytes_processed, size_t total_bytes)>;

/**
 * Compression options
 */
struct CompressOptions {
    Level level = Level::Normal;
    size_t block_size = config::DEFAULT_BLOCK_SIZE;
    size_t threads = 0;  // 0 = auto
    ProgressCallback progress = nullptr;
};

/**
 * Decompression options
 */
struct DecompressOptions {
    size_t threads = 0;  // 0 = auto
    ProgressCallback progress = nullptr;
};

/**
 * Compression result
 */
struct CompressResult {
    ErrorCode error = ErrorCode::Ok;
    size_t original_size = 0;
    size_t compressed_size = 0;
    uint32_t crc32 = 0;
    double ratio = 0.0;            // original_size / compressed_size (e.g. 3.5 = "3.5x")
    double speed_mbps = 0.0;

    [[nodiscard]] bool ok() const { return error == ErrorCode::Ok; }
};

/**
 * Decompression result
 */
struct DecompressResult {
    ErrorCode error = ErrorCode::Ok;
    size_t original_size = 0;
    size_t compressed_size = 0;
    bool crc_valid = false;
    double speed_mbps = 0.0;

    [[nodiscard]] bool ok() const { return error == ErrorCode::Ok; }
};

/**
 * Main CompressUM compressor class
 */
class Compressor {
public:
    Compressor() = default;
    explicit Compressor(const CompressOptions& options) : options_(options) {}

    /**
     * Compress data in memory
     * @param input Input data
     * @return Compressed data and result info
     */
    [[nodiscard]] std::pair<std::vector<Byte>, CompressResult>
    compress(ByteSpan input);

    /**
     * Compress data to output buffer
     * @param input Input data
     * @param output Output buffer (must be large enough)
     * @return Result with compressed size
     */
    [[nodiscard]] CompressResult
    compress(ByteSpan input, MutableByteSpan output);

    /**
     * Compress file to file
     * @param input_path Source file path
     * @param output_path Destination file path
     * @return Compression result
     */
    [[nodiscard]] CompressResult
    compress_file(const std::string& input_path, const std::string& output_path);

    /**
     * Get maximum compressed size for given input size
     * Compressed data will never exceed this
     */
    [[nodiscard]] static size_t max_compressed_size(size_t input_size);

    /**
     * Set compression options
     */
    void set_options(const CompressOptions& options) { options_ = options; }

private:
    CompressOptions options_;
};

/**
 * Main CompressUM decompressor class
 */
class Decompressor {
public:
    Decompressor() = default;
    explicit Decompressor(const DecompressOptions& options) : options_(options) {}

    /**
     * Decompress data in memory
     * @param input Compressed data
     * @return Original data and result info
     */
    [[nodiscard]] std::pair<std::vector<Byte>, DecompressResult>
    decompress(ByteSpan input);

    /**
     * Decompress data to output buffer
     * @param input Compressed data
     * @param output Output buffer (must match original size from header)
     * @return Result
     */
    [[nodiscard]] DecompressResult
    decompress(ByteSpan input, MutableByteSpan output);

    /**
     * Decompress file to file
     * @param input_path Compressed file path
     * @param output_path Destination file path
     * @return Decompression result
     */
    [[nodiscard]] DecompressResult
    decompress_file(const std::string& input_path, const std::string& output_path);

    /**
     * Read original size from compressed data header
     * @param input Compressed data (at least 32 bytes)
     * @return Original size or 0 on error
     */
    [[nodiscard]] static size_t get_original_size(ByteSpan input);

    /**
     * Validate compressed data header
     * @param input Compressed data
     * @return true if valid CompressUM format
     */
    [[nodiscard]] static bool validate_header(ByteSpan input);

    void set_options(const DecompressOptions& options) { options_ = options; }

private:
    DecompressOptions options_;
};

/**
 * Convenience functions
 */

/**
 * One-shot compress
 */
[[nodiscard]] inline std::vector<Byte> compress(ByteSpan input, Level level = Level::Normal) {
    Compressor c(CompressOptions{.level = level});
    auto [data, result] = c.compress(input);
    return data;
}

/**
 * One-shot decompress
 */
[[nodiscard]] inline std::vector<Byte> decompress(ByteSpan input) {
    Decompressor d;
    auto [data, result] = d.decompress(input);
    return data;
}

/**
 * Analyze data
 */
[[nodiscard]] inline DataStats analyze(ByteSpan data) {
    return core::Analyzer::analyze(data);
}

} // namespace compressum
