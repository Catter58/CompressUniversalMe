#pragma once

/**
 * CompressUM - Original Compression Algorithm
 * Arithmetic Coding
 *
 * Implements range-based arithmetic coding where the entire message
 * is encoded as a single number in the interval [0, 1).
 *
 * Uses 32-bit precision with renormalization to handle arbitrary
 * length messages without overflow.
 *
 * Key concepts:
 * - Each symbol narrows the current interval proportionally to its probability
 * - Renormalization outputs bits when interval boundaries have common prefix
 * - End-of-stream marker or explicit length needed for termination
 *
 * References:
 * - Witten, Neal, Cleary, "Arithmetic Coding for Data Compression" (1987)
 * - Howard & Vitter, "Arithmetic Coding for Data Compression" (1994)
 */

#include "../config.hpp"
#include "../types.hpp"
#include "../core/bitstream.hpp"
#include <array>
#include <vector>

namespace compressum::entropy {

/**
 * Arithmetic coding constants
 */
inline constexpr uint32_t ARITH_CODE_VALUE_BITS = 32;
inline constexpr uint64_t ARITH_TOP_VALUE = 1ull << ARITH_CODE_VALUE_BITS;
inline constexpr uint64_t ARITH_FIRST_QTR = ARITH_TOP_VALUE / 4;
inline constexpr uint64_t ARITH_HALF = ARITH_TOP_VALUE / 2;
inline constexpr uint64_t ARITH_THIRD_QTR = 3 * ARITH_TOP_VALUE / 4;
inline constexpr uint32_t ARITH_MAX_FREQ = 1u << 14;  // Max frequency sum

/**
 * Cumulative frequency model for arithmetic coding
 */
class ArithmeticModel {
public:
    static constexpr size_t MAX_SYMBOLS = 257;  // 256 bytes + EOF

    ArithmeticModel() { reset(); }

    /**
     * Reset to uniform distribution
     */
    void reset() {
        for (size_t i = 0; i <= MAX_SYMBOLS; ++i) {
            cumfreq_[i] = static_cast<uint32_t>(i);
        }
        total_ = MAX_SYMBOLS;
    }

    /**
     * Build from frequency counts
     */
    void build(const uint32_t* freq, size_t num_symbols) {
        num_symbols = std::min(num_symbols, MAX_SYMBOLS - 1);  // Reserve space for EOF

        // Scale frequencies if total exceeds limit
        uint64_t total = 0;
        for (size_t i = 0; i < num_symbols; ++i) {
            total += freq[i];
        }
        total += 1;  // EOF

        double scale = 1.0;
        if (total > ARITH_MAX_FREQ) {
            scale = static_cast<double>(ARITH_MAX_FREQ) / static_cast<double>(total);
        }

        // Build cumulative frequencies
        cumfreq_[0] = 0;
        for (size_t i = 0; i < num_symbols; ++i) {
            uint32_t f = static_cast<uint32_t>(freq[i] * scale);
            cumfreq_[i + 1] = cumfreq_[i] + std::max(f, 1u);  // Minimum 1
        }
        // EOF symbol
        cumfreq_[num_symbols + 1] = cumfreq_[num_symbols] + 1;

        num_symbols_ = num_symbols + 1;  // Include EOF
        total_ = cumfreq_[num_symbols_];
    }

    /**
     * Build from byte data (+ EOF)
     */
    void build_from_data(ByteSpan data) {
        std::array<uint32_t, 256> freq{};
        for (Byte b : data) {
            ++freq[b];
        }
        build(freq.data(), 256);
    }

    /**
     * Get symbol range for encoding
     * Returns (low_count, high_count, total)
     */
    [[nodiscard]] std::tuple<uint32_t, uint32_t, uint32_t>
    get_range(uint16_t symbol) const {
        return {cumfreq_[symbol], cumfreq_[symbol + 1], total_};
    }

    /**
     * Find symbol from cumulative count (for decoding)
     * Returns (symbol, low_count, high_count)
     */
    [[nodiscard]] std::tuple<uint16_t, uint32_t, uint32_t>
    find_symbol(uint32_t count) const {
        // Binary search
        size_t lo = 0, hi = num_symbols_;
        while (lo + 1 < hi) {
            size_t mid = (lo + hi) / 2;
            if (cumfreq_[mid] <= count) {
                lo = mid;
            } else {
                hi = mid;
            }
        }
        return {static_cast<uint16_t>(lo), cumfreq_[lo], cumfreq_[lo + 1]};
    }

    /**
     * Get total frequency sum
     */
    [[nodiscard]] uint32_t total() const { return total_; }

    /**
     * Get EOF symbol
     */
    [[nodiscard]] uint16_t eof_symbol() const {
        return static_cast<uint16_t>(num_symbols_ - 1);
    }

    /**
     * Adaptive update: increase frequency of symbol
     * For adaptive arithmetic coding
     */
    void update(uint16_t symbol) {
        // Increment frequencies from symbol to end
        for (size_t i = symbol + 1; i <= num_symbols_; ++i) {
            ++cumfreq_[i];
        }
        ++total_;

        // Rescale if total too large
        if (total_ >= ARITH_MAX_FREQ) {
            rescale();
        }
    }

private:
    void rescale() {
        // Halve all frequencies (keeping minimum of 1)
        cumfreq_[0] = 0;
        for (size_t i = 1; i <= num_symbols_; ++i) {
            uint32_t freq = cumfreq_[i] - cumfreq_[i - 1];
            freq = (freq + 1) / 2;
            cumfreq_[i] = cumfreq_[i - 1] + freq;
        }
        total_ = cumfreq_[num_symbols_];
    }

    std::array<uint32_t, MAX_SYMBOLS + 1> cumfreq_{};
    size_t num_symbols_ = MAX_SYMBOLS;
    uint32_t total_ = MAX_SYMBOLS;
};

/**
 * Arithmetic Encoder
 */
class ArithmeticEncoder {
public:
    explicit ArithmeticEncoder(size_t reserve = 4096) {
        output_.reserve(reserve);
    }

    /**
     * Initialize encoder
     */
    void init() {
        low_ = 0;
        high_ = ARITH_TOP_VALUE - 1;
        bits_to_follow_ = 0;
        output_.clear();
    }

    /**
     * Encode a symbol
     */
    void encode(const ArithmeticModel& model, uint16_t symbol) {
        auto [sym_low, sym_high, total] = model.get_range(symbol);

        // Narrow the interval
        uint64_t range = high_ - low_ + 1;
        high_ = low_ + (range * sym_high) / total - 1;
        low_ = low_ + (range * sym_low) / total;

        // Renormalize
        renormalize();
    }

    /**
     * Finish encoding
     */
    void finish() {
        // Output enough bits to uniquely identify the final interval
        ++bits_to_follow_;
        if (low_ < ARITH_FIRST_QTR) {
            output_bit(0);
        } else {
            output_bit(1);
        }

        // Flush any remaining bits
        if (bit_count_ > 0) {
            output_.push_back(static_cast<Byte>(bit_buffer_ << (8 - bit_count_)));
        }
    }

    /**
     * Get encoded output
     */
    [[nodiscard]] const std::vector<Byte>& output() const { return output_; }

    /**
     * Take output (move)
     */
    [[nodiscard]] std::vector<Byte> take_output() { return std::move(output_); }

private:
    void renormalize() {
        for (;;) {
            if (high_ < ARITH_HALF) {
                // Output 0
                output_bit(0);
            } else if (low_ >= ARITH_HALF) {
                // Output 1
                output_bit(1);
                low_ -= ARITH_HALF;
                high_ -= ARITH_HALF;
            } else if (low_ >= ARITH_FIRST_QTR && high_ < ARITH_THIRD_QTR) {
                // Middle case: scale and remember to output opposite bit later
                ++bits_to_follow_;
                low_ -= ARITH_FIRST_QTR;
                high_ -= ARITH_FIRST_QTR;
            } else {
                break;
            }
            low_ = low_ * 2;
            high_ = high_ * 2 + 1;
        }
    }

    void output_bit(int bit) {
        bit_buffer_ = (bit_buffer_ << 1) | bit;
        ++bit_count_;

        if (bit_count_ == 8) {
            output_.push_back(static_cast<Byte>(bit_buffer_));
            bit_buffer_ = 0;
            bit_count_ = 0;
        }

        // Output opposite bits that were accumulated
        while (bits_to_follow_ > 0) {
            --bits_to_follow_;
            bit_buffer_ = (bit_buffer_ << 1) | (1 - bit);
            ++bit_count_;

            if (bit_count_ == 8) {
                output_.push_back(static_cast<Byte>(bit_buffer_));
                bit_buffer_ = 0;
                bit_count_ = 0;
            }
        }
    }

    uint64_t low_ = 0;
    uint64_t high_ = ARITH_TOP_VALUE - 1;
    uint64_t bits_to_follow_ = 0;
    uint8_t bit_buffer_ = 0;
    uint8_t bit_count_ = 0;
    std::vector<Byte> output_;
};

/**
 * Arithmetic Decoder
 */
class ArithmeticDecoder {
public:
    ArithmeticDecoder() = default;

    /**
     * Initialize decoder from compressed data
     */
    void init(ByteSpan data) {
        data_ = data.data();
        data_size_ = data.size();
        data_pos_ = 0;
        bit_pos_ = 0;

        low_ = 0;
        high_ = ARITH_TOP_VALUE - 1;

        // Read initial value (enough bits to fill the range)
        value_ = 0;
        for (int i = 0; i < ARITH_CODE_VALUE_BITS; ++i) {
            value_ = (value_ << 1) | read_bit();
        }
    }

    /**
     * Decode one symbol
     */
    [[nodiscard]] uint16_t decode(const ArithmeticModel& model) {
        uint32_t total = model.total();
        uint64_t range = high_ - low_ + 1;

        // Find cumulative count
        uint64_t cum = ((value_ - low_ + 1) * total - 1) / range;

        // Find symbol
        auto [symbol, sym_low, sym_high] = model.find_symbol(static_cast<uint32_t>(cum));

        // Narrow interval
        high_ = low_ + (range * sym_high) / total - 1;
        low_ = low_ + (range * sym_low) / total;

        // Renormalize
        renormalize();

        return symbol;
    }

    /**
     * Check if EOF symbol was decoded
     */
    [[nodiscard]] bool at_eof(const ArithmeticModel& model, uint16_t symbol) const {
        return symbol == model.eof_symbol();
    }

private:
    void renormalize() {
        for (;;) {
            if (high_ < ARITH_HALF) {
                // Do nothing
            } else if (low_ >= ARITH_HALF) {
                value_ -= ARITH_HALF;
                low_ -= ARITH_HALF;
                high_ -= ARITH_HALF;
            } else if (low_ >= ARITH_FIRST_QTR && high_ < ARITH_THIRD_QTR) {
                value_ -= ARITH_FIRST_QTR;
                low_ -= ARITH_FIRST_QTR;
                high_ -= ARITH_FIRST_QTR;
            } else {
                break;
            }
            low_ = low_ * 2;
            high_ = high_ * 2 + 1;
            value_ = (value_ * 2) | read_bit();
        }
    }

    [[nodiscard]] int read_bit() {
        if (data_pos_ >= data_size_) {
            return 0;  // Pad with zeros
        }

        int bit = (data_[data_pos_] >> (7 - bit_pos_)) & 1;
        ++bit_pos_;

        if (bit_pos_ == 8) {
            bit_pos_ = 0;
            ++data_pos_;
        }

        return bit;
    }

    const Byte* data_ = nullptr;
    size_t data_size_ = 0;
    size_t data_pos_ = 0;
    size_t bit_pos_ = 0;

    uint64_t low_ = 0;
    uint64_t high_ = ARITH_TOP_VALUE - 1;
    uint64_t value_ = 0;
};

/**
 * Adaptive Arithmetic Encoder
 * Updates model after each symbol
 */
class AdaptiveArithmeticEncoder {
public:
    void init() {
        model_.reset();
        encoder_.init();
    }

    void encode(uint8_t symbol) {
        encoder_.encode(model_, symbol);
        model_.update(symbol);
    }

    void finish() {
        encoder_.encode(model_, model_.eof_symbol());
        encoder_.finish();
    }

    [[nodiscard]] std::vector<Byte> take_output() {
        return encoder_.take_output();
    }

private:
    ArithmeticModel model_;
    ArithmeticEncoder encoder_;
};

/**
 * Adaptive Arithmetic Decoder
 */
class AdaptiveArithmeticDecoder {
public:
    void init(ByteSpan data) {
        model_.reset();
        decoder_.init(data);
    }

    [[nodiscard]] uint8_t decode() {
        uint16_t symbol = decoder_.decode(model_);
        if (!decoder_.at_eof(model_, symbol)) {
            model_.update(symbol);
        }
        return static_cast<uint8_t>(symbol);
    }

    [[nodiscard]] bool at_eof() {
        // Peek at next symbol to check for EOF
        // Note: This is a simplified check
        return false;
    }

    void decode_to(std::vector<Byte>& output, size_t expected_size) {
        output.reserve(expected_size);
        for (size_t i = 0; i < expected_size; ++i) {
            uint16_t symbol = decoder_.decode(model_);
            if (decoder_.at_eof(model_, symbol)) break;
            output.push_back(static_cast<uint8_t>(symbol));
            model_.update(symbol);
        }
    }

private:
    ArithmeticModel model_;
    ArithmeticDecoder decoder_;
};

} // namespace compressum::entropy
