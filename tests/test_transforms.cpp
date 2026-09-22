/**
 * CompressUM - Transform Tests (Delta, BCJ, legacy BWT decoders)
 */

#include "compressum/transform/delta.hpp"
#include "compressum/transform/bwt.hpp"
#include "compressum/transform/bcj.hpp"
#include <iostream>
#include <cassert>
#include <cstring>
#include <random>

using namespace compressum;
using namespace compressum::transform;

#define TEST(name) void test_##name()
#define RUN_TEST(name) do { \
    std::cout << "Running " #name "... " << std::flush; \
    test_##name(); \
    std::cout << "OK\n" << std::flush; \
} while(0)

// ============================================================================
// Delta Tests
// ============================================================================

TEST(delta_subtract_roundtrip) {
    std::vector<Byte> original = {10, 20, 25, 30, 40, 45, 50};

    DeltaConfig config;
    config.type = DeltaType::Subtract;
    config.distance = 1;

    DeltaEncoder encoder(config);
    DeltaDecoder decoder(config);

    auto encoded = encoder.encode(original);
    auto decoded = decoder.decode(encoded);

    assert(decoded == original);
}

TEST(delta_xor_roundtrip) {
    std::vector<Byte> original = {0xAA, 0xAB, 0xAC, 0xAD, 0xAE};

    DeltaConfig config;
    config.type = DeltaType::XOR;

    DeltaEncoder encoder(config);
    DeltaDecoder decoder(config);

    auto encoded = encoder.encode(original);
    auto decoded = decoder.decode(encoded);

    assert(decoded == original);
}

TEST(delta_second_order_roundtrip) {
    // Linear data: second-order delta should give zeros
    std::vector<Byte> original = {0, 10, 20, 30, 40, 50, 60, 70, 80, 90};

    DeltaConfig config;
    config.type = DeltaType::Second;

    DeltaEncoder encoder(config);
    DeltaDecoder decoder(config);

    auto encoded = encoder.encode(original);
    auto decoded = decoder.decode(encoded);

    assert(decoded == original);
}

TEST(delta_multichannel) {
    // Simulated RGB data
    std::vector<Byte> original = {
        100, 50, 25,   // Pixel 1
        102, 51, 26,   // Pixel 2 (similar to pixel 1)
        105, 53, 28,   // Pixel 3
        108, 55, 30    // Pixel 4
    };

    MultichannelDeltaEncoder encoder(3);

    auto encoded = encoder.encode(original);
    auto decoded = encoder.decode(encoded);

    assert(decoded == original);
}

TEST(delta_random_data) {
    std::mt19937 rng(42);
    std::vector<Byte> original(1000);
    for (auto& b : original) {
        b = static_cast<Byte>(rng() % 256);
    }

    DeltaConfig config;
    DeltaEncoder encoder(config);
    DeltaDecoder decoder(config);

    auto encoded = encoder.encode(original);
    auto decoded = decoder.decode(encoded);

    assert(decoded == original);
}

// ============================================================================
// Legacy BWT / MTF / ZLE decoder tests (known vectors)
// ============================================================================

TEST(bwt_inverse_banana) {
    // Sorted rotations of "banana": abanan anaban ananab banana nabana nanaba
    const char* last_column = "nnbaaa";
    std::vector<Byte> bwt_data(last_column, last_column + 6);

    BWT bwt;
    auto decoded = bwt.inverse(bwt_data, 3);

    const char* text = "banana";
    assert(decoded == std::vector<Byte>(text, text + 6));
}

TEST(mtf_inverse_banana) {
    // MTF of "banana" starting from identity list
    std::vector<Byte> encoded = {98, 98, 110, 1, 1, 1};

    MTF mtf;
    auto decoded = mtf.inverse(encoded);

    const char* text = "banana";
    assert(decoded == std::vector<Byte>(text, text + 6));
}

// ============================================================================
// BCJ Tests
// ============================================================================

TEST(bcj_x86_roundtrip) {
    // Create fake x86 code with CALL instruction
    std::vector<Byte> original = {
        0x90,                   // NOP
        0xE8, 0x10, 0x00, 0x00, 0x00,  // CALL +16
        0x90,                   // NOP
        0xE9, 0x05, 0x00, 0x00, 0x00,  // JMP +5
        0x90, 0x90, 0x90, 0x90, 0x90   // NOPs
    };

    BCJx86 bcj;
    auto encoded = bcj.encode(original);
    auto decoded = bcj.decode(encoded);

    assert(decoded == original);
}

TEST(bcj_x86_preserves_non_calls) {
    // Data without CALL/JMP instructions should be unchanged
    std::vector<Byte> original = {0x90, 0x90, 0x48, 0x89, 0xC3, 0xC3};

    BCJx86 bcj;
    auto encoded = bcj.encode(original);

    assert(encoded == original);
}

TEST(bcj_arm64_roundtrip) {
    // Create fake ARM64 code with BL instruction
    // BL opcode: 100101 | imm26
    std::vector<Byte> original = {
        0x00, 0x00, 0x00, 0x94,  // BL #0
        0x04, 0x00, 0x00, 0x94,  // BL #4
        0x00, 0x00, 0x00, 0xD6,  // Not a branch
    };

    BCJArm64 bcj;
    auto encoded = bcj.encode(original);
    auto decoded = bcj.decode(encoded);

    assert(decoded == original);
}

TEST(bcj_auto_detect) {
    // Simple test - just verify no crash
    std::vector<Byte> data = {0x90, 0xE8, 0x00, 0x00, 0x00, 0x00};

    BCJConfig config;
    config.arch = BCJArch::Auto;
    BCJFilter filter(config);

    auto encoded = filter.encode(data);
    assert(!encoded.empty());
}

// ============================================================================
// ZLE decoder test
// ============================================================================

TEST(zle_decode_runs) {
    // RUNA(0) at weight 1 + RUNB(1) at weight 2 = 1 + 4 = 5 zeros; 3 -> byte 2
    std::vector<Byte> encoded = {0, 1, 3};

    ZLE zle;
    auto decoded = zle.decode(encoded);

    assert((decoded == std::vector<Byte>{0, 0, 0, 0, 0, 2}));
}

int main() {
    std::cout << "Transform Tests (Delta, BWT, BCJ)\n";
    std::cout << "=================================\n";

    // Delta tests
    RUN_TEST(delta_subtract_roundtrip);
    RUN_TEST(delta_xor_roundtrip);
    RUN_TEST(delta_second_order_roundtrip);
    RUN_TEST(delta_multichannel);
    RUN_TEST(delta_random_data);

    // Legacy decoder tests
    RUN_TEST(bwt_inverse_banana);
    RUN_TEST(mtf_inverse_banana);

    // BCJ tests
    RUN_TEST(bcj_x86_roundtrip);
    RUN_TEST(bcj_x86_preserves_non_calls);
    RUN_TEST(bcj_arm64_roundtrip);
    RUN_TEST(bcj_auto_detect);

    RUN_TEST(zle_decode_runs);

    std::cout << "\nAll tests passed!\n";
    return 0;
}
