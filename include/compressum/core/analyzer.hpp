#pragma once

/**
 * CompressUM - Original Compression Algorithm
 * Data Analyzer
 *
 * Performs statistical analysis of input data:
 * - Byte frequency histogram
 * - Shannon entropy calculation
 * - Data type detection
 * - Compressibility estimation
 */

#include "../types.hpp"
#include "../config.hpp"
#include <cmath>
#include <algorithm>

namespace compressum::core {

/**
 * Analyze data and compute statistics
 */
class Analyzer {
public:
    /**
     * Analyze data and return statistics
     */
    [[nodiscard]] static DataStats analyze(ByteSpan data) {
        DataStats stats{};
        stats.size = data.size();
        stats.histogram.fill(0);
        stats.media_format = MediaFormat::Unknown;

        if (data.empty()) {
            stats.entropy = 0.0;
            stats.type = DataType::Unknown;
            return stats;
        }

        // Build histogram
        for (Byte b : data) {
            ++stats.histogram[b];
        }

        // Calculate Shannon entropy
        stats.entropy = calculate_entropy(stats.histogram, data.size());

        // Detect data type
        stats.type = detect_type(data, stats.histogram);

        // Detect specific media format if media type
        if (stats.type == DataType::Media) {
            stats.media_format = detect_media_format(data);
        }

        return stats;
    }

    /**
     * Calculate Shannon entropy from histogram
     * H(X) = -Σ P(xᵢ) log₂ P(xᵢ)
     *
     * Returns value in range [0.0, 8.0] bits per byte
     */
    [[nodiscard]] static double calculate_entropy(
        const std::array<uint32_t, 256>& histogram,
        size_t total_count
    ) {
        if (total_count == 0) return 0.0;

        double entropy = 0.0;
        const double inv_total = 1.0 / static_cast<double>(total_count);

        for (uint32_t count : histogram) {
            if (count > 0) {
                double p = static_cast<double>(count) * inv_total;
                entropy -= p * std::log2(p);
            }
        }

        return entropy;
    }

    /**
     * Calculate conditional entropy (order-1)
     * H(X|Y) where Y is previous byte
     */
    [[nodiscard]] static double calculate_conditional_entropy(ByteSpan data) {
        if (data.size() < 2) return 0.0;

        // Bigram counts: [prev_byte][curr_byte]
        std::array<std::array<uint32_t, 256>, 256> bigrams{};
        std::array<uint32_t, 256> prev_counts{};

        for (size_t i = 1; i < data.size(); ++i) {
            Byte prev = data[i - 1];
            Byte curr = data[i];
            ++bigrams[prev][curr];
            ++prev_counts[prev];
        }

        double total_entropy = 0.0;
        const size_t total = data.size() - 1;

        for (size_t prev = 0; prev < 256; ++prev) {
            if (prev_counts[prev] == 0) continue;

            double context_entropy = 0.0;
            double inv_count = 1.0 / static_cast<double>(prev_counts[prev]);

            for (size_t curr = 0; curr < 256; ++curr) {
                if (bigrams[prev][curr] > 0) {
                    double p = static_cast<double>(bigrams[prev][curr]) * inv_count;
                    context_entropy -= p * std::log2(p);
                }
            }

            // Weight by probability of context
            double context_prob = static_cast<double>(prev_counts[prev]) / static_cast<double>(total);
            total_entropy += context_prob * context_entropy;
        }

        return total_entropy;
    }

    /**
     * Detect data type based on content analysis
     */
    [[nodiscard]] static DataType detect_type(
        ByteSpan data,
        const std::array<uint32_t, 256>& histogram
    ) {
        if (data.size() < 16) {
            return DataType::Unknown;
        }

        // Check for executable magic bytes
        if (is_executable(data)) {
            return DataType::Executable;
        }

        // Check for already compressed data
        if (is_compressed(data)) {
            return DataType::Compressed;
        }

        // Check for media files
        if (is_media(data)) {
            return DataType::Media;
        }

        // Check for text
        if (is_text(histogram, data.size())) {
            // Check for structured text (JSON, XML)
            if (is_structured_text(data)) {
                return DataType::Structured;
            }
            return DataType::Text;
        }

        return DataType::Binary;
    }

private:
    /**
     * Check for executable file signatures
     */
    [[nodiscard]] static bool is_executable(ByteSpan data) {
        if (data.size() < 4) return false;

        // ELF
        if (data[0] == 0x7F && data[1] == 'E' && data[2] == 'L' && data[3] == 'F') {
            return true;
        }

        // PE (Windows)
        if (data[0] == 'M' && data[1] == 'Z') {
            return true;
        }

        // Mach-O (macOS)
        uint32_t magic = static_cast<uint32_t>(data[0]) |
                        (static_cast<uint32_t>(data[1]) << 8) |
                        (static_cast<uint32_t>(data[2]) << 16) |
                        (static_cast<uint32_t>(data[3]) << 24);

        if (magic == 0xFEEDFACE || magic == 0xFEEDFACF ||  // Mach-O 32/64
            magic == 0xCAFEBABE || magic == 0xBEBAFECA) {  // Fat binary
            return true;
        }

        return false;
    }

    /**
     * Check for compressed file signatures
     */
    [[nodiscard]] static bool is_compressed(ByteSpan data) {
        if (data.size() < 4) return false;

        // GZIP
        if (data[0] == 0x1F && data[1] == 0x8B) {
            return true;
        }

        // ZLIB
        if ((data[0] == 0x78) && (data[1] == 0x01 || data[1] == 0x5E ||
            data[1] == 0x9C || data[1] == 0xDA)) {
            return true;
        }

        // ZSTD
        if (data[0] == 0x28 && data[1] == 0xB5 && data[2] == 0x2F && data[3] == 0xFD) {
            return true;
        }

        // XZ
        if (data[0] == 0xFD && data[1] == '7' && data[2] == 'z' &&
            data[3] == 'X' && data[4] == 'Z') {
            return true;
        }

        // BZIP2
        if (data[0] == 'B' && data[1] == 'Z' && data[2] == 'h') {
            return true;
        }

        // ZIP/JAR
        if (data[0] == 'P' && data[1] == 'K' && data[2] == 0x03 && data[3] == 0x04) {
            return true;
        }

        // RAR
        if (data[0] == 'R' && data[1] == 'a' && data[2] == 'r' && data[3] == '!') {
            return true;
        }

        // 7z
        if (data[0] == '7' && data[1] == 'z' && data[2] == 0xBC && data[3] == 0xAF) {
            return true;
        }

        return false;
    }

    /**
     * Detect specific media format from file signatures
     */
    [[nodiscard]] static MediaFormat detect_media_format(ByteSpan data) {
        if (data.size() < 12) return MediaFormat::Unknown;

        // JPEG: FF D8 FF
        if (data[0] == 0xFF && data[1] == 0xD8 && data[2] == 0xFF) {
            return MediaFormat::JPEG;
        }

        // PNG: 89 50 4E 47 0D 0A 1A 0A
        if (data[0] == 0x89 && data[1] == 'P' && data[2] == 'N' && data[3] == 'G') {
            return MediaFormat::PNG;
        }

        // GIF: GIF87a or GIF89a
        if (data[0] == 'G' && data[1] == 'I' && data[2] == 'F' && data[3] == '8') {
            return MediaFormat::GIF;
        }

        // WebP: RIFF....WEBP
        if (data[0] == 'R' && data[1] == 'I' && data[2] == 'F' && data[3] == 'F' &&
            data[8] == 'W' && data[9] == 'E' && data[10] == 'B' && data[11] == 'P') {
            return MediaFormat::WebP;
        }

        // BMP: BM
        if (data[0] == 'B' && data[1] == 'M') {
            return MediaFormat::BMP;
        }

        // TIFF: II (little-endian) or MM (big-endian)
        if ((data[0] == 'I' && data[1] == 'I' && data[2] == 0x2A && data[3] == 0x00) ||
            (data[0] == 'M' && data[1] == 'M' && data[2] == 0x00 && data[3] == 0x2A)) {
            return MediaFormat::TIFF;
        }

        return MediaFormat::Unknown;
    }

    /**
     * Check for media file signatures
     */
    [[nodiscard]] static bool is_media(ByteSpan data) {
        if (data.size() < 12) return false;

        // Use detect_media_format for image formats we optimize
        if (detect_media_format(data) != MediaFormat::Unknown) {
            return true;
        }

        // MP3 (ID3 or sync) - audio, not optimized but still media
        if ((data[0] == 'I' && data[1] == 'D' && data[2] == '3') ||
            (data[0] == 0xFF && (data[1] & 0xE0) == 0xE0)) {
            return true;
        }

        // MP4/MOV - video, not optimized but still media
        if ((data[4] == 'f' && data[5] == 't' && data[6] == 'y' && data[7] == 'p') ||
            (data[4] == 'm' && data[5] == 'o' && data[6] == 'o' && data[7] == 'v')) {
            return true;
        }

        return false;
    }

    /**
     * Check if data appears to be text
     */
    [[nodiscard]] static bool is_text(
        const std::array<uint32_t, 256>& histogram,
        size_t total
    ) {
        // Count printable ASCII + common whitespace
        size_t printable = 0;
        for (int i = 32; i < 127; ++i) {
            printable += histogram[i];
        }
        printable += histogram['\t'];
        printable += histogram['\n'];
        printable += histogram['\r'];

        // Count control characters (excluding common whitespace)
        size_t control = 0;
        for (int i = 0; i < 32; ++i) {
            if (i != '\t' && i != '\n' && i != '\r') {
                control += histogram[i];
            }
        }
        control += histogram[127];  // DEL

        // Text if >85% printable and <5% control
        double printable_ratio = static_cast<double>(printable) / static_cast<double>(total);
        double control_ratio = static_cast<double>(control) / static_cast<double>(total);

        return printable_ratio > 0.85 && control_ratio < 0.05;
    }

    /**
     * Check for structured text (JSON, XML, HTML)
     */
    [[nodiscard]] static bool is_structured_text(ByteSpan data) {
        // Skip leading whitespace
        size_t start = 0;
        while (start < data.size() && (data[start] == ' ' || data[start] == '\t' ||
               data[start] == '\n' || data[start] == '\r')) {
            ++start;
        }

        if (start >= data.size()) return false;

        // JSON starts with { or [
        if (data[start] == '{' || data[start] == '[') {
            return true;
        }

        // XML/HTML starts with <
        if (data[start] == '<') {
            // Check for <?xml or <!DOCTYPE or <html
            return true;
        }

        return false;
    }
};

/**
 * Quick entropy estimate for small sample
 * Uses first N bytes for faster analysis
 */
[[nodiscard]] inline double quick_entropy(ByteSpan data, size_t sample_size = 4096) {
    if (data.size() <= sample_size) {
        return Analyzer::analyze(data).entropy;
    }

    // Sample from beginning, middle, and end
    std::array<uint32_t, 256> histogram{};
    size_t total = 0;

    auto sample = [&](size_t start, size_t len) {
        for (size_t i = start; i < start + len && i < data.size(); ++i) {
            ++histogram[data[i]];
            ++total;
        }
    };

    size_t chunk = sample_size / 3;
    sample(0, chunk);                           // Beginning
    sample(data.size() / 2 - chunk / 2, chunk); // Middle
    sample(data.size() - chunk, chunk);         // End

    return Analyzer::calculate_entropy(histogram, total);
}

} // namespace compressum::core
