#pragma once

/**
 * CompressUM - Original Compression Algorithm
 * Burrows-Wheeler Transform (BWT)
 *
 * BWT reorders input bytes to group similar contexts together,
 * which greatly improves compression with entropy coders.
 *
 * Algorithm:
 * 1. Create all rotations of input string
 * 2. Sort rotations lexicographically
 * 3. Output is the last column of sorted rotations
 * 4. Store original string's position for inverse transform
 *
 * Time: O(n log n) with suffix array
 * Space: O(n)
 *
 * Used in: bzip2, 7-Zip
 */

#include "../config.hpp"
#include "../types.hpp"
#include <vector>
#include <algorithm>
#include <numeric>

namespace compressum::transform {

/**
 * BWT Configuration
 */
struct BWTConfig {
    size_t block_size = 900000;  // Default bzip2 block size
};

/**
 * BWT Result
 */
struct BWTResult {
    std::vector<Byte> data;
    uint32_t primary_index;  // Position of original string in sorted rotations
};

/**
 * Burrows-Wheeler Transform
 */
class BWT {
public:
    explicit BWT(const BWTConfig& config = {})
        : config_(config)
    {}

    /**
     * Forward BWT transform
     * @param input Input data
     * @return Transformed data and primary index
     */
    [[nodiscard]] BWTResult transform(ByteSpan input) const {
        if (input.empty()) {
            return {{}, 0};
        }

        const size_t n = input.size();

        // Build suffix array using induced sorting
        std::vector<int32_t> suffix_array = build_suffix_array(input);

        // Extract last column (BWT output)
        std::vector<Byte> output(n);
        uint32_t primary_index = 0;

        for (size_t i = 0; i < n; ++i) {
            if (suffix_array[i] == 0) {
                primary_index = static_cast<uint32_t>(i);
                output[i] = input[n - 1];
            } else {
                output[i] = input[suffix_array[i] - 1];
            }
        }

        return {std::move(output), primary_index};
    }

    /**
     * Inverse BWT transform
     * @param bwt Transformed data
     * @param primary_index Position of original string
     * @return Original data
     */
    [[nodiscard]] std::vector<Byte> inverse(ByteSpan bwt, uint32_t primary_index) const {
        if (bwt.empty()) {
            return {};
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

private:
    /**
     * Build suffix array using simple doubling algorithm
     * For production, use DC3/SA-IS for O(n) complexity
     */
    [[nodiscard]] std::vector<int32_t> build_suffix_array(ByteSpan input) const {
        const size_t n = input.size();

        // Initialize suffix array with indices
        std::vector<int32_t> sa(n);
        std::iota(sa.begin(), sa.end(), 0);

        // Rank array
        std::vector<int32_t> rank(n);
        for (size_t i = 0; i < n; ++i) {
            rank[i] = input[i];
        }

        // Doubling algorithm
        std::vector<int32_t> tmp(n);
        for (size_t k = 1; k < n; k *= 2) {
            // Sort by (rank[i], rank[i+k])
            auto compare = [&](int32_t a, int32_t b) {
                if (rank[a] != rank[b]) return rank[a] < rank[b];
                int32_t ra = (a + k < n) ? rank[a + k] : -1;
                int32_t rb = (b + k < n) ? rank[b + k] : -1;
                return ra < rb;
            };

            std::sort(sa.begin(), sa.end(), compare);

            // Update ranks
            tmp[sa[0]] = 0;
            for (size_t i = 1; i < n; ++i) {
                tmp[sa[i]] = tmp[sa[i - 1]];
                if (compare(sa[i - 1], sa[i])) {
                    ++tmp[sa[i]];
                }
            }
            rank = tmp;

            // Check if all ranks are unique
            if (rank[sa[n - 1]] == static_cast<int32_t>(n - 1)) {
                break;
            }
        }

        return sa;
    }

    BWTConfig config_;
};

/**
 * Move-To-Front transform
 * Often used after BWT to further improve compression
 *
 * Each byte is encoded as its position in a list of recently seen bytes.
 * The list is updated by moving the seen byte to the front.
 *
 * Result: repeated contexts become sequences of small numbers (mostly 0s)
 */
class MTF {
public:
    /**
     * Forward MTF transform
     */
    [[nodiscard]] std::vector<Byte> transform(ByteSpan input) const {
        std::vector<Byte> output(input.size());

        // Initialize symbol list (0-255)
        std::array<Byte, 256> list;
        std::iota(list.begin(), list.end(), 0);

        for (size_t i = 0; i < input.size(); ++i) {
            Byte b = input[i];

            // Find position of b in list
            size_t pos = 0;
            while (list[pos] != b) {
                ++pos;
            }

            output[i] = static_cast<Byte>(pos);

            // Move to front
            for (size_t j = pos; j > 0; --j) {
                list[j] = list[j - 1];
            }
            list[0] = b;
        }

        return output;
    }

    /**
     * Inverse MTF transform
     */
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
 * Zero-Length Encoding for MTF output
 * Encodes runs of zeros efficiently (common after MTF)
 */
class ZLE {
public:
    /**
     * Encode runs of zeros using bijective base-2 numeration
     * RUNA (0) and RUNB (1) represent values 1 and 2 respectively
     * at each position with weight 2^pos
     */
    [[nodiscard]] std::vector<Byte> encode(ByteSpan input) const {
        std::vector<Byte> output;
        output.reserve(input.size());

        size_t i = 0;
        while (i < input.size()) {
            if (input[i] == 0) {
                // Count run of zeros
                size_t run = 0;
                while (i < input.size() && input[i] == 0) {
                    ++run;
                    ++i;
                }

                // Encode run using bijective base-2 numeration
                // run = sum over pos of (symbol[pos]+1) * 2^pos
                size_t n = run;
                while (n > 0) {
                    --n;  // Convert to 0-based for this iteration
                    output.push_back(static_cast<Byte>(n & 1));
                    n >>= 1;
                }
            } else {
                // Non-zero: output as-is (shifted by 2 to make room for RUNA/RUNB)
                output.push_back(static_cast<Byte>(input[i] + 1));
                ++i;
            }
        }

        return output;
    }

    /**
     * Decode zero runs
     */
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
