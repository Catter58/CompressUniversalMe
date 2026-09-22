/**
 * CompressUM - Context Mixing Coder Tests
 */

#include "compressum/entropy/cm.hpp"
#include <cassert>
#include <iostream>
#include <random>
#include <string>

using namespace compressum;

namespace {

void check_roundtrip(const char* name, const std::vector<Byte>& data) {
    auto encoded = entropy::cm::encode(data);
    auto decoded = entropy::cm::decode(encoded, data.size());
    assert(decoded == data);
    std::cout << "  " << name << ": " << data.size() << " -> " << encoded.size() << " OK\n";
}

} // namespace

int main() {
    std::cout << "CompressUM Context Mixing Tests\n";

    check_roundtrip("empty", {});
    check_roundtrip("single byte", {0x42});
    check_roundtrip("all zeros", std::vector<Byte>(100000, 0));
    check_roundtrip("all ones", std::vector<Byte>(100000, 0xFF));

    std::vector<Byte> all_bytes;
    for (int r = 0; r < 64; ++r)
        for (int b = 0; b < 256; ++b) all_bytes.push_back(static_cast<Byte>(b));
    check_roundtrip("all byte values", all_bytes);

    std::mt19937 rng(42);
    std::vector<Byte> random(200000);
    for (auto& b : random) b = static_cast<Byte>(rng());
    check_roundtrip("random", random);

    std::string text;
    const char* words[] = {"the ", "quick ", "brown ", "fox ", "jumps ", "over ", "lazy ", "dog.\n"};
    while (text.size() < 300000) text += words[rng() % 8];
    std::vector<Byte> text_bytes(text.begin(), text.end());
    auto encoded = entropy::cm::encode(text_bytes);
    assert(encoded.size() < text_bytes.size() / 4);
    check_roundtrip("text", text_bytes);

    // Unknown model version must be rejected, not misdecoded
    encoded[0] = entropy::cm::MODEL_VERSION + 1;
    assert(entropy::cm::decode(encoded, text_bytes.size()).empty());

    // Truncated stream must terminate with the requested size
    encoded[0] = entropy::cm::MODEL_VERSION;
    encoded.resize(encoded.size() / 2);
    assert(entropy::cm::decode(encoded, text_bytes.size()).size() == text_bytes.size());

    std::cout << "All CM tests passed!\n";
    return 0;
}
