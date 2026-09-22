#pragma once

/**
 * CompressUM - Original Compression Algorithm
 * Block Compression Module
 *
 * Handles compression of individual data blocks:
 * - Analyzes block data
 * - Selects optimal preprocessor (Delta/BCJ/None)
 * - Applies LZ77 + Huffman (levels 1-6) or context mixing (levels 7-9)
 */

#include "../config.hpp"
#include "../types.hpp"
#include "../core/bitstream.hpp"
#include "../core/analyzer.hpp"
#include "../core/crc32.hpp"
#include "../dictionary/lz77.hpp"
#include "../entropy/huffman.hpp"
#include "../entropy/cm.hpp"
#include "../transform/delta.hpp"
#include "../transform/bwt.hpp"
#include "../transform/bcj.hpp"
#include "../transform/jpeg.hpp"
#include "../transform/png.hpp"
#include <vector>
#include <algorithm>

namespace compressum {

/**
 * Block compression strategy
 */
enum class CompressionMethod : uint8_t {
    Store = 0,      // No compression (store raw)
    LZ77 = 1,       // LZ77 + Huffman
    LZ77_Delta = 2, // Delta + LZ77 + Huffman
    LZ77_BWT = 3,   // Legacy (format v1): BWT + MTF + LZ77 + Huffman, decode only
    LZ77_BCJ = 4,   // BCJ + LZ77 + Huffman
    CM = 5          // Context mixing + arithmetic coding (optionally after BCJ)
};

/**
 * Compressed block result
 */
struct CompressedBlock {
    std::vector<Byte> data;
    uint32_t original_size;
    uint32_t compressed_size;
    Preprocessor preprocessor;
    CompressionMethod method;
    uint32_t crc32;
};

/**
 * Block Compressor - compresses a single data block
 */
class BlockCompressor {
public:
    explicit BlockCompressor(Level level = Level::Normal)
        : level_(level)
    {
        configure_for_level(level);
    }

    /**
     * Compress a block of data
     * @param input Input data block
     * @return Compressed block
     */
    [[nodiscard]] CompressedBlock compress(ByteSpan input) {
        CompressedBlock result;
        result.crc32 = core::crc32c(input);

        if (input.empty()) {
            result.compressed_size = 0;
            result.method = CompressionMethod::Store;
            result.preprocessor = Preprocessor::None;
            return result;
        }

        // Analyze data
        auto stats = core::Analyzer::analyze(input);

        // Select compression strategy based on analysis
        auto [preprocessor, method] = select_strategy(input, stats);
        result.preprocessor = preprocessor;
        result.method = method;

        // Apply preprocessing
        std::vector<Byte> preprocessed = apply_preprocessor(input, preprocessor);

        result.original_size = static_cast<uint32_t>(input.size());

        // Compress
        std::vector<Byte> compressed;
        if (method == CompressionMethod::Store) {
            compressed = preprocessed;
        } else if (method == CompressionMethod::CM) {
            compressed = entropy::cm::encode(preprocessed);
        } else {
            compressed = compress_lz77_huffman(preprocessed);
        }

        // Check if compression is beneficial (8 bytes block header overhead)
        if (compressed.size() + 8 >= input.size()) {
            // Store raw - compression not beneficial
            result.data.assign(input.begin(), input.end());
            result.compressed_size = static_cast<uint32_t>(input.size());
            result.method = CompressionMethod::Store;
            result.preprocessor = Preprocessor::None;
        } else {
            result.data = std::move(compressed);
            result.compressed_size = static_cast<uint32_t>(result.data.size());
        }

        return result;
    }

    /**
     * Set compression level
     */
    void set_level(Level level) {
        level_ = level;
        configure_for_level(level);
    }

private:
    /**
     * Configure parameters based on compression level
     */
    void configure_for_level(Level level) {
        lz_config_.level = level;
        use_cm_ = false;

        switch (level) {
            case Level::Fast:
                lz_config_.max_chain = 4;
                lz_config_.lazy_matching = false;
                break;
            case Level::Normal:
                lz_config_.max_chain = 32;
                lz_config_.lazy_matching = true;
                break;
            case Level::Best:
                lz_config_.max_chain = 128;
                lz_config_.lazy_matching = true;
                use_cm_ = true;
                break;
        }
    }

    /**
     * Select best compression strategy for data
     */
    [[nodiscard]] std::pair<Preprocessor, CompressionMethod>
    select_strategy(ByteSpan input, const DataStats& stats) {
        // Best level: context mixing models the data directly, even media and
        // high-entropy data (JPEG shrinks ~7%); compress() stores the block raw
        // if CM does not help. No BCJ: it made ARM64/universal binaries larger
        if (use_cm_) {
            return {Preprocessor::None, CompressionMethod::CM};
        }

        // Already compressed data - store without compression
        if (stats.type == DataType::Compressed) {
            return {Preprocessor::None, CompressionMethod::Store};
        }

        // Media files (JPEG/PNG) are already entropy-coded. The JPEG/PNG
        // "optimizers" rewrite bytes irreversibly, which breaks lossless
        // round-trip, so they are never selected; decoder keeps them for old files.
        if (stats.type == DataType::Media) {
            return {Preprocessor::None, CompressionMethod::Store};
        }

        // Very high entropy - unlikely to compress well
        if (stats.entropy > 7.9) {
            return {Preprocessor::None, CompressionMethod::Store};
        }

        // Executable files benefit from BCJ filter
        if (stats.type == DataType::Executable) {
            return {Preprocessor::BCJ, CompressionMethod::LZ77_BCJ};
        }

        // Check for sequential/correlated data (delta encoding beneficial)
        if (is_sequential(input)) {
            return {Preprocessor::Delta, CompressionMethod::LZ77_Delta};
        }

        // Default: plain LZ77 + Huffman
        return {Preprocessor::None, CompressionMethod::LZ77};
    }

    /**
     * Check if data has sequential characteristics (beneficial for delta)
     */
    [[nodiscard]] bool is_sequential(ByteSpan input) const {
        if (input.size() < 100) return false;

        // Calculate variance of differences
        int64_t sum = 0;
        int64_t sum_sq = 0;
        size_t count = std::min(input.size() - 1, size_t(1000));

        for (size_t i = 0; i < count; ++i) {
            int diff = static_cast<int>(input[i + 1]) - static_cast<int>(input[i]);
            sum += diff;
            sum_sq += diff * diff;
        }

        double mean = static_cast<double>(sum) / count;
        double variance = static_cast<double>(sum_sq) / count - mean * mean;

        // Low variance indicates sequential data
        return variance < 100.0;
    }

    /**
     * Apply selected preprocessor
     */
    [[nodiscard]] std::vector<Byte> apply_preprocessor(ByteSpan input, Preprocessor prep) {
        if (prep == Preprocessor::None) {
            return {input.begin(), input.end()};
        }

        std::vector<Byte> result(input.begin(), input.end());

        // Format-specific optimizers (applied first, mutually exclusive)
        if (has_flag(prep, Preprocessor::JPEG)) {
            transform::JpegOptimizer jpeg;
            result = jpeg.encode(result);
            return result;  // JPEG optimization is standalone
        }

        if (has_flag(prep, Preprocessor::PNG)) {
            transform::PngOptimizer png;
            result = png.encode(result);
            return result;  // PNG optimization is standalone
        }

        // General preprocessors (can be combined)
        if (has_flag(prep, Preprocessor::BCJ)) {
            transform::BCJx86 bcj;
            result = bcj.encode(result);
        }

        if (has_flag(prep, Preprocessor::Delta)) {
            transform::DeltaEncoder delta;
            result = delta.encode(result);
        }

        return result;
    }

    /**
     * Compress using LZ77 + Huffman
     */
    [[nodiscard]] std::vector<Byte> compress_lz77_huffman(ByteSpan input) {
        // Step 1: LZ77 compression
        dict::LZ77Compressor lz(lz_config_);
        auto tokens = lz.compress(input);

        // Step 2: Collect statistics for Huffman tables
        std::array<uint32_t, 256> literal_freq{};
        std::array<uint32_t, 256> length_freq{};
        std::array<uint32_t, 256> dist_high_freq{};  // High byte of distance

        for (const auto& token : tokens) {
            if (token.type == dict::LZ77Token::Literal) {
                ++literal_freq[token.literal];
            } else {
                // Encode length-3 as a byte
                uint8_t len_code = static_cast<uint8_t>(
                    std::min<uint16_t>(token.match.length - 3, 255));
                ++length_freq[len_code];

                // Distance high byte
                uint8_t dist_high = static_cast<uint8_t>(token.match.distance >> 8);
                ++dist_high_freq[dist_high];
            }
        }

        // Step 3: Build Huffman tables
        entropy::HuffmanEncoder literal_huff;
        entropy::HuffmanEncoder length_huff;
        entropy::HuffmanEncoder dist_huff;

        literal_huff.build(literal_freq.data(), 256);
        length_huff.build(length_freq.data(), 256);
        dist_huff.build(dist_high_freq.data(), 256);

        // Step 4: Encode to bit stream
        core::BitWriter writer;

        // Write Huffman tables (code lengths)
        literal_huff.write_lengths(writer, 256);
        length_huff.write_lengths(writer, 256);
        dist_huff.write_lengths(writer, 256);

        // Write number of tokens
        uint32_t token_count = static_cast<uint32_t>(tokens.size());
        writer.write_bits(token_count, 32);

        // Encode tokens
        for (const auto& token : tokens) {
            if (token.type == dict::LZ77Token::Literal) {
                writer.write_bit(false);  // Flag: literal
                literal_huff.encode(writer, token.literal);
            } else {
                writer.write_bit(true);   // Flag: match

                // Encode length
                uint8_t len_code = static_cast<uint8_t>(
                    std::min<uint16_t>(token.match.length - 3, 255));
                length_huff.encode(writer, len_code);

                // Encode distance: high byte (Huffman) + low byte (raw)
                uint8_t dist_high = static_cast<uint8_t>(token.match.distance >> 8);
                uint8_t dist_low = static_cast<uint8_t>(token.match.distance & 0xFF);
                dist_huff.encode(writer, dist_high);
                writer.write_byte(dist_low);
            }
        }

        writer.flush();
        return writer.take_data();
    }

    Level level_;
    dict::LZ77Config lz_config_;
    bool use_cm_ = false;
};

/**
 * Block Decompressor - decompresses a single data block
 */
class BlockDecompressor {
public:
    BlockDecompressor() = default;

    /**
     * Decompress a block
     * @param input Compressed block data
     * @param method Compression method used
     * @param preprocessor Preprocessor flags
     * @param original_size Expected original size
     * @return Decompressed data
     */
    [[nodiscard]] std::vector<Byte> decompress(
        ByteSpan input,
        CompressionMethod method,
        Preprocessor preprocessor,
        uint32_t original_size
    ) {
        if (input.empty() || original_size == 0) {
            return {};
        }

        std::vector<Byte> result;

        // Decompress
        if (method == CompressionMethod::Store) {
            result.assign(input.begin(), input.end());
        } else if (method == CompressionMethod::CM) {
            result = entropy::cm::decode(input, original_size);
        } else {
            result = decompress_lz77_huffman(input, original_size);
        }

        // Reverse preprocessing
        result = reverse_preprocessor(result, preprocessor);

        return result;
    }

private:
    /**
     * Decompress LZ77 + Huffman
     */
    [[nodiscard]] std::vector<Byte> decompress_lz77_huffman(ByteSpan input, uint32_t original_size) {
        core::BitReader reader(input);

        // Read Huffman tables
        entropy::HuffmanDecoder literal_huff;
        entropy::HuffmanDecoder length_huff;
        entropy::HuffmanDecoder dist_huff;

        literal_huff.read_lengths(reader, 256);
        length_huff.read_lengths(reader, 256);
        dist_huff.read_lengths(reader, 256);

        // Read token count
        uint32_t token_count = static_cast<uint32_t>(reader.read_bits(32));

        // Decode tokens and reconstruct data
        std::vector<Byte> output;
        output.reserve(original_size);

        for (uint32_t i = 0; i < token_count && output.size() < original_size; ++i) {
            bool is_match = reader.read_bit();

            if (!is_match) {
                // Literal
                uint16_t symbol = literal_huff.decode(reader);
                output.push_back(static_cast<Byte>(symbol));
            } else {
                // Match
                uint16_t len_code = length_huff.decode(reader);
                uint16_t length = len_code + 3;

                uint16_t dist_high = dist_huff.decode(reader);
                uint8_t dist_low = reader.read_byte();
                uint16_t distance = (dist_high << 8) | dist_low;

                // Corrupt stream: distance outside produced data. Stop here;
                // the size check in the caller reports CorruptedData
                if (distance == 0 || distance > output.size()) {
                    break;
                }

                // Copy from earlier in output
                size_t copy_pos = output.size() - distance;
                for (uint16_t j = 0; j < length && output.size() < original_size; ++j) {
                    output.push_back(output[copy_pos + j]);
                }
            }
        }

        return output;
    }

    /**
     * Reverse preprocessing transforms
     */
    [[nodiscard]] std::vector<Byte> reverse_preprocessor(std::vector<Byte>& data, Preprocessor prep) {
        if (prep == Preprocessor::None) {
            return data;
        }

        std::vector<Byte> result = std::move(data);

        // Format-specific optimizers (decode is passthrough)
        if (has_flag(prep, Preprocessor::JPEG)) {
            transform::JpegOptimizer jpeg;
            result = jpeg.decode(result);
            return result;
        }

        if (has_flag(prep, Preprocessor::PNG)) {
            transform::PngOptimizer png;
            result = png.decode(result);
            return result;
        }

        // Reverse in opposite order of application
        if (has_flag(prep, Preprocessor::RLE)) {
            transform::ZLE zle;
            result = zle.decode(result);
        }

        if (has_flag(prep, Preprocessor::BWT)) {
            // Legacy format v1 blocks only: reverse MTF first
            transform::MTF mtf;
            result = mtf.inverse(result);

            // Extract primary index (first 4 bytes)
            if (result.size() >= 4) {
                uint32_t primary_index = read_uint32_le(result.data());
                ByteSpan bwt_data(result.data() + 4, result.size() - 4);

                transform::BWT bwt;
                result = bwt.inverse(bwt_data, primary_index);
            }
        }

        if (has_flag(prep, Preprocessor::Delta)) {
            transform::DeltaDecoder delta;
            result = delta.decode(result);
        }

        if (has_flag(prep, Preprocessor::BCJ)) {
            transform::BCJx86 bcj;
            result = bcj.decode(result);
        }

        return result;
    }

    /**
     * Read uint32 little-endian
     */
    static uint32_t read_uint32_le(const Byte* data) {
        return static_cast<uint32_t>(data[0]) |
               (static_cast<uint32_t>(data[1]) << 8) |
               (static_cast<uint32_t>(data[2]) << 16) |
               (static_cast<uint32_t>(data[3]) << 24);
    }
};

} // namespace compressum
