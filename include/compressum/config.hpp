#pragma once

/**
 * CompressUM - Original Compression Algorithm
 * Configuration and compile-time settings
 */

#include <cstddef>
#include <cstdint>

namespace compressum::config {

// ============================================================================
// Version
// ============================================================================
inline constexpr uint16_t VERSION_MAJOR = 1;
inline constexpr uint16_t VERSION_MINOR = 0;
inline constexpr uint16_t VERSION_PATCH = 0;

// ============================================================================
// File Format
// ============================================================================
inline constexpr uint8_t MAGIC[4] = {'C', 'U', 'M', 0x01};
inline constexpr uint16_t FORMAT_VERSION = 1;

// ============================================================================
// Compression Parameters
// ============================================================================

// Block sizes
inline constexpr size_t MIN_BLOCK_SIZE = 4 * 1024;           // 4 KB
inline constexpr size_t DEFAULT_BLOCK_SIZE = 256 * 1024;     // 256 KB
inline constexpr size_t MAX_BLOCK_SIZE = 4 * 1024 * 1024;    // 4 MB

// LZ77 parameters
inline constexpr size_t LZ77_MIN_MATCH = 3;
inline constexpr size_t LZ77_MAX_MATCH = 258;
inline constexpr size_t LZ77_WINDOW_SIZE = 32 * 1024;        // 32 KB
inline constexpr size_t LZ77_HASH_SIZE = 32768;              // 2^15
inline constexpr size_t LZ77_MAX_CHAIN = 64;                 // Max hash chain length

// Huffman parameters
inline constexpr size_t HUFFMAN_MAX_BITS = 15;               // Max code length
inline constexpr size_t HUFFMAN_LITERAL_SYMBOLS = 286;       // 0-255 literals + 256 end + 29 lengths
inline constexpr size_t HUFFMAN_DISTANCE_SYMBOLS = 30;

// rANS parameters
inline constexpr uint32_t RANS_L = 1u << 23;                 // Lower bound of state
inline constexpr uint32_t RANS_SCALE_BITS = 12;              // Probability precision
inline constexpr uint32_t RANS_SCALE = 1u << RANS_SCALE_BITS;

// Arithmetic coding parameters
inline constexpr uint64_t ARITH_TOP = 1ull << 32;
inline constexpr uint64_t ARITH_FIRST_QUARTER = ARITH_TOP / 4;
inline constexpr uint64_t ARITH_HALF = ARITH_TOP / 2;
inline constexpr uint64_t ARITH_THIRD_QUARTER = 3 * ARITH_TOP / 4;

// ============================================================================
// Threading
// ============================================================================
inline constexpr size_t DEFAULT_THREADS = 0;                 // 0 = auto-detect
inline constexpr size_t MAX_THREADS = 64;

// ============================================================================
// Memory
// ============================================================================
inline constexpr size_t DEFAULT_MEMORY_LIMIT = 64 * 1024 * 1024;  // 64 MB
inline constexpr size_t ARENA_CHUNK_SIZE = 1024 * 1024;           // 1 MB

// ============================================================================
// Quality Thresholds (for future lossy modes)
// ============================================================================
inline constexpr float MIN_SSIM_JPEG = 0.92f;
inline constexpr float MIN_SSIM_PNG = 0.95f;
inline constexpr float MAX_SIZE_INCREASE = 1.05f;

// ============================================================================
// Architecture Detection
// ============================================================================
#if defined(__x86_64__) || defined(_M_X64) || defined(__i386__) || defined(_M_IX86)
    #define COMPRESSUM_ARCH_X86 1
    #define COMPRESSUM_ARCH_ARM 0
#elif defined(__aarch64__) || defined(_M_ARM64) || defined(__arm__) || defined(_M_ARM)
    #define COMPRESSUM_ARCH_X86 0
    #define COMPRESSUM_ARCH_ARM 1
#else
    #define COMPRESSUM_ARCH_X86 0
    #define COMPRESSUM_ARCH_ARM 0
#endif

// ============================================================================
// SIMD Detection Macros (x86 only)
// ============================================================================
#if COMPRESSUM_ARCH_X86
    #if defined(COMPRESSUM_HAS_SSE42) || defined(__SSE4_2__)
        #define COMPRESSUM_USE_SSE42 1
    #else
        #define COMPRESSUM_USE_SSE42 0
    #endif

    #if defined(COMPRESSUM_HAS_AVX2) || defined(__AVX2__)
        #define COMPRESSUM_USE_AVX2 1
    #else
        #define COMPRESSUM_USE_AVX2 0
    #endif
#else
    // Non-x86 architecture - no SSE/AVX
    #define COMPRESSUM_USE_SSE42 0
    #define COMPRESSUM_USE_AVX2 0
#endif

// ARM NEON detection
#if COMPRESSUM_ARCH_ARM
    #if defined(__ARM_NEON) || defined(__ARM_NEON__)
        #define COMPRESSUM_USE_NEON 1
    #else
        #define COMPRESSUM_USE_NEON 0
    #endif
#else
    #define COMPRESSUM_USE_NEON 0
#endif

// ARM CRC32 intrinsics detection (ARMv8 with CRC extension)
#if COMPRESSUM_ARCH_ARM
    #if defined(__ARM_FEATURE_CRC32)
        #define COMPRESSUM_USE_ARM_CRC32 1
    #else
        #define COMPRESSUM_USE_ARM_CRC32 0
    #endif
#else
    #define COMPRESSUM_USE_ARM_CRC32 0
#endif

} // namespace compressum::config
