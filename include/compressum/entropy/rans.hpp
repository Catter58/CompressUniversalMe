#pragma once

/**
 * CompressUM - Original Compression Algorithm
 * rANS (Range Asymmetric Numeral Systems)
 *
 * Implements rANS entropy coder based on Jarek Duda's ANS theory.
 * rANS achieves compression ratio close to arithmetic coding
 * with speed similar to Huffman coding.
 *
 * Key formulas:
 * Encode: C(s,x) = (floor(x/freq(s)) << scale) + cumfreq(s) + (x mod freq(s))
 * Decode: D(x) = (s, freq(s) * (x >> scale) + (x & mask) - cumfreq(s))
 *
 * References:
 * - Jarek Duda, "Asymmetric numeral systems" (2009)
 * - Fabian Giesen, ryg_rans (public domain implementation)
 * - Used in: Zstandard, JPEG XL, Draco
 *
 * Source: https://github.com/rygorous/ryg_rans
 */

#include "../config.hpp"
#include "../types.hpp"
#include <array>
#include <vector>
#include <cstring>

namespace compressum::entropy {

/**
 * rANS parameters
 */
inline constexpr uint32_t RANS_BYTE_L = 1u << 23;      // Lower bound for renormalization
inline constexpr uint32_t RANS_PROB_BITS = 12;         // Probability scale bits
inline constexpr uint32_t RANS_PROB_SCALE = 1u << RANS_PROB_BITS;

/**
 * Symbol statistics for rANS
 * freq: Symbol frequency (scaled to RANS_PROB_SCALE)
 * cumfreq: Cumulative frequency of all symbols before this one
 */
struct RansSymbol {
    uint16_t freq;      // Frequency (probability * PROB_SCALE)
    uint16_t cumfreq;   // Cumulative frequency
};

/**
 * rANS state (32-bit)
 */
using RansState = uint32_t;

/**
 * rANS Symbol Table
 * Manages probability distribution for encoding/decoding
 */
class RansTable {
public:
    static constexpr size_t MAX_SYMBOLS = 256;

    RansTable() = default;

    /**
     * Build table from frequency counts
     * @param freq Frequency counts for each symbol
     * @param num_symbols Number of symbols
     */
    void build(const uint32_t* freq, size_t num_symbols) {
        num_symbols_ = std::min(num_symbols, MAX_SYMBOLS);

        // Calculate total frequency
        uint64_t total = 0;
        for (size_t i = 0; i < num_symbols_; ++i) {
            total += freq[i];
        }

        if (total == 0) {
            // Uniform distribution
            uint16_t per_symbol = static_cast<uint16_t>(RANS_PROB_SCALE / num_symbols_);
            uint16_t cumfreq = 0;
            for (size_t i = 0; i < num_symbols_; ++i) {
                symbols_[i].freq = per_symbol;
                symbols_[i].cumfreq = cumfreq;
                cumfreq += per_symbol;
            }
            return;
        }

        // Scale frequencies to RANS_PROB_SCALE
        // Ensure no symbol has freq = 0 if it appeared at least once
        uint32_t scaled_total = 0;
        for (size_t i = 0; i < num_symbols_; ++i) {
            if (freq[i] > 0) {
                // Scale with rounding, minimum 1
                uint32_t scaled = static_cast<uint32_t>(
                    (static_cast<uint64_t>(freq[i]) * RANS_PROB_SCALE + total / 2) / total
                );
                symbols_[i].freq = static_cast<uint16_t>(std::max(scaled, 1u));
            } else {
                symbols_[i].freq = 0;
            }
            scaled_total += symbols_[i].freq;
        }

        // Adjust to ensure total equals RANS_PROB_SCALE
        // Add/subtract from most frequent symbol
        int32_t diff = static_cast<int32_t>(RANS_PROB_SCALE) - static_cast<int32_t>(scaled_total);
        if (diff != 0) {
            // Find most frequent symbol
            size_t max_idx = 0;
            uint16_t max_freq = 0;
            for (size_t i = 0; i < num_symbols_; ++i) {
                if (symbols_[i].freq > max_freq) {
                    max_freq = symbols_[i].freq;
                    max_idx = i;
                }
            }
            symbols_[max_idx].freq = static_cast<uint16_t>(
                static_cast<int32_t>(symbols_[max_idx].freq) + diff
            );
        }

        // Compute cumulative frequencies
        uint16_t cumfreq = 0;
        for (size_t i = 0; i < num_symbols_; ++i) {
            symbols_[i].cumfreq = cumfreq;
            cumfreq += symbols_[i].freq;
        }

        // Build decode table
        build_decode_table();
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
     * Get symbol info for encoding
     */
    [[nodiscard]] const RansSymbol& get_symbol(uint8_t sym) const {
        return symbols_[sym];
    }

    /**
     * Decode symbol from cumulative frequency
     * Returns (symbol, freq, cumfreq)
     */
    [[nodiscard]] std::tuple<uint8_t, uint16_t, uint16_t>
    decode_symbol(uint16_t cumfreq_value) const {
        // Use decode table for O(1) lookup
        uint8_t sym = decode_table_[cumfreq_value];
        return {sym, symbols_[sym].freq, symbols_[sym].cumfreq};
    }

    /**
     * Write table to output (for storing in compressed stream)
     */
    void write_table(std::vector<Byte>& output) const {
        // Write frequencies as 12-bit values (packed)
        // Format: num_symbols (2 bytes, little-endian) + freq values
        output.push_back(static_cast<Byte>(num_symbols_ & 0xFF));
        output.push_back(static_cast<Byte>((num_symbols_ >> 8) & 0xFF));

        for (size_t i = 0; i < num_symbols_; i += 2) {
            uint16_t f1 = symbols_[i].freq;
            uint16_t f2 = (i + 1 < num_symbols_) ? symbols_[i + 1].freq : 0;

            // Pack two 12-bit values into 3 bytes
            output.push_back(static_cast<Byte>(f1 & 0xFF));
            output.push_back(static_cast<Byte>(((f1 >> 8) & 0x0F) | ((f2 & 0x0F) << 4)));
            output.push_back(static_cast<Byte>(f2 >> 4));
        }
    }

    /**
     * Read table from input
     */
    size_t read_table(ByteSpan input) {
        if (input.size() < 2) return 0;

        num_symbols_ = input[0] | (static_cast<size_t>(input[1]) << 8);
        size_t pos = 2;

        for (size_t i = 0; i < num_symbols_ && pos + 2 < input.size(); i += 2) {
            uint16_t f1 = input[pos] | ((input[pos + 1] & 0x0F) << 8);
            uint16_t f2 = ((input[pos + 1] >> 4) & 0x0F) | (input[pos + 2] << 4);

            symbols_[i].freq = f1;
            if (i + 1 < num_symbols_) {
                symbols_[i + 1].freq = f2;
            }
            pos += 3;
        }

        // Rebuild cumfreqs and decode table
        uint16_t cumfreq = 0;
        for (size_t i = 0; i < num_symbols_; ++i) {
            symbols_[i].cumfreq = cumfreq;
            cumfreq += symbols_[i].freq;
        }
        build_decode_table();

        return pos;
    }

private:
    void build_decode_table() {
        // Build lookup table: cumfreq -> symbol
        for (size_t i = 0; i < num_symbols_; ++i) {
            uint16_t start = symbols_[i].cumfreq;
            uint16_t end = start + symbols_[i].freq;
            for (uint16_t j = start; j < end && j < RANS_PROB_SCALE; ++j) {
                decode_table_[j] = static_cast<uint8_t>(i);
            }
        }
    }

    std::array<RansSymbol, MAX_SYMBOLS> symbols_{};
    std::array<uint8_t, RANS_PROB_SCALE> decode_table_{};
    size_t num_symbols_ = 0;
};

/**
 * rANS Encoder
 *
 * Encodes symbols in reverse order (LIFO) because rANS
 * naturally decodes in reverse of encoding order.
 */
class RansEncoder {
public:
    explicit RansEncoder(size_t reserve = 4096) {
        output_.reserve(reserve);
    }

    /**
     * Initialize encoder state
     */
    void init() {
        state_ = RANS_BYTE_L;
        output_.clear();
    }

    /**
     * Encode a symbol
     * @param sym Symbol info (freq, cumfreq)
     */
    void encode(const RansSymbol& sym) {
        // Guard against zero frequency (would cause infinite loop)
        uint32_t freq = sym.freq;
        if (freq == 0) {
            freq = 1;  // Treat as minimum frequency
        }

        // Renormalize: output bytes while state is too large
        // state >= (RANS_BYTE_L >> PROB_BITS) * freq
        uint32_t x_max = ((RANS_BYTE_L >> RANS_PROB_BITS) << 8) * freq;

        while (state_ >= x_max) {
            output_.push_back(static_cast<Byte>(state_ & 0xFF));
            state_ >>= 8;
        }

        // Encode step: C(s,x) = (x/freq)*M + cumfreq + (x mod freq)
        state_ = ((state_ / freq) << RANS_PROB_BITS) + sym.cumfreq + (state_ % freq);
    }

    /**
     * Encode using table
     */
    void encode(const RansTable& table, uint8_t symbol) {
        encode(table.get_symbol(symbol));
    }

    /**
     * Finish encoding - flush final state
     * Must be called before getting output
     */
    void finish() {
        // Write final state (4 bytes, big-endian so it becomes little-endian after reversal)
        output_.push_back(static_cast<Byte>((state_ >> 24) & 0xFF));
        output_.push_back(static_cast<Byte>((state_ >> 16) & 0xFF));
        output_.push_back(static_cast<Byte>((state_ >> 8) & 0xFF));
        output_.push_back(static_cast<Byte>(state_ & 0xFF));
    }

    /**
     * Get encoded output
     * NOTE: Output is in reverse order! Decoder reads from end.
     */
    [[nodiscard]] const std::vector<Byte>& output() const { return output_; }

    /**
     * Get reversed output (ready for storage)
     */
    [[nodiscard]] std::vector<Byte> get_output_reversed() const {
        std::vector<Byte> result(output_.rbegin(), output_.rend());
        return result;
    }

private:
    RansState state_ = RANS_BYTE_L;
    std::vector<Byte> output_;
};

/**
 * rANS Decoder
 */
class RansDecoder {
public:
    RansDecoder() = default;

    /**
     * Initialize decoder from compressed data
     * @param data Compressed data (must include 4-byte state at start)
     */
    void init(ByteSpan data) {
        data_ = data.data();
        pos_ = 0;
        end_ = data.size();

        // Read initial state (4 bytes, little-endian)
        if (end_ >= 4) {
            state_ = static_cast<uint32_t>(data_[0]) |
                    (static_cast<uint32_t>(data_[1]) << 8) |
                    (static_cast<uint32_t>(data_[2]) << 16) |
                    (static_cast<uint32_t>(data_[3]) << 24);
            pos_ = 4;
        } else {
            state_ = RANS_BYTE_L;
        }
    }

    /**
     * Decode one symbol
     * @param table Symbol table for decoding
     * @return Decoded symbol
     */
    [[nodiscard]] uint8_t decode(const RansTable& table) {
        // Get cumfreq from state
        uint16_t cumfreq_value = state_ & (RANS_PROB_SCALE - 1);

        // Lookup symbol
        auto [sym, freq, cumfreq] = table.decode_symbol(cumfreq_value);

        // Decode step: advance state
        // D(x) = freq * (x >> scale) + (x & mask) - cumfreq
        state_ = freq * (state_ >> RANS_PROB_BITS) + cumfreq_value - cumfreq;

        // Renormalize: read bytes while state is too small
        while (state_ < RANS_BYTE_L && pos_ < end_) {
            state_ = (state_ << 8) | data_[pos_++];
        }

        return sym;
    }

    /**
     * Decode multiple symbols
     */
    void decode(const RansTable& table, uint8_t* symbols, size_t count) {
        for (size_t i = 0; i < count; ++i) {
            symbols[i] = decode(table);
        }
    }

    /**
     * Check if more data available
     */
    [[nodiscard]] bool has_data() const {
        return pos_ < end_ || state_ >= RANS_BYTE_L;
    }

private:
    const Byte* data_ = nullptr;
    size_t pos_ = 0;
    size_t end_ = 0;
    RansState state_ = RANS_BYTE_L;
};

/**
 * Interleaved rANS (4 streams for better throughput)
 * Useful for SIMD optimization
 */
class InterleavedRansEncoder {
public:
    static constexpr size_t NUM_STREAMS = 4;

    void init() {
        for (auto& enc : encoders_) {
            enc.init();
        }
    }

    void encode(const RansTable& table, const uint8_t* symbols, size_t count) {
        // Encode symbols round-robin across streams
        // Process in reverse for correct decode order
        for (size_t i = count; i > 0; --i) {
            size_t stream = (i - 1) % NUM_STREAMS;
            encoders_[stream].encode(table, symbols[i - 1]);
        }
    }

    void finish() {
        for (auto& enc : encoders_) {
            enc.finish();
        }
    }

    [[nodiscard]] std::vector<Byte> get_output() const {
        std::vector<Byte> result;

        // Write stream sizes first
        for (const auto& enc : encoders_) {
            uint32_t size = static_cast<uint32_t>(enc.output().size());
            result.push_back(static_cast<Byte>(size & 0xFF));
            result.push_back(static_cast<Byte>((size >> 8) & 0xFF));
            result.push_back(static_cast<Byte>((size >> 16) & 0xFF));
            result.push_back(static_cast<Byte>((size >> 24) & 0xFF));
        }

        // Write streams (reversed)
        for (const auto& enc : encoders_) {
            auto reversed = enc.get_output_reversed();
            result.insert(result.end(), reversed.begin(), reversed.end());
        }

        return result;
    }

private:
    std::array<RansEncoder, NUM_STREAMS> encoders_;
};

class InterleavedRansDecoder {
public:
    static constexpr size_t NUM_STREAMS = 4;

    void init(ByteSpan data) {
        // Read stream sizes
        if (data.size() < NUM_STREAMS * 4) return;

        size_t offset = NUM_STREAMS * 4;
        for (size_t i = 0; i < NUM_STREAMS; ++i) {
            uint32_t size = static_cast<uint32_t>(data[i * 4]) |
                           (static_cast<uint32_t>(data[i * 4 + 1]) << 8) |
                           (static_cast<uint32_t>(data[i * 4 + 2]) << 16) |
                           (static_cast<uint32_t>(data[i * 4 + 3]) << 24);

            decoders_[i].init(ByteSpan(data.data() + offset, size));
            offset += size;
        }
        current_stream_ = 0;
    }

    [[nodiscard]] uint8_t decode(const RansTable& table) {
        uint8_t sym = decoders_[current_stream_].decode(table);
        current_stream_ = (current_stream_ + 1) % NUM_STREAMS;
        return sym;
    }

    void decode(const RansTable& table, uint8_t* symbols, size_t count) {
        for (size_t i = 0; i < count; ++i) {
            symbols[i] = decode(table);
        }
    }

private:
    std::array<RansDecoder, NUM_STREAMS> decoders_;
    size_t current_stream_ = 0;
};

} // namespace compressum::entropy
