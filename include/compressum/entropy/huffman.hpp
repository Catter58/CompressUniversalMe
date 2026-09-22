#pragma once

/**
 * CompressUM - Original Compression Algorithm
 * Canonical Huffman Coding
 *
 * Implements Huffman coding with canonical code assignment.
 * Canonical codes are sorted by length, making them efficient to store
 * (only code lengths needed) and fast to decode (table lookup).
 *
 * References:
 * - "Managing Gigabytes" by Witten, Moffat, Bell
 * - RFC 1951 (DEFLATE) for canonical Huffman
 */

#include "../config.hpp"
#include "../types.hpp"
#include "../core/bitstream.hpp"
#include <array>
#include <vector>
#include <algorithm>
#include <queue>

namespace compressum::entropy {

/**
 * Maximum code length (15 bits as in DEFLATE)
 */
inline constexpr size_t HUFFMAN_MAX_CODE_LEN = 15;

/**
 * Huffman code entry
 */
struct HuffmanCode {
    uint16_t code;      // The code bits (right-aligned)
    uint8_t length;     // Code length in bits

    [[nodiscard]] bool valid() const { return length > 0; }
};

/**
 * Huffman tree node for building the tree
 */
struct HuffmanNode {
    uint32_t frequency;
    int16_t symbol;     // -1 for internal nodes
    int16_t left;       // Index of left child (-1 if leaf)
    int16_t right;      // Index of right child (-1 if leaf)

    [[nodiscard]] bool is_leaf() const { return symbol >= 0; }
};

/**
 * Canonical Huffman Encoder
 */
class HuffmanEncoder {
public:
    static constexpr size_t MAX_SYMBOLS = 286;  // DEFLATE: 256 literals + 1 EOB + 29 lengths

    HuffmanEncoder() = default;

    /**
     * Build Huffman codes from frequency table
     * @param frequencies Symbol frequencies (index = symbol)
     * @param num_symbols Number of symbols
     */
    void build(const uint32_t* frequencies, size_t num_symbols) {
        num_symbols_ = std::min(num_symbols, MAX_SYMBOLS);

        // Reset codes
        for (auto& code : codes_) {
            code = {0, 0};
        }

        // Count non-zero frequencies
        std::vector<std::pair<uint32_t, uint16_t>> symbols;  // (freq, symbol)
        for (size_t i = 0; i < num_symbols_; ++i) {
            if (frequencies[i] > 0) {
                symbols.emplace_back(frequencies[i], static_cast<uint16_t>(i));
            }
        }

        if (symbols.empty()) {
            return;
        }

        if (symbols.size() == 1) {
            // Single symbol - assign code 0 with length 1
            codes_[symbols[0].second] = {0, 1};
            lengths_[symbols[0].second] = 1;  // Also set lengths_ for decoder
            return;
        }

        // Build Huffman tree using priority queue
        build_tree(symbols);

        // Convert tree to code lengths
        compute_code_lengths();

        // Limit code lengths to MAX_CODE_LEN
        limit_code_lengths();

        // Generate canonical codes
        generate_canonical_codes();
    }

    /**
     * Build from byte data
     */
    void build_from_data(ByteSpan data) {
        std::array<uint32_t, 256> freq{};
        for (Byte b : data) {
            ++freq[b];
        }
        build(freq.data(), 256);
    }

    /**
     * Encode a symbol to the bit stream
     */
    void encode(core::BitWriter& writer, uint16_t symbol) const {
        const auto& code = codes_[symbol];
        writer.write_bits(code.code, code.length);
    }

    /**
     * Encode multiple symbols
     */
    void encode(core::BitWriter& writer, const uint16_t* symbols, size_t count) const {
        for (size_t i = 0; i < count; ++i) {
            encode(writer, symbols[i]);
        }
    }

    /**
     * Get code for symbol
     */
    [[nodiscard]] HuffmanCode get_code(uint16_t symbol) const {
        return codes_[symbol];
    }

    /**
     * Get code lengths (for storing in compressed stream)
     */
    [[nodiscard]] const std::array<uint8_t, MAX_SYMBOLS>& get_lengths() const {
        return lengths_;
    }

    /**
     * Write code lengths to stream (for decoder)
     */
    void write_lengths(core::BitWriter& writer, size_t num_symbols) const {
        for (size_t i = 0; i < num_symbols; ++i) {
            writer.write_bits(lengths_[i], 4);  // 4 bits per length (max 15)
        }
    }

private:
    void build_tree(std::vector<std::pair<uint32_t, uint16_t>>& symbols) {
        // Priority queue: (frequency, node_index)
        auto cmp = [](const auto& a, const auto& b) { return a.first > b.first; };
        std::priority_queue<std::pair<uint32_t, int16_t>,
                           std::vector<std::pair<uint32_t, int16_t>>,
                           decltype(cmp)> pq(cmp);

        nodes_.clear();
        nodes_.reserve(symbols.size() * 2);

        // Create leaf nodes
        for (const auto& [freq, sym] : symbols) {
            int16_t idx = static_cast<int16_t>(nodes_.size());
            nodes_.push_back({freq, static_cast<int16_t>(sym), -1, -1});
            pq.emplace(freq, idx);
        }

        // Build tree bottom-up
        while (pq.size() > 1) {
            auto [freq1, idx1] = pq.top(); pq.pop();
            auto [freq2, idx2] = pq.top(); pq.pop();

            int16_t new_idx = static_cast<int16_t>(nodes_.size());
            nodes_.push_back({freq1 + freq2, -1, idx1, idx2});
            pq.emplace(freq1 + freq2, new_idx);
        }

        root_ = pq.top().second;
    }

    void compute_code_lengths() {
        lengths_.fill(0);
        if (nodes_.empty()) return;

        // DFS to compute depths
        compute_depth(root_, 0);
    }

    void compute_depth(int16_t node, uint8_t depth) {
        if (node < 0) return;

        const auto& n = nodes_[node];
        if (n.is_leaf()) {
            lengths_[n.symbol] = depth;
        } else {
            compute_depth(n.left, depth + 1);
            compute_depth(n.right, depth + 1);
        }
    }

    void limit_code_lengths() {
        // Length-limiting algorithm: limit all codes to MAX_CODE_LEN
        // Uses a simplified approach: clamp long codes and adjust short codes

        bool needs_fix = false;
        for (size_t i = 0; i < num_symbols_; ++i) {
            if (lengths_[i] > HUFFMAN_MAX_CODE_LEN) {
                needs_fix = true;
                break;
            }
        }

        if (!needs_fix) return;

        // Count symbols at each length (clamped to MAX_CODE_LEN)
        std::array<int32_t, HUFFMAN_MAX_CODE_LEN + 1> count{};
        for (size_t i = 0; i < num_symbols_; ++i) {
            if (lengths_[i] > 0) {
                size_t len = std::min<size_t>(lengths_[i], HUFFMAN_MAX_CODE_LEN);
                ++count[len];
            }
        }

        // Calculate Kraft sum: sum of 2^(-length) must be <= 1
        // Equivalently: sum of 2^(MAX_CODE_LEN - length) must be <= 2^MAX_CODE_LEN
        int32_t kraft = 0;
        for (size_t len = 1; len <= HUFFMAN_MAX_CODE_LEN; ++len) {
            kraft += count[len] << (HUFFMAN_MAX_CODE_LEN - len);
        }

        int32_t limit = 1 << HUFFMAN_MAX_CODE_LEN;

        // If Kraft sum exceeds limit, we need to increase some lengths
        // This happens when we clamped long codes
        while (kraft > limit) {
            // Increase shortest codes to make room
            for (size_t len = HUFFMAN_MAX_CODE_LEN - 1; len >= 1 && kraft > limit; --len) {
                while (count[len] > 0 && kraft > limit) {
                    --count[len];
                    ++count[len + 1];
                    kraft -= (1 << (HUFFMAN_MAX_CODE_LEN - len)) - (1 << (HUFFMAN_MAX_CODE_LEN - len - 1));
                }
            }
        }

        // Reassign lengths based on count
        // Sort symbols by original length, then assign new lengths
        std::vector<std::pair<uint8_t, uint16_t>> sorted;  // (length, symbol)
        for (size_t i = 0; i < num_symbols_; ++i) {
            if (lengths_[i] > 0) {
                sorted.emplace_back(lengths_[i], static_cast<uint16_t>(i));
            }
        }
        std::sort(sorted.begin(), sorted.end());

        size_t idx = 0;
        for (size_t len = 1; len <= HUFFMAN_MAX_CODE_LEN && idx < sorted.size(); ++len) {
            for (int32_t c = 0; c < count[len] && idx < sorted.size(); ++c) {
                lengths_[sorted[idx++].second] = static_cast<uint8_t>(len);
            }
        }
    }

    void generate_canonical_codes() {
        // Canonical Huffman: codes are assigned in order of:
        // 1. Code length (shorter first)
        // 2. Symbol value (smaller first for same length)

        // Count codes of each length
        std::array<uint32_t, HUFFMAN_MAX_CODE_LEN + 1> bl_count{};
        for (size_t i = 0; i < num_symbols_; ++i) {
            if (lengths_[i] > 0) {
                ++bl_count[lengths_[i]];
            }
        }

        // Find starting code for each length
        std::array<uint16_t, HUFFMAN_MAX_CODE_LEN + 1> next_code{};
        uint16_t code = 0;
        for (size_t bits = 1; bits <= HUFFMAN_MAX_CODE_LEN; ++bits) {
            code = (code + bl_count[bits - 1]) << 1;
            next_code[bits] = code;
        }

        // Assign codes
        for (size_t i = 0; i < num_symbols_; ++i) {
            uint8_t len = lengths_[i];
            if (len > 0) {
                codes_[i] = {next_code[len]++, len};
            }
        }
    }

    std::array<HuffmanCode, MAX_SYMBOLS> codes_{};
    std::array<uint8_t, MAX_SYMBOLS> lengths_{};
    std::vector<HuffmanNode> nodes_;
    int16_t root_ = -1;
    size_t num_symbols_ = 0;
};

/**
 * Canonical Huffman Decoder
 */
class HuffmanDecoder {
public:
    static constexpr size_t MAX_SYMBOLS = HuffmanEncoder::MAX_SYMBOLS;

    HuffmanDecoder() = default;

    /**
     * Build decoder from code lengths
     */
    void build(const uint8_t* lengths, size_t num_symbols) {
        num_symbols_ = std::min(num_symbols, MAX_SYMBOLS);

        // Copy lengths
        for (size_t i = 0; i < num_symbols_; ++i) {
            lengths_[i] = lengths[i];
        }
        for (size_t i = num_symbols_; i < MAX_SYMBOLS; ++i) {
            lengths_[i] = 0;
        }

        // Build lookup tables
        build_tables();
    }

    /**
     * Read code lengths from stream
     */
    void read_lengths(core::BitReader& reader, size_t num_symbols) {
        for (size_t i = 0; i < num_symbols && i < MAX_SYMBOLS; ++i) {
            lengths_[i] = static_cast<uint8_t>(reader.read_bits(4));
        }
        num_symbols_ = num_symbols;
        build_tables();
    }

    /**
     * Decode one symbol from bit stream
     */
    [[nodiscard]] uint16_t decode(core::BitReader& reader) const {
        // Fast path: use lookup table for short codes
        uint64_t bits = reader.peek_bits(TABLE_BITS);
        uint16_t idx = static_cast<uint16_t>(bits);

        const auto& entry = table_[idx];
        if (entry.length <= TABLE_BITS) {
            reader.skip_bits(entry.length);
            return entry.symbol;
        }

        // Slow path: decode bit by bit for long codes
        return decode_slow(reader);
    }

    /**
     * Decode multiple symbols
     */
    void decode(core::BitReader& reader, uint16_t* symbols, size_t count) const {
        for (size_t i = 0; i < count; ++i) {
            symbols[i] = decode(reader);
        }
    }

private:
    static constexpr size_t TABLE_BITS = 9;  // Lookup table for codes up to 9 bits
    static constexpr size_t TABLE_SIZE = 1 << TABLE_BITS;

    struct TableEntry {
        uint16_t symbol;
        uint8_t length;
    };

    void build_tables() {
        // Build first-level lookup table
        // Mark all entries as needing slow path initially (length = 255)
        for (auto& entry : table_) {
            entry = {0, 255};  // 255 = needs slow decode
        }

        // Count codes of each length
        std::array<uint32_t, HUFFMAN_MAX_CODE_LEN + 1> bl_count{};
        for (size_t i = 0; i < num_symbols_; ++i) {
            if (lengths_[i] > 0) {
                ++bl_count[lengths_[i]];
            }
        }

        // Over-subscribed lengths (corrupt input) cannot form a prefix code:
        // leave the decoder empty so every decode returns symbol 0
        sym_count_ = 0;
        uint64_t kraft = 0;
        for (size_t bits = 1; bits <= HUFFMAN_MAX_CODE_LEN; ++bits) {
            kraft += static_cast<uint64_t>(bl_count[bits]) << (HUFFMAN_MAX_CODE_LEN - bits);
        }
        if (kraft > (uint64_t{1} << HUFFMAN_MAX_CODE_LEN)) {
            return;
        }

        // Find starting code for each length
        std::array<uint16_t, HUFFMAN_MAX_CODE_LEN + 1> next_code{};
        uint16_t code = 0;
        for (size_t bits = 1; bits <= HUFFMAN_MAX_CODE_LEN; ++bits) {
            code = (code + bl_count[bits - 1]) << 1;
            next_code[bits] = code;
        }

        // Build symbols and codes arrays for slow decode
        for (size_t i = 0; i < num_symbols_; ++i) {
            uint8_t len = lengths_[i];
            if (len > 0) {
                uint16_t c = next_code[len]++;
                symbols_[sym_count_] = static_cast<uint16_t>(i);
                codes_[sym_count_] = c;
                code_lens_[sym_count_] = len;
                ++sym_count_;

                // Fill lookup table for fast decode
                if (len <= TABLE_BITS) {
                    // Replicate entry for all bit patterns with this prefix
                    size_t replicate = 1 << (TABLE_BITS - len);
                    size_t base = static_cast<size_t>(c) << (TABLE_BITS - len);
                    for (size_t j = 0; j < replicate; ++j) {
                        table_[base + j] = {static_cast<uint16_t>(i), len};
                    }
                }
            }
        }
    }

    [[nodiscard]] uint16_t decode_slow(core::BitReader& reader) const {
        // Linear search through codes (could use binary search)
        uint32_t bits = 0;
        for (uint8_t len = 1; len <= HUFFMAN_MAX_CODE_LEN; ++len) {
            bits = (bits << 1) | reader.read_bits(1);
            for (size_t i = 0; i < sym_count_; ++i) {
                if (code_lens_[i] == len && codes_[i] == bits) {
                    return symbols_[i];
                }
            }
        }
        return 0;  // Error: invalid code
    }

    std::array<uint8_t, MAX_SYMBOLS> lengths_{};
    std::array<TableEntry, TABLE_SIZE> table_{};

    // For slow path
    std::array<uint16_t, MAX_SYMBOLS> symbols_{};
    std::array<uint16_t, MAX_SYMBOLS> codes_{};
    std::array<uint8_t, MAX_SYMBOLS> code_lens_{};
    size_t sym_count_ = 0;
    size_t num_symbols_ = 0;
};

} // namespace compressum::entropy
