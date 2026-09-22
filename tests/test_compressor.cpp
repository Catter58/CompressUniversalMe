/**
 * CompressUM - Phase 5 Integration Tests
 * Tests the full compression/decompression pipeline
 */

#include "compressum/compressum.hpp"
#include "compressum/transform/deflate.hpp"
#include <iostream>
#include <cassert>
#include <cstring>
#include <random>
#include <fstream>

using namespace compressum;

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

TEST(simple_text) {
    const char* text = "Hello, CompressUM! This is a test of the compression algorithm.";
    std::vector<Byte> input(text, text + strlen(text));

    Compressor c;
    auto [compressed, comp_result] = c.compress(input);

    assert(comp_result.ok());
    assert(comp_result.original_size == input.size());
    assert(comp_result.compressed_size > 0);

    Decompressor d;
    auto [decompressed, decomp_result] = d.decompress(compressed);

    assert(decomp_result.ok());
    assert(decomp_result.crc_valid);
    assert(decompressed == input);
}

TEST(repeated_text) {
    // Highly compressible data
    std::string text = "AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA";
    for (int i = 0; i < 10; ++i) {
        text += text;
    }
    std::vector<Byte> input(text.begin(), text.end());

    Compressor c;
    auto [compressed, comp_result] = c.compress(input);

    assert(comp_result.ok());
    // Should compress significantly
    assert(comp_result.compressed_size < input.size() / 2);
    assert(comp_result.ratio > 2.0);

    Decompressor d;
    auto [decompressed, decomp_result] = d.decompress(compressed);

    assert(decomp_result.ok());
    assert(decompressed == input);
}

TEST(random_data) {
    std::mt19937 rng(42);
    std::vector<Byte> input(10000);
    for (auto& b : input) {
        b = static_cast<Byte>(rng() % 256);
    }

    Compressor c;
    auto [compressed, comp_result] = c.compress(input);

    assert(comp_result.ok());

    Decompressor d;
    auto [decompressed, decomp_result] = d.decompress(compressed);

    assert(decomp_result.ok());
    assert(decompressed == input);
}

TEST(binary_data) {
    // Simulated binary data with some patterns
    std::vector<Byte> input;
    input.reserve(5000);

    // Add some structured binary data
    for (int i = 0; i < 100; ++i) {
        // Simulate a record with header and data
        input.push_back(0x01);  // Record type
        input.push_back(static_cast<Byte>(i & 0xFF));  // ID low
        input.push_back(static_cast<Byte>((i >> 8) & 0xFF));  // ID high
        input.push_back(0x10);  // Data length
        for (int j = 0; j < 16; ++j) {
            input.push_back(static_cast<Byte>((i + j) % 256));
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
// Compression Level Tests
// ============================================================================

TEST(level_fast) {
    std::string text = "The quick brown fox jumps over the lazy dog. ";
    for (int i = 0; i < 8; ++i) {
        text += text;
    }
    std::vector<Byte> input(text.begin(), text.end());

    CompressOptions options;
    options.level = Level::Fast;

    Compressor c(options);
    auto [compressed, comp_result] = c.compress(input);

    assert(comp_result.ok());

    Decompressor d;
    auto [decompressed, decomp_result] = d.decompress(compressed);

    assert(decomp_result.ok());
    assert(decompressed == input);
}

TEST(level_normal) {
    std::string text = "The quick brown fox jumps over the lazy dog. ";
    for (int i = 0; i < 8; ++i) {
        text += text;
    }
    std::vector<Byte> input(text.begin(), text.end());

    CompressOptions options;
    options.level = Level::Normal;

    Compressor c(options);
    auto [compressed, comp_result] = c.compress(input);

    assert(comp_result.ok());

    Decompressor d;
    auto [decompressed, decomp_result] = d.decompress(compressed);

    assert(decomp_result.ok());
    assert(decompressed == input);
}

TEST(level_best) {
    std::string text = "The quick brown fox jumps over the lazy dog. ";
    for (int i = 0; i < 8; ++i) {
        text += text;
    }
    std::vector<Byte> input(text.begin(), text.end());

    CompressOptions options;
    options.level = Level::Best;

    Compressor c(options);
    auto [compressed, comp_result] = c.compress(input);

    assert(comp_result.ok());

    Decompressor d;
    auto [decompressed, decomp_result] = d.decompress(compressed);

    assert(decomp_result.ok());
    assert(decompressed == input);
}

// ============================================================================
// Block Size Tests
// ============================================================================

TEST(small_block_size) {
    std::string text = "Test data for small block compression. ";
    for (int i = 0; i < 6; ++i) {
        text += text;
    }
    std::vector<Byte> input(text.begin(), text.end());

    CompressOptions options;
    options.block_size = 1024;  // 1KB blocks

    Compressor c(options);
    auto [compressed, comp_result] = c.compress(input);

    assert(comp_result.ok());

    Decompressor d;
    auto [decompressed, decomp_result] = d.decompress(compressed);

    assert(decomp_result.ok());
    assert(decompressed == input);
}

TEST(large_data_multiple_blocks) {
    // Create data larger than default block size
    std::mt19937 rng(123);
    std::vector<Byte> input(500000);  // 500KB

    // Mix of random and structured data
    for (size_t i = 0; i < input.size(); ++i) {
        if (i % 100 < 50) {
            input[i] = static_cast<Byte>(i % 256);  // Structured
        } else {
            input[i] = static_cast<Byte>(rng() % 256);  // Random
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
// Parallel Compression Tests
// ============================================================================

TEST(parallel_compression) {
    // Create data large enough to benefit from parallel compression
    std::string text = "Parallel compression test data. ";
    for (int i = 0; i < 10; ++i) {
        text += text;
    }
    std::vector<Byte> input(text.begin(), text.end());

    CompressOptions options;
    options.threads = 4;
    options.block_size = 8192;  // Small blocks for more parallelism

    Compressor c(options);
    auto [compressed, comp_result] = c.compress(input);

    assert(comp_result.ok());

    Decompressor d;
    auto [decompressed, decomp_result] = d.decompress(compressed);

    assert(decomp_result.ok());
    assert(decompressed == input);
}

// ============================================================================
// Header Validation Tests
// ============================================================================

TEST(validate_header) {
    const char* text = "Test data";
    std::vector<Byte> input(text, text + strlen(text));

    Compressor c;
    auto [compressed, comp_result] = c.compress(input);

    assert(comp_result.ok());

    // Valid header
    assert(Decompressor::validate_header(compressed));

    // Invalid magic
    std::vector<Byte> bad_magic = compressed;
    bad_magic[0] = 'X';
    assert(!Decompressor::validate_header(bad_magic));

    // Too small
    std::vector<Byte> too_small = {0x01, 0x02, 0x03};
    assert(!Decompressor::validate_header(too_small));

    // Get original size
    size_t orig_size = Decompressor::get_original_size(compressed);
    assert(orig_size == input.size());
}

TEST(crc_verification) {
    const char* text = "CRC verification test data";
    std::vector<Byte> input(text, text + strlen(text));

    Compressor c;
    auto [compressed, comp_result] = c.compress(input);

    assert(comp_result.ok());

    // Corrupt the compressed data (but not the header)
    if (compressed.size() > sizeof(FileHeader) + 10) {
        compressed[sizeof(FileHeader) + 5] ^= 0xFF;  // Flip bits in data
    }

    Decompressor d;
    auto [decompressed, decomp_result] = d.decompress(compressed);

    // Should fail due to CRC mismatch or corrupted data
    assert(!decomp_result.ok() || !decomp_result.crc_valid);
}

// ============================================================================
// Convenience Function Tests
// ============================================================================

TEST(convenience_functions) {
    const char* text = "Test using convenience functions";
    std::vector<Byte> input(text, text + strlen(text));

    // One-shot compress/decompress
    auto compressed = compress(input, Level::Normal);
    auto decompressed = decompress(compressed);

    assert(decompressed == input);
}

TEST(analyze_function) {
    // Text data
    const char* text = "This is English text with normal ASCII characters.";
    std::vector<Byte> text_input(text, text + strlen(text));

    auto text_stats = analyze(text_input);
    assert(text_stats.type == DataType::Text);
    assert(text_stats.entropy > 0 && text_stats.entropy < 8);
    assert(text_stats.is_compressible());

    // Random data (high entropy)
    std::mt19937 rng(42);
    std::vector<Byte> random_input(1000);
    for (auto& b : random_input) {
        b = static_cast<Byte>(rng() % 256);
    }

    auto random_stats = analyze(random_input);
    assert(random_stats.entropy > 7.5);  // High entropy
}

// ============================================================================
// Edge Cases
// ============================================================================

TEST(single_byte) {
    std::vector<Byte> input = {0x42};

    Compressor c;
    auto [compressed, comp_result] = c.compress(input);

    assert(comp_result.ok());

    Decompressor d;
    auto [decompressed, decomp_result] = d.decompress(compressed);

    assert(decomp_result.ok());
    assert(decompressed == input);
}

TEST(all_same_bytes) {
    std::vector<Byte> input(1000, 0xAA);

    Compressor c;
    auto [compressed, comp_result] = c.compress(input);

    assert(comp_result.ok());
    // Should compress well (accounting for header + Huffman tables overhead)
    assert(comp_result.compressed_size < input.size());

    Decompressor d;
    auto [decompressed, decomp_result] = d.decompress(compressed);

    assert(decomp_result.ok());
    assert(decompressed == input);
}

TEST(alternating_bytes) {
    std::vector<Byte> input(1000);
    for (size_t i = 0; i < input.size(); ++i) {
        input[i] = static_cast<Byte>(i % 2 ? 0xAA : 0x55);
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
// Phase 6: Media Format Integration Tests
// ============================================================================

// Helper: Create minimal JPEG
std::vector<Byte> create_minimal_jpeg() {
    // Create a minimal valid JPEG with simple data
    std::vector<Byte> jpeg;

    // SOI marker
    jpeg.push_back(0xFF);
    jpeg.push_back(0xD8);

    // APP0 (JFIF)
    jpeg.push_back(0xFF);
    jpeg.push_back(0xE0);
    jpeg.push_back(0x00);
    jpeg.push_back(0x10);  // Length: 16
    jpeg.push_back('J'); jpeg.push_back('F'); jpeg.push_back('I'); jpeg.push_back('F'); jpeg.push_back(0x00);
    jpeg.push_back(0x01); jpeg.push_back(0x01);  // Version 1.1
    jpeg.push_back(0x00);  // Units
    jpeg.push_back(0x00); jpeg.push_back(0x01);  // X density
    jpeg.push_back(0x00); jpeg.push_back(0x01);  // Y density
    jpeg.push_back(0x00); jpeg.push_back(0x00);  // Thumbnail

    // DQT (Define Quantization Table)
    jpeg.push_back(0xFF);
    jpeg.push_back(0xDB);
    jpeg.push_back(0x00);
    jpeg.push_back(0x43);  // Length: 67
    jpeg.push_back(0x00);  // Table 0, 8-bit precision
    for (int i = 0; i < 64; ++i) {
        jpeg.push_back(static_cast<Byte>(16 + (i % 16)));  // Simple quantization values
    }

    // SOF0 (Start of Frame)
    jpeg.push_back(0xFF);
    jpeg.push_back(0xC0);
    jpeg.push_back(0x00);
    jpeg.push_back(0x0B);  // Length: 11
    jpeg.push_back(0x08);  // 8-bit precision
    jpeg.push_back(0x00); jpeg.push_back(0x08);  // Height: 8
    jpeg.push_back(0x00); jpeg.push_back(0x08);  // Width: 8
    jpeg.push_back(0x01);  // 1 component (grayscale)
    jpeg.push_back(0x01);  // Component ID
    jpeg.push_back(0x11);  // Sampling factors
    jpeg.push_back(0x00);  // Quantization table

    // DHT (Define Huffman Table) - DC
    jpeg.push_back(0xFF);
    jpeg.push_back(0xC4);
    jpeg.push_back(0x00);
    jpeg.push_back(0x1F);  // Length: 31
    jpeg.push_back(0x00);  // DC table 0
    // Code lengths (16 bytes)
    jpeg.push_back(0x00); jpeg.push_back(0x01); jpeg.push_back(0x05); jpeg.push_back(0x01);
    jpeg.push_back(0x01); jpeg.push_back(0x01); jpeg.push_back(0x01); jpeg.push_back(0x01);
    jpeg.push_back(0x01); jpeg.push_back(0x00); jpeg.push_back(0x00); jpeg.push_back(0x00);
    jpeg.push_back(0x00); jpeg.push_back(0x00); jpeg.push_back(0x00); jpeg.push_back(0x00);
    // Symbols (12 total)
    jpeg.push_back(0x00); jpeg.push_back(0x01); jpeg.push_back(0x02); jpeg.push_back(0x03);
    jpeg.push_back(0x04); jpeg.push_back(0x05); jpeg.push_back(0x06); jpeg.push_back(0x07);
    jpeg.push_back(0x08); jpeg.push_back(0x09); jpeg.push_back(0x0A); jpeg.push_back(0x0B);

    // DHT (Define Huffman Table) - AC
    jpeg.push_back(0xFF);
    jpeg.push_back(0xC4);
    jpeg.push_back(0x00);
    jpeg.push_back(0xB5);  // Length: 181
    jpeg.push_back(0x10);  // AC table 0
    // Code lengths (16 bytes)
    jpeg.push_back(0x00); jpeg.push_back(0x02); jpeg.push_back(0x01); jpeg.push_back(0x03);
    jpeg.push_back(0x03); jpeg.push_back(0x02); jpeg.push_back(0x04); jpeg.push_back(0x03);
    jpeg.push_back(0x05); jpeg.push_back(0x05); jpeg.push_back(0x04); jpeg.push_back(0x04);
    jpeg.push_back(0x00); jpeg.push_back(0x00); jpeg.push_back(0x01); jpeg.push_back(0x7D);
    // Symbols (162 total = 181 - 16 - 1 - 2)
    for (int i = 0; i < 162; ++i) {
        jpeg.push_back(static_cast<Byte>(i % 256));
    }

    // SOS (Start of Scan)
    jpeg.push_back(0xFF);
    jpeg.push_back(0xDA);
    jpeg.push_back(0x00);
    jpeg.push_back(0x08);  // Length: 8
    jpeg.push_back(0x01);  // 1 component
    jpeg.push_back(0x01);  // Component ID
    jpeg.push_back(0x00);  // DC/AC table selectors
    jpeg.push_back(0x00); jpeg.push_back(0x3F); jpeg.push_back(0x00);  // Spectral selection, approx

    // Entropy-coded data (simple data that won't contain FF)
    for (int i = 0; i < 20; ++i) {
        jpeg.push_back(static_cast<Byte>(i * 10 % 255));  // Avoid 0xFF
    }

    // EOI marker
    jpeg.push_back(0xFF);
    jpeg.push_back(0xD9);

    return jpeg;
}

// Helper: Create minimal PNG
std::vector<Byte> create_minimal_png() {
    std::vector<Byte> png;

    // PNG signature
    png.insert(png.end(), {0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A});

    auto write_chunk = [&png](uint32_t type, const std::vector<Byte>& data) {
        // Length (big-endian)
        uint32_t len = static_cast<uint32_t>(data.size());
        png.push_back(static_cast<Byte>((len >> 24) & 0xFF));
        png.push_back(static_cast<Byte>((len >> 16) & 0xFF));
        png.push_back(static_cast<Byte>((len >> 8) & 0xFF));
        png.push_back(static_cast<Byte>(len & 0xFF));

        // Type
        png.push_back(static_cast<Byte>((type >> 24) & 0xFF));
        png.push_back(static_cast<Byte>((type >> 16) & 0xFF));
        png.push_back(static_cast<Byte>((type >> 8) & 0xFF));
        png.push_back(static_cast<Byte>(type & 0xFF));

        // Data
        png.insert(png.end(), data.begin(), data.end());

        // CRC (placeholder)
        png.insert(png.end(), {0x00, 0x00, 0x00, 0x00});
    };

    // IHDR chunk (type = 0x49484452 = "IHDR")
    std::vector<Byte> ihdr = {
        0x00, 0x00, 0x00, 0x04,  // Width: 4
        0x00, 0x00, 0x00, 0x04,  // Height: 4
        0x08,                     // Bit depth: 8
        0x00,                     // Color type: Grayscale
        0x00,                     // Compression: deflate
        0x00,                     // Filter: adaptive
        0x00                      // Interlace: none
    };
    write_chunk(0x49484452, ihdr);

    // IDAT chunk - use deflater to create proper zlib stream
    transform::Deflater deflater;
    std::vector<Byte> raw_data;
    for (int y = 0; y < 4; ++y) {
        raw_data.push_back(0);  // Filter: None
        for (int x = 0; x < 4; ++x) {
            raw_data.push_back(static_cast<Byte>((x + y) * 16));
        }
    }
    auto compressed = deflater.deflate_zlib(raw_data);
    write_chunk(0x49444154, compressed);  // IDAT = 0x49444154

    // IEND chunk
    write_chunk(0x49454E44, {});  // IEND = 0x49454E44

    return png;
}

TEST(media_jpeg_detection) {
    auto jpeg = create_minimal_jpeg();

    // Verify it's detected as JPEG
    auto stats = analyze(jpeg);
    assert(stats.type == DataType::Media);
    assert(stats.media_format == MediaFormat::JPEG);
}

TEST(media_png_detection) {
    auto png = create_minimal_png();

    // Verify it's detected as PNG
    auto stats = analyze(png);
    assert(stats.type == DataType::Media);
    assert(stats.media_format == MediaFormat::PNG);
}

// Regression: Level::Best used to route high-entropy text through BWT,
// whose forward/inverse mismatch corrupted repeated suffixes
TEST(level_best_high_entropy_text) {
    const std::string alphabet =
        "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789(){};,.=+-*/";
    std::vector<std::string> words;
    uint32_t seed = 12345;
    auto next = [&seed] { seed = seed * 1103515245u + 12345u; return seed >> 16; };
    for (int w = 0; w < 400; ++w) {
        std::string word;
        size_t len = 2 + next() % 8;
        for (size_t k = 0; k < len; ++k) word += alphabet[next() % alphabet.size()];
        words.push_back(word);
    }
    std::vector<Byte> data;
    while (data.size() < 300000) {
        const auto& word = words[next() % words.size()];
        data.insert(data.end(), word.begin(), word.end());
        data.push_back(next() % 12 == 0 ? '\n' : ' ');
    }

    CompressOptions options;
    options.level = Level::Best;
    Compressor c(options);
    auto [compressed, comp_result] = c.compress(data);
    assert(comp_result.ok());

    Decompressor d;
    auto [decompressed, decomp_result] = d.decompress(compressed);
    assert(decomp_result.ok());
    assert(decompressed == data);
}

TEST(media_jpeg_roundtrip) {
    auto jpeg = create_minimal_jpeg();

    Compressor c;
    auto [compressed, comp_result] = c.compress(jpeg);

    assert(comp_result.ok());

    Decompressor d;
    auto [decompressed, decomp_result] = d.decompress(compressed);

    assert(decomp_result.ok());
    // Lossless: bytes must be identical
    assert(decompressed == jpeg);
}

TEST(media_png_roundtrip) {
    auto png = create_minimal_png();

    Compressor c;
    auto [compressed, comp_result] = c.compress(png);

    assert(comp_result.ok());

    Decompressor d;
    auto [decompressed, decomp_result] = d.decompress(compressed);

    assert(decomp_result.ok());
    // Lossless: bytes must be identical
    assert(decompressed == png);
}

// ============================================================================
// Main
// ============================================================================

int main() {
    std::cout << "CompressUM Integration Tests (Phase 5 + Phase 6)\n";
    std::cout << "================================================\n";

    // Basic roundtrip tests
    RUN_TEST(empty_data);
    RUN_TEST(simple_text);
    RUN_TEST(repeated_text);
    RUN_TEST(random_data);
    RUN_TEST(binary_data);

    // Compression level tests
    RUN_TEST(level_fast);
    RUN_TEST(level_normal);
    RUN_TEST(level_best);
    RUN_TEST(level_best_high_entropy_text);

    // Block size tests
    RUN_TEST(small_block_size);
    RUN_TEST(large_data_multiple_blocks);

    // Parallel compression
    RUN_TEST(parallel_compression);

    // Header validation
    RUN_TEST(validate_header);
    RUN_TEST(crc_verification);

    // Convenience functions
    RUN_TEST(convenience_functions);
    RUN_TEST(analyze_function);

    // Edge cases
    RUN_TEST(single_byte);
    RUN_TEST(all_same_bytes);
    RUN_TEST(alternating_bytes);

    // Phase 6: Media format integration tests
    std::cout << "\n--- Phase 6: Media Format Tests ---\n";
    RUN_TEST(media_jpeg_detection);
    RUN_TEST(media_png_detection);
    RUN_TEST(media_jpeg_roundtrip);
    RUN_TEST(media_png_roundtrip);

    std::cout << "\nAll integration tests passed!\n";
    return 0;
}
