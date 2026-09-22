/**
 * CompressUM - Edge Case Tests
 *
 * Tests boundary conditions, malformed input, and corner cases
 * to ensure robust error handling.
 */

#include "compressum/compressum.hpp"
#include "compressum/transform/deflate.hpp"
#include <iostream>
#include <cassert>
#include <cstring>
#include <random>
#include <string>
#include <limits>

using namespace compressum;

#define TEST(name) void test_##name()
#define RUN_TEST(name) do { \
    std::cout << "Running " #name "... " << std::flush; \
    test_##name(); \
    std::cout << "OK\n" << std::flush; \
} while(0)

// ============================================================================
// Empty and Minimal Input Tests
// ============================================================================

TEST(empty_input) {
    std::vector<Byte> empty;

    Compressor c;
    auto [compressed, comp_result] = c.compress(empty);
    assert(comp_result.ok());
    assert(comp_result.original_size == 0);

    Decompressor d;
    auto [decompressed, decomp_result] = d.decompress(compressed);
    assert(decomp_result.ok());
    assert(decompressed.empty());
}

TEST(single_byte_all_values) {
    // Test all 256 possible single-byte inputs
    for (int i = 0; i < 256; ++i) {
        std::vector<Byte> input = {static_cast<Byte>(i)};

        Compressor c;
        auto [compressed, comp_result] = c.compress(input);
        assert(comp_result.ok());

        Decompressor d;
        auto [decompressed, decomp_result] = d.decompress(compressed);
        assert(decomp_result.ok());
        assert(decompressed == input);
    }
}

TEST(two_bytes) {
    std::vector<Byte> input = {0x00, 0xFF};

    Compressor c;
    auto [compressed, comp_result] = c.compress(input);
    assert(comp_result.ok());

    Decompressor d;
    auto [decompressed, decomp_result] = d.decompress(compressed);
    assert(decomp_result.ok());
    assert(decompressed == input);
}

TEST(three_bytes_min_match) {
    // Three bytes is the minimum LZ77 match length
    std::vector<Byte> input = {'A', 'B', 'C'};

    Compressor c;
    auto [compressed, comp_result] = c.compress(input);
    assert(comp_result.ok());

    Decompressor d;
    auto [decompressed, decomp_result] = d.decompress(compressed);
    assert(decomp_result.ok());
    assert(decompressed == input);
}

// ============================================================================
// Uniform Data Tests
// ============================================================================

TEST(all_zeros) {
    std::vector<Byte> input(10000, 0x00);

    Compressor c;
    auto [compressed, comp_result] = c.compress(input);
    assert(comp_result.ok());
    // Uniform data should compress to some degree (or at least not expand much)
    assert(comp_result.compressed_size <= input.size() + 100);

    Decompressor d;
    auto [decompressed, decomp_result] = d.decompress(compressed);
    assert(decomp_result.ok());
    assert(decompressed == input);
}

TEST(all_ones) {
    std::vector<Byte> input(10000, 0xFF);

    Compressor c;
    auto [compressed, comp_result] = c.compress(input);
    assert(comp_result.ok());
    // Uniform data should compress to some degree (or at least not expand much)
    assert(comp_result.compressed_size <= input.size() + 100);

    Decompressor d;
    auto [decompressed, decomp_result] = d.decompress(compressed);
    assert(decomp_result.ok());
    assert(decompressed == input);
}

TEST(each_byte_value_once) {
    // All 256 byte values exactly once
    std::vector<Byte> input(256);
    for (int i = 0; i < 256; ++i) {
        input[i] = static_cast<Byte>(i);
    }

    Compressor c;
    auto [compressed, comp_result] = c.compress(input);
    assert(comp_result.ok());

    Decompressor d;
    auto [decompressed, decomp_result] = d.decompress(compressed);
    assert(decomp_result.ok());
    assert(decompressed == input);
}

TEST(alternating_01) {
    std::vector<Byte> input(10000);
    for (size_t i = 0; i < input.size(); ++i) {
        input[i] = static_cast<Byte>(i % 2);
    }

    Compressor c;
    auto [compressed, comp_result] = c.compress(input);
    assert(comp_result.ok());

    Decompressor d;
    auto [decompressed, decomp_result] = d.decompress(compressed);
    assert(decomp_result.ok());
    assert(decompressed == input);
}

TEST(alternating_00FF) {
    std::vector<Byte> input(10000);
    for (size_t i = 0; i < input.size(); ++i) {
        input[i] = (i % 2 == 0) ? 0x00 : 0xFF;
    }

    Compressor c;
    auto [compressed, comp_result] = c.compress(input);
    assert(comp_result.ok());

    Decompressor d;
    auto [decompressed, decomp_result] = d.decompress(compressed);
    assert(decomp_result.ok());
    assert(decompressed == input);
}

// ============================================================================
// Pattern Tests
// ============================================================================

TEST(repeated_pattern_short) {
    // "ABC" repeated many times
    std::string pattern = "ABC";
    std::vector<Byte> input;
    for (int i = 0; i < 1000; ++i) {
        input.insert(input.end(), pattern.begin(), pattern.end());
    }

    Compressor c;
    auto [compressed, comp_result] = c.compress(input);
    assert(comp_result.ok());

    Decompressor d;
    auto [decompressed, decomp_result] = d.decompress(compressed);
    assert(decomp_result.ok());
    assert(decompressed == input);
}

TEST(repeated_pattern_long) {
    // Long pattern (258 bytes - max LZ77 match)
    std::vector<Byte> pattern(258);
    for (size_t i = 0; i < pattern.size(); ++i) {
        pattern[i] = static_cast<Byte>(i % 256);
    }

    std::vector<Byte> input;
    for (int i = 0; i < 100; ++i) {
        input.insert(input.end(), pattern.begin(), pattern.end());
    }

    Compressor c;
    auto [compressed, comp_result] = c.compress(input);
    assert(comp_result.ok());

    Decompressor d;
    auto [decompressed, decomp_result] = d.decompress(compressed);
    assert(decomp_result.ok());
    assert(decompressed == input);
}

TEST(run_length_sequences) {
    // Sequences of runs: 1 A, 2 Bs, 3 Cs, etc.
    std::vector<Byte> input;
    for (int len = 1; len <= 100; ++len) {
        for (int i = 0; i < len; ++i) {
            input.push_back(static_cast<Byte>('A' + (len - 1) % 26));
        }
    }

    Compressor c;
    auto [compressed, comp_result] = c.compress(input);
    assert(comp_result.ok());

    Decompressor d;
    auto [decompressed, decomp_result] = d.decompress(compressed);
    assert(decomp_result.ok());
    assert(decompressed == input);
}

// ============================================================================
// Malformed Input Tests (Decompression)
// ============================================================================

TEST(invalid_magic) {
    std::vector<Byte> bad_data = {'X', 'Y', 'Z', 0x01, 0, 0, 0, 0};
    assert(!Decompressor::validate_header(bad_data));
}

TEST(truncated_header) {
    std::vector<Byte> short_data = {'C', 'U', 'M'};  // Missing bytes
    assert(!Decompressor::validate_header(short_data));
}

TEST(truncated_data) {
    std::vector<Byte> original(1000, 'X');
    Compressor c;
    auto [compressed, comp_result] = c.compress(original);
    assert(comp_result.ok());

    std::vector<Byte> truncated(compressed.begin(), compressed.begin() + compressed.size() / 2);

    Decompressor d;
    auto [decompressed, result] = d.decompress(truncated);
    assert(!result.ok());
}

TEST(corrupted_data) {
    std::vector<Byte> original(1000, 'X');
    Compressor c;
    auto [compressed, comp_result] = c.compress(original);
    assert(comp_result.ok());

    if (compressed.size() > 50) {
        compressed[40] ^= 0xFF;
        compressed[41] ^= 0xFF;
        compressed[42] ^= 0xFF;
    }

    Decompressor d;
    auto [decompressed, result] = d.decompress(compressed);
    assert(!result.ok());
}

TEST(zero_original_size_in_header) {
    // Valid-looking header with size 0 but garbage after it
    std::vector<Byte> bad_header = {
        'C', 'U', 'M', 0x01,
        0x01, 0x00,
        0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,  // Original size = 0
        0x10, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00,
        0xDE, 0xAD, 0xBE, 0xEF
    };

    Decompressor d;
    auto [decompressed, result] = d.decompress(bad_header);
    assert(decompressed.empty());
}

TEST(huge_sizes_in_header) {
    // Claims 2^60 bytes and 2^32-1 blocks: must be rejected, not allocated
    std::vector<Byte> bad_header = {
        'C', 'U', 'M', 0x01,
        0x02, 0x00,
        0x09, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x10,
        0x10, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00,
        0xFF, 0xFF, 0xFF, 0xFF,
        0xDE, 0xAD, 0xBE, 0xEF
    };

    Decompressor d;
    auto [decompressed, result] = d.decompress(bad_header);
    assert(!result.ok());
}

// Random corruption of real files: must never crash, hang or return wrong data
TEST(random_mutations_never_silently_corrupt) {
    std::mt19937 rng(7);
    std::string text;
    const char* words[] = {"alpha ", "beta ", "gamma ", "delta\n", "{\"k\":1}", "\x01\x02\xff"};
    while (text.size() < 3000) text += words[rng() % 6];
    std::vector<Byte> original(text.begin(), text.end());

    for (Level level : {Level::Fast, Level::Normal, Level::Best}) {
        CompressOptions options;
        options.level = level;
        options.block_size = 1024;
        auto [compressed, comp_result] = Compressor(options).compress(original);
        assert(comp_result.ok());

        for (int i = 0; i < 150; ++i) {
            auto mutated = compressed;
            if (i % 3 == 0) {
                mutated.resize(rng() % mutated.size());
            } else {
                mutated[rng() % mutated.size()] ^= static_cast<Byte>(1 + rng() % 255);
            }
            auto [decompressed, result] = Decompressor().decompress(mutated);
            assert(!result.ok() || decompressed == original);
        }
    }
}


// ============================================================================
// Size Boundary Tests
// ============================================================================

TEST(size_just_under_block) {
    // Just under a smaller block size (64KB for faster tests)
    std::vector<Byte> input(64 * 1024 - 1, 'A');

    Compressor c;
    auto [compressed, comp_result] = c.compress(input);
    assert(comp_result.ok());

    Decompressor d;
    auto [decompressed, decomp_result] = d.decompress(compressed);
    assert(decomp_result.ok());
    assert(decompressed == input);
}

TEST(size_exactly_block) {
    // Exactly 64KB
    std::vector<Byte> input(64 * 1024, 'B');

    Compressor c;
    auto [compressed, comp_result] = c.compress(input);
    assert(comp_result.ok());

    Decompressor d;
    auto [decompressed, decomp_result] = d.decompress(compressed);
    assert(decomp_result.ok());
    assert(decompressed == input);
}

TEST(size_just_over_block) {
    // Just over 64KB (triggers multi-block on smaller configs)
    std::vector<Byte> input(64 * 1024 + 1, 'C');

    Compressor c;
    auto [compressed, comp_result] = c.compress(input);
    assert(comp_result.ok());

    Decompressor d;
    auto [decompressed, decomp_result] = d.decompress(compressed);
    assert(decomp_result.ok());
    assert(decompressed == input);
}

// ============================================================================
// High Entropy (Incompressible) Tests
// ============================================================================

TEST(pure_random_data) {
    std::mt19937 rng(12345);
    std::vector<Byte> input(10000);  // Reduced for faster tests
    for (auto& b : input) {
        b = static_cast<Byte>(rng() % 256);
    }

    Compressor c;
    auto [compressed, comp_result] = c.compress(input);
    assert(comp_result.ok());
    // Random data shouldn't compress much
    assert(comp_result.compressed_size >= input.size() * 0.95);

    Decompressor d;
    auto [decompressed, decomp_result] = d.decompress(compressed);
    assert(decomp_result.ok());
    assert(decompressed == input);
}

TEST(already_compressed_data) {
    // First compression
    std::string text = "The quick brown fox jumps over the lazy dog. ";
    for (int i = 0; i < 10; ++i) text += text;
    std::vector<Byte> original(text.begin(), text.end());

    Compressor c1;
    auto [compressed1, _1] = c1.compress(original);

    // Second compression (of compressed data)
    Compressor c2;
    auto [compressed2, comp_result] = c2.compress(compressed1);
    assert(comp_result.ok());

    // Decompress twice
    Decompressor d1;
    auto [decompressed1, _2] = d1.decompress(compressed2);
    Decompressor d2;
    auto [decompressed2, decomp_result] = d2.decompress(decompressed1);
    assert(decomp_result.ok());
    assert(decompressed2 == original);
}

// ============================================================================
// Compression Level Tests
// ============================================================================

TEST(all_levels_produce_same_output) {
    std::string text = "Test data for all compression levels.";
    std::vector<Byte> input(text.begin(), text.end());

    std::vector<Byte> decompressed_fast, decompressed_normal, decompressed_best;

    {
        CompressOptions opts;
        opts.level = Level::Fast;
        Compressor c(opts);
        auto [compressed, _] = c.compress(input);
        Decompressor d;
        auto [dec, __] = d.decompress(compressed);
        decompressed_fast = std::move(dec);
    }

    {
        CompressOptions opts;
        opts.level = Level::Normal;
        Compressor c(opts);
        auto [compressed, _] = c.compress(input);
        Decompressor d;
        auto [dec, __] = d.decompress(compressed);
        decompressed_normal = std::move(dec);
    }

    {
        CompressOptions opts;
        opts.level = Level::Best;
        Compressor c(opts);
        auto [compressed, _] = c.compress(input);
        Decompressor d;
        auto [dec, __] = d.decompress(compressed);
        decompressed_best = std::move(dec);
    }

    // All should produce identical decompressed output
    assert(decompressed_fast == input);
    assert(decompressed_normal == input);
    assert(decompressed_best == input);
}

// ============================================================================
// Main
// ============================================================================

int main() {
    std::cout << "CompressUM Edge Case Tests\n";
    std::cout << "==========================\n";

    // Empty and minimal
    RUN_TEST(empty_input);
    RUN_TEST(single_byte_all_values);
    RUN_TEST(two_bytes);
    RUN_TEST(three_bytes_min_match);

    // Uniform data
    RUN_TEST(all_zeros);
    RUN_TEST(all_ones);
    RUN_TEST(each_byte_value_once);
    RUN_TEST(alternating_01);
    RUN_TEST(alternating_00FF);

    // Patterns
    RUN_TEST(repeated_pattern_short);
    RUN_TEST(repeated_pattern_long);
    RUN_TEST(run_length_sequences);

    // Malformed input
    RUN_TEST(invalid_magic);
    RUN_TEST(truncated_header);
    RUN_TEST(truncated_data);
    RUN_TEST(corrupted_data);
    RUN_TEST(zero_original_size_in_header);
    RUN_TEST(huge_sizes_in_header);
    RUN_TEST(random_mutations_never_silently_corrupt);

    // Size boundaries
    RUN_TEST(size_just_under_block);
    RUN_TEST(size_exactly_block);
    RUN_TEST(size_just_over_block);

    // High entropy
    RUN_TEST(pure_random_data);
    RUN_TEST(already_compressed_data);

    // Levels
    RUN_TEST(all_levels_produce_same_output);

    std::cout << "\nAll edge case tests passed!\n";
    return 0;
}
