/**
 * CompressUM - JPEG Optimizer Tests
 * Tests for JPEG detection and optimization
 */

#include "compressum/transform/jpeg.hpp"
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

TEST(detect_jpeg_signature) {
    // Valid JPEG signature
    std::vector<Byte> jpeg_data = {0xFF, 0xD8, 0xFF, 0xE0, 0x00, 0x10};
    assert(JpegOptimizer::is_jpeg(jpeg_data));

    // Invalid - wrong signature
    std::vector<Byte> not_jpeg = {0x89, 0x50, 0x4E, 0x47};  // PNG
    assert(!JpegOptimizer::is_jpeg(not_jpeg));

    // Too short
    std::vector<Byte> too_short = {0xFF, 0xD8};
    assert(!JpegOptimizer::is_jpeg(too_short));

    // Empty
    std::vector<Byte> empty;
    assert(!JpegOptimizer::is_jpeg(empty));
}

TEST(detect_various_formats) {
    // JFIF (APP0)
    std::vector<Byte> jfif = {0xFF, 0xD8, 0xFF, 0xE0};
    assert(JpegOptimizer::is_jpeg(jfif));

    // EXIF (APP1)
    std::vector<Byte> exif = {0xFF, 0xD8, 0xFF, 0xE1};
    assert(JpegOptimizer::is_jpeg(exif));
}

// ============================================================================
// Passthrough Tests
// ============================================================================

TEST(passthrough_non_jpeg) {
    // Non-JPEG data should be returned unchanged
    std::vector<Byte> data = {0x01, 0x02, 0x03, 0x04, 0x05};

    JpegOptimizer optimizer;
    auto result = optimizer.encode(data);

    assert(result == data);
}

TEST(passthrough_small_jpeg) {
    // Too small to optimize - returned unchanged
    std::vector<Byte> small_jpeg = {0xFF, 0xD8, 0xFF, 0xE0, 0x00, 0x10};
    for (int i = 0; i < 50; ++i) small_jpeg.push_back(0x00);
    small_jpeg.push_back(0xFF);
    small_jpeg.push_back(0xD9);

    JpegOptimizer optimizer;
    auto result = optimizer.encode(small_jpeg);

    // Should return original (too small)
    assert(result == small_jpeg);
}

TEST(decode_passthrough) {
    // Decode should always return input unchanged
    std::vector<Byte> data = {0xFF, 0xD8, 0xFF, 0xE0, 0x00, 0x10, 0xFF, 0xD9};

    JpegOptimizer optimizer;
    auto result = optimizer.decode(data);

    assert(result == data);
}

// ============================================================================
// Minimal JPEG Parsing Tests
// ============================================================================

TEST(parse_minimal_jpeg) {
    // Create a minimal valid JPEG structure
    std::vector<Byte> jpeg;

    // SOI
    jpeg.push_back(0xFF);
    jpeg.push_back(jpeg::SOI);

    // APP0 (JFIF marker)
    jpeg.push_back(0xFF);
    jpeg.push_back(jpeg::APP0);
    jpeg.push_back(0x00);
    jpeg.push_back(0x10);  // Length = 16
    const char* jfif = "JFIF";
    for (int i = 0; i < 14; ++i) {
        jpeg.push_back(i < 5 ? static_cast<Byte>(jfif[i]) : 0x00);
    }

    // DQT (Define Quantization Table)
    jpeg.push_back(0xFF);
    jpeg.push_back(jpeg::DQT);
    jpeg.push_back(0x00);
    jpeg.push_back(0x43);  // Length = 67
    jpeg.push_back(0x00);  // Table 0
    for (int i = 0; i < 64; ++i) {
        jpeg.push_back(0x10);  // Dummy quantization values
    }

    // SOF0 (Start of Frame - Baseline DCT)
    jpeg.push_back(0xFF);
    jpeg.push_back(jpeg::SOF0);
    jpeg.push_back(0x00);
    jpeg.push_back(0x0B);  // Length = 11
    jpeg.push_back(0x08);  // Precision = 8 bits
    jpeg.push_back(0x00);
    jpeg.push_back(0x01);  // Height = 1
    jpeg.push_back(0x00);
    jpeg.push_back(0x01);  // Width = 1
    jpeg.push_back(0x01);  // 1 component
    jpeg.push_back(0x01);  // Component ID = 1
    jpeg.push_back(0x11);  // Sampling = 1x1
    jpeg.push_back(0x00);  // Quant table 0

    // DHT (Define Huffman Table) - DC table
    jpeg.push_back(0xFF);
    jpeg.push_back(jpeg::DHT);
    jpeg.push_back(0x00);
    jpeg.push_back(0x1F);  // Length
    jpeg.push_back(0x00);  // DC table 0
    // Code counts for lengths 1-16
    for (int i = 0; i < 16; ++i) {
        jpeg.push_back(i == 0 ? 1 : 0);  // 1 code of length 1
    }
    jpeg.push_back(0x00);  // Symbol: 0

    // DHT - AC table
    jpeg.push_back(0xFF);
    jpeg.push_back(jpeg::DHT);
    jpeg.push_back(0x00);
    jpeg.push_back(0x14);  // Length
    jpeg.push_back(0x10);  // AC table 0
    for (int i = 0; i < 16; ++i) {
        jpeg.push_back(0);  // No codes (EOB is implicit)
    }

    // SOS (Start of Scan)
    jpeg.push_back(0xFF);
    jpeg.push_back(jpeg::SOS);
    jpeg.push_back(0x00);
    jpeg.push_back(0x08);  // Length = 8
    jpeg.push_back(0x01);  // 1 component
    jpeg.push_back(0x01);  // Component ID = 1
    jpeg.push_back(0x00);  // DC/AC table 0/0
    jpeg.push_back(0x00);  // Ss = 0
    jpeg.push_back(0x3F);  // Se = 63
    jpeg.push_back(0x00);  // Ah/Al = 0/0

    // Minimal scan data (just 0 DC coefficient)
    jpeg.push_back(0x00);  // DC = 0 (Huffman code)

    // EOI
    jpeg.push_back(0xFF);
    jpeg.push_back(jpeg::EOI);

    // Try to optimize
    JpegOptimizer optimizer;
    auto result = optimizer.encode(jpeg);

    // Should produce valid output (at least SOI and EOI)
    assert(result.size() >= 4);
    assert(result[0] == 0xFF && result[1] == jpeg::SOI);
    assert(result[result.size() - 2] == 0xFF && result[result.size() - 1] == jpeg::EOI);
}

// ============================================================================
// Main
// ============================================================================

int main() {
    std::cout << "JPEG Optimizer Tests\n";
    std::cout << "====================\n";

    // Detection tests
    RUN_TEST(detect_jpeg_signature);
    RUN_TEST(detect_various_formats);

    // Passthrough tests
    RUN_TEST(passthrough_non_jpeg);
    RUN_TEST(passthrough_small_jpeg);
    RUN_TEST(decode_passthrough);

    // Parsing tests
    RUN_TEST(parse_minimal_jpeg);

    std::cout << "\nAll JPEG optimizer tests passed!\n";
    return 0;
}
