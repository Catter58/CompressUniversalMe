#pragma once

/**
 * CompressUM - Original Compression Algorithm
 * CRC32-C implementation with SSE4.2 hardware acceleration
 *
 * CRC32-C uses polynomial 0x1EDC6F41 (Castagnoli)
 * Used by: iSCSI, SCTP, ext4, Btrfs
 *
 * When SSE4.2 is available, uses _mm_crc32_u8/u32/u64 intrinsics.
 * Falls back to table-based implementation otherwise.
 */

#include "../config.hpp"
#include "../types.hpp"

#if COMPRESSUM_USE_SSE42 && COMPRESSUM_ARCH_X86
    #include <nmmintrin.h>  // SSE4.2 intrinsics (x86 only)
#endif

#if COMPRESSUM_USE_ARM_CRC32 && COMPRESSUM_ARCH_ARM
    #include <arm_acle.h>   // ARM CRC32 intrinsics
#endif

namespace compressum::core {

/**
 * CRC32-C lookup table for software fallback
 * Generated using polynomial 0x82F63B78 (reflected form of 0x1EDC6F41)
 */
class CRC32Table {
public:
    constexpr CRC32Table() : table_{} {
        for (uint32_t i = 0; i < 256; ++i) {
            uint32_t crc = i;
            for (int j = 0; j < 8; ++j) {
                crc = (crc >> 1) ^ ((crc & 1) ? 0x82F63B78u : 0);
            }
            table_[i] = crc;
        }
    }

    [[nodiscard]] constexpr uint32_t operator[](size_t i) const {
        return table_[i];
    }

private:
    uint32_t table_[256];
};

// Compile-time generated CRC table
inline constexpr CRC32Table CRC32_TABLE{};

/**
 * Software CRC32-C implementation (fallback)
 */
[[nodiscard]] inline uint32_t crc32c_software(uint32_t crc, ByteSpan data) {
    crc = ~crc;
    for (Byte byte : data) {
        crc = CRC32_TABLE[(crc ^ byte) & 0xFF] ^ (crc >> 8);
    }
    return ~crc;
}

/**
 * Software CRC32-C for single byte
 */
[[nodiscard]] inline uint32_t crc32c_software(uint32_t crc, Byte byte) {
    crc = ~crc;
    crc = CRC32_TABLE[(crc ^ byte) & 0xFF] ^ (crc >> 8);
    return ~crc;
}

#if COMPRESSUM_USE_SSE42

/**
 * Hardware-accelerated CRC32-C using SSE4.2
 * Processes 8 bytes at a time when possible
 */
[[nodiscard]] inline uint32_t crc32c_hardware(uint32_t crc, ByteSpan data) {
    const Byte* ptr = data.data();
    size_t len = data.size();

    crc = ~crc;

    // Process 8 bytes at a time on 64-bit systems
    #if defined(__x86_64__) || defined(_M_X64)
    while (len >= 8) {
        crc = static_cast<uint32_t>(_mm_crc32_u64(crc, *reinterpret_cast<const uint64_t*>(ptr)));
        ptr += 8;
        len -= 8;
    }
    #endif

    // Process 4 bytes at a time
    while (len >= 4) {
        crc = _mm_crc32_u32(crc, *reinterpret_cast<const uint32_t*>(ptr));
        ptr += 4;
        len -= 4;
    }

    // Process remaining bytes
    while (len > 0) {
        crc = _mm_crc32_u8(crc, *ptr);
        ++ptr;
        --len;
    }

    return ~crc;
}

/**
 * Hardware CRC32-C for single byte
 */
[[nodiscard]] inline uint32_t crc32c_hardware(uint32_t crc, Byte byte) {
    return ~_mm_crc32_u8(~crc, byte);
}

#endif // COMPRESSUM_USE_SSE42

#if COMPRESSUM_USE_ARM_CRC32

/**
 * Hardware-accelerated CRC32-C using ARM CRC32 intrinsics
 * Available on ARMv8 with CRC extension (Apple Silicon, modern ARM64)
 * Processes 8 bytes at a time when possible (5-10x faster than software)
 */
[[nodiscard]] inline uint32_t crc32c_hardware(uint32_t crc, ByteSpan data) {
    const Byte* ptr = data.data();
    size_t len = data.size();

    crc = ~crc;

    // Process 8 bytes at a time (64-bit CRC)
    while (len >= 8) {
        crc = __crc32cd(crc, *reinterpret_cast<const uint64_t*>(ptr));
        ptr += 8;
        len -= 8;
    }

    // Process 4 bytes at a time
    while (len >= 4) {
        crc = __crc32cw(crc, *reinterpret_cast<const uint32_t*>(ptr));
        ptr += 4;
        len -= 4;
    }

    // Process 2 bytes at a time
    while (len >= 2) {
        crc = __crc32ch(crc, *reinterpret_cast<const uint16_t*>(ptr));
        ptr += 2;
        len -= 2;
    }

    // Process remaining byte
    if (len > 0) {
        crc = __crc32cb(crc, *ptr);
    }

    return ~crc;
}

/**
 * Hardware CRC32-C for single byte (ARM)
 */
[[nodiscard]] inline uint32_t crc32c_hardware(uint32_t crc, Byte byte) {
    return ~__crc32cb(~crc, byte);
}

#endif // COMPRESSUM_USE_ARM_CRC32

/**
 * CRC32-C calculator class
 * Automatically uses hardware acceleration when available
 */
class CRC32 {
public:
    CRC32() = default;

    /**
     * Update CRC with data
     */
    void update(ByteSpan data) {
        #if COMPRESSUM_USE_SSE42 || COMPRESSUM_USE_ARM_CRC32
            crc_ = crc32c_hardware(crc_, data);
        #else
            crc_ = crc32c_software(crc_, data);
        #endif
    }

    /**
     * Update CRC with single byte
     */
    void update(Byte byte) {
        #if COMPRESSUM_USE_SSE42 || COMPRESSUM_USE_ARM_CRC32
            crc_ = crc32c_hardware(crc_, byte);
        #else
            crc_ = crc32c_software(crc_, byte);
        #endif
    }

    /**
     * Get current CRC value
     */
    [[nodiscard]] uint32_t value() const { return crc_; }

    /**
     * Reset CRC to initial state
     */
    void reset() { crc_ = 0; }

    /**
     * Combine two CRC values
     * Used for parallel CRC computation
     */
    static uint32_t combine(uint32_t crc1, uint32_t crc2, size_t len2);

private:
    uint32_t crc_ = 0;
};

/**
 * One-shot CRC32-C computation
 */
[[nodiscard]] inline uint32_t crc32c(ByteSpan data) {
    CRC32 crc;
    crc.update(data);
    return crc.value();
}

/**
 * CRC32-C with initial value
 */
[[nodiscard]] inline uint32_t crc32c(uint32_t initial, ByteSpan data) {
    #if COMPRESSUM_USE_SSE42 || COMPRESSUM_USE_ARM_CRC32
        return crc32c_hardware(initial, data);
    #else
        return crc32c_software(initial, data);
    #endif
}

/**
 * Check if hardware CRC is available (runtime check)
 */
[[nodiscard]] inline bool has_hardware_crc32() {
    #if COMPRESSUM_USE_SSE42 || COMPRESSUM_USE_ARM_CRC32
        return true;  // Compiled with hardware CRC support
    #else
        return false;
    #endif
}

} // namespace compressum::core
