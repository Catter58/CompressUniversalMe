/**
 * CompressUM - Deflate Tests
 * Tests for deflate/inflate implementation
 */

#include "compressum/transform/deflate.hpp"
#include <iostream>
#include <cassert>
#include <cstring>
#include <random>

using namespace compressum;
using namespace compressum::transform;

#define TEST(name) void test_##name()
#define RUN_TEST(name) do { \
    std::cout << "Running " #name "... " << std::flush; \
    test_##name(); \
    std::cout << "OK\n" << std::flush; \
} while(0)

// ============================================================================
// Basic Roundtrip Tests
// ============================================================================

TEST(empty_data) {
    std::vector<Byte> input;

    Deflater deflater;
    auto compressed = deflater.deflate(input);

    Inflater inflater;
    auto decompressed = inflater.inflate(compressed);

    assert(decompressed.empty());
}

TEST(single_byte) {
    std::vector<Byte> input = {0x42};

    Deflater deflater;
    auto compressed = deflater.deflate(input);

    Inflater inflater;
    auto decompressed = inflater.inflate(compressed);

    assert(decompressed == input);
}

TEST(simple_text) {
    const char* text = "Hello, World!";
    std::vector<Byte> input(text, text + strlen(text));

    Deflater deflater;
    auto compressed = deflater.deflate(input);

    Inflater inflater;
    auto decompressed = inflater.inflate(compressed);

    assert(decompressed == input);
}

TEST(repeated_data) {
    // Data with good compression potential
    std::string text = "ABCABCABCABCABCABCABC";
    for (int i = 0; i < 5; ++i) {
        text += text;
    }
    std::vector<Byte> input(text.begin(), text.end());

    Deflater deflater;
    auto compressed = deflater.deflate(input);

    // Should compress
    assert(compressed.size() < input.size());

    Inflater inflater;
    auto decompressed = inflater.inflate(compressed);

    assert(decompressed == input);
}

TEST(all_same_bytes) {
    std::vector<Byte> input(1000, 0xAA);

    Deflater deflater;
    auto compressed = deflater.deflate(input);

    // Should compress very well
    assert(compressed.size() < input.size() / 2);

    Inflater inflater;
    auto decompressed = inflater.inflate(compressed);

    assert(decompressed == input);
}

TEST(random_data) {
    std::mt19937 rng(42);
    std::vector<Byte> input(1000);
    for (auto& b : input) {
        b = static_cast<Byte>(rng() % 256);
    }

    Deflater deflater;
    auto compressed = deflater.deflate(input);

    Inflater inflater;
    auto decompressed = inflater.inflate(compressed);

    assert(decompressed == input);
}

TEST(binary_pattern) {
    // Structured binary data
    std::vector<Byte> input;
    for (int i = 0; i < 100; ++i) {
        input.push_back(0x01);  // Marker
        input.push_back(static_cast<Byte>(i & 0xFF));
        for (int j = 0; j < 8; ++j) {
            input.push_back(static_cast<Byte>((i + j) % 256));
        }
    }

    Deflater deflater;
    auto compressed = deflater.deflate(input);

    Inflater inflater;
    auto decompressed = inflater.inflate(compressed);

    assert(decompressed == input);
}

// ============================================================================
// Zlib Wrapper Tests
// ============================================================================

TEST(zlib_roundtrip) {
    const char* text = "Test data for zlib wrapper";
    std::vector<Byte> input(text, text + strlen(text));

    Deflater deflater;
    auto compressed = deflater.deflate_zlib(input);

    // Check zlib header
    assert(compressed.size() >= 6);
    assert(compressed[0] == 0x78);  // CMF

    Inflater inflater;
    auto decompressed = inflater.inflate_zlib(compressed);

    assert(decompressed == input);
}

TEST(zlib_repeated) {
    std::string text = "Repeated text for zlib. ";
    for (int i = 0; i < 6; ++i) {
        text += text;
    }
    std::vector<Byte> input(text.begin(), text.end());

    Deflater deflater;
    auto compressed = deflater.deflate_zlib(input);

    Inflater inflater;
    auto decompressed = inflater.inflate_zlib(compressed);

    assert(decompressed == input);
}

// ============================================================================
// Edge Cases
// ============================================================================

TEST(max_match_length) {
    // Create data with long matches
    std::vector<Byte> pattern = {0x12, 0x34, 0x56, 0x78};
    std::vector<Byte> input;
    for (int i = 0; i < 100; ++i) {
        input.insert(input.end(), pattern.begin(), pattern.end());
    }

    Deflater deflater;
    auto compressed = deflater.deflate(input);

    Inflater inflater;
    auto decompressed = inflater.inflate(compressed);

    assert(decompressed == input);
}

TEST(various_distances) {
    // Create data requiring various back-reference distances
    std::vector<Byte> input;
    for (int d = 1; d <= 100; ++d) {
        // Add unique bytes then reference back
        for (int i = 0; i < d; ++i) {
            input.push_back(static_cast<Byte>((d + i) % 256));
        }
        // Repeat pattern
        for (int i = 0; i < 3; ++i) {
            input.push_back(input[input.size() - d]);
        }
    }

    Deflater deflater;
    auto compressed = deflater.deflate(input);

    Inflater inflater;
    auto decompressed = inflater.inflate(compressed);

    assert(decompressed == input);
}

// ============================================================================
// Main
// ============================================================================

int main() {
    std::cout << "Deflate Tests\n";
    std::cout << "=============\n";

    // Basic roundtrip tests
    RUN_TEST(empty_data);
    RUN_TEST(single_byte);
    RUN_TEST(simple_text);
    RUN_TEST(repeated_data);
    RUN_TEST(all_same_bytes);
    RUN_TEST(random_data);
    RUN_TEST(binary_pattern);

    // Zlib wrapper tests
    RUN_TEST(zlib_roundtrip);
    RUN_TEST(zlib_repeated);

    // Edge cases
    RUN_TEST(max_match_length);
    RUN_TEST(various_distances);

    std::cout << "\nAll deflate tests passed!\n";
    return 0;
}
