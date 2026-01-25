/**
 * CompressUM - Phase 7 Performance Benchmarks
 *
 * Measures compression/decompression speed and ratio for various
 * data types and sizes. Provides performance regression tracking.
 */

#include "compressum/compressum.hpp"
#include "compressum/config.hpp"
#include "compressum/core/crc32.hpp"
#include "compressum/dictionary/hashchain.hpp"

#include <iostream>
#include <iomanip>
#include <chrono>
#include <vector>
#include <random>
#include <cstring>
#include <numeric>
#include <algorithm>

using namespace compressum;

// ============================================================================
// Timing Utilities
// ============================================================================

class Timer {
public:
    void start() {
        start_ = std::chrono::high_resolution_clock::now();
    }

    void stop() {
        end_ = std::chrono::high_resolution_clock::now();
    }

    double elapsed_ms() const {
        return std::chrono::duration<double, std::milli>(end_ - start_).count();
    }

    double elapsed_sec() const {
        return std::chrono::duration<double>(end_ - start_).count();
    }

private:
    std::chrono::high_resolution_clock::time_point start_;
    std::chrono::high_resolution_clock::time_point end_;
};

// ============================================================================
// Data Generators
// ============================================================================

namespace generators {

// Random binary data (high entropy, low compressibility)
std::vector<Byte> random_data(size_t size, uint32_t seed = 42) {
    std::mt19937 rng(seed);
    std::vector<Byte> data(size);
    for (auto& b : data) {
        b = static_cast<Byte>(rng() % 256);
    }
    return data;
}

// Text-like data (English-like character distribution)
std::vector<Byte> text_data(size_t size) {
    static const char* words[] = {
        "the ", "be ", "to ", "of ", "and ", "a ", "in ", "that ", "have ", "I ",
        "it ", "for ", "not ", "on ", "with ", "he ", "as ", "you ", "do ", "at ",
        "this ", "but ", "his ", "by ", "from ", "they ", "we ", "say ", "her ", "she ",
        "or ", "an ", "will ", "my ", "one ", "all ", "would ", "there ", "their ", "what "
    };
    const size_t num_words = sizeof(words) / sizeof(words[0]);

    std::mt19937 rng(123);
    std::vector<Byte> data;
    data.reserve(size);

    while (data.size() < size) {
        const char* word = words[rng() % num_words];
        size_t len = strlen(word);
        for (size_t i = 0; i < len && data.size() < size; ++i) {
            data.push_back(static_cast<Byte>(word[i]));
        }
    }

    return data;
}

// Repeated pattern data (high compressibility)
std::vector<Byte> repeated_data(size_t size, size_t pattern_len = 16) {
    std::vector<Byte> pattern(pattern_len);
    for (size_t i = 0; i < pattern_len; ++i) {
        pattern[i] = static_cast<Byte>('A' + (i % 26));
    }

    std::vector<Byte> data;
    data.reserve(size);
    while (data.size() < size) {
        for (size_t i = 0; i < pattern_len && data.size() < size; ++i) {
            data.push_back(pattern[i]);
        }
    }

    return data;
}

// Binary structured data (records, headers)
std::vector<Byte> binary_data(size_t size) {
    std::vector<Byte> data;
    data.reserve(size);

    uint32_t record_id = 0;
    while (data.size() < size) {
        // Record header
        data.push_back(0x01);  // Record type
        data.push_back(static_cast<Byte>(record_id & 0xFF));
        data.push_back(static_cast<Byte>((record_id >> 8) & 0xFF));
        data.push_back(static_cast<Byte>((record_id >> 16) & 0xFF));
        data.push_back(static_cast<Byte>((record_id >> 24) & 0xFF));
        data.push_back(0x10);  // Data length

        // Record data
        for (int j = 0; j < 16 && data.size() < size; ++j) {
            data.push_back(static_cast<Byte>((record_id + j) % 256));
        }

        ++record_id;
    }

    return data;
}

// Mixed data (combination of patterns)
std::vector<Byte> mixed_data(size_t size) {
    std::vector<Byte> data;
    data.reserve(size);

    std::mt19937 rng(456);
    size_t chunk_size = size / 4;

    // Text chunk
    auto text = text_data(chunk_size);
    data.insert(data.end(), text.begin(), text.end());

    // Random chunk
    auto random = random_data(chunk_size, 789);
    data.insert(data.end(), random.begin(), random.end());

    // Binary chunk
    auto binary = binary_data(chunk_size);
    data.insert(data.end(), binary.begin(), binary.end());

    // Repeated chunk
    auto repeated = repeated_data(chunk_size);
    data.insert(data.end(), repeated.begin(), repeated.end());

    return data;
}

} // namespace generators

// ============================================================================
// Benchmark Results
// ============================================================================

struct BenchmarkResult {
    std::string name;
    size_t original_size;
    size_t compressed_size;
    double compression_time_ms;
    double decompression_time_ms;
    bool roundtrip_ok;

    double compression_ratio() const {
        return original_size > 0
            ? static_cast<double>(original_size) / compressed_size
            : 0.0;
    }

    double compression_speed_mbps() const {
        return compression_time_ms > 0
            ? (original_size / (1024.0 * 1024.0)) / (compression_time_ms / 1000.0)
            : 0.0;
    }

    double decompression_speed_mbps() const {
        return decompression_time_ms > 0
            ? (original_size / (1024.0 * 1024.0)) / (decompression_time_ms / 1000.0)
            : 0.0;
    }
};

// ============================================================================
// Benchmark Runner
// ============================================================================

BenchmarkResult run_benchmark(
    const std::string& name,
    const std::vector<Byte>& data,
    Level level,
    int iterations = 3
) {
    BenchmarkResult result;
    result.name = name;
    result.original_size = data.size();

    std::vector<double> comp_times;
    std::vector<double> decomp_times;
    std::vector<Byte> compressed;

    CompressOptions options;
    options.level = level;

    for (int i = 0; i < iterations; ++i) {
        Timer timer;

        // Compression
        Compressor c(options);
        timer.start();
        auto [comp, comp_result] = c.compress(data);
        timer.stop();

        comp_times.push_back(timer.elapsed_ms());
        compressed = std::move(comp);
        result.compressed_size = compressed.size();

        // Decompression
        Decompressor d;
        timer.start();
        auto [decomp, decomp_result] = d.decompress(compressed);
        timer.stop();

        decomp_times.push_back(timer.elapsed_ms());
        result.roundtrip_ok = (decomp == data);
    }

    // Take median (more stable than mean)
    std::sort(comp_times.begin(), comp_times.end());
    std::sort(decomp_times.begin(), decomp_times.end());

    result.compression_time_ms = comp_times[iterations / 2];
    result.decompression_time_ms = decomp_times[iterations / 2];

    return result;
}

void print_result(const BenchmarkResult& r) {
    std::cout << std::setw(30) << std::left << r.name
              << std::setw(10) << std::right << (r.original_size / 1024) << " KB"
              << std::setw(10) << std::right << std::fixed << std::setprecision(2) << r.compression_ratio() << "x"
              << std::setw(12) << std::right << std::fixed << std::setprecision(1) << r.compression_speed_mbps() << " MB/s"
              << std::setw(12) << std::right << std::fixed << std::setprecision(1) << r.decompression_speed_mbps() << " MB/s"
              << "  " << (r.roundtrip_ok ? "OK" : "FAIL")
              << "\n";
}

void print_header() {
    std::cout << std::setw(30) << std::left << "Benchmark"
              << std::setw(14) << std::right << "Size"
              << std::setw(10) << std::right << "Ratio"
              << std::setw(12) << std::right << "Compress"
              << std::setw(12) << std::right << "Decompress"
              << "  Status\n";
    std::cout << std::string(85, '-') << "\n";
}

// ============================================================================
// SIMD Feature Detection Display
// ============================================================================

void print_simd_features() {
    std::cout << "SIMD Features:\n";

    #if COMPRESSUM_ARCH_X86
    std::cout << "  Architecture: x86-64\n";
    #elif COMPRESSUM_ARCH_ARM
    std::cout << "  Architecture: ARM64\n";
    #else
    std::cout << "  Architecture: Unknown\n";
    #endif

    #if COMPRESSUM_USE_SSE42
    std::cout << "  SSE4.2: Enabled (16-byte string comparison, CRC32)\n";
    #else
    std::cout << "  SSE4.2: Disabled\n";
    #endif

    #if COMPRESSUM_USE_AVX2
    std::cout << "  AVX2: Enabled (32-byte string comparison)\n";
    #else
    std::cout << "  AVX2: Disabled\n";
    #endif

    #if COMPRESSUM_USE_NEON
    std::cout << "  NEON: Enabled (16-byte string comparison)\n";
    #else
    std::cout << "  NEON: Disabled\n";
    #endif

    #if COMPRESSUM_USE_ARM_CRC32
    std::cout << "  ARM CRC32: Enabled (hardware CRC32-C)\n";
    #else
    std::cout << "  ARM CRC32: Disabled\n";
    #endif

    std::cout << "  Hardware CRC32: " << (core::has_hardware_crc32() ? "Yes" : "No") << "\n";
    std::cout << "\n";
}

// ============================================================================
// CRC32 Micro-benchmark
// ============================================================================

void benchmark_crc32() {
    std::cout << "CRC32-C Benchmark:\n";
    std::cout << std::string(50, '-') << "\n";

    const size_t sizes[] = {1024, 10*1024, 100*1024, 1024*1024};

    for (size_t size : sizes) {
        auto data = generators::random_data(size, 777);
        const int iterations = 100;

        Timer timer;
        timer.start();
        uint32_t crc = 0;
        for (int i = 0; i < iterations; ++i) {
            crc = core::crc32c(crc, data);
        }
        timer.stop();

        double throughput = (static_cast<double>(size) * iterations / (1024.0 * 1024.0)) / timer.elapsed_sec();

        std::cout << "  " << std::setw(10) << std::right << (size / 1024) << " KB: "
                  << std::setw(8) << std::fixed << std::setprecision(1) << throughput << " MB/s"
                  << " (CRC: 0x" << std::hex << crc << std::dec << ")\n";
    }

    std::cout << "\n";
}

// ============================================================================
// Hash Chain Micro-benchmark
// ============================================================================

void benchmark_hashchain() {
    std::cout << "Hash Chain Match Finding Benchmark:\n";
    std::cout << std::string(50, '-') << "\n";

    // Generate text data with patterns
    auto data = generators::text_data(64 * 1024);  // 64 KB

    dict::HashChainConfig config;
    config.window_size = 32768;
    config.max_chain = 64;

    dict::HashChain hc(config);

    // Build hash chain
    Timer timer;
    timer.start();
    for (size_t i = 0; i + 3 < data.size(); ++i) {
        hc.insert(data.data(), static_cast<uint32_t>(i));
    }
    timer.stop();

    double insert_rate = (data.size() / 1024.0) / timer.elapsed_ms() * 1000.0;
    std::cout << "  Insert rate: " << std::fixed << std::setprecision(1)
              << insert_rate << " KB/ms\n";

    // Find matches
    size_t total_matches = 0;
    size_t total_match_len = 0;

    timer.start();
    for (size_t i = 1000; i + 258 < data.size(); i += 10) {
        auto match = hc.find_match(
            data.data(),
            static_cast<uint32_t>(i),
            static_cast<uint32_t>(std::min(size_t(258), data.size() - i))
        );
        if (match.length >= 3) {
            ++total_matches;
            total_match_len += match.length;
        }
    }
    timer.stop();

    size_t lookups = (data.size() - 1000 - 258) / 10;
    double lookup_rate = static_cast<double>(lookups) / timer.elapsed_ms() * 1000.0;

    std::cout << "  Lookup rate: " << std::fixed << std::setprecision(0)
              << lookup_rate << " lookups/sec\n";
    std::cout << "  Matches found: " << total_matches
              << " (avg len: " << std::fixed << std::setprecision(1)
              << (total_matches > 0 ? static_cast<double>(total_match_len) / total_matches : 0)
              << ")\n";

    std::cout << "\n";
}

// ============================================================================
// Main Compression Benchmarks
// ============================================================================

void run_size_benchmarks(Level level, const std::string& level_name) {
    std::cout << "Compression Benchmarks (" << level_name << "):\n";
    print_header();

    // Different sizes
    const size_t sizes[] = {1024, 100*1024, 1024*1024, 10*1024*1024};
    const char* size_names[] = {"1 KB", "100 KB", "1 MB", "10 MB"};

    for (size_t i = 0; i < sizeof(sizes) / sizeof(sizes[0]); ++i) {
        size_t size = sizes[i];

        // Text data
        auto text = generators::text_data(size);
        auto r = run_benchmark(std::string("Text ") + size_names[i], text, level);
        print_result(r);

        // Binary data
        auto binary = generators::binary_data(size);
        r = run_benchmark(std::string("Binary ") + size_names[i], binary, level);
        print_result(r);

        // Random data
        auto random = generators::random_data(size);
        r = run_benchmark(std::string("Random ") + size_names[i], random, level);
        print_result(r);
    }

    std::cout << "\n";
}

void run_type_benchmarks() {
    std::cout << "Data Type Benchmarks (1 MB, Normal):\n";
    print_header();

    const size_t size = 1024 * 1024;

    auto r = run_benchmark("Text (English)", generators::text_data(size), Level::Normal);
    print_result(r);

    r = run_benchmark("Binary (Structured)", generators::binary_data(size), Level::Normal);
    print_result(r);

    r = run_benchmark("Repeated Pattern", generators::repeated_data(size), Level::Normal);
    print_result(r);

    r = run_benchmark("Random (Incompressible)", generators::random_data(size), Level::Normal);
    print_result(r);

    r = run_benchmark("Mixed Content", generators::mixed_data(size), Level::Normal);
    print_result(r);

    std::cout << "\n";
}

void run_level_comparison() {
    std::cout << "Compression Level Comparison (1 MB Text):\n";
    print_header();

    auto data = generators::text_data(1024 * 1024);

    auto r = run_benchmark("Fast", data, Level::Fast);
    print_result(r);

    r = run_benchmark("Normal", data, Level::Normal);
    print_result(r);

    r = run_benchmark("Best", data, Level::Best);
    print_result(r);

    std::cout << "\n";
}

// ============================================================================
// Main
// ============================================================================

int main(int argc, char* argv[]) {
    std::cout << "CompressUM Performance Benchmarks (Phase 7)\n";
    std::cout << "============================================\n\n";

    // Print SIMD features
    print_simd_features();

    // Micro-benchmarks
    benchmark_crc32();
    benchmark_hashchain();

    // Main benchmarks
    run_type_benchmarks();
    run_level_comparison();

    // Full size benchmark (only for Fast mode to avoid long runtime)
    if (argc > 1 && std::string(argv[1]) == "--full") {
        run_size_benchmarks(Level::Fast, "Fast");
        run_size_benchmarks(Level::Normal, "Normal");
        run_size_benchmarks(Level::Best, "Best");
    } else {
        run_size_benchmarks(Level::Fast, "Fast");
        std::cout << "(Run with --full for all levels)\n\n";
    }

    // Performance targets summary
    std::cout << "Performance Targets (from CLAUDE.md):\n";
    std::cout << std::string(40, '-') << "\n";
    std::cout << "  Compression speed:   100+ MB/s (Fast mode)\n";
    std::cout << "  Decompression speed: 300+ MB/s\n";
    std::cout << "  Compression ratio:   2.5-3.5x (generic data)\n";
    std::cout << "\n";

    return 0;
}
