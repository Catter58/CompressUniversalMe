#pragma once

/**
 * CompressUM - Original Compression Algorithm
 * Context Mixing (CM) Coder
 *
 * Bitwise context-mixing compressor:
 * - Every bit is predicted by several independent context models
 *   (order 0-6, words, sparse/stride contexts, long-range match model)
 * - Predictions are combined in the logistic domain by a gated linear mixer
 *   trained online to minimise coding cost
 * - Two mixers with different weight selectors are averaged
 * - Three adaptive probability maps (SSE) refine the mixed prediction
 * - A 32-bit binary arithmetic coder turns probabilities into bits
 *
 * Encoder and decoder run the identical model, so nothing but the
 * arithmetic-coded stream is stored.
 *
 * Stream layout: [MODEL_VERSION: 1 byte][arithmetic-coded bits]
 */

#include "../types.hpp"
#include <algorithm>
#include <array>
#include <cstdint>
#include <memory>
#include <vector>

namespace compressum::entropy::cm {

/// Bumped whenever the model changes in a way that alters the bitstream
inline constexpr uint8_t MODEL_VERSION = 1;

namespace detail {

// =============================================================================
// Logistic helpers (probabilities are 12-bit: 0..4095 = P(bit == 1))
// =============================================================================

/// squash(x) = 4096 / (1 + e^(-x/256)), x in [-2047, 2047]
[[nodiscard]] inline int squash(int x) {
    static constexpr int table[33] = {
        1, 2, 3, 6, 10, 16, 27, 45, 73, 120, 194, 310, 488, 747, 1101, 1546,
        2047, 2549, 2994, 3348, 3607, 3785, 3901, 3975, 4022, 4050, 4068, 4079,
        4085, 4089, 4092, 4093, 4094};
    if (x > 2047) return 4095;
    if (x < -2047) return 1;
    int w = x & 127;
    int i = (x >> 7) + 16;
    return (table[i] * (128 - w) + table[i + 1] * w + 64) >> 7;
}

/// stretch(p) = ln(p / (1 - p)), inverse of squash
[[nodiscard]] inline int stretch(int p) {
    static const auto table = [] {
        std::array<int16_t, 4096> t{};
        int pi = 0;
        for (int x = -2047; x <= 2047; ++x) {
            int v = squash(x);
            for (int i = pi; i <= v; ++i) t[i] = static_cast<int16_t>(x);
            pi = v + 1;
        }
        for (int i = pi; i < 4096; ++i) t[i] = 2047;
        return t;
    }();
    return table[p];
}

[[nodiscard]] inline uint32_t hash(uint32_t a, uint32_t b) {
    uint32_t h = a * 0x9E3779B1u ^ b * 0x85EBCA77u;
    h ^= h >> 15;
    h *= 0xC2B2AE3Du;
    return h ^ (h >> 13);
}

[[nodiscard]] inline int ilog2(size_t v) {
    int r = 0;
    while (v >>= 1) ++r;
    return r;
}

// =============================================================================
// Adaptive bit counter: uint32 = probability (22 bits) | hit count (10 bits)
// Learning rate starts at ~1/1.5 and decays to 1/(limit + 1.5)
// =============================================================================

inline constexpr uint32_t COUNTER_INIT = 1u << 31;  // p = 0.5, n = 0

[[nodiscard]] inline int counter_p(uint32_t s) { return static_cast<int>(s >> 20); }

inline void counter_update(uint32_t& s, int y, int limit) {
    static const auto rate = [] {
        std::array<int32_t, 1024> r{};
        for (int n = 0; n < 1024; ++n) r[n] = static_cast<int32_t>(65536.0 / (n + 1.5));
        return r;
    }();
    int n = static_cast<int>(s & 1023);
    int64_t p = s >> 10;
    p += ((static_cast<int64_t>(y) << 22) - p) * rate[n] >> 16;
    if (p < 0) p = 0;
    if (p > (1 << 22) - 1) p = (1 << 22) - 1;
    if (n < limit) ++n;
    s = static_cast<uint32_t>(p << 10) | static_cast<uint32_t>(n);
}

// =============================================================================
// Adaptive probability map (SSE): refines p given a small context
// 33 interpolation buckets over the stretch domain per context
// =============================================================================

class APM {
public:
    explicit APM(size_t contexts) : t_(contexts * 33) {
        for (size_t c = 0; c < contexts; ++c) {
            for (int j = 0; j < 33; ++j) {
                t_[c * 33 + j] = static_cast<uint16_t>(squash((j - 16) * 128) * 16);
            }
        }
    }

    [[nodiscard]] int refine(int p, size_t ctx) {
        int s = stretch(p) + 2048;  // 1..4095
        int lo = s >> 7;
        int w = s & 127;
        size_t base = ctx * 33 + lo;
        index_ = base + (w >> 6);
        return (t_[base] * (128 - w) + t_[base + 1] * w) >> 11;
    }

    void update(int y, int rate = 7) {
        // Target slightly above 65535 for y=1 so floor division still reaches it
        int target = (y << 16) + (y << rate) - y - y;
        t_[index_] = static_cast<uint16_t>(t_[index_] + ((target - t_[index_]) >> rate));
    }

private:
    std::vector<uint16_t> t_;
    size_t index_ = 0;
};

// =============================================================================
// Binary arithmetic coder (32-bit, carry-less)
// =============================================================================

class Encoder {
public:
    explicit Encoder(std::vector<Byte>& out) : out_(out) {}

    void encode(int y, int p) {
        uint32_t mid = x1_ + static_cast<uint32_t>((static_cast<uint64_t>(x2_ - x1_) * p) >> 12);
        if (y) x2_ = mid; else x1_ = mid + 1;
        while (((x1_ ^ x2_) & 0xFF000000u) == 0) {
            out_.push_back(static_cast<Byte>(x2_ >> 24));
            x1_ <<= 8;
            x2_ = (x2_ << 8) | 0xFF;
        }
    }

    void flush() {
        for (int i = 0; i < 4; ++i) {
            out_.push_back(static_cast<Byte>(x1_ >> 24));
            x1_ <<= 8;
        }
    }

private:
    std::vector<Byte>& out_;
    uint32_t x1_ = 0;
    uint32_t x2_ = 0xFFFFFFFFu;
};

class Decoder {
public:
    explicit Decoder(ByteSpan in) : in_(in) {
        for (int i = 0; i < 4; ++i) x_ = (x_ << 8) | next_byte();
    }

    [[nodiscard]] int decode(int p) {
        uint32_t mid = x1_ + static_cast<uint32_t>((static_cast<uint64_t>(x2_ - x1_) * p) >> 12);
        int y = x_ <= mid;
        if (y) x2_ = mid; else x1_ = mid + 1;
        while (((x1_ ^ x2_) & 0xFF000000u) == 0) {
            x1_ <<= 8;
            x2_ = (x2_ << 8) | 0xFF;
            x_ = (x_ << 8) | next_byte();
        }
        return y;
    }

private:
    // Truncated/corrupt input decodes as zeros; the file CRC rejects it
    uint32_t next_byte() { return pos_ < in_.size() ? in_[pos_++] : 0; }

    ByteSpan in_;
    size_t pos_ = 0;
    uint32_t x1_ = 0;
    uint32_t x2_ = 0xFFFFFFFFu;
    uint32_t x_ = 0;
};

// =============================================================================
// Predictor: context models + match model + mixer + SSE
// =============================================================================

class Predictor {
public:
    /// Table size is derived from the block size, which the decoder also knows
    explicit Predictor(size_t block_size)
        : table_shift_(32 - std::clamp(ilog2(block_size) + 2, 16, MAX_TABLE_BITS)) {
        history_.reserve(block_size);
        for (auto& t : tables_) t.assign(size_t{1} << (32 - table_shift_), COUNTER_INIT);
        order0_.fill(COUNTER_INIT);
        order1_.assign(size_t{1} << 16, COUNTER_INIT);
        match_counters_.fill(COUNTER_INIT);
        match_table_.assign(size_t{1} << MATCH_BITS, 0);
        weights_.assign(MIXER_SETS * INPUTS, (1 << 16) / 4);
        update_contexts();
        predict();
    }

    /// Probability (12-bit) that the next bit is 1
    [[nodiscard]] int p() const { return pr_; }

    void update(int y) {
        train(y);

        c0_ = (c0_ << 1) | static_cast<uint32_t>(y);
        if (++bit_count_ == 8) {
            end_of_byte(static_cast<Byte>(c0_ & 0xFF));
            c0_ = 1;
            bit_count_ = 0;
            update_contexts();
        }
        predict();
    }

private:
    static constexpr int MAX_TABLE_BITS = 22;
    static constexpr int NUM_TABLES = 9;
    static constexpr int MATCH_BITS = 20;
    static constexpr int MATCH_MIN = 6;
    static constexpr int INPUTS = NUM_TABLES + 4;  // + order0, order1, match, bias
    static constexpr size_t MIXER1_SETS = 256 * 3;
    static constexpr size_t MIXER_SETS = MIXER1_SETS + 256 * 8;
    static constexpr int LIMIT = 8;

    void train(int y) {
        for (auto* s : slots_) counter_update(*s, y, LIMIT);
        counter_update(*order0_slot_, y, LIMIT);
        counter_update(*order1_slot_, y, LIMIT);
        if (match_len_ > 0) counter_update(*match_slot_, y, 1023);

        train_mixer(&weights_[mixer_set_ * INPUTS], mixer_pr_[0], y);
        train_mixer(&weights_[(MIXER1_SETS + mixer_set2_) * INPUTS], mixer_pr_[1], y);

        apm1_.update(y);
        apm2_.update(y);
        apm3_.update(y);
    }

    void end_of_byte(Byte c) {
        history_.push_back(c);
        c8_ = (c8_ << 8) | (c4_ >> 24);
        c4_ = (c4_ << 8) | c;

        // Word model: hash of the current run of letters (case-folded)
        bool letter = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c >= 128;
        if (letter) {
            word0_ = hash(word0_ + 1, (c >= 'A' && c <= 'Z') ? c + 32 : c);
        } else if (word0_ != 0) {
            word1_ = word0_;
            word0_ = 0;
        }

        update_match(c);
    }

    void update_match(Byte c) {
        const size_t pos = history_.size();

        if (match_len_ > 0) {
            if (history_[match_ptr_] == c) {
                if (match_len_ < 65535) ++match_len_;
                ++match_ptr_;
            } else {
                match_len_ = 0;
            }
        }

        if (pos < MATCH_MIN) return;
        uint32_t h = hash(c4_, c8_ & 0xFFFF) >> (32 - MATCH_BITS);
        if (match_len_ == 0) {
            // Candidate is always an earlier position, so cand < pos
            uint32_t cand = match_table_[h];
            if (cand > 0) {
                size_t len = 0;
                while (len < 32 && len < cand && history_[cand - 1 - len] == history_[pos - 1 - len]) {
                    ++len;
                }
                if (len >= MATCH_MIN) {
                    match_len_ = static_cast<uint32_t>(len);
                    match_ptr_ = cand;
                }
            }
        }
        match_table_[h] = static_cast<uint32_t>(pos);
    }

    void update_contexts() {
        const uint32_t c1 = c4_ & 0xFF;
        ctx_[0] = hash(1, c4_ & 0xFFFF);                           // order 2
        ctx_[1] = hash(2, c4_ & 0xFFFFFF);                         // order 3
        ctx_[2] = hash(3, c4_);                                    // order 4
        ctx_[3] = hash(4, hash(c4_, c8_ & 0xFFFF));                // order 6
        ctx_[4] = hash(5, hash(word0_, c1));                       // current word
        ctx_[5] = hash(6, hash(word0_, word1_));                   // word pair
        ctx_[6] = hash(7, (c4_ >> 8) & 0xFFFF);                    // bytes -2,-3
        ctx_[7] = hash(8, c4_ & 0xFF00FF00u);                      // bytes -2,-4 (16-bit stride)
        ctx_[8] = hash(9, (c4_ >> 24) | ((c8_ >> 24) << 8));       // bytes -4,-8 (32-bit stride)
        match_byte_ = match_len_ > 0 ? history_[match_ptr_] : 0;
    }

    /// Find (or claim) the 16-counter bucket for a context at a nibble boundary.
    /// Slot 0 holds a check tag; 2-way associative, evicts the less used bucket.
    [[nodiscard]] int mix(const int32_t* w) const {
        int64_t dot = 0;
        for (int i = 0; i < INPUTS; ++i) dot += static_cast<int64_t>(inputs_[i]) * w[i];
        return std::clamp(static_cast<int>(dot >> 16), -2047, 2047);
    }

    void train_mixer(int32_t* w, int pr, int y) {
        int err = ((y << 12) - pr) * 3;
        for (int i = 0; i < INPUTS; ++i) w[i] += (inputs_[i] * err + (1 << 11)) >> 12;
    }

    [[nodiscard]] uint32_t* find_bucket(std::vector<uint32_t>& table, uint32_t ctx) const {
        uint32_t h = hash(ctx, c0_);
        uint32_t tag = h | 1;
        size_t idx = static_cast<size_t>(h >> table_shift_) & ~size_t{15};
        uint32_t* b0 = &table[idx];
        uint32_t* b1 = &table[idx ^ 16];
        if (b0[0] == tag) return b0;
        if (b1[0] == tag) return b1;
        uint32_t* victim = (b0[1] & 1023) <= (b1[1] & 1023) ? b0 : b1;
        victim[0] = tag;
        for (int j = 1; j < 16; ++j) victim[j] = COUNTER_INIT;
        return victim;
    }

    void predict() {
        if (bit_count_ == 0 || bit_count_ == 4) {
            for (int i = 0; i < NUM_TABLES; ++i) buckets_[i] = find_bucket(tables_[i], ctx_[i]);
        }
        // Counter index inside the bucket: bits of the current nibble with a leading 1
        const uint32_t nibble_bits = static_cast<uint32_t>(bit_count_ & 3);
        const uint32_t j = (c0_ & ((1u << nibble_bits) - 1)) | (1u << nibble_bits);
        for (int i = 0; i < NUM_TABLES; ++i) {
            slots_[i] = &buckets_[i][j];
            inputs_[i] = stretch(counter_p(*slots_[i]));
        }
        order0_slot_ = &order0_[c0_];
        order1_slot_ = &order1_[((c4_ & 0xFF) << 8) | c0_];
        inputs_[NUM_TABLES] = stretch(counter_p(*order0_slot_));
        inputs_[NUM_TABLES + 1] = stretch(counter_p(*order1_slot_));

        // Match model: predicted byte must agree with the bits seen so far
        int match_bucket = 0;
        if (match_len_ > 0 && ((match_byte_ | 0x100u) >> (8 - bit_count_)) != c0_) {
            match_len_ = 0;
        }
        if (match_len_ > 0) {
            int bit = (match_byte_ >> (7 - bit_count_)) & 1;
            int len_bucket = match_len_ < 16 ? static_cast<int>(match_len_)
                                             : 12 + ilog2(match_len_);
            if (len_bucket > 31) len_bucket = 31;
            match_slot_ = &match_counters_[len_bucket * 2 + bit];
            inputs_[NUM_TABLES + 2] = stretch(counter_p(*match_slot_));
            match_bucket = match_len_ < 32 ? 1 : 2;
        } else {
            inputs_[NUM_TABLES + 2] = 0;
        }
        inputs_[NUM_TABLES + 3] = 256;  // bias

        // Two mixers with different weight selectors, averaged in the logistic
        // domain: (partial byte, match state) and (previous byte, bit position)
        mixer_set_ = static_cast<size_t>(match_bucket) * 256 + c0_;
        mixer_set2_ = (c4_ & 0xFF) * 8 + static_cast<size_t>(bit_count_);
        int x1 = mix(&weights_[mixer_set_ * INPUTS]);
        int x2 = mix(&weights_[(MIXER1_SETS + mixer_set2_) * INPUTS]);
        mixer_pr_ = {squash(x1), squash(x2)};
        mix_pr_ = squash((x1 + x2) / 2);

        // SSE refinement on order-0, order-1 and order-2 contexts
        int p1 = apm1_.refine(mix_pr_, c0_);
        int p2 = apm2_.refine(mix_pr_, ((c4_ & 0xFF) << 8) | c0_);
        int p3 = apm3_.refine(mix_pr_, hash(c4_ & 0xFFFF, c0_) >> 16);
        int p = (mix_pr_ + p1 + p2 + p3 + 2) >> 2;
        pr_ = p < 1 ? 1 : (p > 4095 ? 4095 : p);
    }

    // Hashed context tables of 64-byte buckets, up to 9 x 16 MB per block in flight
    const int table_shift_;
    std::array<std::vector<uint32_t>, NUM_TABLES> tables_;
    std::array<uint32_t, 256> order0_{};
    std::vector<uint32_t> order1_;
    std::array<uint32_t, 64> match_counters_{};
    std::vector<uint32_t> match_table_;
    std::vector<int32_t> weights_;
    APM apm1_{256};
    APM apm2_{65536};
    APM apm3_{65536};

    std::vector<Byte> history_;
    std::array<uint32_t, NUM_TABLES> ctx_{};
    std::array<uint32_t*, NUM_TABLES> buckets_{};
    std::array<uint32_t*, NUM_TABLES> slots_{};
    uint32_t* order0_slot_ = nullptr;
    uint32_t* order1_slot_ = nullptr;
    uint32_t* match_slot_ = nullptr;
    std::array<int, INPUTS> inputs_{};

    uint32_t c0_ = 1;       // partial byte with leading 1
    int bit_count_ = 0;
    uint32_t c4_ = 0;       // last 4 bytes
    uint32_t c8_ = 0;       // bytes 5-8 back
    uint32_t word0_ = 0;
    uint32_t word1_ = 0;
    uint32_t match_len_ = 0;
    size_t match_ptr_ = 0;
    uint32_t match_byte_ = 0;
    size_t mixer_set_ = 0;
    size_t mixer_set2_ = 0;
    std::array<int, 2> mixer_pr_{2048, 2048};
    int mix_pr_ = 2048;
    int pr_ = 2048;
};

} // namespace detail

/**
 * Compress data with the context-mixing model
 */
[[nodiscard]] inline std::vector<Byte> encode(ByteSpan input) {
    std::vector<Byte> out;
    out.reserve(input.size() / 2 + 16);
    out.push_back(MODEL_VERSION);

    auto model = std::make_unique<detail::Predictor>(input.size());
    detail::Encoder enc(out);
    for (Byte c : input) {
        for (int i = 7; i >= 0; --i) {
            int y = (c >> i) & 1;
            enc.encode(y, model->p());
            model->update(y);
        }
    }
    enc.flush();
    return out;
}

/**
 * Decompress data produced by encode()
 * @param input Encoded stream
 * @param original_size Number of bytes to reconstruct
 * @return Decoded bytes, or empty on unknown model version
 */
[[nodiscard]] inline std::vector<Byte> decode(ByteSpan input, size_t original_size) {
    if (input.empty() || input[0] != MODEL_VERSION) {
        return {};
    }

    std::vector<Byte> out;
    out.reserve(original_size);

    auto model = std::make_unique<detail::Predictor>(original_size);
    detail::Decoder dec(input.subspan(1));
    for (size_t n = 0; n < original_size; ++n) {
        int c = 0;
        for (int i = 0; i < 8; ++i) {
            int y = dec.decode(model->p());
            model->update(y);
            c = (c << 1) | y;
        }
        out.push_back(static_cast<Byte>(c));
    }
    return out;
}

} // namespace compressum::entropy::cm
