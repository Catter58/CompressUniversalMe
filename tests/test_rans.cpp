/**
 * CompressUM - rANS Coding Tests
 */

#include "compressum/entropy/rans.hpp"
#include <iostream>
#include <cassert>
#include <random>
#include <algorithm>

using namespace compressum;
using namespace compressum::entropy;

#define TEST(name) void test_##name()
#define RUN_TEST(name) do { \
    std::cout << "Running " #name "... " << std::flush; \
    test_##name(); \
    std::cout << "OK\n" << std::flush; \
} while(0)

TEST(table_uniform) {
    // Uniform distribution
    std::array<uint32_t, 256> freq{};
    for (auto& f : freq) f = 1;

    RansTable table;
    table.build(freq.data(), 256);

    // Check that all symbols have same frequency
    auto sym0 = table.get_symbol(0);
    auto sym1 = table.get_symbol(1);
    assert(sym0.freq == sym1.freq);
}

TEST(table_skewed) {
    // Skewed distribution
    std::array<uint32_t, 4> freq = {1000, 100, 10, 1};

    RansTable table;
    table.build(freq.data(), 4);

    // Higher frequency should have higher scaled freq
    auto sym0 = table.get_symbol(0);
    auto sym1 = table.get_symbol(1);
    auto sym2 = table.get_symbol(2);
    auto sym3 = table.get_symbol(3);

    assert(sym0.freq > sym1.freq);
    assert(sym1.freq > sym2.freq);
    assert(sym2.freq >= sym3.freq);

    // Cumulative frequencies should be correct
    assert(sym0.cumfreq == 0);
    assert(sym1.cumfreq == sym0.freq);
    assert(sym2.cumfreq == sym0.freq + sym1.freq);
}

TEST(encode_decode_single) {
    // Single symbol
    std::array<uint32_t, 256> freq{};
    freq['A'] = 100;

    RansTable table;
    table.build(freq.data(), 256);

    RansEncoder encoder;
    encoder.init();
    encoder.encode(table, 'A');
    encoder.finish();

    auto output = encoder.get_output_reversed();

    RansDecoder decoder;
    decoder.init(output);
    uint8_t decoded = decoder.decode(table);

    assert(decoded == 'A');
}

TEST(encode_decode_simple) {
    const char* text = "AAABBC";
    std::vector<Byte> data(text, text + strlen(text));

    RansTable table;
    table.build_from_data(data);

    // Encode in reverse order (rANS requirement)
    RansEncoder encoder;
    encoder.init();
    for (auto it = data.rbegin(); it != data.rend(); ++it) {
        encoder.encode(table, *it);
    }
    encoder.finish();

    auto output = encoder.get_output_reversed();

    // Decode
    RansDecoder decoder;
    decoder.init(output);

    std::vector<Byte> decoded;
    for (size_t i = 0; i < data.size(); ++i) {
        decoded.push_back(decoder.decode(table));
    }

    assert(decoded == data);
}

TEST(encode_decode_random) {
    std::mt19937 rng(42);
    std::vector<Byte> data(1000);
    for (auto& b : data) {
        b = static_cast<Byte>(rng() % 256);
    }

    RansTable table;
    table.build_from_data(data);

    // Encode in reverse
    RansEncoder encoder;
    encoder.init();
    for (auto it = data.rbegin(); it != data.rend(); ++it) {
        encoder.encode(table, *it);
    }
    encoder.finish();

    auto output = encoder.get_output_reversed();

    // Decode
    RansDecoder decoder;
    decoder.init(output);

    std::vector<Byte> decoded;
    for (size_t i = 0; i < data.size(); ++i) {
        decoded.push_back(decoder.decode(table));
    }

    assert(decoded == data);
}

TEST(table_serialization) {
    std::array<uint32_t, 256> freq{};
    for (int i = 0; i < 256; ++i) {
        freq[i] = (i % 32) + 1;
    }

    RansTable table1;
    table1.build(freq.data(), 256);

    // Serialize
    std::vector<Byte> serialized;
    table1.write_table(serialized);

    // Deserialize
    RansTable table2;
    table2.read_table(serialized);

    // Compare: encode/decode should produce same results
    std::vector<Byte> test_data = {0, 50, 100, 150, 200, 255};

    RansEncoder enc1, enc2;
    enc1.init(); enc2.init();

    for (auto it = test_data.rbegin(); it != test_data.rend(); ++it) {
        enc1.encode(table1, *it);
        enc2.encode(table2, *it);
    }
    enc1.finish(); enc2.finish();

    auto out1 = enc1.get_output_reversed();
    auto out2 = enc2.get_output_reversed();

    assert(out1 == out2);
}

TEST(compression_ratio) {
    // Test that rANS achieves good compression on skewed data
    std::vector<Byte> data(10000);
    std::mt19937 rng(123);

    // Skewed distribution: 90% 'A', 10% others
    for (auto& b : data) {
        if (rng() % 10 == 0) {
            b = static_cast<Byte>(rng() % 256);
        } else {
            b = 'A';
        }
    }

    RansTable table;
    table.build_from_data(data);

    RansEncoder encoder;
    encoder.init();
    for (auto it = data.rbegin(); it != data.rend(); ++it) {
        encoder.encode(table, *it);
    }
    encoder.finish();

    auto output = encoder.get_output_reversed();

    // Should compress well (< 50% of original)
    double ratio = static_cast<double>(output.size()) / data.size();
    std::cout << "(ratio: " << ratio << ") ";
    assert(ratio < 0.5);
}

TEST(interleaved_encode_decode) {
    std::mt19937 rng(42);
    std::vector<Byte> data(400);  // Multiple of 4 for interleaving
    for (auto& b : data) {
        b = static_cast<Byte>(rng() % 256);
    }

    RansTable table;
    table.build_from_data(data);

    // Encode with interleaved encoder
    InterleavedRansEncoder encoder;
    encoder.init();
    encoder.encode(table, data.data(), data.size());
    encoder.finish();

    auto output = encoder.get_output();

    // Decode
    InterleavedRansDecoder decoder;
    decoder.init(output);

    std::vector<uint8_t> decoded(data.size());
    decoder.decode(table, decoded.data(), decoded.size());

    std::vector<Byte> decoded_bytes(decoded.begin(), decoded.end());
    assert(decoded_bytes == data);
}

TEST(edge_cases) {
    // Empty data
    {
        std::vector<Byte> data;
        RansTable table;
        std::array<uint32_t, 256> freq{};
        freq[0] = 1;  // At least one symbol
        table.build(freq.data(), 256);

        RansEncoder encoder;
        encoder.init();
        encoder.finish();
        auto output = encoder.get_output_reversed();

        assert(output.size() == 4);  // Just the final state
    }

    // Single byte
    {
        std::vector<Byte> data = {0x42};
        RansTable table;
        table.build_from_data(data);

        RansEncoder encoder;
        encoder.init();
        encoder.encode(table, 0x42);
        encoder.finish();

        auto output = encoder.get_output_reversed();

        RansDecoder decoder;
        decoder.init(output);
        assert(decoder.decode(table) == 0x42);
    }
}

int main() {
    std::cout << "rANS Coding Tests\n";
    std::cout << "=================\n";

    RUN_TEST(table_uniform);
    RUN_TEST(table_skewed);
    RUN_TEST(encode_decode_single);
    RUN_TEST(encode_decode_simple);
    RUN_TEST(encode_decode_random);
    RUN_TEST(table_serialization);
    RUN_TEST(compression_ratio);
    RUN_TEST(interleaved_encode_decode);
    RUN_TEST(edge_cases);

    std::cout << "\nAll tests passed!\n";
    return 0;
}
