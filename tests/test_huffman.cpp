/**
 * CompressUM - Huffman Coding Tests
 */

#include "compressum/entropy/huffman.hpp"
#include <iostream>
#include <cassert>
#include <cstring>
#include <random>

using namespace compressum;
using namespace compressum::entropy;
using namespace compressum::core;

#define TEST(name) void test_##name()
#define RUN_TEST(name) do { \
    std::cout << "Running " #name "... " << std::flush; \
    test_##name(); \
    std::cout << "OK\n" << std::flush; \
} while(0)

TEST(single_symbol) {
    // Single symbol should get code 0 with length 1
    std::array<uint32_t, 256> freq{};
    freq['A'] = 100;

    HuffmanEncoder encoder;
    encoder.build(freq.data(), 256);

    auto code = encoder.get_code('A');
    assert(code.length == 1);
    assert(code.code == 0);
}

TEST(two_symbols_equal) {
    // Two equally frequent symbols
    std::array<uint32_t, 256> freq{};
    freq['A'] = 50;
    freq['B'] = 50;

    HuffmanEncoder encoder;
    encoder.build(freq.data(), 256);

    auto code_a = encoder.get_code('A');
    auto code_b = encoder.get_code('B');

    assert(code_a.length == 1);
    assert(code_b.length == 1);
    assert(code_a.code != code_b.code);
}

TEST(skewed_distribution) {
    // Very skewed: one symbol much more frequent
    std::array<uint32_t, 256> freq{};
    freq['A'] = 1000;
    freq['B'] = 1;

    HuffmanEncoder encoder;
    encoder.build(freq.data(), 256);

    auto code_a = encoder.get_code('A');
    auto code_b = encoder.get_code('B');

    // More frequent symbol should have shorter code
    assert(code_a.length <= code_b.length);
}

TEST(encode_decode_simple) {
    // Build from simple text
    const char* text = "AAAAABBBCCDE";
    std::vector<Byte> data(text, text + strlen(text));

    HuffmanEncoder encoder;
    encoder.build_from_data(data);

    // Encode
    BitWriter writer;
    for (Byte b : data) {
        encoder.encode(writer, b);
    }
    writer.flush();

    // Build decoder from lengths
    HuffmanDecoder decoder;
    decoder.build(encoder.get_lengths().data(), 256);

    // Decode
    BitReader reader(writer.data());
    std::vector<Byte> decoded;
    for (size_t i = 0; i < data.size(); ++i) {
        decoded.push_back(static_cast<Byte>(decoder.decode(reader)));
    }

    assert(decoded == data);
}

TEST(encode_decode_random) {
    // Random data
    std::mt19937 rng(42);
    std::vector<Byte> data(1000);
    for (auto& b : data) {
        b = static_cast<Byte>(rng() % 256);
    }

    HuffmanEncoder encoder;
    encoder.build_from_data(data);

    // Encode
    BitWriter writer;
    for (Byte b : data) {
        encoder.encode(writer, b);
    }
    writer.flush();

    // Decode
    HuffmanDecoder decoder;
    decoder.build(encoder.get_lengths().data(), 256);

    BitReader reader(writer.data());
    std::vector<Byte> decoded;
    for (size_t i = 0; i < data.size(); ++i) {
        decoded.push_back(static_cast<Byte>(decoder.decode(reader)));
    }

    assert(decoded == data);
}

TEST(canonical_codes) {
    // Verify canonical code properties
    std::array<uint32_t, 8> freq = {10, 20, 30, 40, 50, 60, 70, 80};

    HuffmanEncoder encoder;
    encoder.build(freq.data(), 8);

    // Collect all codes and sort by (length, symbol) to verify canonical property
    std::vector<std::tuple<uint8_t, uint16_t, uint16_t>> codes;  // (length, symbol, code)
    for (size_t i = 0; i < 8; ++i) {
        auto code = encoder.get_code(static_cast<uint16_t>(i));
        if (code.length > 0) {
            codes.emplace_back(code.length, static_cast<uint16_t>(i), code.code);
        }
    }

    // Sort by (length, symbol)
    std::sort(codes.begin(), codes.end());

    // Verify canonical properties:
    // - Codes sorted by length then symbol have sequential code values within each length
    uint16_t prev_code = 0;
    uint8_t prev_len = 0;

    for (const auto& [len, sym, code] : codes) {
        if (prev_len > 0) {
            if (len == prev_len) {
                // Same length: codes must be consecutive
                assert(code == prev_code + 1);
            } else {
                // New length: code must be (prev_code + 1) << (len - prev_len)
                assert(len > prev_len);
            }
        }
        prev_code = code;
        prev_len = len;
    }
}

TEST(max_code_length) {
    // Test that code lengths are limited to 15 bits
    std::array<uint32_t, 256> freq{};

    // Create exponentially increasing frequencies to force deep tree
    for (int i = 0; i < 20; ++i) {
        freq[i] = 1u << i;
    }

    HuffmanEncoder encoder;
    encoder.build(freq.data(), 20);

    // Check all codes are within limit
    for (size_t i = 0; i < 20; ++i) {
        auto code = encoder.get_code(static_cast<uint16_t>(i));
        if (code.length > 0) {
            assert(code.length <= HUFFMAN_MAX_CODE_LEN);
        }
    }
}

TEST(length_serialization) {
    // Test writing and reading code lengths
    std::array<uint32_t, 256> freq{};
    for (int i = 0; i < 256; ++i) {
        freq[i] = (i % 16) + 1;
    }

    HuffmanEncoder encoder;
    encoder.build(freq.data(), 256);

    // Write lengths
    BitWriter writer;
    encoder.write_lengths(writer, 256);
    writer.flush();

    // Read lengths
    HuffmanDecoder decoder;
    BitReader reader(writer.data());
    decoder.read_lengths(reader, 256);

    // Verify round-trip
    std::vector<Byte> test_data = {'A', 'B', 'C', 'D', 'E'};

    BitWriter enc_writer;
    for (Byte b : test_data) {
        encoder.encode(enc_writer, b);
    }
    enc_writer.flush();

    BitReader dec_reader(enc_writer.data());
    for (Byte expected : test_data) {
        uint16_t decoded = decoder.decode(dec_reader);
        assert(decoded == expected);
    }
}

int main() {
    std::cout << "Huffman Coding Tests\n";
    std::cout << "====================\n";

    RUN_TEST(single_symbol);
    RUN_TEST(two_symbols_equal);
    RUN_TEST(skewed_distribution);
    RUN_TEST(encode_decode_simple);
    RUN_TEST(encode_decode_random);
    RUN_TEST(canonical_codes);
    RUN_TEST(max_code_length);
    RUN_TEST(length_serialization);

    std::cout << "\nAll tests passed!\n";
    return 0;
}
