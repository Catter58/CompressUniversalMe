#pragma once

/**
 * CompressUM - Original Compression Algorithm
 * PNG Filter Optimizer
 *
 * Lossless PNG optimization by choosing optimal filter for each scanline.
 * Typically achieves 5-15% size reduction without any quality loss.
 *
 * Approach:
 * 1. Parse PNG chunks, preserve ancillary chunks (tEXt, tIME, etc.)
 * 2. Decompress IDAT with inflate
 * 3. For each scanline, select best filter using minimum sum heuristic
 * 4. Apply selected filters, recompress with deflate
 * 5. Rebuild PNG with optimized IDAT
 */

#include "../types.hpp"
#include "deflate.hpp"
#include <vector>
#include <array>
#include <cstring>
#include <cmath>

namespace compressum::transform {

// PNG constants
namespace png {
    // PNG signature
    constexpr std::array<Byte, 8> SIGNATURE = {0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A};

    // Critical chunk types
    constexpr uint32_t IHDR = 0x49484452;  // Image Header
    constexpr uint32_t PLTE = 0x504C5445;  // Palette
    constexpr uint32_t IDAT = 0x49444154;  // Image Data
    constexpr uint32_t IEND = 0x49454E44;  // Image End

    // Filter types
    constexpr Byte FILTER_NONE    = 0;
    constexpr Byte FILTER_SUB     = 1;
    constexpr Byte FILTER_UP      = 2;
    constexpr Byte FILTER_AVERAGE = 3;
    constexpr Byte FILTER_PAETH   = 4;
}

/**
 * PNG image header info
 */
struct PngHeader {
    uint32_t width = 0;
    uint32_t height = 0;
    uint8_t bit_depth = 0;
    uint8_t color_type = 0;
    uint8_t compression = 0;
    uint8_t filter = 0;
    uint8_t interlace = 0;

    // Derived values
    [[nodiscard]] uint8_t channels() const {
        switch (color_type) {
            case 0: return 1;  // Grayscale
            case 2: return 3;  // RGB
            case 3: return 1;  // Indexed
            case 4: return 2;  // Grayscale + Alpha
            case 6: return 4;  // RGBA
            default: return 1;
        }
    }

    [[nodiscard]] size_t bytes_per_pixel() const {
        return (channels() * bit_depth + 7) / 8;
    }

    [[nodiscard]] size_t scanline_bytes() const {
        return (width * channels() * bit_depth + 7) / 8;
    }
};

/**
 * PNG chunk
 */
struct PngChunk {
    uint32_t type = 0;
    std::vector<Byte> data;
};

/**
 * PNG Optimizer
 */
class PngOptimizer {
public:
    /**
     * Check if data is a PNG file
     */
    [[nodiscard]] static bool is_png(ByteSpan data) {
        if (data.size() < 8) return false;
        return std::memcmp(data.data(), png::SIGNATURE.data(), 8) == 0;
    }

    /**
     * Optimize PNG by recompressing with optimal filters
     * @param input Original PNG data
     * @return Optimized PNG (or copy if optimization fails)
     */
    [[nodiscard]] std::vector<Byte> encode(ByteSpan input) const {
        if (!is_png(input) || input.size() < 57) {  // Minimum valid PNG size
            return {input.begin(), input.end()};
        }

        // Parse PNG
        PngHeader header;
        std::vector<PngChunk> chunks;
        std::vector<Byte> idat_compressed;

        if (!parse_png(input, header, chunks, idat_compressed)) {
            return {input.begin(), input.end()};
        }

        // Skip interlaced images (more complex)
        if (header.interlace != 0) {
            return {input.begin(), input.end()};
        }

        // Decompress IDAT
        Inflater inflater;
        auto raw_data = inflater.inflate_zlib(idat_compressed);
        if (raw_data.empty()) {
            return {input.begin(), input.end()};
        }

        // Verify size
        size_t expected_size = header.height * (1 + header.scanline_bytes());
        if (raw_data.size() != expected_size) {
            return {input.begin(), input.end()};
        }

        // Remove existing filters and get raw pixels
        auto pixels = unfilter(raw_data, header);
        if (pixels.empty()) {
            return {input.begin(), input.end()};
        }

        // Apply optimal filters
        auto filtered = apply_optimal_filters(pixels, header);

        // Recompress with deflate
        Deflater deflater;
        auto new_idat = deflater.deflate_zlib(filtered);

        // If no improvement, return original
        if (new_idat.size() >= idat_compressed.size()) {
            return {input.begin(), input.end()};
        }

        // Rebuild PNG with new IDAT
        return rebuild_png(header, chunks, new_idat);
    }

    /**
     * Decode (passthrough - PNG is already decoded format)
     */
    [[nodiscard]] std::vector<Byte> decode(ByteSpan input) const {
        return {input.begin(), input.end()};
    }

private:
    [[nodiscard]] bool parse_png(ByteSpan data, PngHeader& header,
                                  std::vector<PngChunk>& chunks,
                                  std::vector<Byte>& idat_data) const {
        size_t pos = 8;  // Skip signature

        while (pos + 12 <= data.size()) {
            // Read chunk length and type
            uint32_t length = read_be32(data.data() + pos);
            uint32_t type = read_be32(data.data() + pos + 4);
            pos += 8;

            if (pos + length + 4 > data.size()) {
                return false;  // Invalid chunk
            }

            if (type == png::IHDR) {
                if (length < 13) return false;
                header.width = read_be32(data.data() + pos);
                header.height = read_be32(data.data() + pos + 4);
                header.bit_depth = data[pos + 8];
                header.color_type = data[pos + 9];
                header.compression = data[pos + 10];
                header.filter = data[pos + 11];
                header.interlace = data[pos + 12];
            } else if (type == png::IDAT) {
                // Concatenate all IDAT chunks
                idat_data.insert(idat_data.end(),
                                data.data() + pos,
                                data.data() + pos + length);
            } else if (type == png::IEND) {
                break;
            } else {
                // Store other chunks
                PngChunk chunk;
                chunk.type = type;
                chunk.data.assign(data.data() + pos, data.data() + pos + length);
                chunks.push_back(std::move(chunk));
            }

            pos += length + 4;  // Skip data and CRC
        }

        return header.width > 0 && header.height > 0 && !idat_data.empty();
    }

    [[nodiscard]] std::vector<Byte> unfilter(const std::vector<Byte>& filtered,
                                              const PngHeader& header) const {
        size_t bpp = header.bytes_per_pixel();
        size_t scanline = header.scanline_bytes();
        std::vector<Byte> raw(header.height * scanline);

        for (uint32_t y = 0; y < header.height; ++y) {
            size_t src_offset = y * (scanline + 1);
            size_t dst_offset = y * scanline;
            Byte filter = filtered[src_offset];

            for (size_t x = 0; x < scanline; ++x) {
                Byte cur = filtered[src_offset + 1 + x];
                Byte a = (x >= bpp) ? raw[dst_offset + x - bpp] : 0;
                Byte b = (y > 0) ? raw[dst_offset - scanline + x] : 0;
                Byte c = (x >= bpp && y > 0) ? raw[dst_offset - scanline + x - bpp] : 0;

                Byte result = 0;
                switch (filter) {
                    case png::FILTER_NONE:
                        result = cur;
                        break;
                    case png::FILTER_SUB:
                        result = cur + a;
                        break;
                    case png::FILTER_UP:
                        result = cur + b;
                        break;
                    case png::FILTER_AVERAGE:
                        result = cur + static_cast<Byte>((static_cast<int>(a) + b) / 2);
                        break;
                    case png::FILTER_PAETH:
                        result = cur + paeth_predictor(a, b, c);
                        break;
                    default:
                        return {};  // Unknown filter
                }
                raw[dst_offset + x] = result;
            }
        }

        return raw;
    }

    [[nodiscard]] std::vector<Byte> apply_optimal_filters(
        const std::vector<Byte>& pixels,
        const PngHeader& header
    ) const {
        size_t bpp = header.bytes_per_pixel();
        size_t scanline = header.scanline_bytes();
        std::vector<Byte> result(header.height * (scanline + 1));

        for (uint32_t y = 0; y < header.height; ++y) {
            size_t src_offset = y * scanline;
            size_t dst_offset = y * (scanline + 1);

            // Try all filters and pick the one with minimum sum of absolute values
            Byte best_filter = 0;
            int64_t best_sum = INT64_MAX;

            for (Byte filter = 0; filter <= 4; ++filter) {
                int64_t sum = 0;

                for (size_t x = 0; x < scanline; ++x) {
                    Byte raw = pixels[src_offset + x];
                    Byte a = (x >= bpp) ? pixels[src_offset + x - bpp] : 0;
                    Byte b = (y > 0) ? pixels[src_offset - scanline + x] : 0;
                    Byte c = (x >= bpp && y > 0) ? pixels[src_offset - scanline + x - bpp] : 0;

                    int filtered = 0;
                    switch (filter) {
                        case png::FILTER_NONE:
                            filtered = raw;
                            break;
                        case png::FILTER_SUB:
                            filtered = raw - a;
                            break;
                        case png::FILTER_UP:
                            filtered = raw - b;
                            break;
                        case png::FILTER_AVERAGE:
                            filtered = raw - (static_cast<int>(a) + b) / 2;
                            break;
                        case png::FILTER_PAETH:
                            filtered = raw - paeth_predictor(a, b, c);
                            break;
                    }

                    // Sum of absolute differences (lower is better for compression)
                    sum += std::abs(static_cast<int8_t>(filtered));
                }

                if (sum < best_sum) {
                    best_sum = sum;
                    best_filter = filter;
                }
            }

            // Apply best filter
            result[dst_offset] = best_filter;
            for (size_t x = 0; x < scanline; ++x) {
                Byte raw = pixels[src_offset + x];
                Byte a = (x >= bpp) ? pixels[src_offset + x - bpp] : 0;
                Byte b = (y > 0) ? pixels[src_offset - scanline + x] : 0;
                Byte c = (x >= bpp && y > 0) ? pixels[src_offset - scanline + x - bpp] : 0;

                Byte filtered = 0;
                switch (best_filter) {
                    case png::FILTER_NONE:
                        filtered = raw;
                        break;
                    case png::FILTER_SUB:
                        filtered = raw - a;
                        break;
                    case png::FILTER_UP:
                        filtered = raw - b;
                        break;
                    case png::FILTER_AVERAGE:
                        filtered = raw - static_cast<Byte>((static_cast<int>(a) + b) / 2);
                        break;
                    case png::FILTER_PAETH:
                        filtered = raw - paeth_predictor(a, b, c);
                        break;
                }
                result[dst_offset + 1 + x] = filtered;
            }
        }

        return result;
    }

    [[nodiscard]] std::vector<Byte> rebuild_png(
        const PngHeader& header,
        const std::vector<PngChunk>& chunks,
        const std::vector<Byte>& new_idat
    ) const {
        std::vector<Byte> result;
        result.reserve(new_idat.size() + 1024);

        // Signature
        result.insert(result.end(), png::SIGNATURE.begin(), png::SIGNATURE.end());

        // IHDR
        write_chunk(result, png::IHDR, make_ihdr(header));

        // Other chunks (PLTE, etc.) before IDAT
        for (const auto& chunk : chunks) {
            if (is_before_idat(chunk.type)) {
                write_chunk(result, chunk.type, chunk.data);
            }
        }

        // IDAT (single chunk)
        write_chunk(result, png::IDAT, new_idat);

        // Other chunks after IDAT
        for (const auto& chunk : chunks) {
            if (!is_before_idat(chunk.type)) {
                write_chunk(result, chunk.type, chunk.data);
            }
        }

        // IEND
        write_chunk(result, png::IEND, {});

        return result;
    }

    [[nodiscard]] static Byte paeth_predictor(Byte a, Byte b, Byte c) {
        int p = static_cast<int>(a) + b - c;
        int pa = std::abs(p - a);
        int pb = std::abs(p - b);
        int pc = std::abs(p - c);

        if (pa <= pb && pa <= pc) return a;
        if (pb <= pc) return b;
        return c;
    }

    [[nodiscard]] static uint32_t read_be32(const Byte* p) {
        return (static_cast<uint32_t>(p[0]) << 24) |
               (static_cast<uint32_t>(p[1]) << 16) |
               (static_cast<uint32_t>(p[2]) << 8) |
               static_cast<uint32_t>(p[3]);
    }

    static void write_be32(std::vector<Byte>& out, uint32_t value) {
        out.push_back(static_cast<Byte>((value >> 24) & 0xFF));
        out.push_back(static_cast<Byte>((value >> 16) & 0xFF));
        out.push_back(static_cast<Byte>((value >> 8) & 0xFF));
        out.push_back(static_cast<Byte>(value & 0xFF));
    }

    [[nodiscard]] static std::vector<Byte> make_ihdr(const PngHeader& h) {
        std::vector<Byte> data(13);
        data[0] = static_cast<Byte>((h.width >> 24) & 0xFF);
        data[1] = static_cast<Byte>((h.width >> 16) & 0xFF);
        data[2] = static_cast<Byte>((h.width >> 8) & 0xFF);
        data[3] = static_cast<Byte>(h.width & 0xFF);
        data[4] = static_cast<Byte>((h.height >> 24) & 0xFF);
        data[5] = static_cast<Byte>((h.height >> 16) & 0xFF);
        data[6] = static_cast<Byte>((h.height >> 8) & 0xFF);
        data[7] = static_cast<Byte>(h.height & 0xFF);
        data[8] = h.bit_depth;
        data[9] = h.color_type;
        data[10] = h.compression;
        data[11] = h.filter;
        data[12] = h.interlace;
        return data;
    }

    static void write_chunk(std::vector<Byte>& out, uint32_t type, const std::vector<Byte>& data) {
        write_be32(out, static_cast<uint32_t>(data.size()));
        write_be32(out, type);
        out.insert(out.end(), data.begin(), data.end());

        // Calculate CRC (type + data)
        uint32_t crc = crc32_png(type, data);
        write_be32(out, crc);
    }

    [[nodiscard]] static bool is_before_idat(uint32_t type) {
        // PLTE, cHRM, gAMA, iCCP, sBIT, sRGB, bKGD, hIST, tRNS, pHYs, sPLT
        return type == png::PLTE ||
               (type >> 24) == 'c' ||  // cHRM
               (type >> 24) == 'g' ||  // gAMA
               (type >> 24) == 'i' ||  // iCCP
               (type >> 24) == 's' ||  // sBIT, sRGB, sPLT
               (type >> 24) == 'b' ||  // bKGD
               (type >> 24) == 'h' ||  // hIST
               (type >> 24) == 't' ||  // tRNS
               (type >> 24) == 'p';    // pHYs
    }

    [[nodiscard]] static uint32_t crc32_png(uint32_t type, const std::vector<Byte>& data) {
        // CRC32 with PNG polynomial
        static const auto crc_table = make_crc_table();

        uint32_t crc = 0xFFFFFFFF;

        // Include type bytes
        crc = crc_table[(crc ^ ((type >> 24) & 0xFF)) & 0xFF] ^ (crc >> 8);
        crc = crc_table[(crc ^ ((type >> 16) & 0xFF)) & 0xFF] ^ (crc >> 8);
        crc = crc_table[(crc ^ ((type >> 8) & 0xFF)) & 0xFF] ^ (crc >> 8);
        crc = crc_table[(crc ^ (type & 0xFF)) & 0xFF] ^ (crc >> 8);

        // Include data bytes
        for (Byte b : data) {
            crc = crc_table[(crc ^ b) & 0xFF] ^ (crc >> 8);
        }

        return crc ^ 0xFFFFFFFF;
    }

    [[nodiscard]] static std::array<uint32_t, 256> make_crc_table() {
        std::array<uint32_t, 256> table{};
        for (uint32_t n = 0; n < 256; ++n) {
            uint32_t c = n;
            for (int k = 0; k < 8; ++k) {
                if (c & 1) {
                    c = 0xEDB88320 ^ (c >> 1);
                } else {
                    c >>= 1;
                }
            }
            table[n] = c;
        }
        return table;
    }
};

} // namespace compressum::transform
