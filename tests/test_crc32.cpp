/**
 * CompressUM - CRC32 Tests
 */

#include "compressum/core/crc32.hpp"
#include <iostream>
#include <cassert>
#include <cstring>
#include <vector>

using namespace compressum;
using namespace compressum::core;

#define TEST(name) void test_##name()
#define RUN_TEST(name) do { \
    std::cout << "Running " #name "... "; \
    test_##name(); \
    std::cout << "OK\n"; \
} while(0)

TEST(empty_data) {
    std::vector<Byte> data;
    uint32_t crc = crc32c(data);
    assert(crc == 0);  // CRC of empty data is 0
}

TEST(known_values) {
    // Test vectors for CRC32-C (Castagnoli)
    // These are well-known test values

    // "123456789" -> 0xE3069283
    const char* test1 = "123456789";
    auto crc1 = crc32c(ByteSpan(reinterpret_cast<const Byte*>(test1), 9));
    assert(crc1 == 0xE3069283);

    // Single zero byte
    std::vector<Byte> single_zero = {0x00};
    auto crc2 = crc32c(single_zero);
    // CRC32-C of 0x00 is 0x527D5351
    assert(crc2 == 0x527D5351);
}

TEST(incremental) {
    const char* data = "Hello, World!";
    size_t len = std::strlen(data);

    // Compute in one shot
    uint32_t crc_full = crc32c(ByteSpan(reinterpret_cast<const Byte*>(data), len));

    // Compute incrementally
    CRC32 crc_inc;
    for (size_t i = 0; i < len; ++i) {
        crc_inc.update(static_cast<Byte>(data[i]));
    }

    assert(crc_inc.value() == crc_full);
}

TEST(chunk_incremental) {
    std::vector<Byte> data(1000);
    for (size_t i = 0; i < data.size(); ++i) {
        data[i] = static_cast<Byte>(i & 0xFF);
    }

    // Full computation
    uint32_t crc_full = crc32c(data);

    // Chunked computation
    CRC32 crc_chunk;
    crc_chunk.update(ByteSpan(data.data(), 300));
    crc_chunk.update(ByteSpan(data.data() + 300, 400));
    crc_chunk.update(ByteSpan(data.data() + 700, 300));

    assert(crc_chunk.value() == crc_full);
}

TEST(reset) {
    CRC32 crc;

    std::vector<Byte> data1 = {0x01, 0x02, 0x03};
    std::vector<Byte> data2 = {0x04, 0x05, 0x06};

    crc.update(data1);
    uint32_t crc1 = crc.value();

    crc.reset();
    crc.update(data2);
    uint32_t crc2 = crc.value();

    // Should be different
    assert(crc1 != crc2);

    // Reset and recompute first
    crc.reset();
    crc.update(data1);
    assert(crc.value() == crc1);
}

TEST(software_vs_table) {
    // Test that software implementation produces correct results
    std::vector<Byte> data(256);
    for (size_t i = 0; i < data.size(); ++i) {
        data[i] = static_cast<Byte>(i);
    }

    uint32_t crc = crc32c_software(0, data);

    // Verify with incremental
    uint32_t crc_inc = 0;
    for (size_t i = 0; i < data.size(); ++i) {
        crc_inc = crc32c_software(crc_inc, ByteSpan(data.data() + i, 1));
    }

    assert(crc == crc_inc);
}

TEST(combine) {
    std::vector<Byte> data1 = {0x01, 0x02, 0x03, 0x04};
    std::vector<Byte> data2 = {0x05, 0x06, 0x07, 0x08};

    // Concatenated CRC
    std::vector<Byte> combined;
    combined.insert(combined.end(), data1.begin(), data1.end());
    combined.insert(combined.end(), data2.begin(), data2.end());
    uint32_t crc_combined = crc32c(combined);

    // Compute separately and combine
    uint32_t crc1 = crc32c(data1);
    uint32_t crc2 = crc32c(data2);
    uint32_t crc_from_combine = CRC32::combine(crc1, crc2, data2.size());

    assert(crc_from_combine == crc_combined);
}

TEST(hardware_check) {
    std::cout << "(Hardware CRC: " << (has_hardware_crc32() ? "YES" : "NO") << ") ";

    // Test should produce same results regardless of implementation
    std::vector<Byte> data(1024);
    for (size_t i = 0; i < data.size(); ++i) {
        data[i] = static_cast<Byte>((i * 17) & 0xFF);
    }

    uint32_t crc_sw = crc32c_software(0, data);
    uint32_t crc_auto = crc32c(data);

    #if COMPRESSUM_USE_SSE42
        uint32_t crc_hw = crc32c_hardware(0, data);
        assert(crc_sw == crc_hw);
    #endif

    assert(crc_sw == crc_auto);
}

int main() {
    std::cout << "CRC32 Tests\n";
    std::cout << "===========\n";

    RUN_TEST(empty_data);
    RUN_TEST(known_values);
    RUN_TEST(incremental);
    RUN_TEST(chunk_incremental);
    RUN_TEST(reset);
    RUN_TEST(software_vs_table);
    RUN_TEST(combine);
    RUN_TEST(hardware_check);

    std::cout << "\nAll tests passed!\n";
    return 0;
}
