#pragma once

/**
 * CompressUM - Original Compression Algorithm
 * BCJ (Branch/Call/Jump) Filter
 *
 * Converts relative addresses in executable code to absolute addresses.
 * This improves compression because similar instructions at different
 * positions will have the same absolute target address.
 *
 * Supported architectures:
 * - x86/x86-64: CALL (E8), JMP (E9), Jcc (0F 8x)
 * - ARM: B, BL instructions
 * - ARM64: B, BL instructions
 *
 * Used in: 7-Zip, XZ
 */

#include "../config.hpp"
#include "../types.hpp"
#include <vector>
#include <cstring>

namespace compressum::transform {

/**
 * Architecture for BCJ filter
 */
enum class BCJArch {
    X86,
    X86_64,
    ARM,
    ARM64,
    Auto  // Auto-detect
};

/**
 * BCJ Configuration
 */
struct BCJConfig {
    BCJArch arch = BCJArch::Auto;
    uint32_t start_address = 0;  // Virtual start address
};

/**
 * BCJ Filter for x86/x86-64
 *
 * Converts relative CALL/JMP addresses to absolute:
 * - E8 xx xx xx xx (CALL rel32)
 * - E9 xx xx xx xx (JMP rel32)
 */
class BCJx86 {
public:
    explicit BCJx86(uint32_t start_address = 0)
        : pos_(start_address)
    {}

    /**
     * Forward transform (relative -> absolute)
     */
    [[nodiscard]] std::vector<Byte> encode(ByteSpan input) const {
        std::vector<Byte> output(input.begin(), input.end());
        encode_inplace(output.data(), output.size(), pos_);
        return output;
    }

    /**
     * Inverse transform (absolute -> relative)
     */
    [[nodiscard]] std::vector<Byte> decode(ByteSpan input) const {
        std::vector<Byte> output(input.begin(), input.end());
        decode_inplace(output.data(), output.size(), pos_);
        return output;
    }

    /**
     * Encode in-place
     */
    static void encode_inplace(Byte* data, size_t size, uint32_t start_pos = 0) {
        if (size < 5) return;

        for (size_t i = 0; i < size - 4; ++i) {
            // Check for CALL (E8) or JMP (E9)
            if (data[i] == 0xE8 || data[i] == 0xE9) {
                // Read relative address (little-endian)
                uint32_t rel = static_cast<uint32_t>(data[i + 1]) |
                              (static_cast<uint32_t>(data[i + 2]) << 8) |
                              (static_cast<uint32_t>(data[i + 3]) << 16) |
                              (static_cast<uint32_t>(data[i + 4]) << 24);

                // Convert to absolute: abs = rel + current_pos + 5
                uint32_t pos = static_cast<uint32_t>(i) + start_pos;
                uint32_t abs = rel + pos + 5;

                // Write absolute address
                data[i + 1] = static_cast<Byte>(abs & 0xFF);
                data[i + 2] = static_cast<Byte>((abs >> 8) & 0xFF);
                data[i + 3] = static_cast<Byte>((abs >> 16) & 0xFF);
                data[i + 4] = static_cast<Byte>((abs >> 24) & 0xFF);

                i += 4;  // Skip the address bytes
            }
        }
    }

    /**
     * Decode in-place
     */
    static void decode_inplace(Byte* data, size_t size, uint32_t start_pos = 0) {
        if (size < 5) return;

        for (size_t i = 0; i < size - 4; ++i) {
            if (data[i] == 0xE8 || data[i] == 0xE9) {
                // Read absolute address
                uint32_t abs = static_cast<uint32_t>(data[i + 1]) |
                              (static_cast<uint32_t>(data[i + 2]) << 8) |
                              (static_cast<uint32_t>(data[i + 3]) << 16) |
                              (static_cast<uint32_t>(data[i + 4]) << 24);

                // Convert to relative: rel = abs - current_pos - 5
                uint32_t pos = static_cast<uint32_t>(i) + start_pos;
                uint32_t rel = abs - pos - 5;

                // Write relative address
                data[i + 1] = static_cast<Byte>(rel & 0xFF);
                data[i + 2] = static_cast<Byte>((rel >> 8) & 0xFF);
                data[i + 3] = static_cast<Byte>((rel >> 16) & 0xFF);
                data[i + 4] = static_cast<Byte>((rel >> 24) & 0xFF);

                i += 4;
            }
        }
    }

private:
    uint32_t pos_;
};

/**
 * BCJ Filter for ARM (32-bit)
 *
 * Converts BL (Branch with Link) instructions:
 * - Opcode: 0xEB xxxxxx (24-bit signed offset)
 */
class BCJArm {
public:
    explicit BCJArm(uint32_t start_address = 0)
        : pos_(start_address)
    {}

    [[nodiscard]] std::vector<Byte> encode(ByteSpan input) const {
        std::vector<Byte> output(input.begin(), input.end());
        encode_inplace(output.data(), output.size(), pos_);
        return output;
    }

    [[nodiscard]] std::vector<Byte> decode(ByteSpan input) const {
        std::vector<Byte> output(input.begin(), input.end());
        decode_inplace(output.data(), output.size(), pos_);
        return output;
    }

    static void encode_inplace(Byte* data, size_t size, uint32_t start_pos = 0) {
        // Process 4-byte aligned instructions
        for (size_t i = 0; i + 3 < size; i += 4) {
            // Check for BL instruction (ARM mode)
            // Instruction format: cond (4 bits) | 1011 | imm24
            // Byte 3 has condition code and opcode bits
            if ((data[i + 3] & 0x0F) == 0x0B) {
                // Read 24-bit offset (stored in bytes 0-2)
                uint32_t offset = static_cast<uint32_t>(data[i]) |
                                 (static_cast<uint32_t>(data[i + 1]) << 8) |
                                 (static_cast<uint32_t>(data[i + 2]) << 16);

                // Sign extend if negative
                if (offset & 0x800000) {
                    offset |= 0xFF000000;
                }

                // Convert to absolute
                // ARM: target = PC + 8 + (offset << 2)
                uint32_t pos = static_cast<uint32_t>(i) + start_pos;
                uint32_t abs = ((offset << 2) + pos + 8) >> 2;

                // Write absolute address (24-bit)
                data[i] = static_cast<Byte>(abs & 0xFF);
                data[i + 1] = static_cast<Byte>((abs >> 8) & 0xFF);
                data[i + 2] = static_cast<Byte>((abs >> 16) & 0xFF);
            }
        }
    }

    static void decode_inplace(Byte* data, size_t size, uint32_t start_pos = 0) {
        for (size_t i = 0; i + 3 < size; i += 4) {
            if ((data[i + 3] & 0x0F) == 0x0B) {
                uint32_t abs = static_cast<uint32_t>(data[i]) |
                              (static_cast<uint32_t>(data[i + 1]) << 8) |
                              (static_cast<uint32_t>(data[i + 2]) << 16);

                // Convert to relative
                uint32_t pos = static_cast<uint32_t>(i) + start_pos;
                uint32_t offset = ((abs << 2) - pos - 8) >> 2;

                // Write relative offset (24-bit)
                data[i] = static_cast<Byte>(offset & 0xFF);
                data[i + 1] = static_cast<Byte>((offset >> 8) & 0xFF);
                data[i + 2] = static_cast<Byte>((offset >> 16) & 0xFF);
            }
        }
    }

private:
    uint32_t pos_;
};

/**
 * BCJ Filter for ARM64 (AArch64)
 *
 * Converts B and BL instructions:
 * - B:  000101 | imm26
 * - BL: 100101 | imm26
 */
class BCJArm64 {
public:
    explicit BCJArm64(uint32_t start_address = 0)
        : pos_(start_address)
    {}

    [[nodiscard]] std::vector<Byte> encode(ByteSpan input) const {
        std::vector<Byte> output(input.begin(), input.end());
        encode_inplace(output.data(), output.size(), pos_);
        return output;
    }

    [[nodiscard]] std::vector<Byte> decode(ByteSpan input) const {
        std::vector<Byte> output(input.begin(), input.end());
        decode_inplace(output.data(), output.size(), pos_);
        return output;
    }

    static void encode_inplace(Byte* data, size_t size, uint32_t start_pos = 0) {
        // Process 4-byte aligned instructions
        for (size_t i = 0; i + 3 < size; i += 4) {
            // Read instruction (little-endian)
            uint32_t instr = static_cast<uint32_t>(data[i]) |
                            (static_cast<uint32_t>(data[i + 1]) << 8) |
                            (static_cast<uint32_t>(data[i + 2]) << 16) |
                            (static_cast<uint32_t>(data[i + 3]) << 24);

            // Check for B (opcode 000101) or BL (opcode 100101)
            uint32_t opcode = (instr >> 26) & 0x3F;
            if (opcode == 0x05 || opcode == 0x25) {
                // Extract 26-bit offset
                int32_t offset = instr & 0x03FFFFFF;

                // Sign extend
                if (offset & 0x02000000) {
                    offset |= 0xFC000000;
                }

                // Convert to absolute
                uint32_t pos = static_cast<uint32_t>(i) + start_pos;
                int32_t abs = offset + (pos >> 2);

                // Replace offset while preserving opcode
                instr = (instr & 0xFC000000) | (abs & 0x03FFFFFF);

                // Write instruction
                data[i] = static_cast<Byte>(instr & 0xFF);
                data[i + 1] = static_cast<Byte>((instr >> 8) & 0xFF);
                data[i + 2] = static_cast<Byte>((instr >> 16) & 0xFF);
                data[i + 3] = static_cast<Byte>((instr >> 24) & 0xFF);
            }
        }
    }

    static void decode_inplace(Byte* data, size_t size, uint32_t start_pos = 0) {
        for (size_t i = 0; i + 3 < size; i += 4) {
            uint32_t instr = static_cast<uint32_t>(data[i]) |
                            (static_cast<uint32_t>(data[i + 1]) << 8) |
                            (static_cast<uint32_t>(data[i + 2]) << 16) |
                            (static_cast<uint32_t>(data[i + 3]) << 24);

            uint32_t opcode = (instr >> 26) & 0x3F;
            if (opcode == 0x05 || opcode == 0x25) {
                int32_t abs = instr & 0x03FFFFFF;
                if (abs & 0x02000000) {
                    abs |= 0xFC000000;
                }

                uint32_t pos = static_cast<uint32_t>(i) + start_pos;
                int32_t offset = abs - (pos >> 2);

                instr = (instr & 0xFC000000) | (offset & 0x03FFFFFF);

                data[i] = static_cast<Byte>(instr & 0xFF);
                data[i + 1] = static_cast<Byte>((instr >> 8) & 0xFF);
                data[i + 2] = static_cast<Byte>((instr >> 16) & 0xFF);
                data[i + 3] = static_cast<Byte>((instr >> 24) & 0xFF);
            }
        }
    }

private:
    uint32_t pos_;
};

/**
 * Auto-detecting BCJ filter
 */
class BCJFilter {
public:
    explicit BCJFilter(const BCJConfig& config = {})
        : config_(config)
    {}

    [[nodiscard]] std::vector<Byte> encode(ByteSpan input) const {
        BCJArch arch = config_.arch;

        if (arch == BCJArch::Auto) {
            arch = detect_architecture(input);
        }

        switch (arch) {
            case BCJArch::X86:
            case BCJArch::X86_64:
                return BCJx86(config_.start_address).encode(input);
            case BCJArch::ARM:
                return BCJArm(config_.start_address).encode(input);
            case BCJArch::ARM64:
                return BCJArm64(config_.start_address).encode(input);
            default:
                return {input.begin(), input.end()};
        }
    }

    [[nodiscard]] std::vector<Byte> decode(ByteSpan input, BCJArch arch) const {
        switch (arch) {
            case BCJArch::X86:
            case BCJArch::X86_64:
                return BCJx86(config_.start_address).decode(input);
            case BCJArch::ARM:
                return BCJArm(config_.start_address).decode(input);
            case BCJArch::ARM64:
                return BCJArm64(config_.start_address).decode(input);
            default:
                return {input.begin(), input.end()};
        }
    }

private:
    /**
     * Simple heuristic to detect executable architecture
     */
    [[nodiscard]] static BCJArch detect_architecture(ByteSpan data) {
        if (data.size() < 16) {
            return BCJArch::X86;  // Default
        }

        // Check for ELF magic
        if (data[0] == 0x7F && data[1] == 'E' && data[2] == 'L' && data[3] == 'F') {
            // ELF file - check machine type at offset 18
            if (data.size() >= 20) {
                uint16_t machine = data[18] | (data[19] << 8);
                switch (machine) {
                    case 3:   return BCJArch::X86;     // EM_386
                    case 40:  return BCJArch::ARM;     // EM_ARM
                    case 62:  return BCJArch::X86_64;  // EM_X86_64
                    case 183: return BCJArch::ARM64;   // EM_AARCH64
                }
            }
        }

        // Check for PE magic (Windows executables)
        if (data[0] == 'M' && data[1] == 'Z') {
            // PE file - would need to parse PE header for machine type
            return BCJArch::X86;  // Assume x86 for now
        }

        // Check for Mach-O magic (macOS executables)
        uint32_t magic = data[0] | (data[1] << 8) | (data[2] << 16) | (data[3] << 24);
        if (magic == 0xFEEDFACE || magic == 0xCEFAEDFE) {
            return BCJArch::X86;  // 32-bit Mach-O
        }
        if (magic == 0xFEEDFACF || magic == 0xCFFAEDFE) {
            // 64-bit Mach-O - check CPU type
            if (data.size() >= 8) {
                uint32_t cputype = data[4] | (data[5] << 8) | (data[6] << 16) | (data[7] << 24);
                if (cputype == 0x0100000C) return BCJArch::ARM64;
                if (cputype == 0x01000007) return BCJArch::X86_64;
            }
        }

        // Default to x86
        return BCJArch::X86;
    }

    BCJConfig config_;
};

} // namespace compressum::transform
