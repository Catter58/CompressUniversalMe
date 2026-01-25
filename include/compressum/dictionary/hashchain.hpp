#pragma once

/**
 * CompressUM - Original Compression Algorithm
 * Hash Chain for LZ77 Match Finding
 *
 * Implements a hash chain data structure for efficient string matching
 * in dictionary compression. Uses a hash table with chained entries
 * pointing to previous occurrences of the same hash value.
 *
 * Time complexity: O(chain_length) per lookup
 * Space complexity: O(window_size)
 */

#include "../config.hpp"
#include "../types.hpp"

#if COMPRESSUM_USE_SSE42 && COMPRESSUM_ARCH_X86
    #include <nmmintrin.h>  // SSE4.2 intrinsics (x86 only)
#endif

#if COMPRESSUM_USE_AVX2 && COMPRESSUM_ARCH_X86
    #include <immintrin.h>  // AVX2 intrinsics (x86 only)
#endif

#if COMPRESSUM_USE_NEON && COMPRESSUM_ARCH_ARM
    #include <arm_neon.h>   // ARM NEON intrinsics
#endif

#include <array>
#include <cstring>
#include <algorithm>

namespace compressum::dict {

/**
 * Hash chain configuration
 */
struct HashChainConfig {
    size_t window_size = config::LZ77_WINDOW_SIZE;  // Sliding window size
    size_t hash_bits = 15;                          // Hash table size = 2^hash_bits
    size_t max_chain = config::LZ77_MAX_CHAIN;      // Maximum chain traversal depth
    size_t min_match = config::LZ77_MIN_MATCH;      // Minimum match length
    size_t max_match = config::LZ77_MAX_MATCH;      // Maximum match length
};

/**
 * Hash Chain Match Finder
 *
 * Maintains a hash table where each entry points to the most recent
 * position with that hash value. Each position also stores a link
 * to the previous position with the same hash (the "chain").
 */
class HashChain {
public:
    static constexpr uint32_t NIL = UINT32_MAX;  // End of chain marker

    explicit HashChain(const HashChainConfig& config = {})
        : config_(config)
        , hash_size_(1u << config.hash_bits)
        , hash_mask_(hash_size_ - 1)
    {
        head_.resize(hash_size_, NIL);
        prev_.resize(config.window_size, NIL);
    }

    /**
     * Reset hash chain to initial state
     */
    void reset() {
        std::fill(head_.begin(), head_.end(), NIL);
        std::fill(prev_.begin(), prev_.end(), NIL);
    }

    /**
     * Compute hash value for 3+ bytes
     * Uses CRC32 intrinsic if available for better distribution
     */
    [[nodiscard]] uint32_t hash(const Byte* data) const {
        #if COMPRESSUM_USE_SSE42
            // CRC32 gives excellent hash distribution
            return _mm_crc32_u32(0, *reinterpret_cast<const uint32_t*>(data)) & hash_mask_;
        #else
            // Fallback: simple multiplicative hash
            uint32_t h = data[0];
            h = (h << 5) ^ data[1];
            h = (h << 5) ^ data[2];
            return h & hash_mask_;
        #endif
    }

    /**
     * Insert position into hash chain
     * @param data Pointer to data buffer
     * @param pos Current position in buffer
     */
    void insert(const Byte* data, uint32_t pos) {
        uint32_t h = hash(data + pos);
        prev_[pos & (config_.window_size - 1)] = head_[h];
        head_[h] = pos;
    }

    /**
     * Insert multiple positions (for bulk processing)
     */
    void insert_range(const Byte* data, uint32_t start, uint32_t end) {
        for (uint32_t pos = start; pos < end; ++pos) {
            insert(data, pos);
        }
    }

    /**
     * Find best match for position
     * @param data Pointer to data buffer
     * @param pos Current position
     * @param max_len Maximum match length to search for
     * @return Best match found (distance, length)
     */
    [[nodiscard]] Match find_match(
        const Byte* data,
        uint32_t pos,
        uint32_t max_len
    ) const {
        Match best{0, 0};

        if (max_len < config_.min_match) {
            return best;
        }

        uint32_t h = hash(data + pos);
        uint32_t chain_pos = head_[h];
        uint32_t chain_count = 0;

        const uint32_t min_pos = (pos >= config_.window_size)
                                ? pos - static_cast<uint32_t>(config_.window_size) + 1
                                : 0;

        while (chain_pos != NIL && chain_pos >= min_pos && chain_count < config_.max_chain) {
            ++chain_count;

            // Prefetch next chain position data for reduced latency
            uint32_t next_chain_pos = prev_[chain_pos & (config_.window_size - 1)];
            if (next_chain_pos != NIL && next_chain_pos >= min_pos) {
                __builtin_prefetch(data + next_chain_pos, 0, 1);  // Read, low temporal locality
            }

            // Skip if this is the current position (can't match against ourselves)
            if (chain_pos >= pos) {
                chain_pos = next_chain_pos;
                continue;
            }

            // Quick check: compare first and last bytes
            if (data[chain_pos] == data[pos] &&
                data[chain_pos + best.length] == data[pos + best.length]) {

                uint32_t len = compare_strings(data + chain_pos, data + pos, max_len);

                if (len > best.length) {
                    best.distance = pos - chain_pos;
                    best.length = len;

                    // Early exit if we found maximum length match
                    if (len >= max_len) {
                        break;
                    }
                }
            }

            chain_pos = next_chain_pos;
        }

        // Only return match if it meets minimum length
        if (best.length < config_.min_match) {
            best.length = 0;
            best.distance = 0;
        }

        return best;
    }

    /**
     * Find match with lazy evaluation
     * Looks ahead one position to see if a better match exists
     * @return (current_match, next_match_is_better)
     */
    [[nodiscard]] std::pair<Match, bool> find_match_lazy(
        const Byte* data,
        uint32_t pos,
        uint32_t max_len,
        uint32_t data_size
    ) const {
        Match current = find_match(data, pos, max_len);

        if (current.length < config_.min_match || pos + 1 >= data_size) {
            return {current, false};
        }

        // Look ahead
        Match next = find_match(data, pos + 1, max_len);

        // Next is better if it's longer (with some bias toward current)
        bool next_better = next.length > current.length + 1;

        return {current, next_better};
    }

    /**
     * Get configuration
     */
    [[nodiscard]] const HashChainConfig& config() const { return config_; }

private:
    /**
     * Compare strings and return match length
     * Uses SIMD for fast comparison: AVX2 (32-byte), SSE4.2 (16-byte), NEON (16-byte)
     */
    [[nodiscard]] static uint32_t compare_strings(
        const Byte* s1,
        const Byte* s2,
        uint32_t max_len
    ) {
        uint32_t len = 0;

        #if COMPRESSUM_USE_AVX2 && COMPRESSUM_ARCH_X86 && defined(__x86_64__)
        // AVX2: 32-byte comparison (~1.5x faster than SSE4.2)
        while (len + 32 <= max_len) {
            __m256i v1 = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(s1 + len));
            __m256i v2 = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(s2 + len));
            __m256i cmp = _mm256_cmpeq_epi8(v1, v2);
            int mask = _mm256_movemask_epi8(cmp);

            if (mask != -1) {  // -1 = 0xFFFFFFFF = all 32 bytes match
                // Found mismatch - count trailing ones
                return len + static_cast<uint32_t>(__builtin_ctz(~static_cast<uint32_t>(mask)));
            }
            len += 32;
        }
        #endif

        #if COMPRESSUM_USE_SSE42 && COMPRESSUM_ARCH_X86 && defined(__x86_64__)
        // SSE4.2: 16-byte comparison
        while (len + 16 <= max_len) {
            __m128i v1 = _mm_loadu_si128(reinterpret_cast<const __m128i*>(s1 + len));
            __m128i v2 = _mm_loadu_si128(reinterpret_cast<const __m128i*>(s2 + len));
            __m128i cmp = _mm_cmpeq_epi8(v1, v2);
            int mask = _mm_movemask_epi8(cmp);

            if (mask != 0xFFFF) {
                // Found mismatch - count trailing ones
                return len + static_cast<uint32_t>(__builtin_ctz(~static_cast<uint32_t>(mask)));
            }
            len += 16;
        }
        #endif

        #if COMPRESSUM_USE_NEON && COMPRESSUM_ARCH_ARM
        // ARM NEON: 16-byte comparison
        while (len + 16 <= max_len) {
            uint8x16_t v1 = vld1q_u8(s1 + len);
            uint8x16_t v2 = vld1q_u8(s2 + len);
            uint8x16_t cmp = vceqq_u8(v1, v2);

            // Check if all bytes matched (all 0xFF)
            uint64x2_t cmp64 = vreinterpretq_u64_u8(cmp);
            uint64_t lo = vgetq_lane_u64(cmp64, 0);
            uint64_t hi = vgetq_lane_u64(cmp64, 1);

            if (lo != ~0ULL || hi != ~0ULL) {
                // Found mismatch - find first differing byte
                // Scalar search within the 16-byte chunk
                for (uint32_t i = 0; i < 16 && len + i < max_len; ++i) {
                    if (s1[len + i] != s2[len + i]) {
                        return len + i;
                    }
                }
            }
            len += 16;
        }
        #endif

        // Scalar comparison for remaining bytes
        while (len < max_len && s1[len] == s2[len]) {
            ++len;
        }

        return len;
    }

    HashChainConfig config_;
    size_t hash_size_;
    uint32_t hash_mask_;
    std::vector<uint32_t> head_;  // Hash table: hash -> most recent position
    std::vector<uint32_t> prev_;  // Chain links: position -> previous position with same hash
};

/**
 * Hash Chain with 4-byte hashing for better match quality
 */
class HashChain4 {
public:
    static constexpr uint32_t NIL = UINT32_MAX;
    static constexpr size_t HASH_SIZE = 1u << 16;  // 64K entries

    HashChain4() {
        head_.fill(NIL);
    }

    void reset() {
        head_.fill(NIL);
        prev_.clear();
    }

    [[nodiscard]] uint32_t hash(const Byte* data) const {
        #if COMPRESSUM_USE_SSE42
            return _mm_crc32_u32(0, *reinterpret_cast<const uint32_t*>(data)) & (HASH_SIZE - 1);
        #else
            uint32_t v = *reinterpret_cast<const uint32_t*>(data);
            v ^= v >> 16;
            v *= 0x85ebca6b;
            v ^= v >> 13;
            return v & (HASH_SIZE - 1);
        #endif
    }

    void insert(const Byte* data, uint32_t pos) {
        uint32_t h = hash(data + pos);

        // Grow prev_ array if needed
        if (pos >= prev_.size()) {
            prev_.resize(pos + 1, NIL);
        }

        prev_[pos] = head_[h];
        head_[h] = pos;
    }

    [[nodiscard]] Match find_match(
        const Byte* data,
        uint32_t pos,
        uint32_t max_len,
        uint32_t window_size,
        uint32_t max_chain = 64
    ) const {
        Match best{0, 0};

        if (max_len < 4 || pos >= prev_.size()) {
            return best;
        }

        uint32_t h = hash(data + pos);
        uint32_t chain_pos = head_[h];
        uint32_t chain_count = 0;
        uint32_t min_pos = (pos >= window_size) ? pos - window_size + 1 : 0;

        while (chain_pos != NIL && chain_pos >= min_pos && chain_count < max_chain) {
            ++chain_count;

            // Skip if this is the current position
            if (chain_pos >= pos) {
                if (chain_pos < prev_.size()) {
                    chain_pos = prev_[chain_pos];
                } else {
                    break;
                }
                continue;
            }

            // Compare 4 bytes at once
            if (*reinterpret_cast<const uint32_t*>(data + chain_pos) ==
                *reinterpret_cast<const uint32_t*>(data + pos)) {

                uint32_t len = 4;
                while (len < max_len && data[chain_pos + len] == data[pos + len]) {
                    ++len;
                }

                if (len > best.length) {
                    best.distance = pos - chain_pos;
                    best.length = len;

                    if (len >= max_len) break;
                }
            }

            if (chain_pos < prev_.size()) {
                chain_pos = prev_[chain_pos];
            } else {
                break;
            }
        }

        if (best.length < 4) {
            best = {0, 0};
        }

        return best;
    }

private:
    std::array<uint32_t, HASH_SIZE> head_;
    std::vector<uint32_t> prev_;
};

} // namespace compressum::dict
