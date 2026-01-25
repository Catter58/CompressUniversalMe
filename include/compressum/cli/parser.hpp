#pragma once

/**
 * CompressUM - Original Compression Algorithm
 * CLI Parser - Lightweight command-line argument parser
 *
 * Implemented from scratch without external libraries.
 * Supports subcommands, options, and positional arguments.
 */

#include "../types.hpp"
#include "../config.hpp"
#include <string>
#include <string_view>
#include <vector>
#include <optional>
#include <functional>
#include <unordered_map>
#include <iostream>
#include <algorithm>
#include <charconv>
#include <cstring>

namespace compressum::cli {

/**
 * Command type
 */
enum class Command {
    None,
    Compress,
    Decompress,
    Analyze,
    Benchmark,
    Help,
    Version
};

/**
 * Parsed command-line options
 */
struct Options {
    Command command = Command::None;
    std::vector<std::string> input_files;
    std::string output_file;
    Level level = Level::Normal;
    size_t threads = 0;          // 0 = auto
    size_t block_size = config::DEFAULT_BLOCK_SIZE;
    size_t memory_limit = config::DEFAULT_MEMORY_LIMIT;
    bool force = false;          // Overwrite existing files
    bool keep = true;            // Keep input files
    bool verbose = false;
    bool quiet = false;
    bool recursive = false;      // Process directories recursively

    [[nodiscard]] bool valid() const {
        return command != Command::None && !input_files.empty();
    }
};

/**
 * CLI Parser
 */
class Parser {
public:
    Parser(std::string program_name, std::string description)
        : program_name_(std::move(program_name))
        , description_(std::move(description))
    {}

    /**
     * Parse command-line arguments
     * @return Parsed options or empty optional on error
     */
    [[nodiscard]] std::optional<Options> parse(int argc, char* argv[]) {
        Options opts;

        if (argc < 2) {
            print_usage();
            return std::nullopt;
        }

        // First argument is the command
        std::string_view cmd(argv[1]);

        if (cmd == "compress" || cmd == "c") {
            opts.command = Command::Compress;
        } else if (cmd == "decompress" || cmd == "d" || cmd == "extract" || cmd == "x") {
            opts.command = Command::Decompress;
        } else if (cmd == "analyze" || cmd == "a") {
            opts.command = Command::Analyze;
        } else if (cmd == "benchmark" || cmd == "bench" || cmd == "b") {
            opts.command = Command::Benchmark;
        } else if (cmd == "--help" || cmd == "-h" || cmd == "help") {
            opts.command = Command::Help;
            return opts;
        } else if (cmd == "--version" || cmd == "-V" || cmd == "version") {
            opts.command = Command::Version;
            return opts;
        } else if (cmd[0] == '-') {
            std::cerr << "Error: Unknown option: " << cmd << "\n";
            std::cerr << "Use '" << program_name_ << " --help' for usage.\n";
            return std::nullopt;
        } else {
            // Assume compress if no command given and first arg is a file
            opts.command = Command::Compress;
            opts.input_files.emplace_back(cmd);
        }

        // Parse remaining arguments
        for (int i = 2; i < argc; ++i) {
            std::string_view arg(argv[i]);

            if (arg == "-o" || arg == "--output") {
                if (++i >= argc) {
                    std::cerr << "Error: " << arg << " requires an argument\n";
                    return std::nullopt;
                }
                opts.output_file = argv[i];
            }
            else if (arg == "-l" || arg == "--level") {
                if (++i >= argc) {
                    std::cerr << "Error: " << arg << " requires an argument\n";
                    return std::nullopt;
                }
                int level;
                auto [ptr, ec] = std::from_chars(argv[i], argv[i] + std::strlen(argv[i]), level);
                if (ec != std::errc{} || level < 1 || level > 9) {
                    std::cerr << "Error: Invalid compression level (1-9)\n";
                    return std::nullopt;
                }
                if (level <= 3) opts.level = Level::Fast;
                else if (level <= 6) opts.level = Level::Normal;
                else opts.level = Level::Best;
            }
            else if (arg == "-1" || arg == "--fast") {
                opts.level = Level::Fast;
            }
            else if (arg == "-9" || arg == "--best") {
                opts.level = Level::Best;
            }
            else if (arg == "-t" || arg == "--threads") {
                if (++i >= argc) {
                    std::cerr << "Error: " << arg << " requires an argument\n";
                    return std::nullopt;
                }
                auto [ptr, ec] = std::from_chars(argv[i], argv[i] + std::strlen(argv[i]), opts.threads);
                if (ec != std::errc{}) {
                    std::cerr << "Error: Invalid thread count\n";
                    return std::nullopt;
                }
            }
            else if (arg == "-B" || arg == "--block-size") {
                if (++i >= argc) {
                    std::cerr << "Error: " << arg << " requires an argument\n";
                    return std::nullopt;
                }
                if (!parse_size(argv[i], opts.block_size)) {
                    std::cerr << "Error: Invalid block size\n";
                    return std::nullopt;
                }
            }
            else if (arg == "-M" || arg == "--memory") {
                if (++i >= argc) {
                    std::cerr << "Error: " << arg << " requires an argument\n";
                    return std::nullopt;
                }
                if (!parse_size(argv[i], opts.memory_limit)) {
                    std::cerr << "Error: Invalid memory limit\n";
                    return std::nullopt;
                }
            }
            else if (arg == "-f" || arg == "--force") {
                opts.force = true;
            }
            else if (arg == "-k" || arg == "--keep") {
                opts.keep = true;
            }
            else if (arg == "--rm" || arg == "--remove") {
                opts.keep = false;
            }
            else if (arg == "-v" || arg == "--verbose") {
                opts.verbose = true;
            }
            else if (arg == "-q" || arg == "--quiet") {
                opts.quiet = true;
            }
            else if (arg == "-r" || arg == "--recursive") {
                opts.recursive = true;
            }
            else if (arg == "-h" || arg == "--help") {
                opts.command = Command::Help;
                return opts;
            }
            else if (arg[0] == '-') {
                std::cerr << "Error: Unknown option: " << arg << "\n";
                return std::nullopt;
            }
            else {
                // Positional argument = input file
                opts.input_files.emplace_back(arg);
            }
        }

        // Validate
        if (opts.command != Command::Help && opts.command != Command::Version) {
            if (opts.input_files.empty()) {
                std::cerr << "Error: No input files specified\n";
                return std::nullopt;
            }
        }

        return opts;
    }

    /**
     * Print usage information
     */
    void print_usage() const {
        std::cout << description_ << "\n\n";
        std::cout << "Usage: " << program_name_ << " <command> [options] <files...>\n\n";
        std::cout << "Commands:\n";
        std::cout << "  compress, c     Compress files\n";
        std::cout << "  decompress, d   Decompress files\n";
        std::cout << "  analyze, a      Analyze files (entropy, type detection)\n";
        std::cout << "  benchmark, b    Run compression benchmarks\n";
        std::cout << "  help            Show this help message\n";
        std::cout << "  version         Show version information\n";
        std::cout << "\n";
        std::cout << "Options:\n";
        std::cout << "  -o, --output <file>   Output file (default: <input>.cum)\n";
        std::cout << "  -l, --level <1-9>     Compression level (default: 5)\n";
        std::cout << "  -1, --fast            Fast compression (level 1)\n";
        std::cout << "  -9, --best            Best compression (level 9)\n";
        std::cout << "  -t, --threads <n>     Number of threads (default: auto)\n";
        std::cout << "  -B, --block-size <n>  Block size (e.g., 256K, 1M)\n";
        std::cout << "  -M, --memory <n>      Memory limit (e.g., 64M, 1G)\n";
        std::cout << "  -f, --force           Overwrite existing files\n";
        std::cout << "  -k, --keep            Keep input files (default)\n";
        std::cout << "  --rm, --remove        Remove input files after compression\n";
        std::cout << "  -r, --recursive       Process directories recursively\n";
        std::cout << "  -v, --verbose         Verbose output\n";
        std::cout << "  -q, --quiet           Quiet mode (errors only)\n";
        std::cout << "  -h, --help            Show help\n";
        std::cout << "  -V, --version         Show version\n";
        std::cout << "\n";
        std::cout << "Examples:\n";
        std::cout << "  " << program_name_ << " compress file.txt\n";
        std::cout << "  " << program_name_ << " c -9 -o output.cum input.bin\n";
        std::cout << "  " << program_name_ << " decompress file.cum\n";
        std::cout << "  " << program_name_ << " analyze data.bin\n";
    }

    /**
     * Print version information
     */
    void print_version() const {
        std::cout << program_name_ << " v"
                  << config::VERSION_MAJOR << "."
                  << config::VERSION_MINOR << "."
                  << config::VERSION_PATCH << "\n";
        std::cout << "Original compression algorithm by CompressUM project\n";
        std::cout << "Built with: ";
        #if COMPRESSUM_USE_SSE42
            std::cout << "SSE4.2 ";
        #endif
        #if COMPRESSUM_USE_AVX2
            std::cout << "AVX2 ";
        #endif
        std::cout << "\n";
    }

private:
    /**
     * Parse size string with suffix (K, M, G)
     */
    [[nodiscard]] static bool parse_size(const char* str, size_t& out) {
        size_t value;
        auto [ptr, ec] = std::from_chars(str, str + std::strlen(str), value);
        if (ec != std::errc{}) {
            return false;
        }

        // Check for suffix
        if (*ptr != '\0') {
            switch (*ptr) {
                case 'K': case 'k': value *= 1024; break;
                case 'M': case 'm': value *= 1024 * 1024; break;
                case 'G': case 'g': value *= 1024 * 1024 * 1024; break;
                default: return false;
            }
        }

        out = value;
        return true;
    }

    std::string program_name_;
    std::string description_;
};

} // namespace compressum::cli
