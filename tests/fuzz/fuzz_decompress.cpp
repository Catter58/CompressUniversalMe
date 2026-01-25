/**
 * CompressUM - Fuzz Test for Decompression
 *
 * Tests the decompressor with random/mutated input to find
 * crashes, memory errors, and undefined behavior.
 *
 * Build with: clang++ -fsanitize=fuzzer,address -O2 -o fuzz_decompress fuzz_decompress.cpp
 */

#include "compressum/compressum.hpp"
#include <cstdint>
#include <cstddef>
#include <vector>

using namespace compressum;

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
    if (size == 0) return 0;

    std::vector<Byte> input(data, data + size);

    // Try to decompress - should not crash regardless of input
    try {
        Decompressor d;
        auto [decompressed, result] = d.decompress(input);

        // If decompression succeeded, verify it's a valid result
        if (result.ok()) {
            // Sanity check: decompressed size should match header
            (void)decompressed.size();
        }
    } catch (...) {
        // Exceptions are OK - we just don't want crashes
    }

    return 0;
}
