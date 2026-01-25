/**
 * CompressUM - PNG Optimizer Tests
 * Tests for PNG detection and optimization
 */

#include "compressum/transform/png.hpp"
#include <iostream>
#include <cassert>
#include <cstring>

using namespace compressum;
using namespace compressum::transform;

#define TEST(name) void test_##name()
#define RUN_TEST(name) do { \
    std::cout << "Running " #name "... " << std::flush; \
    test_##name(); \
    std::cout << "OK\n" << std::flush; \
} while(0)

// ============================================================================
// Detection Tests
// ============================================================================

TEST(detect_png_signature) {
    // Valid PNG signature
    std::vector<Byte> png_data = {0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A, 0x00};
    assert(PngOptimizer::is_png(png_data));

    // Invalid - JPEG signature
    std::vector<Byte> jpeg_data = {0xFF, 0xD8, 0xFF, 0xE0};
    assert(!PngOptimizer::is_png(jpeg_data));

    // Too short
    std::vector<Byte> too_short = {0x89, 0x50, 0x4E, 0x47};
    assert(!PngOptimizer::is_png(too_short));

    // Empty
    std::vector<Byte> empty;
    assert(!PngOptimizer::is_png(empty));
}

// ============================================================================
// Passthrough Tests
// ============================================================================

TEST(passthrough_non_png) {
    // Non-PNG data should be returned unchanged
    std::vector<Byte> data = {0x01, 0x02, 0x03, 0x04, 0x05};

    PngOptimizer optimizer;
    auto result = optimizer.encode(data);

    assert(result == data);
}

TEST(passthrough_small_png) {
    // PNG signature but too small to be valid
    std::vector<Byte> small_png = {0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A};
    for (int i = 0; i < 20; ++i) small_png.push_back(0x00);

    PngOptimizer optimizer;
    auto result = optimizer.encode(small_png);

    // Should return original (too small/invalid)
    assert(result == small_png);
}

TEST(decode_passthrough) {
    // Decode should always return input unchanged
    std::vector<Byte> data = {0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A};

    PngOptimizer optimizer;
    auto result = optimizer.decode(data);

    assert(result == data);
}

// ============================================================================
// Helper function to create minimal PNG
// ============================================================================

std::vector<Byte> create_test_png(uint32_t width, uint32_t height, uint8_t color_type) {
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

        // Type (big-endian)
        png.push_back(static_cast<Byte>((type >> 24) & 0xFF));
        png.push_back(static_cast<Byte>((type >> 16) & 0xFF));
        png.push_back(static_cast<Byte>((type >> 8) & 0xFF));
        png.push_back(static_cast<Byte>(type & 0xFF));

        // Data
        png.insert(png.end(), data.begin(), data.end());

        // CRC (simplified - just use 0 for test)
        png.insert(png.end(), {0x00, 0x00, 0x00, 0x00});
    };

    // IHDR chunk
    std::vector<Byte> ihdr = {
        static_cast<Byte>((width >> 24) & 0xFF),
        static_cast<Byte>((width >> 16) & 0xFF),
        static_cast<Byte>((width >> 8) & 0xFF),
        static_cast<Byte>(width & 0xFF),
        static_cast<Byte>((height >> 24) & 0xFF),
        static_cast<Byte>((height >> 16) & 0xFF),
        static_cast<Byte>((height >> 8) & 0xFF),
        static_cast<Byte>(height & 0xFF),
        8,              // Bit depth
        color_type,     // Color type
        0,              // Compression
        0,              // Filter
        0               // Interlace
    };
    write_chunk(png::IHDR, ihdr);

    // Create raw image data with filter bytes
    size_t bytes_per_pixel = (color_type == 0) ? 1 : (color_type == 2) ? 3 : (color_type == 6) ? 4 : 1;
    size_t scanline_bytes = width * bytes_per_pixel;
    std::vector<Byte> raw_data;

    for (uint32_t y = 0; y < height; ++y) {
        raw_data.push_back(0);  // Filter: None
        for (size_t x = 0; x < scanline_bytes; ++x) {
            raw_data.push_back(static_cast<Byte>((x + y) % 256));  // Gradient pattern
        }
    }

    // Compress with zlib
    Deflater deflater;
    auto compressed = deflater.deflate_zlib(raw_data);

    // IDAT chunk
    write_chunk(png::IDAT, compressed);

    // IEND chunk
    write_chunk(png::IEND, {});

    return png;
}

// ============================================================================
// Minimal PNG Tests
// ============================================================================

TEST(parse_grayscale_png) {
    // Create a simple 4x4 grayscale PNG
    auto png = create_test_png(4, 4, 0);  // Grayscale

    assert(PngOptimizer::is_png(png));

    PngOptimizer optimizer;
    auto result = optimizer.encode(png);

    // Should produce valid PNG output
    assert(result.size() >= 57);  // Minimum valid PNG size
    assert(PngOptimizer::is_png(result));
}

TEST(parse_rgb_png) {
    // Create a simple 4x4 RGB PNG
    auto png = create_test_png(4, 4, 2);  // RGB

    assert(PngOptimizer::is_png(png));

    PngOptimizer optimizer;
    auto result = optimizer.encode(png);

    // Should produce valid PNG output
    assert(result.size() >= 57);
    assert(PngOptimizer::is_png(result));
}

TEST(parse_rgba_png) {
    // Create a simple 4x4 RGBA PNG
    auto png = create_test_png(4, 4, 6);  // RGBA

    assert(PngOptimizer::is_png(png));

    PngOptimizer optimizer;
    auto result = optimizer.encode(png);

    // Should produce valid PNG output
    assert(result.size() >= 57);
    assert(PngOptimizer::is_png(result));
}

TEST(larger_png) {
    // Create a larger 32x32 RGB PNG
    auto png = create_test_png(32, 32, 2);

    PngOptimizer optimizer;
    auto result = optimizer.encode(png);

    // Should produce valid PNG output
    assert(PngOptimizer::is_png(result));
}

// ============================================================================
// Paeth Predictor Test
// ============================================================================

TEST(paeth_predictor) {
    // Test Paeth predictor edge cases
    // paeth(a, b, c) predicts the pixel using a=left, b=above, c=upper-left

    // When all inputs are same, should return that value
    // This is tested indirectly through filter operations
    auto png = create_test_png(8, 8, 0);

    PngOptimizer optimizer;
    auto result = optimizer.encode(png);

    assert(PngOptimizer::is_png(result));
}

// ============================================================================
// Main
// ============================================================================

int main() {
    std::cout << "PNG Optimizer Tests\n";
    std::cout << "===================\n";

    // Detection tests
    RUN_TEST(detect_png_signature);

    // Passthrough tests
    RUN_TEST(passthrough_non_png);
    RUN_TEST(passthrough_small_png);
    RUN_TEST(decode_passthrough);

    // PNG parsing tests
    RUN_TEST(parse_grayscale_png);
    RUN_TEST(parse_rgb_png);
    RUN_TEST(parse_rgba_png);
    RUN_TEST(larger_png);

    // Paeth predictor
    RUN_TEST(paeth_predictor);

    std::cout << "\nAll PNG optimizer tests passed!\n";
    return 0;
}
