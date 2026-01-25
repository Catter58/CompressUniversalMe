/**
 * CompressUM - Fuzz Test for Deflate Inflation
 *
 * Tests the Deflate inflater with random input to find vulnerabilities
 * in zlib-compatible decompression.
 *
 * Build with: clang++ -fsanitize=fuzzer,address -O2 -o fuzz_deflate fuzz_deflate.cpp
 */

#include "compressum/transform/deflate.hpp"
#include <cstdint>
#include <cstddef>
#include <vector>

using namespace compressum;

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
    if (size == 0) return 0;
    if (size > 256 * 1024) return 0;  // Limit to 256KB

    std::vector<Byte> input(data, data + size);

    try {
        // Try raw deflate
        transform::Inflater inflater;
        auto result = inflater.inflate_raw(input);
        (void)result;
    } catch (...) {
        // Exceptions are OK
    }

    try {
        // Try zlib format
        transform::Inflater inflater;
        auto result = inflater.inflate_zlib(input);
        (void)result;
    } catch (...) {
        // Exceptions are OK
    }

    return 0;
}
