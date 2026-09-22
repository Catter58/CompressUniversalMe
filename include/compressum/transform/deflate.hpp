#pragma once

/**
 * CompressUM - Original Compression Algorithm
 * Deflate Implementation (RFC 1951)
 *
 * Minimal deflate/inflate for PNG support.
 * Note: DEFLATE uses LSB-first bit packing, unlike our standard BitWriter.
 */

#include "../types.hpp"
#include "../config.hpp"
#include <vector>
#include <array>
#include <cstring>

namespace compressum::transform {

// Deflate constants
inline constexpr size_t DEFLATE_MAX_MATCH_LEN = 258;
inline constexpr size_t DEFLATE_MIN_MATCH_LEN = 3;
inline constexpr size_t DEFLATE_WINDOW_SIZE = 32768;  // 32KB

// Fixed Huffman code lengths (RFC 1951)
inline constexpr std::array<uint8_t, 288> make_fixed_literal_lengths() {
    std::array<uint8_t, 288> lengths{};
    for (size_t i = 0; i <= 143; ++i) lengths[i] = 8;
    for (size_t i = 144; i <= 255; ++i) lengths[i] = 9;
    for (size_t i = 256; i <= 279; ++i) lengths[i] = 7;
    for (size_t i = 280; i <= 287; ++i) lengths[i] = 8;
    return lengths;
}

inline constexpr std::array<uint8_t, 32> make_fixed_distance_lengths() {
    std::array<uint8_t, 32> lengths{};
    for (size_t i = 0; i < 32; ++i) lengths[i] = 5;
    return lengths;
}

// Length code base values and extra bits (RFC 1951)
inline constexpr std::array<uint16_t, 29> LENGTH_BASE = {
    3, 4, 5, 6, 7, 8, 9, 10, 11, 13, 15, 17, 19, 23, 27, 31,
    35, 43, 51, 59, 67, 83, 99, 115, 131, 163, 195, 227, 258
};

inline constexpr std::array<uint8_t, 29> LENGTH_EXTRA = {
    0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2, 2,
    3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 0
};

// Distance code base values and extra bits (RFC 1951)
inline constexpr std::array<uint16_t, 30> DISTANCE_BASE = {
    1, 2, 3, 4, 5, 7, 9, 13, 17, 25, 33, 49, 65, 97, 129, 193,
    257, 385, 513, 769, 1025, 1537, 2049, 3073, 4097, 6145, 8193, 12289, 16385, 24577
};

inline constexpr std::array<uint8_t, 30> DISTANCE_EXTRA = {
    0, 0, 0, 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6,
    7, 7, 8, 8, 9, 9, 10, 10, 11, 11, 12, 12, 13, 13
};

/**
 * LSB-first bit reader for DEFLATE with look-ahead support
 */
class DeflateBitReader {
public:
    explicit DeflateBitReader(ByteSpan data) : data_(data) {
        // Pre-fill buffer
        fill_buffer();
    }

    uint32_t read_bits(size_t count) {
        ensure_bits(count);
        uint32_t result = buffer_ & ((1u << count) - 1);
        buffer_ >>= count;
        bits_in_buffer_ -= count;
        return result;
    }

    // Peek without consuming
    uint32_t peek_bits(size_t count) {
        ensure_bits(count);
        return buffer_ & ((1u << count) - 1);
    }

    // Consume bits after peeking
    void drop_bits(size_t count) {
        buffer_ >>= count;
        bits_in_buffer_ -= count;
    }

    void align_to_byte() {
        size_t discard = bits_in_buffer_ & 7;
        if (discard > 0) {
            drop_bits(discard);
        }
    }

    bool eof() const {
        return byte_pos_ >= data_.size() && bits_in_buffer_ == 0;
    }

private:
    void ensure_bits(size_t count) {
        while (bits_in_buffer_ < count && byte_pos_ < data_.size()) {
            buffer_ |= static_cast<uint32_t>(data_[byte_pos_++]) << bits_in_buffer_;
            bits_in_buffer_ += 8;
        }
    }

    void fill_buffer() {
        while (bits_in_buffer_ < 24 && byte_pos_ < data_.size()) {
            buffer_ |= static_cast<uint32_t>(data_[byte_pos_++]) << bits_in_buffer_;
            bits_in_buffer_ += 8;
        }
    }

    ByteSpan data_;
    size_t byte_pos_ = 0;
    uint32_t buffer_ = 0;
    size_t bits_in_buffer_ = 0;
};

/**
 * LSB-first bit writer for DEFLATE
 */
class DeflateBitWriter {
public:
    void write_bits(uint32_t value, size_t count) {
        for (size_t i = 0; i < count; ++i) {
            current_byte_ |= ((value >> i) & 1) << bit_pos_;
            bit_pos_++;
            if (bit_pos_ == 8) {
                output_.push_back(current_byte_);
                current_byte_ = 0;
                bit_pos_ = 0;
            }
        }
    }

    void flush() {
        if (bit_pos_ > 0) {
            output_.push_back(current_byte_);
            current_byte_ = 0;
            bit_pos_ = 0;
        }
    }

    std::vector<Byte> take_data() {
        flush();
        return std::move(output_);
    }

private:
    std::vector<Byte> output_;
    uint8_t current_byte_ = 0;
    size_t bit_pos_ = 0;
};

/**
 * Simple Huffman decoder for DEFLATE (builds lookup table from code lengths)
 */
class DeflateHuffmanDecoder {
public:
    static constexpr size_t MAX_BITS = 15;

    void build(const uint8_t* lengths, size_t num_symbols) {
        // Clear
        for (auto& e : table_) e = {0, 0};
        symbols_.clear();
        codes_.clear();
        code_lens_.clear();

        // Count codes of each length
        std::array<uint32_t, MAX_BITS + 1> bl_count{};
        for (size_t i = 0; i < num_symbols; ++i) {
            if (lengths[i] > 0 && lengths[i] <= MAX_BITS) {
                ++bl_count[lengths[i]];
            }
        }

        // Find starting code for each length
        std::array<uint16_t, MAX_BITS + 1> next_code{};
        uint16_t code = 0;
        for (size_t bits = 1; bits <= MAX_BITS; ++bits) {
            code = (code + bl_count[bits - 1]) << 1;
            next_code[bits] = code;
        }

        // Build symbol table and fast lookup
        for (size_t i = 0; i < num_symbols; ++i) {
            uint8_t len = lengths[i];
            if (len > 0 && len <= MAX_BITS) {
                uint16_t c = next_code[len]++;
                symbols_.push_back(static_cast<uint16_t>(i));
                codes_.push_back(c);
                code_lens_.push_back(len);

                // Build fast lookup table for short codes
                if (len <= TABLE_BITS) {
                    // Reverse bits for lookup (DEFLATE reads LSB first)
                    uint16_t reversed = 0;
                    for (uint8_t j = 0; j < len; ++j) {
                        reversed |= ((c >> (len - 1 - j)) & 1) << j;
                    }
                    // Fill all entries with this prefix
                    size_t fill_count = 1 << (TABLE_BITS - len);
                    for (size_t f = 0; f < fill_count; ++f) {
                        size_t idx = reversed | (f << len);
                        table_[idx] = {static_cast<uint16_t>(i), len};
                    }
                }
            }
        }
    }

    uint16_t decode(DeflateBitReader& reader) const {
        // Peek at TABLE_BITS without consuming
        uint32_t bits = reader.peek_bits(TABLE_BITS);
        const auto& entry = table_[bits];
        if (entry.length > 0 && entry.length <= TABLE_BITS) {
            // Only consume the actual code length
            reader.drop_bits(entry.length);
            return entry.symbol;
        }

        // Slow path for long codes (shouldn't happen with TABLE_BITS=9 for fixed Huffman)
        uint32_t code = 0;
        for (size_t len = 1; len <= MAX_BITS; ++len) {
            code = (code << 1) | reader.read_bits(1);
            for (size_t i = 0; i < codes_.size(); ++i) {
                if (code_lens_[i] == len && codes_[i] == code) {
                    return symbols_[i];
                }
            }
        }
        return 0;
    }

private:
    static constexpr size_t TABLE_BITS = 9;
    struct TableEntry {
        uint16_t symbol;
        uint8_t length;
    };
    std::array<TableEntry, 1 << TABLE_BITS> table_{};
    std::vector<uint16_t> symbols_;
    std::vector<uint16_t> codes_;
    std::vector<uint8_t> code_lens_;
};

/**
 * Deflate decompressor (inflater)
 */
class Inflater {
public:
    [[nodiscard]] std::vector<Byte> inflate(ByteSpan input) {
        DeflateBitReader reader(input);
        std::vector<Byte> output;
        output.reserve(input.size() * 4);

        bool final_block = false;
        while (!final_block && !reader.eof()) {
            final_block = reader.read_bits(1) == 1;
            uint32_t block_type = reader.read_bits(2);

            switch (block_type) {
                case 0:  // Stored
                    inflate_stored(reader, output);
                    break;
                case 1:  // Fixed Huffman
                    inflate_fixed(reader, output);
                    break;
                case 2:  // Dynamic Huffman
                    inflate_dynamic(reader, output);
                    break;
                default:  // Reserved (error)
                    return output;
            }
        }

        return output;
    }

    [[nodiscard]] std::vector<Byte> inflate_zlib(ByteSpan input) {
        if (input.size() < 6) return {};
        // Skip 2-byte zlib header, exclude 4-byte Adler-32 checksum
        ByteSpan deflate_data(input.data() + 2, input.size() - 6);
        return inflate(deflate_data);
    }

private:
    void inflate_stored(DeflateBitReader& reader, std::vector<Byte>& output) {
        reader.align_to_byte();
        uint16_t len = static_cast<uint16_t>(reader.read_bits(16));
        [[maybe_unused]] uint16_t nlen = static_cast<uint16_t>(reader.read_bits(16));

        for (uint16_t i = 0; i < len; ++i) {
            output.push_back(static_cast<Byte>(reader.read_bits(8)));
        }
    }

    void inflate_fixed(DeflateBitReader& reader, std::vector<Byte>& output) {
        static const auto fixed_lit_lengths = make_fixed_literal_lengths();
        static const auto fixed_dist_lengths = make_fixed_distance_lengths();

        DeflateHuffmanDecoder lit_decoder, dist_decoder;
        lit_decoder.build(fixed_lit_lengths.data(), 288);
        dist_decoder.build(fixed_dist_lengths.data(), 32);

        decode_compressed_block(reader, lit_decoder, dist_decoder, output);
    }

    void inflate_dynamic(DeflateBitReader& reader, std::vector<Byte>& output) {
        uint32_t hlit = reader.read_bits(5) + 257;
        uint32_t hdist = reader.read_bits(5) + 1;
        uint32_t hclen = reader.read_bits(4) + 4;

        static constexpr std::array<uint8_t, 19> CL_ORDER = {
            16, 17, 18, 0, 8, 7, 9, 6, 10, 5, 11, 4, 12, 3, 13, 2, 14, 1, 15
        };

        std::array<uint8_t, 19> cl_lengths{};
        for (uint32_t i = 0; i < hclen; ++i) {
            cl_lengths[CL_ORDER[i]] = static_cast<uint8_t>(reader.read_bits(3));
        }

        DeflateHuffmanDecoder cl_decoder;
        cl_decoder.build(cl_lengths.data(), 19);

        std::vector<uint8_t> all_lengths(hlit + hdist);
        size_t i = 0;
        while (i < all_lengths.size()) {
            uint16_t symbol = cl_decoder.decode(reader);
            if (symbol < 16) {
                all_lengths[i++] = static_cast<uint8_t>(symbol);
            } else if (symbol == 16) {
                uint32_t repeat = reader.read_bits(2) + 3;
                uint8_t prev = i > 0 ? all_lengths[i - 1] : 0;
                for (uint32_t j = 0; j < repeat && i < all_lengths.size(); ++j) {
                    all_lengths[i++] = prev;
                }
            } else if (symbol == 17) {
                uint32_t repeat = reader.read_bits(3) + 3;
                for (uint32_t j = 0; j < repeat && i < all_lengths.size(); ++j) {
                    all_lengths[i++] = 0;
                }
            } else if (symbol == 18) {
                uint32_t repeat = reader.read_bits(7) + 11;
                for (uint32_t j = 0; j < repeat && i < all_lengths.size(); ++j) {
                    all_lengths[i++] = 0;
                }
            }
        }

        DeflateHuffmanDecoder lit_decoder, dist_decoder;
        lit_decoder.build(all_lengths.data(), hlit);
        dist_decoder.build(all_lengths.data() + hlit, hdist);

        decode_compressed_block(reader, lit_decoder, dist_decoder, output);
    }

    void decode_compressed_block(
        DeflateBitReader& reader,
        const DeflateHuffmanDecoder& lit_decoder,
        const DeflateHuffmanDecoder& dist_decoder,
        std::vector<Byte>& output
    ) {
        while (true) {
            uint16_t symbol = lit_decoder.decode(reader);

            if (symbol < 256) {
                output.push_back(static_cast<Byte>(symbol));
            } else if (symbol == 256) {
                break;
            } else {
                symbol -= 257;
                if (symbol >= LENGTH_BASE.size()) break;

                uint32_t length = LENGTH_BASE[symbol];
                if (LENGTH_EXTRA[symbol] > 0) {
                    length += reader.read_bits(LENGTH_EXTRA[symbol]);
                }

                uint16_t dist_symbol = dist_decoder.decode(reader);
                if (dist_symbol >= DISTANCE_BASE.size()) break;

                uint32_t distance = DISTANCE_BASE[dist_symbol];
                if (DISTANCE_EXTRA[dist_symbol] > 0) {
                    distance += reader.read_bits(DISTANCE_EXTRA[dist_symbol]);
                }

                if (distance == 0 || distance > output.size()) break;  // Corrupt stream

                size_t start = output.size() - distance;
                for (uint32_t j = 0; j < length; ++j) {
                    output.push_back(output[start + j]);
                }
            }
        }
    }
};

/**
 * Deflate compressor - uses fixed Huffman codes for simplicity
 */
class Deflater {
public:
    [[nodiscard]] std::vector<Byte> deflate(ByteSpan input) {
        DeflateBitWriter writer;

        if (input.empty()) {
            // Final stored block with no data
            writer.write_bits(1, 1);  // BFINAL = 1
            writer.write_bits(0, 2);  // BTYPE = 00 (stored)
            writer.flush();
            // Need to be byte-aligned, then write LEN/NLEN
            writer.write_bits(0, 16);        // LEN = 0
            writer.write_bits(0xFFFF, 16);   // NLEN = ~0 = 0xFFFF
            return writer.take_data();
        }

        // Single final block with fixed Huffman
        writer.write_bits(1, 1);  // BFINAL = 1
        writer.write_bits(1, 2);  // BTYPE = 01 (fixed Huffman)

        // Build fixed code tables
        build_fixed_codes();

        // Simple LZ77 encoding
        encode_lz77(writer, input);

        // End of block (symbol 256)
        write_literal(writer, 256);

        return writer.take_data();
    }

    [[nodiscard]] std::vector<Byte> deflate_zlib(ByteSpan input) {
        std::vector<Byte> result;

        // Zlib header: CMF=0x78 (deflate, 32K window), FLG=0x9C (level 2, divisible check)
        result.push_back(0x78);
        result.push_back(0x9C);

        auto deflated = deflate(input);
        result.insert(result.end(), deflated.begin(), deflated.end());

        // Adler-32 checksum (big-endian)
        uint32_t adler = compute_adler32(input);
        result.push_back(static_cast<Byte>((adler >> 24) & 0xFF));
        result.push_back(static_cast<Byte>((adler >> 16) & 0xFF));
        result.push_back(static_cast<Byte>((adler >> 8) & 0xFF));
        result.push_back(static_cast<Byte>(adler & 0xFF));

        return result;
    }

private:
    struct HuffmanCode {
        uint16_t code;
        uint8_t length;
    };

    std::array<HuffmanCode, 288> lit_codes_{};
    std::array<HuffmanCode, 32> dist_codes_{};

    void build_fixed_codes() {
        static const auto lit_lengths = make_fixed_literal_lengths();
        static const auto dist_lengths = make_fixed_distance_lengths();

        // Build literal/length codes
        build_codes(lit_codes_.data(), lit_lengths.data(), 288);
        build_codes(dist_codes_.data(), dist_lengths.data(), 32);
    }

    void build_codes(HuffmanCode* codes, const uint8_t* lengths, size_t num) {
        // Count codes of each length
        std::array<uint32_t, 16> bl_count{};
        for (size_t i = 0; i < num; ++i) {
            if (lengths[i] > 0) ++bl_count[lengths[i]];
        }

        // Find starting code for each length
        std::array<uint16_t, 16> next_code{};
        uint16_t code = 0;
        for (size_t bits = 1; bits < 16; ++bits) {
            code = (code + bl_count[bits - 1]) << 1;
            next_code[bits] = code;
        }

        // Assign codes
        for (size_t i = 0; i < num; ++i) {
            uint8_t len = lengths[i];
            if (len > 0) {
                codes[i] = {next_code[len]++, len};
            } else {
                codes[i] = {0, 0};
            }
        }
    }

    void write_literal(DeflateBitWriter& writer, uint16_t symbol) {
        const auto& code = lit_codes_[symbol];
        // Write Huffman code in reverse bit order (LSB first per DEFLATE spec)
        for (int i = code.length - 1; i >= 0; --i) {
            writer.write_bits((code.code >> i) & 1, 1);
        }
    }

    void write_length_distance(DeflateBitWriter& writer, size_t length, size_t distance) {
        // Find length code
        uint16_t len_code = 0;
        for (size_t i = 0; i < LENGTH_BASE.size(); ++i) {
            if (LENGTH_BASE[i] <= length &&
                (i + 1 >= LENGTH_BASE.size() || LENGTH_BASE[i + 1] > length)) {
                len_code = static_cast<uint16_t>(i);
                break;
            }
        }

        // Write length code (257 + len_code)
        write_literal(writer, 257 + len_code);

        // Write extra length bits (LSB first)
        if (LENGTH_EXTRA[len_code] > 0) {
            uint32_t extra = static_cast<uint32_t>(length - LENGTH_BASE[len_code]);
            writer.write_bits(extra, LENGTH_EXTRA[len_code]);
        }

        // Find distance code
        uint16_t dist_code = 0;
        for (size_t i = 0; i < DISTANCE_BASE.size(); ++i) {
            if (DISTANCE_BASE[i] <= distance &&
                (i + 1 >= DISTANCE_BASE.size() || DISTANCE_BASE[i + 1] > distance)) {
                dist_code = static_cast<uint16_t>(i);
                break;
            }
        }

        // Write distance code (reverse bit order)
        const auto& dc = dist_codes_[dist_code];
        for (int i = dc.length - 1; i >= 0; --i) {
            writer.write_bits((dc.code >> i) & 1, 1);
        }

        // Write extra distance bits (LSB first)
        if (DISTANCE_EXTRA[dist_code] > 0) {
            uint32_t extra = static_cast<uint32_t>(distance - DISTANCE_BASE[dist_code]);
            writer.write_bits(extra, DISTANCE_EXTRA[dist_code]);
        }
    }

    void encode_lz77(DeflateBitWriter& writer, ByteSpan input) {
        size_t pos = 0;

        while (pos < input.size()) {
            // Find best match
            size_t best_len = 0;
            size_t best_dist = 0;

            size_t window_start = pos > DEFLATE_WINDOW_SIZE ? pos - DEFLATE_WINDOW_SIZE : 0;

            for (size_t j = window_start; j < pos; ++j) {
                size_t len = 0;
                while (len < DEFLATE_MAX_MATCH_LEN &&
                       pos + len < input.size() &&
                       input[j + len] == input[pos + len]) {
                    ++len;
                }

                if (len >= DEFLATE_MIN_MATCH_LEN && len > best_len) {
                    best_len = len;
                    best_dist = pos - j;
                }
            }

            if (best_len >= DEFLATE_MIN_MATCH_LEN) {
                write_length_distance(writer, best_len, best_dist);
                pos += best_len;
            } else {
                write_literal(writer, input[pos]);
                ++pos;
            }
        }
    }

    [[nodiscard]] static uint32_t compute_adler32(ByteSpan data) {
        uint32_t a = 1, b = 0;
        constexpr uint32_t MOD = 65521;

        for (Byte byte : data) {
            a = (a + byte) % MOD;
            b = (b + a) % MOD;
        }

        return (b << 16) | a;
    }
};

} // namespace compressum::transform
