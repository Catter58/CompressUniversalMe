/**
 * CompressUM - Analyzer Tests
 */

#include "compressum/core/analyzer.hpp"
#include <iostream>
#include <cassert>
#include <cmath>
#include <vector>

using namespace compressum;
using namespace compressum::core;

#define TEST(name) void test_##name()
#define RUN_TEST(name) do { \
    std::cout << "Running " #name "... "; \
    test_##name(); \
    std::cout << "OK\n"; \
} while(0)

// Helper to compare floating point
bool approx_equal(double a, double b, double epsilon = 0.01) {
    return std::abs(a - b) < epsilon;
}

TEST(empty_data) {
    std::vector<Byte> data;
    auto stats = Analyzer::analyze(data);

    assert(stats.size == 0);
    assert(stats.entropy == 0.0);
    assert(stats.type == DataType::Unknown);
}

TEST(single_byte_repeated) {
    // All same bytes = 0 entropy
    std::vector<Byte> data(1000, 0x42);
    auto stats = Analyzer::analyze(data);

    assert(stats.size == 1000);
    assert(stats.entropy == 0.0);  // All same bytes
    assert(stats.histogram[0x42] == 1000);
}

TEST(uniform_distribution) {
    // Uniform distribution = max entropy (8 bits)
    std::vector<Byte> data;
    for (int i = 0; i < 256; ++i) {
        for (int j = 0; j < 100; ++j) {
            data.push_back(static_cast<Byte>(i));
        }
    }

    auto stats = Analyzer::analyze(data);

    assert(stats.size == 25600);
    assert(approx_equal(stats.entropy, 8.0, 0.001));  // Should be exactly 8.0
}

TEST(two_symbols) {
    // 50/50 two symbols = 1 bit entropy
    std::vector<Byte> data;
    for (int i = 0; i < 1000; ++i) {
        data.push_back(i % 2 == 0 ? 0x00 : 0xFF);
    }

    auto stats = Analyzer::analyze(data);

    assert(approx_equal(stats.entropy, 1.0, 0.001));
}

TEST(text_detection) {
    // ASCII text
    const char* text = "Hello, World! This is a test of the text detection algorithm. "
                       "It should recognize this as text data because most characters "
                       "are printable ASCII characters with some whitespace.\n";

    std::vector<Byte> data(text, text + std::strlen(text));
    auto stats = Analyzer::analyze(data);

    assert(stats.type == DataType::Text);
}

TEST(json_detection) {
    const char* json = R"({
        "name": "CompressUM",
        "version": "1.0.0",
        "features": ["compression", "decompression", "analysis"]
    })";

    std::vector<Byte> data(json, json + std::strlen(json));
    auto stats = Analyzer::analyze(data);

    assert(stats.type == DataType::Structured);
}

TEST(xml_detection) {
    const char* xml = R"(<?xml version="1.0"?>
    <root>
        <element>value</element>
    </root>)";

    std::vector<Byte> data(xml, xml + std::strlen(xml));
    auto stats = Analyzer::analyze(data);

    assert(stats.type == DataType::Structured);
}

TEST(executable_detection) {
    // ELF header
    std::vector<Byte> elf = {0x7F, 'E', 'L', 'F', 0x02, 0x01, 0x01, 0x00};
    elf.resize(64, 0);  // Pad to minimum size

    auto stats = Analyzer::analyze(elf);
    assert(stats.type == DataType::Executable);

    // PE header
    std::vector<Byte> pe = {'M', 'Z', 0x90, 0x00};
    pe.resize(64, 0);

    stats = Analyzer::analyze(pe);
    assert(stats.type == DataType::Executable);
}

TEST(compressed_detection) {
    // GZIP header
    std::vector<Byte> gzip = {0x1F, 0x8B, 0x08, 0x00};
    gzip.resize(64, 0);

    auto stats = Analyzer::analyze(gzip);
    assert(stats.type == DataType::Compressed);

    // ZSTD header
    std::vector<Byte> zstd = {0x28, 0xB5, 0x2F, 0xFD};
    zstd.resize(64, 0);

    stats = Analyzer::analyze(zstd);
    assert(stats.type == DataType::Compressed);
}

TEST(media_detection) {
    // JPEG header
    std::vector<Byte> jpeg = {0xFF, 0xD8, 0xFF, 0xE0};
    jpeg.resize(64, 0);

    auto stats = Analyzer::analyze(jpeg);
    assert(stats.type == DataType::Media);

    // PNG header
    std::vector<Byte> png = {0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A};
    png.resize(64, 0);

    stats = Analyzer::analyze(png);
    assert(stats.type == DataType::Media);
}

TEST(binary_detection) {
    // Random-looking binary data
    std::vector<Byte> data(1000);
    for (size_t i = 0; i < data.size(); ++i) {
        data[i] = static_cast<Byte>((i * 97 + 13) % 256);
    }

    auto stats = Analyzer::analyze(data);
    assert(stats.type == DataType::Binary);
}

TEST(compression_estimate) {
    // Low entropy = good compression estimate
    std::vector<Byte> repetitive(1000, 'A');
    auto stats = Analyzer::analyze(repetitive);
    assert(stats.compression_estimate() < 0.1);  // Should compress well

    // High entropy = poor compression estimate
    std::vector<Byte> random;
    for (int i = 0; i < 256; ++i) {
        for (int j = 0; j < 4; ++j) {
            random.push_back(static_cast<Byte>(i));
        }
    }
    stats = Analyzer::analyze(random);
    assert(stats.compression_estimate() > 0.9);  // Won't compress much
}

TEST(quick_entropy) {
    // Large data - quick_entropy should sample
    std::vector<Byte> data(100000);
    for (size_t i = 0; i < data.size(); ++i) {
        data[i] = static_cast<Byte>(i % 256);
    }

    double full_entropy = Analyzer::analyze(data).entropy;
    double quick = quick_entropy(data, 4096);

    // Should be reasonably close
    assert(approx_equal(full_entropy, quick, 0.5));
}

TEST(conditional_entropy) {
    // Highly correlated data (each byte = previous + 1)
    std::vector<Byte> sequential(1000);
    for (size_t i = 0; i < sequential.size(); ++i) {
        sequential[i] = static_cast<Byte>(i & 0xFF);
    }

    double cond_entropy = Analyzer::calculate_conditional_entropy(sequential);

    // Conditional entropy should be much lower than unconditional
    // because knowing prev byte tells you next byte
    double uncond_entropy = Analyzer::analyze(sequential).entropy;

    assert(cond_entropy < uncond_entropy);
}

int main() {
    std::cout << "Analyzer Tests\n";
    std::cout << "==============\n";

    RUN_TEST(empty_data);
    RUN_TEST(single_byte_repeated);
    RUN_TEST(uniform_distribution);
    RUN_TEST(two_symbols);
    RUN_TEST(text_detection);
    RUN_TEST(json_detection);
    RUN_TEST(xml_detection);
    RUN_TEST(executable_detection);
    RUN_TEST(compressed_detection);
    RUN_TEST(media_detection);
    RUN_TEST(binary_detection);
    RUN_TEST(compression_estimate);
    RUN_TEST(quick_entropy);
    RUN_TEST(conditional_entropy);

    std::cout << "\nAll tests passed!\n";
    return 0;
}
