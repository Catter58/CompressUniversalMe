/**
 * CompressUM - Transform Tests (Delta, BWT, BCJ)
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
// BWT Tests
// ============================================================================

TEST(bwt_simple) {
    const char* text = "banana";
    std::vector<Byte> original(text, text + strlen(text));

    BWT bwt;
    auto result = bwt.transform(original);
    auto decoded = bwt.inverse(result.data, result.primary_index);

    assert(decoded == original);
}

TEST(bwt_repeated) {
    std::vector<Byte> original(100, 'A');

    BWT bwt;
    auto result = bwt.transform(original);
    auto decoded = bwt.inverse(result.data, result.primary_index);

    assert(decoded == original);
}

TEST(bwt_longer_text) {
    const char* text = "the quick brown fox jumps over the lazy dog";
    std::vector<Byte> original(text, text + strlen(text));

    BWT bwt;
    auto result = bwt.transform(original);
    auto decoded = bwt.inverse(result.data, result.primary_index);

    assert(decoded == original);
}

TEST(mtf_simple) {
    std::vector<Byte> original = {'a', 'b', 'a', 'b', 'a', 'c'};

    MTF mtf;
    auto encoded = mtf.transform(original);
    auto decoded = mtf.inverse(encoded);

    assert(decoded == original);
}

TEST(mtf_repeated) {
    // Repeated characters should produce runs of zeros
    std::vector<Byte> original = {'a', 'a', 'a', 'a', 'a'};

    MTF mtf;
    auto encoded = mtf.transform(original);

    // First is position of 'a', rest should be 0
    assert(encoded[0] == 'a');  // Position of 'a' in initial list
    for (size_t i = 1; i < encoded.size(); ++i) {
        assert(encoded[i] == 0);
    }

    auto decoded = mtf.inverse(encoded);
    assert(decoded == original);
}

TEST(bwt_mtf_combined) {
    const char* text = "mississippi";
    std::vector<Byte> original(text, text + strlen(text));

    BWT bwt;
    MTF mtf;

    // Forward: BWT then MTF
    auto bwt_result = bwt.transform(original);
    auto mtf_encoded = mtf.transform(bwt_result.data);

    // Inverse: MTF then BWT
    auto mtf_decoded = mtf.inverse(mtf_encoded);
    auto decoded = bwt.inverse(mtf_decoded, bwt_result.primary_index);

    assert(decoded == original);
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
// ZLE Tests
// ============================================================================

TEST(zle_roundtrip) {
    // Data with runs of zeros (typical MTF output)
    std::vector<Byte> original = {5, 0, 0, 0, 3, 0, 0, 0, 0, 0, 2};

    ZLE zle;
    auto encoded = zle.encode(original);
    auto decoded = zle.decode(encoded);

    assert(decoded == original);
}

TEST(zle_all_zeros) {
    std::vector<Byte> original(100, 0);

    ZLE zle;
    auto encoded = zle.encode(original);

    // Should compress significantly
    assert(encoded.size() < original.size() / 2);

    auto decoded = zle.decode(encoded);
    assert(decoded == original);
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

    // BWT tests
    RUN_TEST(bwt_simple);
    RUN_TEST(bwt_repeated);
    RUN_TEST(bwt_longer_text);
    RUN_TEST(mtf_simple);
    RUN_TEST(mtf_repeated);
    RUN_TEST(bwt_mtf_combined);

    // BCJ tests
    RUN_TEST(bcj_x86_roundtrip);
    RUN_TEST(bcj_x86_preserves_non_calls);
    RUN_TEST(bcj_arm64_roundtrip);
    RUN_TEST(bcj_auto_detect);

    // ZLE tests
    RUN_TEST(zle_roundtrip);
    RUN_TEST(zle_all_zeros);

    std::cout << "\nAll tests passed!\n";
    return 0;
}
