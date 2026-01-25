/**
 * CompressUM - BitStream Tests
 */

#include "compressum/core/bitstream.hpp"
#include <iostream>
#include <cassert>
#include <cstring>

using namespace compressum;
using namespace compressum::core;

#define TEST(name) void test_##name()
#define RUN_TEST(name) do { \
    std::cout << "Running " #name "... "; \
    test_##name(); \
    std::cout << "OK\n"; \
} while(0)

TEST(write_read_bits) {
    BitWriter writer;

    // Write various bit lengths
    writer.write_bits(0b101, 3);     // 3 bits
    writer.write_bits(0b1100, 4);    // 4 bits
    writer.write_bits(0b1, 1);       // 1 bit
    writer.write_bits(0xFF, 8);      // 8 bits
    writer.write_bits(0xABCD, 16);   // 16 bits
    writer.flush();

    auto data = writer.data();
    BitReader reader(data);

    assert(reader.read_bits(3) == 0b101);
    assert(reader.read_bits(4) == 0b1100);
    assert(reader.read_bits(1) == 0b1);
    assert(reader.read_bits(8) == 0xFF);
    assert(reader.read_bits(16) == 0xABCD);
}

TEST(write_read_bytes) {
    BitWriter writer;

    writer.write_byte(0x12);
    writer.write_byte(0x34);
    writer.write_byte(0x56);
    writer.write_byte(0x78);
    writer.flush();

    auto data = writer.data();
    assert(data.size() == 4);
    assert(data[0] == 0x12);
    assert(data[1] == 0x34);
    assert(data[2] == 0x56);
    assert(data[3] == 0x78);

    BitReader reader(data);
    assert(reader.read_byte() == 0x12);
    assert(reader.read_byte() == 0x34);
    assert(reader.read_byte() == 0x56);
    assert(reader.read_byte() == 0x78);
}

TEST(write_read_single_bits) {
    BitWriter writer;

    // Write pattern: 10110100
    writer.write_bit(true);
    writer.write_bit(false);
    writer.write_bit(true);
    writer.write_bit(true);
    writer.write_bit(false);
    writer.write_bit(true);
    writer.write_bit(false);
    writer.write_bit(false);
    writer.flush();

    auto data = writer.data();
    assert(data.size() == 1);
    assert(data[0] == 0b10110100);

    BitReader reader(data);
    assert(reader.read_bit() == true);
    assert(reader.read_bit() == false);
    assert(reader.read_bit() == true);
    assert(reader.read_bit() == true);
    assert(reader.read_bit() == false);
    assert(reader.read_bit() == true);
    assert(reader.read_bit() == false);
    assert(reader.read_bit() == false);
}

TEST(varint) {
    BitWriter writer;

    writer.write_varint(0);
    writer.write_varint(127);
    writer.write_varint(128);
    writer.write_varint(16383);
    writer.write_varint(16384);
    writer.write_varint(0xFFFFFFFF);
    writer.flush();

    auto data = writer.data();
    BitReader reader(data);

    assert(reader.read_varint() == 0);
    assert(reader.read_varint() == 127);
    assert(reader.read_varint() == 128);
    assert(reader.read_varint() == 16383);
    assert(reader.read_varint() == 16384);
    assert(reader.read_varint() == 0xFFFFFFFF);
}

TEST(peek_bits) {
    std::vector<Byte> data = {0xAB, 0xCD};
    BitReader reader(data);

    // Peek should not consume bits
    assert(reader.peek_bits(8) == 0xAB);
    assert(reader.peek_bits(8) == 0xAB);
    assert(reader.read_bits(8) == 0xAB);
    assert(reader.peek_bits(8) == 0xCD);
    assert(reader.read_bits(8) == 0xCD);
}

TEST(mixed_bits_and_bytes) {
    BitWriter writer;

    writer.write_bits(0b101, 3);
    writer.write_byte(0xAB);
    writer.write_bits(0b11, 2);
    writer.write_byte(0xCD);
    writer.flush();

    auto data = writer.data();
    BitReader reader(data);

    assert(reader.read_bits(3) == 0b101);
    assert(reader.read_byte() == 0xAB);
    assert(reader.read_bits(2) == 0b11);
    assert(reader.read_byte() == 0xCD);
}

TEST(large_values) {
    BitWriter writer;

    writer.write_bits(0x123456789ABCDEFull, 57);
    writer.flush();

    auto data = writer.data();
    BitReader reader(data);

    uint64_t value = reader.read_bits(57);
    assert(value == (0x123456789ABCDEFull & ((1ull << 57) - 1)));
}

TEST(bit_position) {
    BitWriter writer;

    assert(writer.bit_position() == 0);
    writer.write_bits(0, 5);
    assert(writer.bit_position() == 5);
    writer.write_bits(0, 8);
    assert(writer.bit_position() == 13);
    writer.flush();

    auto data = writer.data();
    BitReader reader(data);

    assert(reader.bit_position() == 0);
    reader.read_bits(5);
    assert(reader.bit_position() == 5);
    reader.read_bits(8);
    assert(reader.bit_position() == 13);
}

int main() {
    std::cout << "BitStream Tests\n";
    std::cout << "===============\n";

    RUN_TEST(write_read_bits);
    RUN_TEST(write_read_bytes);
    RUN_TEST(write_read_single_bits);
    RUN_TEST(varint);
    RUN_TEST(peek_bits);
    RUN_TEST(mixed_bits_and_bytes);
    RUN_TEST(large_values);
    RUN_TEST(bit_position);

    std::cout << "\nAll tests passed!\n";
    return 0;
}
