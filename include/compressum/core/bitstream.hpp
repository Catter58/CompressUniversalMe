#pragma once

/**
 * CompressUM - Original Compression Algorithm
 * BitStream: Bit-level I/O with buffering
 *
 * Implements efficient bit reading/writing for entropy coding.
 * Uses a 64-bit buffer to minimize memory operations.
 */

#include "../types.hpp"
#include <vector>
#include <cstring>

namespace compressum::core {

/**
 * BitWriter - Write bits to an output buffer
 *
 * Bits are written MSB-first (most significant bit first) within each byte.
 * Uses a 64-bit accumulator for efficient multi-bit writes.
 */
class BitWriter {
public:
    explicit BitWriter(size_t reserve_bytes = 4096) {
        output_.reserve(reserve_bytes);
    }

    /**
     * Write `count` bits from `value` (MSB-first)
     * @param value  The value to write (only lowest `count` bits are used)
     * @param count  Number of bits to write (1-57)
     */
    void write_bits(uint64_t value, size_t count) {
        // Mask to keep only the bits we want
        uint64_t mask = (count == 64) ? ~0ull : ((1ull << count) - 1);
        value &= mask;

        // Add to buffer
        buffer_ |= value << (64 - bit_count_ - count);
        bit_count_ += count;

        // Flush complete bytes
        while (bit_count_ >= 8) {
            output_.push_back(static_cast<Byte>(buffer_ >> 56));
            buffer_ <<= 8;
            bit_count_ -= 8;
        }
    }

    /**
     * Write a single bit
     */
    void write_bit(bool bit) {
        write_bits(bit ? 1 : 0, 1);
    }

    /**
     * Write a full byte (8 bits)
     */
    void write_byte(Byte byte) {
        write_bits(byte, 8);
    }

    /**
     * Write multiple bytes
     */
    void write_bytes(ByteSpan data) {
        // If byte-aligned, copy directly
        if (bit_count_ == 0) {
            output_.insert(output_.end(), data.begin(), data.end());
        } else {
            for (Byte b : data) {
                write_byte(b);
            }
        }
    }

    /**
     * Write a variable-length integer (1-5 bytes)
     * Uses 7 bits per byte with MSB as continuation flag
     */
    void write_varint(uint64_t value) {
        while (value >= 0x80) {
            write_byte(static_cast<Byte>((value & 0x7F) | 0x80));
            value >>= 7;
        }
        write_byte(static_cast<Byte>(value));
    }

    /**
     * Flush remaining bits (padded with zeros)
     */
    void flush() {
        if (bit_count_ > 0) {
            output_.push_back(static_cast<Byte>(buffer_ >> 56));
            buffer_ = 0;
            bit_count_ = 0;
        }
    }

    /**
     * Get current bit position in stream
     */
    [[nodiscard]] size_t bit_position() const {
        return output_.size() * 8 + bit_count_;
    }

    /**
     * Get output data (after flush)
     */
    [[nodiscard]] const std::vector<Byte>& data() const { return output_; }

    /**
     * Move output data out (after flush)
     */
    [[nodiscard]] std::vector<Byte> take_data() {
        flush();
        return std::move(output_);
    }

    /**
     * Get current size in bytes (not including buffered bits)
     */
    [[nodiscard]] size_t size() const { return output_.size(); }

    /**
     * Reset writer to empty state
     */
    void reset() {
        output_.clear();
        buffer_ = 0;
        bit_count_ = 0;
    }

private:
    std::vector<Byte> output_;
    uint64_t buffer_ = 0;      // Bit buffer (MSB-aligned)
    size_t bit_count_ = 0;     // Number of valid bits in buffer
};

/**
 * BitReader - Read bits from an input buffer
 *
 * Reads bits MSB-first to match BitWriter.
 * Uses a 64-bit buffer for efficient multi-bit reads.
 */
class BitReader {
public:
    explicit BitReader(ByteSpan data)
        : data_(data.data())
        , size_(data.size())
        , pos_(0)
        , buffer_(0)
        , bit_count_(0)
    {
        refill();
    }

    /**
     * Read `count` bits (MSB-first)
     * @param count  Number of bits to read (1-57)
     * @return The value of the bits
     */
    [[nodiscard]] uint64_t read_bits(size_t count) {
        // Ensure we have enough bits
        if (bit_count_ < count) {
            refill();
        }

        // Extract bits from MSB side
        uint64_t value = buffer_ >> (64 - count);
        buffer_ <<= count;
        bit_count_ -= count;
        consumed_ += count;

        return value;
    }

    /**
     * Read a single bit
     */
    [[nodiscard]] bool read_bit() {
        return read_bits(1) != 0;
    }

    /**
     * Read a full byte (8 bits)
     */
    [[nodiscard]] Byte read_byte() {
        return static_cast<Byte>(read_bits(8));
    }

    /**
     * Read multiple bytes into buffer
     */
    void read_bytes(MutableByteSpan out) {
        for (Byte& b : out) {
            b = read_byte();
        }
    }

    /**
     * Read a variable-length integer
     */
    [[nodiscard]] uint64_t read_varint() {
        uint64_t value = 0;
        size_t shift = 0;
        Byte byte;
        do {
            byte = read_byte();
            value |= static_cast<uint64_t>(byte & 0x7F) << shift;
            shift += 7;
        } while (byte & 0x80);
        return value;
    }

    /**
     * Peek at next `count` bits without consuming
     */
    [[nodiscard]] uint64_t peek_bits(size_t count) {
        if (bit_count_ < count) {
            refill();
        }
        return buffer_ >> (64 - count);
    }

    /**
     * Skip `count` bits
     */
    void skip_bits(size_t count) {
        while (count > 0) {
            if (bit_count_ < count) {
                count -= bit_count_;
                consumed_ += bit_count_;
                bit_count_ = 0;
                buffer_ = 0;
                refill();
            } else {
                buffer_ <<= count;
                bit_count_ -= count;
                consumed_ += count;
                count = 0;
            }
        }
    }

    /**
     * Align to byte boundary (skip remaining bits in current byte)
     */
    void align_to_byte() {
        size_t skip = bit_count_ % 8;
        if (skip > 0) {
            buffer_ <<= skip;
            bit_count_ -= skip;
            consumed_ += skip;
        }
    }

    /**
     * Get current bit position in stream
     */
    [[nodiscard]] size_t bit_position() const {
        return consumed_;
    }

    /**
     * Check if end of stream reached
     */
    [[nodiscard]] bool eof() const {
        return consumed_ >= size_ * 8;
    }

    /**
     * Get remaining bytes (approximate)
     */
    [[nodiscard]] size_t remaining_bytes() const {
        return consumed_ < size_ * 8 ? (size_ * 8 - consumed_) / 8 : 0;
    }

private:
    /**
     * Refill buffer from input
     */
    void refill() {
        // Past the end the stream reads as zero bits, so truncated or corrupt
        // input can never stall a decoder; callers detect it via eof() or CRC
        while (bit_count_ <= 56) {
            if (pos_ < size_) {
                buffer_ |= static_cast<uint64_t>(data_[pos_++]) << (56 - bit_count_);
            }
            bit_count_ += 8;
        }
    }

    const Byte* data_;
    size_t size_;
    size_t pos_;
    uint64_t buffer_;
    size_t bit_count_;
    size_t consumed_ = 0;
};

} // namespace compressum::core
