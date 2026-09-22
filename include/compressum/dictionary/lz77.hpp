#pragma once

/**
 * CompressUM - Original Compression Algorithm
 * LZ77 Dictionary Compression
 *
 * Implements LZ77-style compression with:
 * - Sliding window dictionary
 * - Hash chain match finding
 * - Lazy matching for better compression
 * - Configurable compression levels
 *
 * Output format:
 * - Literal: flag=0, followed by byte value
 * - Match: flag=1, followed by (length, distance) pair
 *
 * Length and distance are encoded using variable-length codes:
 * - Length: 3-258 (stored as 0-255 + 3)
 * - Distance: 1-32768 (various encoding schemes)
 */

#include "../config.hpp"
#include "../types.hpp"
#include "../core/bitstream.hpp"
#include "hashchain.hpp"
#include <vector>
#include <algorithm>

namespace compressum::dict {

/**
 * LZ77 token: either a literal byte or a match reference
 */
struct LZ77Token {
    enum Type : uint8_t { Literal, Match };

    Type type;
    union {
        Byte literal;
        struct {
            uint16_t length;
            uint16_t distance;
        } match;
    };

    static LZ77Token make_literal(Byte b) {
        LZ77Token t;
        t.type = Literal;
        t.literal = b;
        return t;
    }

    static LZ77Token make_match(uint16_t len, uint16_t dist) {
        LZ77Token t;
        t.type = Match;
        t.match.length = len;
        t.match.distance = dist;
        return t;
    }
};

/**
 * LZ77 Configuration
 */
struct LZ77Config {
    size_t window_size = config::LZ77_WINDOW_SIZE;
    size_t min_match = config::LZ77_MIN_MATCH;
    size_t max_match = config::LZ77_MAX_MATCH;
    size_t max_chain = config::LZ77_MAX_CHAIN;
    bool lazy_matching = true;
    Level level = Level::Normal;
};

/**
 * LZ77 Compressor
 */
class LZ77Compressor {
public:
    explicit LZ77Compressor(const LZ77Config& config = {})
        : config_(config)
    {
        // Adjust hash chain config based on compression level
        HashChainConfig hc_config;
        hc_config.window_size = config.window_size;
        hc_config.min_match = config.min_match;
        hc_config.max_match = config.max_match;

        switch (config.level) {
            case Level::Fast:
                hc_config.max_chain = 4;
                config_.lazy_matching = false;
                break;
            case Level::Normal:
                hc_config.max_chain = 32;
                config_.lazy_matching = true;
                break;
            case Level::Best:
                hc_config.max_chain = 128;
                config_.lazy_matching = true;
                break;
        }

        hash_chain_ = HashChain(hc_config);
    }

    /**
     * Compress data to tokens
     * @param input Input data
     * @return Vector of LZ77 tokens
     */
    [[nodiscard]] std::vector<LZ77Token> compress(ByteSpan input) {
        std::vector<LZ77Token> tokens;
        tokens.reserve(input.size() / 2);  // Estimate

        hash_chain_.reset();

        const Byte* data = input.data();
        const size_t size = input.size();
        size_t pos = 0;

        while (pos < size) {
            size_t remaining = size - pos;
            uint32_t max_len = static_cast<uint32_t>(std::min(remaining, config_.max_match));

            if (max_len < config_.min_match) {
                // Not enough data for a match - emit literal
                tokens.push_back(LZ77Token::make_literal(data[pos]));
                if (pos + config_.min_match - 1 < size) {
                    hash_chain_.insert(data, static_cast<uint32_t>(pos));
                }
                ++pos;
                continue;
            }

            Match match = hash_chain_.find_match(data, static_cast<uint32_t>(pos), max_len);

            if (!match.valid()) {
                // No match found - emit literal
                tokens.push_back(LZ77Token::make_literal(data[pos]));
                hash_chain_.insert(data, static_cast<uint32_t>(pos));
                ++pos;
                continue;
            }

            // Lazy matching: check if next position has better match
            if (config_.lazy_matching && pos + 1 < size) {
                hash_chain_.insert(data, static_cast<uint32_t>(pos));

                uint32_t next_max = static_cast<uint32_t>(std::min(size - pos - 1, config_.max_match));
                Match next_match = hash_chain_.find_match(data, static_cast<uint32_t>(pos + 1), next_max);

                if (next_match.length > match.length + 1) {
                    // Next match is better - emit literal now
                    tokens.push_back(LZ77Token::make_literal(data[pos]));
                    ++pos;
                    continue;
                }
            }

            // Emit match
            tokens.push_back(LZ77Token::make_match(
                static_cast<uint16_t>(match.length),
                static_cast<uint16_t>(match.distance)
            ));

            // Insert all positions covered by match into hash chain
            for (size_t i = pos; i < pos + match.length && i + config_.min_match - 1 < size; ++i) {
                hash_chain_.insert(data, static_cast<uint32_t>(i));
            }

            pos += match.length;
        }

        return tokens;
    }

    /**
     * Compress and encode to byte stream
     * @param input Input data
     * @return Compressed bytes
     */
    [[nodiscard]] std::vector<Byte> compress_to_bytes(ByteSpan input) {
        auto tokens = compress(input);

        core::BitWriter writer;
        encode_tokens(writer, tokens);
        writer.flush();

        return writer.take_data();
    }

    /**
     * Get configuration
     */
    [[nodiscard]] const LZ77Config& config() const { return config_; }

private:
    /**
     * Encode tokens to bit stream
     */
    void encode_tokens(core::BitWriter& writer, const std::vector<LZ77Token>& tokens) {
        for (const auto& token : tokens) {
            if (token.type == LZ77Token::Literal) {
                // Literal: 0 bit + 8 bits for byte
                writer.write_bit(false);
                writer.write_byte(token.literal);
            } else {
                // Match: 1 bit + encoded length + encoded distance
                writer.write_bit(true);
                encode_length(writer, token.match.length);
                encode_distance(writer, token.match.distance);
            }
        }
    }

    /**
     * Encode match length (3-258)
     * Simple encoding: 8 bits for (length - 3)
     */
    void encode_length(core::BitWriter& writer, uint16_t length) {
        writer.write_bits(length - 3, 8);  // 0-255 covers lengths 3-258
    }

    /**
     * Encode match distance (1-32768)
     * Simple encoding: 15 bits for (distance - 1)
     */
    void encode_distance(core::BitWriter& writer, uint16_t distance) {
        writer.write_bits(distance - 1, 15);  // 0-32767 covers distances 1-32768
    }

    LZ77Config config_;
    HashChain hash_chain_;
};

/**
 * LZ77 Decompressor
 */
class LZ77Decompressor {
public:
    explicit LZ77Decompressor(const LZ77Config& config = {})
        : config_(config)
    {}

    /**
     * Decompress from tokens
     */
    [[nodiscard]] std::vector<Byte> decompress(const std::vector<LZ77Token>& tokens) {
        std::vector<Byte> output;
        output.reserve(tokens.size() * 2);

        for (const auto& token : tokens) {
            if (token.type == LZ77Token::Literal) {
                output.push_back(token.literal);
            } else {
                // Copy match from earlier in output
                size_t copy_pos = output.size() - token.match.distance;
                for (uint16_t i = 0; i < token.match.length; ++i) {
                    output.push_back(output[copy_pos + i]);
                }
            }
        }

        return output;
    }

    /**
     * Decompress from byte stream
     */
    [[nodiscard]] std::vector<Byte> decompress_from_bytes(ByteSpan input, size_t original_size) {
        core::BitReader reader(input);

        std::vector<Byte> output;
        output.reserve(original_size);

        while (output.size() < original_size) {
            bool is_match = reader.read_bit();

            if (!is_match) {
                // Literal
                output.push_back(reader.read_byte());
            } else {
                // Match
                uint16_t length = decode_length(reader);
                uint16_t distance = decode_distance(reader);

                if (distance == 0 || distance > output.size()) {
                    break;  // Corrupt stream: distance outside produced data
                }
                size_t copy_pos = output.size() - distance;
                for (uint16_t i = 0; i < length; ++i) {
                    output.push_back(output[copy_pos + i]);
                }
            }
        }

        return output;
    }

private:
    [[nodiscard]] uint16_t decode_length(core::BitReader& reader) {
        // Simple decoding: 8 bits for (length - 3)
        return static_cast<uint16_t>(reader.read_bits(8) + 3);
    }

    [[nodiscard]] uint16_t decode_distance(core::BitReader& reader) {
        // Simple decoding: 15 bits for (distance - 1)
        return static_cast<uint16_t>(reader.read_bits(15) + 1);
    }

    LZ77Config config_;
};

} // namespace compressum::dict
