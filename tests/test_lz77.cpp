/**
 * CompressUM - LZ77 Compression Tests
 */

#include "compressum/dictionary/lz77.hpp"
#include "compressum/dictionary/hashchain.hpp"
#include <iostream>
#include <cassert>
#include <random>
#include <cstring>

using namespace compressum;
using namespace compressum::dict;

#define TEST(name) void test_##name()
#define RUN_TEST(name) do { \
    std::cout << "Running " #name "... "; \
    test_##name(); \
    std::cout << "OK\n"; \
} while(0)

TEST(hashchain_basic) {
    HashChainConfig config;
    HashChain hc(config);

    std::vector<Byte> data = {'A', 'B', 'C', 'D', 'A', 'B', 'C', 'D'};

    // Insert first occurrence
    for (uint32_t i = 0; i < 4; ++i) {
        hc.insert(data.data(), i);
    }

    // Find match at position 4
    Match match = hc.find_match(data.data(), 4, 4);

    assert(match.valid());
    assert(match.length >= 3);  // Should find ABC or ABCD
    assert(match.distance == 4); // Distance to position 0
}

TEST(hashchain_no_match) {
    HashChainConfig config;
    HashChain hc(config);

    std::vector<Byte> data = {'A', 'B', 'C', 'D', 'E', 'F', 'G', 'H'};

    for (uint32_t i = 0; i < 4; ++i) {
        hc.insert(data.data(), i);
    }

    // No match at position 4 (different bytes)
    Match match = hc.find_match(data.data(), 4, 4);
    assert(!match.valid());
}

TEST(lz77_compress_simple) {
    LZ77Config config;
    config.level = Level::Normal;
    LZ77Compressor compressor(config);

    // Repetitive data should compress
    const char* text = "ABCDABCDABCDABCD";
    std::vector<Byte> data(text, text + strlen(text));

    auto tokens = compressor.compress(data);

    // Should have some matches
    size_t match_count = 0;
    for (const auto& token : tokens) {
        if (token.type == LZ77Token::Match) {
            ++match_count;
        }
    }

    assert(match_count > 0);
}

TEST(lz77_roundtrip_simple) {
    LZ77Config config;
    LZ77Compressor compressor(config);
    LZ77Decompressor decompressor(config);

    const char* text = "Hello, Hello, World! World!";
    std::vector<Byte> original(text, text + strlen(text));

    auto tokens = compressor.compress(original);
    auto decompressed = decompressor.decompress(tokens);

    assert(original == decompressed);
}

TEST(lz77_roundtrip_repetitive) {
    LZ77Config config;
    LZ77Compressor compressor(config);
    LZ77Decompressor decompressor(config);

    // Highly repetitive data
    std::vector<Byte> original(1000, 'A');

    auto tokens = compressor.compress(original);
    auto decompressed = decompressor.decompress(tokens);

    assert(original == decompressed);

    // Should compress well (few tokens for 1000 bytes)
    std::cout << "(tokens: " << tokens.size() << ") ";
    assert(tokens.size() < 100);
}

TEST(lz77_roundtrip_random) {
    LZ77Config config;
    LZ77Compressor compressor(config);
    LZ77Decompressor decompressor(config);

    std::mt19937 rng(42);
    std::vector<Byte> original(1000);
    for (auto& b : original) {
        b = static_cast<Byte>(rng() % 256);
    }

    auto tokens = compressor.compress(original);
    auto decompressed = decompressor.decompress(tokens);

    assert(original == decompressed);
}

TEST(lz77_roundtrip_text) {
    LZ77Config config;
    LZ77Compressor compressor(config);
    LZ77Decompressor decompressor(config);

    // Typical text with repetition
    const char* text =
        "The quick brown fox jumps over the lazy dog. "
        "The quick brown fox jumps over the lazy dog. "
        "The quick brown fox jumps over the lazy dog. ";

    std::vector<Byte> original(text, text + strlen(text));

    auto tokens = compressor.compress(original);
    auto decompressed = decompressor.decompress(tokens);

    assert(original == decompressed);

    // Check compression ratio
    double ratio = static_cast<double>(tokens.size()) / original.size();
    std::cout << "(ratio: " << ratio << ") ";
    assert(ratio < 0.8);  // Should compress
}

TEST(lz77_compression_levels) {
    const char* text =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
        "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
        "ABCDEFGHIJKLMNOPQRSTUVWXYZ";

    std::vector<Byte> data(text, text + strlen(text));

    for (Level level : {Level::Fast, Level::Normal, Level::Best}) {
        LZ77Config config;
        config.level = level;
        LZ77Compressor compressor(config);
        LZ77Decompressor decompressor(config);

        auto tokens = compressor.compress(data);
        auto decompressed = decompressor.decompress(tokens);

        assert(data == decompressed);
    }
}

TEST(lz77_bytes_roundtrip) {
    LZ77Config config;
    LZ77Compressor compressor(config);
    LZ77Decompressor decompressor(config);

    const char* text = "Hello World Hello World Hello World";
    std::vector<Byte> original(text, text + strlen(text));

    auto compressed = compressor.compress_to_bytes(original);
    auto decompressed = decompressor.decompress_from_bytes(compressed, original.size());

    assert(original == decompressed);
}

TEST(lz77_empty_input) {
    LZ77Config config;
    LZ77Compressor compressor(config);
    LZ77Decompressor decompressor(config);

    std::vector<Byte> original;
    auto tokens = compressor.compress(original);
    auto decompressed = decompressor.decompress(tokens);

    assert(original == decompressed);
    assert(tokens.empty());
}

TEST(lz77_single_byte) {
    LZ77Config config;
    LZ77Compressor compressor(config);
    LZ77Decompressor decompressor(config);

    std::vector<Byte> original = {0x42};
    auto tokens = compressor.compress(original);
    auto decompressed = decompressor.decompress(tokens);

    assert(original == decompressed);
    assert(tokens.size() == 1);
    assert(tokens[0].type == LZ77Token::Literal);
}

TEST(lz77_long_match) {
    LZ77Config config;
    LZ77Compressor compressor(config);
    LZ77Decompressor decompressor(config);

    // Create pattern that should produce long match
    std::vector<Byte> original;
    for (int i = 0; i < 100; ++i) {
        original.push_back(static_cast<Byte>(i % 26 + 'A'));
    }
    // Duplicate
    original.insert(original.end(), original.begin(), original.end());

    auto tokens = compressor.compress(original);
    auto decompressed = decompressor.decompress(tokens);

    assert(original == decompressed);

    // Should have at least one long match
    bool has_long_match = false;
    for (const auto& token : tokens) {
        if (token.type == LZ77Token::Match && token.match.length >= 50) {
            has_long_match = true;
            break;
        }
    }
    assert(has_long_match);
}

int main() {
    std::cout << "LZ77 Compression Tests\n";
    std::cout << "======================\n";

    RUN_TEST(hashchain_basic);
    RUN_TEST(hashchain_no_match);
    RUN_TEST(lz77_compress_simple);
    RUN_TEST(lz77_roundtrip_simple);
    RUN_TEST(lz77_roundtrip_repetitive);
    RUN_TEST(lz77_roundtrip_random);
    RUN_TEST(lz77_roundtrip_text);
    RUN_TEST(lz77_compression_levels);
    RUN_TEST(lz77_bytes_roundtrip);
    RUN_TEST(lz77_empty_input);
    RUN_TEST(lz77_single_byte);
    RUN_TEST(lz77_long_match);

    std::cout << "\nAll tests passed!\n";
    return 0;
}
