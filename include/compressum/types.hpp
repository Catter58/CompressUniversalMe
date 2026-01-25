#pragma once

/**
 * CompressUM - Original Compression Algorithm
 * Common types and data structures
 */

#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>
#include <variant>
#include <array>

namespace compressum {

// ============================================================================
// Basic Types
// ============================================================================
using Byte = uint8_t;
using ByteSpan = std::span<const Byte>;
using MutableByteSpan = std::span<Byte>;

// ============================================================================
// Error Handling (no exceptions)
// ============================================================================
enum class ErrorCode : uint8_t {
    Ok = 0,
    InvalidInput,
    InvalidFormat,
    CorruptedData,
    ChecksumMismatch,
    OutputTooSmall,
    OutOfMemory,
    IoError,
    UnsupportedVersion,
    InternalError
};

inline constexpr const char* error_message(ErrorCode code) {
    switch (code) {
        case ErrorCode::Ok:               return "Success";
        case ErrorCode::InvalidInput:     return "Invalid input";
        case ErrorCode::InvalidFormat:    return "Invalid file format";
        case ErrorCode::CorruptedData:    return "Corrupted data";
        case ErrorCode::ChecksumMismatch: return "Checksum mismatch";
        case ErrorCode::OutputTooSmall:   return "Output buffer too small";
        case ErrorCode::OutOfMemory:      return "Out of memory";
        case ErrorCode::IoError:          return "I/O error";
        case ErrorCode::UnsupportedVersion: return "Unsupported format version";
        case ErrorCode::InternalError:    return "Internal error";
    }
    return "Unknown error";
}

template<typename T>
struct Result {
    T value;
    ErrorCode error;

    [[nodiscard]] bool ok() const { return error == ErrorCode::Ok; }
    [[nodiscard]] explicit operator bool() const { return ok(); }

    static Result success(T val) { return {val, ErrorCode::Ok}; }
    static Result failure(ErrorCode err) { return {{}, err}; }
};

template<>
struct Result<void> {
    ErrorCode error;

    [[nodiscard]] bool ok() const { return error == ErrorCode::Ok; }
    [[nodiscard]] explicit operator bool() const { return ok(); }

    static Result success() { return {ErrorCode::Ok}; }
    static Result failure(ErrorCode err) { return {err}; }
};

// ============================================================================
// Compression Level
// ============================================================================
enum class Level : uint8_t {
    Fast = 1,      // Fastest compression, lower ratio
    Normal = 5,    // Balanced speed/ratio
    Best = 9       // Best compression, slower
};

// ============================================================================
// Data Type Detection
// ============================================================================
enum class DataType : uint8_t {
    Unknown = 0,
    Text,           // ASCII/UTF-8 text
    Binary,         // Generic binary
    Executable,     // PE/ELF/Mach-O executable
    Structured,     // JSON, XML, etc.
    Compressed,     // Already compressed data
    Media           // Images, audio, video
};

// ============================================================================
// Preprocessor Flags (stored in block header)
// ============================================================================
enum class Preprocessor : uint8_t {
    None = 0,
    Delta = 1 << 0,      // Delta encoding applied
    BWT = 1 << 1,        // Burrows-Wheeler Transform
    BCJ = 1 << 2,        // Executable filter (x86/x64)
    RLE = 1 << 3,        // Run-length encoding for BWT output
    JPEG = 1 << 4,       // JPEG Huffman optimization
    PNG = 1 << 5         // PNG filter optimization
};

// ============================================================================
// Media Format Detection (for format-specific optimization)
// ============================================================================
enum class MediaFormat : uint8_t {
    Unknown = 0,
    JPEG,        // JPEG/JFIF image
    PNG,         // PNG image
    GIF,         // GIF image
    WebP,        // WebP image
    BMP,         // BMP image
    TIFF         // TIFF image
};

inline Preprocessor operator|(Preprocessor a, Preprocessor b) {
    return static_cast<Preprocessor>(static_cast<uint8_t>(a) | static_cast<uint8_t>(b));
}

inline Preprocessor operator&(Preprocessor a, Preprocessor b) {
    return static_cast<Preprocessor>(static_cast<uint8_t>(a) & static_cast<uint8_t>(b));
}

inline bool has_flag(Preprocessor flags, Preprocessor flag) {
    return (static_cast<uint8_t>(flags) & static_cast<uint8_t>(flag)) != 0;
}

// ============================================================================
// LZ77 Match
// ============================================================================
struct Match {
    uint32_t distance;   // Offset back in the window
    uint32_t length;     // Match length

    [[nodiscard]] bool valid() const { return length >= 3; }
};

// ============================================================================
// Data Statistics (from analyzer)
// ============================================================================
struct DataStats {
    size_t size;                           // Total size in bytes
    double entropy;                        // Shannon entropy (0.0 - 8.0)
    DataType type;                         // Detected data type
    MediaFormat media_format;              // Specific media format (if type == Media)
    std::array<uint32_t, 256> histogram;   // Byte frequency histogram

    // Derived metrics
    [[nodiscard]] double compression_estimate() const {
        return entropy / 8.0;  // Theoretical minimum compression ratio
    }

    [[nodiscard]] bool is_compressible() const {
        return entropy < 7.5;  // High entropy data won't compress well
    }
};

// ============================================================================
// File Format Structures
// ============================================================================
#pragma pack(push, 1)

struct FileHeader {
    uint8_t magic[4];           // "CUM\x01"
    uint16_t version;           // Format version
    uint16_t flags;             // Global flags
    uint64_t original_size;     // Original file size
    uint64_t compressed_size;   // Compressed data size
    uint32_t crc32;             // CRC32 of original data
    uint32_t block_count;       // Number of blocks
};
static_assert(sizeof(FileHeader) == 32, "FileHeader must be 32 bytes");

struct BlockEntry {
    uint32_t compressed_size : 24;   // Compressed block size (max 16 MB)
    uint32_t original_size : 24;     // Original block size (max 16 MB)
    uint8_t preprocessor;            // Preprocessor flags
    uint8_t level;                   // Compression level used
};

struct BlockHeader {
    uint8_t compressed_size[3];   // 24-bit compressed size
    uint8_t original_size[3];     // 24-bit original size
    uint8_t preprocessor;         // Preprocessor flags
    uint8_t level;                // Compression level
};
static_assert(sizeof(BlockHeader) == 8, "BlockHeader must be 8 bytes");

#pragma pack(pop)

// Helper functions for 24-bit values
inline uint32_t read_uint24(const uint8_t* p) {
    return static_cast<uint32_t>(p[0]) |
           (static_cast<uint32_t>(p[1]) << 8) |
           (static_cast<uint32_t>(p[2]) << 16);
}

inline void write_uint24(uint8_t* p, uint32_t value) {
    p[0] = static_cast<uint8_t>(value & 0xFF);
    p[1] = static_cast<uint8_t>((value >> 8) & 0xFF);
    p[2] = static_cast<uint8_t>((value >> 16) & 0xFF);
}

} // namespace compressum
