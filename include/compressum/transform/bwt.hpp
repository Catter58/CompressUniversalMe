#pragma once

/**
 * CompressUM - Original Compression Algorithm
 * Legacy BWT + MTF + ZLE decoders
 *
 * Format v1 level 9 used BWT -> MTF -> ZLE before LZ77. The forward side did
 * not round-trip (suffix sort without sentinel, ZLE overflow on byte 255), so
 * it was removed and level 9 now uses context mixing (entropy/cm.hpp).
 * Only the inverse transforms remain, to read v1 files that did decode.
 */

#include "../config.hpp"
#include "../types.hpp"
#include <array>
#include <numeric>
#include <vector>

namespace compressum::transform {

/**
 * Inverse Burrows-Wheeler Transform
 */
class BWT {
public:
    /**
     * @param bwt Last column of sorted rotations
     * @param primary_index Row of the original string
     * @return Original data
     */
    [[nodiscard]] std::vector<Byte> inverse(ByteSpan bwt, uint32_t primary_index) const {
        if (bwt.empty() || primary_index >= bwt.size()) {
            return {};  // Corrupt input: caller's size check reports it
        }

        const size_t n = bwt.size();

        // Count occurrences of each byte
        std::array<uint32_t, 256> count{};
        for (Byte b : bwt) {
            ++count[b];
        }

        // Cumulative count (first occurrence position after sorting)
        std::array<uint32_t, 256> cumulative{};
        uint32_t sum = 0;
        for (size_t i = 0; i < 256; ++i) {
            cumulative[i] = sum;
            sum += count[i];
        }

        // Build transformation vector
        // T[i] = position in sorted order where bwt[i] goes
        std::vector<uint32_t> T(n);
        std::array<uint32_t, 256> pos = cumulative;
        for (size_t i = 0; i < n; ++i) {
            T[i] = pos[bwt[i]]++;
        }

        // Reconstruct original string
        std::vector<Byte> output(n);
        uint32_t idx = primary_index;
        for (size_t i = n; i > 0; --i) {
            output[i - 1] = bwt[idx];
            idx = T[idx];
        }

        return output;
    }
};

/**
 * Inverse Move-To-Front transform
 */
class MTF {
public:
    [[nodiscard]] std::vector<Byte> inverse(ByteSpan input) const {
        std::vector<Byte> output(input.size());

        std::array<Byte, 256> list;
        std::iota(list.begin(), list.end(), 0);

        for (size_t i = 0; i < input.size(); ++i) {
            size_t pos = input[i];
            Byte b = list[pos];
            output[i] = b;

            // Move to front
            for (size_t j = pos; j > 0; --j) {
                list[j] = list[j - 1];
            }
            list[0] = b;
        }

        return output;
    }
};

/**
 * Zero-Length Encoding decoder: zero runs in bijective base-2 (RUNA=0, RUNB=1),
 * other bytes stored as value + 1
 */
class ZLE {
public:
    /// Blocks are at most 2^24 bytes (24-bit size in the block header)
    static constexpr size_t MAX_OUTPUT = size_t{1} << 24;

    [[nodiscard]] std::vector<Byte> decode(ByteSpan input) const {
        std::vector<Byte> output;
        output.reserve(input.size() * 2);

        size_t i = 0;
        while (i < input.size()) {
            if (input[i] <= 1) {
                // Decode run of zeros using bijective base-2
                size_t run = 0;
                size_t power = 1;

                while (i < input.size() && input[i] <= 1) {
                    run += (input[i] + 1) * power;
                    power *= 2;
                    ++i;
                    if (output.size() + run > MAX_OUTPUT) {
                        return {};  // Corrupt input: no valid block is this large
                    }
                }

                // Output 'run' zeros
                for (size_t j = 0; j < run; ++j) {
                    output.push_back(0);
                }
            } else {
                // Non-zero: shift back
                output.push_back(static_cast<Byte>(input[i] - 1));
                ++i;
            }
        }

        return output;
    }
};

} // namespace compressum::transform
