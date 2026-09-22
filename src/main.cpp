/**
 * CompressUM - Original Compression Algorithm
 * Main Entry Point
 *
 * CLI interface for compression, decompression, and analysis.
 */

#include "compressum/compressum.hpp"
#include "compressum/cli/parser.hpp"

#include <iostream>
#include <filesystem>
#include <chrono>
#include <iomanip>

namespace fs = std::filesystem;

using namespace compressum;

/**
 * Print analysis results
 */
void print_analysis(const std::string& filename, const DataStats& stats) {
    std::cout << "File: " << filename << "\n";
    std::cout << "  Size:      " << stats.size << " bytes\n";
    std::cout << "  Entropy:   " << std::fixed << std::setprecision(3)
              << stats.entropy << " bits/byte\n";
    std::cout << "  Type:      ";

    switch (stats.type) {
        case DataType::Text:       std::cout << "Text"; break;
        case DataType::Binary:     std::cout << "Binary"; break;
        case DataType::Executable: std::cout << "Executable"; break;
        case DataType::Structured: std::cout << "Structured (JSON/XML)"; break;
        case DataType::Compressed: std::cout << "Already compressed"; break;
        case DataType::Media:      std::cout << "Media (image/audio/video)"; break;
        default:                   std::cout << "Unknown"; break;
    }
    std::cout << "\n";

    std::cout << "  Estimated: " << std::fixed << std::setprecision(1)
              << (stats.compression_estimate() * 100.0) << "% of original\n";

    if (!stats.is_compressible()) {
        std::cout << "  Warning: High entropy - may not compress well\n";
    }

    std::cout << "\n";
}

/**
 * Analyze files
 */
int cmd_analyze(const cli::Options& opts) {
    for (const auto& filename : opts.input_files) {
        if (!fs::exists(filename)) {
            std::cerr << "Error: File not found: " << filename << "\n";
            continue;
        }

        core::MappedFileReader file;
        auto result = file.open(filename);
        if (!result.ok()) {
            std::cerr << "Error: Cannot open file: " << filename << "\n";
            continue;
        }

        auto stats = analyze(file.span());
        print_analysis(filename, stats);
    }

    return 0;
}

/**
 * Compress files
 */
int cmd_compress(const cli::Options& opts) {
    CompressOptions comp_opts{
        .level = opts.level,
        .block_size = opts.block_size,
        .threads = opts.threads
    };

    Compressor compressor(comp_opts);

    int rc = 0;
    for (const auto& input_file : opts.input_files) {
        if (!fs::exists(input_file)) {
            std::cerr << "Error: File not found: " << input_file << "\n";
            rc = 1;
            continue;
        }

        // Determine output filename
        std::string output_file = opts.output_file;
        if (output_file.empty()) {
            output_file = input_file + ".cum";
        }

        // Check if output exists
        if (fs::exists(output_file) && !opts.force) {
            std::cerr << "Error: Output file exists: " << output_file
                      << " (use -f to overwrite)\n";
            rc = 1;
            continue;
        }

        if (!opts.quiet) {
            std::cout << "Compressing: " << input_file << " -> " << output_file << "\n";
        }

        auto start = std::chrono::high_resolution_clock::now();
        auto result = compressor.compress_file(input_file, output_file);
        auto end = std::chrono::high_resolution_clock::now();

        if (!result.ok()) {
            std::cerr << "Error: Compression failed: " << error_message(result.error) << "\n";
            rc = 1;
            continue;
        }

        if (!opts.quiet) {
            auto duration = std::chrono::duration<double>(end - start).count();
            std::cout << "  Original:   " << result.original_size << " bytes\n";
            std::cout << "  Compressed: " << result.compressed_size << " bytes\n";
            std::cout << "  Ratio:      " << std::fixed << std::setprecision(2)
                      << result.ratio
                      << "x\n";
            std::cout << "  Speed:      " << std::fixed << std::setprecision(1)
                      << (result.original_size / 1024.0 / 1024.0 / duration)
                      << " MB/s\n";
        }

        // Remove input if requested
        if (!opts.keep && result.ok()) {
            fs::remove(input_file);
        }
    }

    return rc;
}

/**
 * Decompress files
 */
int cmd_decompress(const cli::Options& opts) {
    DecompressOptions decomp_opts{
        .threads = opts.threads
    };

    Decompressor decompressor(decomp_opts);

    int rc = 0;
    for (const auto& input_file : opts.input_files) {
        if (!fs::exists(input_file)) {
            std::cerr << "Error: File not found: " << input_file << "\n";
            rc = 1;
            continue;
        }

        // Determine output filename
        std::string output_file = opts.output_file;
        if (output_file.empty()) {
            // Remove .cum extension if present
            if (input_file.size() > 4 && input_file.substr(input_file.size() - 4) == ".cum") {
                output_file = input_file.substr(0, input_file.size() - 4);
            } else {
                output_file = input_file + ".out";
            }
        }

        // Check if output exists
        if (fs::exists(output_file) && !opts.force) {
            std::cerr << "Error: Output file exists: " << output_file
                      << " (use -f to overwrite)\n";
            rc = 1;
            continue;
        }

        if (!opts.quiet) {
            std::cout << "Decompressing: " << input_file << " -> " << output_file << "\n";
        }

        auto start = std::chrono::high_resolution_clock::now();
        auto result = decompressor.decompress_file(input_file, output_file);
        auto end = std::chrono::high_resolution_clock::now();

        if (!result.ok()) {
            std::cerr << "Error: Decompression failed: " << error_message(result.error) << "\n";
            rc = 1;
            continue;
        }

        if (!opts.quiet) {
            auto duration = std::chrono::duration<double>(end - start).count();
            std::cout << "  Original:   " << result.original_size << " bytes\n";
            std::cout << "  Speed:      " << std::fixed << std::setprecision(1)
                      << (result.original_size / 1024.0 / 1024.0 / duration)
                      << " MB/s\n";
            if (!result.crc_valid) {
                std::cerr << "  Warning: CRC mismatch!\n";
            }
        }

        // Remove input if requested
        if (!opts.keep && result.ok()) {
            fs::remove(input_file);
        }
    }

    return rc;
}

/**
 * Run benchmarks
 */
int cmd_benchmark(const cli::Options& opts) {
    std::cout << "CompressUM Benchmark\n";
    std::cout << "====================\n\n";

    for (const auto& input_file : opts.input_files) {
        if (!fs::exists(input_file)) {
            std::cerr << "Skipping: " << input_file << " (not found)\n";
            continue;
        }

        core::MappedFileReader file;
        if (!file.open(input_file).ok()) {
            std::cerr << "Skipping: " << input_file << " (cannot open)\n";
            continue;
        }

        std::cout << "File: " << input_file << " (" << file.size() << " bytes)\n";

        // Analyze first
        auto stats = analyze(file.span());
        std::cout << "  Entropy: " << std::fixed << std::setprecision(3)
                  << stats.entropy << " bits/byte\n";

        // Test each level
        for (auto level : {Level::Fast, Level::Normal, Level::Best}) {
            const char* level_name = (level == Level::Fast) ? "Fast" :
                                     (level == Level::Normal) ? "Normal" : "Best";

            Compressor compressor(CompressOptions{.level = level});

            // Warm up
            auto [_, __] = compressor.compress(file.span());

            // Benchmark compression
            auto start = std::chrono::high_resolution_clock::now();
            auto [compressed, result] = compressor.compress(file.span());
            auto end = std::chrono::high_resolution_clock::now();

            double compress_time = std::chrono::duration<double>(end - start).count();
            double compress_speed = file.size() / 1024.0 / 1024.0 / compress_time;

            // Benchmark decompression
            Decompressor decompressor;
            start = std::chrono::high_resolution_clock::now();
            auto [decompressed, decomp_result] = decompressor.decompress(compressed);
            end = std::chrono::high_resolution_clock::now();

            double decompress_time = std::chrono::duration<double>(end - start).count();
            double decompress_speed = file.size() / 1024.0 / 1024.0 / decompress_time;

            std::cout << "  " << std::setw(7) << level_name << ": "
                      << std::setw(10) << compressed.size() << " bytes ("
                      << std::fixed << std::setprecision(2) << result.ratio << "x) | "
                      << "C: " << std::setw(6) << std::setprecision(1) << compress_speed << " MB/s | "
                      << "D: " << std::setw(6) << decompress_speed << " MB/s\n";
        }

        std::cout << "\n";
    }

    return 0;
}

int main(int argc, char* argv[]) {
    cli::Parser parser("cum", "CompressUM - Hybrid Compression Tool");

    auto opts = parser.parse(argc, argv);
    if (!opts) {
        return 1;
    }

    switch (opts->command) {
        case cli::Command::Help:
            parser.print_usage();
            return 0;

        case cli::Command::Version:
            parser.print_version();
            return 0;

        case cli::Command::Analyze:
            return cmd_analyze(*opts);

        case cli::Command::Compress:
            return cmd_compress(*opts);

        case cli::Command::Decompress:
            return cmd_decompress(*opts);

        case cli::Command::Benchmark:
            return cmd_benchmark(*opts);

        default:
            std::cerr << "Error: Unknown command\n";
            return 1;
    }
}
