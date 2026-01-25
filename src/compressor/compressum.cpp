/**
 * CompressUM - Original Compression Algorithm
 * Main Compressor/Decompressor Implementation (Phase 5)
 */

#include "compressum/compressum.hpp"
#include <fstream>
#include <chrono>
#include <thread>
#include <future>

namespace compressum {

// =============================================================================
// Helper Functions
// =============================================================================

namespace {

/**
 * Write FileHeader to output
 */
void write_header(std::vector<Byte>& output, const FileHeader& header) {
    output.resize(sizeof(FileHeader));
    std::memcpy(output.data(), &header, sizeof(FileHeader));
}

/**
 * Read FileHeader from input
 */
bool read_header(ByteSpan input, FileHeader& header) {
    if (input.size() < sizeof(FileHeader)) {
        return false;
    }
    std::memcpy(&header, input.data(), sizeof(FileHeader));
    return true;
}

/**
 * Write block header to output
 */
void write_block_header(std::vector<Byte>& output, const BlockHeader& header) {
    size_t pos = output.size();
    output.resize(pos + sizeof(BlockHeader));
    std::memcpy(output.data() + pos, &header, sizeof(BlockHeader));
}

/**
 * Read block header from input
 */
bool read_block_header(ByteSpan input, size_t offset, BlockHeader& header) {
    if (offset + sizeof(BlockHeader) > input.size()) {
        return false;
    }
    std::memcpy(&header, input.data() + offset, sizeof(BlockHeader));
    return true;
}

/**
 * Get current time in seconds (for speed calculation)
 */
double get_time_seconds() {
    auto now = std::chrono::high_resolution_clock::now();
    return std::chrono::duration<double>(now.time_since_epoch()).count();
}

} // anonymous namespace

// =============================================================================
// Compressor Implementation
// =============================================================================

std::pair<std::vector<Byte>, CompressResult>
Compressor::compress(ByteSpan input) {
    CompressResult result;
    result.original_size = input.size();

    if (input.empty()) {
        result.error = ErrorCode::Ok;
        result.compressed_size = sizeof(FileHeader);
        result.ratio = 0.0;

        // Write minimal header for empty file
        FileHeader header{};
        header.magic[0] = 'C';
        header.magic[1] = 'U';
        header.magic[2] = 'M';
        header.magic[3] = 0x01;
        header.version = config::FORMAT_VERSION;
        header.flags = 0;
        header.original_size = 0;
        header.compressed_size = 0;
        header.crc32 = 0;
        header.block_count = 0;

        std::vector<Byte> output;
        write_header(output, header);
        return {output, result};
    }

    double start_time = get_time_seconds();

    // Determine block size
    size_t block_size = options_.block_size;

    // Calculate number of blocks
    size_t num_blocks = (input.size() + block_size - 1) / block_size;

    // Compress blocks (use parallel if multiple threads requested)
    std::vector<CompressedBlock> compressed_blocks;
    size_t num_threads = options_.threads;
    if (num_threads == 0) {
        num_threads = default_thread_count();
    }

    if (num_threads > 1 && num_blocks > 1) {
        // Parallel compression
        ParallelBlockCompressor parallel_compressor(options_.level, num_threads);
        compressed_blocks = parallel_compressor.compress_blocks(input, block_size);

        // Call progress callback for completion
        if (options_.progress) {
            if (!options_.progress(input.size(), input.size())) {
                result.error = ErrorCode::InternalError;
                return {{}, result};
            }
        }
    } else {
        // Sequential compression
        BlockCompressor block_compressor(options_.level);
        compressed_blocks.reserve(num_blocks);

        for (size_t i = 0; i < num_blocks; ++i) {
            size_t offset = i * block_size;
            size_t len = std::min(block_size, input.size() - offset);
            ByteSpan block_data(input.data() + offset, len);

            auto compressed = block_compressor.compress(block_data);
            compressed_blocks.push_back(std::move(compressed));

            // Progress callback
            if (options_.progress) {
                if (!options_.progress(offset + len, input.size())) {
                    result.error = ErrorCode::InternalError;
                    return {{}, result};
                }
            }
        }
    }

    // Calculate total sizes and CRC
    // For media optimizers (JPEG/PNG), the block original_size is the preprocessed size,
    // so we need to sum block original_sizes to get the true output size
    size_t total_compressed = sizeof(FileHeader);
    size_t total_original = 0;
    for (const auto& block : compressed_blocks) {
        total_compressed += sizeof(BlockHeader) + block.data.size();
        total_original += block.original_size;
    }

    // Calculate CRC on what will be the reconstructed output
    // For blocks with media optimizers, this is the optimized data (block.data)
    // For normal blocks, this is the original input
    std::vector<Byte> logical_output;
    logical_output.reserve(total_original);
    size_t input_offset = 0;
    for (const auto& block : compressed_blocks) {
        bool is_media = has_flag(block.preprocessor, Preprocessor::JPEG) ||
                        has_flag(block.preprocessor, Preprocessor::PNG);
        if (is_media) {
            // For media blocks, the "original" is the optimized data
            logical_output.insert(logical_output.end(), block.data.begin(), block.data.end());
        } else {
            // For normal blocks, the "original" is the input
            size_t block_len = block.original_size;
            logical_output.insert(logical_output.end(),
                                  input.data() + input_offset,
                                  input.data() + input_offset + block_len);
        }
        // Move input offset by the actual input block size
        size_t input_block_len = std::min(options_.block_size, input.size() - input_offset);
        input_offset += input_block_len;
    }
    result.crc32 = core::crc32c(logical_output);

    // Build output
    std::vector<Byte> output;
    output.reserve(total_compressed);

    // Write file header
    FileHeader header{};
    header.magic[0] = 'C';
    header.magic[1] = 'U';
    header.magic[2] = 'M';
    header.magic[3] = 0x01;
    header.version = config::FORMAT_VERSION;
    header.flags = static_cast<uint16_t>(options_.level);
    header.original_size = total_original;
    header.compressed_size = total_compressed - sizeof(FileHeader);
    header.crc32 = result.crc32;
    header.block_count = static_cast<uint32_t>(num_blocks);

    write_header(output, header);

    // Write blocks
    for (const auto& block : compressed_blocks) {
        BlockHeader block_header{};
        write_uint24(block_header.compressed_size, block.compressed_size);
        write_uint24(block_header.original_size, block.original_size);
        block_header.preprocessor = static_cast<uint8_t>(block.preprocessor);
        block_header.level = static_cast<uint8_t>(block.method);

        write_block_header(output, block_header);
        output.insert(output.end(), block.data.begin(), block.data.end());
    }

    double elapsed = get_time_seconds() - start_time;

    result.error = ErrorCode::Ok;
    result.compressed_size = output.size();
    result.ratio = 1.0 - (static_cast<double>(result.compressed_size) /
                         static_cast<double>(result.original_size));
    result.speed_mbps = elapsed > 0 ?
        (static_cast<double>(result.original_size) / (1024.0 * 1024.0)) / elapsed : 0;

    return {output, result};
}

CompressResult
Compressor::compress(ByteSpan input, MutableByteSpan output) {
    auto [data, result] = compress(input);

    if (!result.ok()) {
        return result;
    }

    if (output.size() < data.size()) {
        result.error = ErrorCode::OutputTooSmall;
        return result;
    }

    std::memcpy(output.data(), data.data(), data.size());
    result.compressed_size = data.size();

    return result;
}

CompressResult
Compressor::compress_file(const std::string& input_path, const std::string& output_path) {
    CompressResult result;

    // Memory-map input file
    core::MappedFileReader mapped_file;
    auto open_result = mapped_file.open(input_path);
    if (!open_result.ok()) {
        result.error = ErrorCode::IoError;
        return result;
    }

    // Compress
    auto [data, compress_result] = compress(mapped_file.span());

    if (!compress_result.ok()) {
        return compress_result;
    }

    // Write output file
    std::ofstream out(output_path, std::ios::binary);
    if (!out) {
        compress_result.error = ErrorCode::IoError;
        return compress_result;
    }

    out.write(reinterpret_cast<const char*>(data.data()), data.size());

    if (!out) {
        compress_result.error = ErrorCode::IoError;
        return compress_result;
    }

    return compress_result;
}

size_t Compressor::max_compressed_size(size_t input_size) {
    // Worst case: header + uncompressible data with overhead
    // Each block adds 8 bytes header, and incompressible data is stored raw
    size_t num_blocks = (input_size + config::DEFAULT_BLOCK_SIZE - 1) / config::DEFAULT_BLOCK_SIZE;
    return sizeof(FileHeader) + num_blocks * sizeof(BlockHeader) + input_size + 1024;
}

// =============================================================================
// Decompressor Implementation
// =============================================================================

std::pair<std::vector<Byte>, DecompressResult>
Decompressor::decompress(ByteSpan input) {
    DecompressResult result;

    if (input.size() < sizeof(FileHeader)) {
        result.error = ErrorCode::InvalidFormat;
        return {{}, result};
    }

    // Read and validate header
    FileHeader header;
    if (!read_header(input, header)) {
        result.error = ErrorCode::InvalidFormat;
        return {{}, result};
    }

    // Validate magic
    if (header.magic[0] != 'C' || header.magic[1] != 'U' ||
        header.magic[2] != 'M' || header.magic[3] != 0x01) {
        result.error = ErrorCode::InvalidFormat;
        return {{}, result};
    }

    // Validate version
    if (header.version > config::FORMAT_VERSION) {
        result.error = ErrorCode::UnsupportedVersion;
        return {{}, result};
    }

    result.compressed_size = input.size();
    result.original_size = header.original_size;

    // Handle empty file
    if (header.original_size == 0 || header.block_count == 0) {
        result.error = ErrorCode::Ok;
        result.crc_valid = true;
        return {{}, result};
    }

    double start_time = get_time_seconds();

    // Decompress blocks
    std::vector<Byte> output;
    output.reserve(header.original_size);

    BlockDecompressor block_decompressor;
    size_t offset = sizeof(FileHeader);

    for (uint32_t i = 0; i < header.block_count; ++i) {
        // Read block header
        BlockHeader block_header;
        if (!read_block_header(input, offset, block_header)) {
            result.error = ErrorCode::CorruptedData;
            return {{}, result};
        }
        offset += sizeof(BlockHeader);

        uint32_t compressed_size = read_uint24(block_header.compressed_size);
        uint32_t original_size = read_uint24(block_header.original_size);
        auto preprocessor = static_cast<Preprocessor>(block_header.preprocessor);
        auto method = static_cast<CompressionMethod>(block_header.level);

        // Read compressed block data
        if (offset + compressed_size > input.size()) {
            result.error = ErrorCode::CorruptedData;
            return {{}, result};
        }

        ByteSpan block_data(input.data() + offset, compressed_size);
        offset += compressed_size;

        // Decompress block
        auto decompressed = block_decompressor.decompress(
            block_data, method, preprocessor, original_size);

        if (decompressed.size() != original_size) {
            result.error = ErrorCode::CorruptedData;
            return {{}, result};
        }

        output.insert(output.end(), decompressed.begin(), decompressed.end());

        // Progress callback
        if (options_.progress) {
            if (!options_.progress(output.size(), header.original_size)) {
                result.error = ErrorCode::InternalError;
                return {{}, result};
            }
        }
    }

    // Verify CRC
    uint32_t computed_crc = core::crc32c(output);
    result.crc_valid = (computed_crc == header.crc32);

    if (!result.crc_valid) {
        result.error = ErrorCode::ChecksumMismatch;
        return {{}, result};
    }

    double elapsed = get_time_seconds() - start_time;

    result.error = ErrorCode::Ok;
    result.speed_mbps = elapsed > 0 ?
        (static_cast<double>(result.original_size) / (1024.0 * 1024.0)) / elapsed : 0;

    return {output, result};
}

DecompressResult
Decompressor::decompress(ByteSpan input, MutableByteSpan output) {
    auto [data, result] = decompress(input);

    if (!result.ok()) {
        return result;
    }

    if (output.size() < data.size()) {
        result.error = ErrorCode::OutputTooSmall;
        return result;
    }

    std::memcpy(output.data(), data.data(), data.size());

    return result;
}

DecompressResult
Decompressor::decompress_file(const std::string& input_path, const std::string& output_path) {
    DecompressResult result;

    // Memory-map input file
    core::MappedFileReader mapped_file;
    auto open_result = mapped_file.open(input_path);
    if (!open_result.ok()) {
        result.error = ErrorCode::IoError;
        return result;
    }

    // Decompress
    auto [data, decompress_result] = decompress(mapped_file.span());

    if (!decompress_result.ok()) {
        return decompress_result;
    }

    // Write output file
    std::ofstream out(output_path, std::ios::binary);
    if (!out) {
        decompress_result.error = ErrorCode::IoError;
        return decompress_result;
    }

    out.write(reinterpret_cast<const char*>(data.data()), data.size());

    if (!out) {
        decompress_result.error = ErrorCode::IoError;
        return decompress_result;
    }

    return decompress_result;
}

size_t Decompressor::get_original_size(ByteSpan input) {
    if (input.size() < sizeof(FileHeader)) {
        return 0;
    }

    const auto* header = reinterpret_cast<const FileHeader*>(input.data());

    // Validate magic
    if (header->magic[0] != 'C' || header->magic[1] != 'U' ||
        header->magic[2] != 'M' || header->magic[3] != 0x01) {
        return 0;
    }

    return header->original_size;
}

bool Decompressor::validate_header(ByteSpan input) {
    if (input.size() < sizeof(FileHeader)) {
        return false;
    }

    const auto* header = reinterpret_cast<const FileHeader*>(input.data());

    // Check magic
    if (header->magic[0] != 'C' || header->magic[1] != 'U' ||
        header->magic[2] != 'M' || header->magic[3] != 0x01) {
        return false;
    }

    // Check version
    if (header->version > config::FORMAT_VERSION) {
        return false;
    }

    return true;
}

} // namespace compressum
