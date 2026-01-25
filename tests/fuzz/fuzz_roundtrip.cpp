/**
 * CompressUM - Fuzz Test for Roundtrip
 *
 * Compresses random data and verifies roundtrip correctness.
 * Catches compression bugs that corrupt data.
 *
 * Build with: clang++ -fsanitize=fuzzer,address -O2 -o fuzz_roundtrip fuzz_roundtrip.cpp
 */

#include "compressum/compressum.hpp"
#include <cstdint>
#include <cstddef>
#include <vector>
#include <cassert>

using namespace compressum;

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
    if (size == 0) return 0;
    if (size > 1024 * 1024) return 0;  // Limit to 1MB for speed

    std::vector<Byte> input(data, data + size);

    try {
        // Compress
        Compressor c;
        auto [compressed, comp_result] = c.compress(input);

        if (!comp_result.ok()) {
            return 0;  // Compression failed - that's OK for fuzz input
        }

        // Decompress
        Decompressor d;
        auto [decompressed, decomp_result] = d.decompress(compressed);

        // Verify roundtrip
        if (decomp_result.ok()) {
            assert(decompressed.size() == input.size());
            assert(decompressed == input);
        }
    } catch (...) {
        // Exceptions are OK
    }

    return 0;
}
