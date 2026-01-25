#pragma once

/**
 * CompressUM - Original Compression Algorithm
 * Delta Encoding/Decoding
 *
 * Delta encoding stores differences between consecutive values.
 * Effective for data with local correlation:
 * - Images (pixel differences)
 * - Audio (sample differences)
 * - Time series (incremental values)
 *
 * Variants:
 * - Delta1: d[i] = x[i] - x[i-1]
 * - Delta2: d[i] = x[i] - 2*x[i-1] + x[i-2]
 * - XOR: d[i] = x[i] ^ x[i-1]
 */

#include "../config.hpp"
#include "../types.hpp"
#include <vector>
#include <cstring>

namespace compressum::transform {

/**
 * Delta filter type
 */
enum class DeltaType {
    Subtract,   // x[i] - x[i-1]
    XOR,        // x[i] ^ x[i-1]
    Second,     // x[i] - 2*x[i-1] + x[i-2]
};

/**
 * Delta configuration
 */
struct DeltaConfig {
    DeltaType type = DeltaType::Subtract;
    size_t distance = 1;  // Byte distance for differencing
};

/**
 * Delta Encoder
 */
class DeltaEncoder {
public:
    explicit DeltaEncoder(const DeltaConfig& config = {})
        : config_(config)
    {}

    /**
     * Encode data using delta transform
     * @param input Input data
     * @return Delta-encoded data
     */
    [[nodiscard]] std::vector<Byte> encode(ByteSpan input) const {
        if (input.empty()) {
            return {};
        }

        std::vector<Byte> output(input.size());
        const size_t dist = config_.distance;

        switch (config_.type) {
            case DeltaType::Subtract:
                encode_subtract(input, output, dist);
                break;
            case DeltaType::XOR:
                encode_xor(input, output, dist);
                break;
            case DeltaType::Second:
                encode_second(input, output, dist);
                break;
        }

        return output;
    }

    /**
     * Encode in-place
     */
    void encode_inplace(std::vector<Byte>& data) const {
        auto encoded = encode(data);
        data = std::move(encoded);
    }

    /**
     * Get configuration
     */
    [[nodiscard]] const DeltaConfig& config() const { return config_; }

private:
    void encode_subtract(ByteSpan input, std::vector<Byte>& output, size_t dist) const {
        // First 'dist' bytes are copied as-is
        for (size_t i = 0; i < dist && i < input.size(); ++i) {
            output[i] = input[i];
        }

        // Remaining bytes are differences
        for (size_t i = dist; i < input.size(); ++i) {
            output[i] = static_cast<Byte>(input[i] - input[i - dist]);
        }
    }

    void encode_xor(ByteSpan input, std::vector<Byte>& output, size_t dist) const {
        for (size_t i = 0; i < dist && i < input.size(); ++i) {
            output[i] = input[i];
        }

        for (size_t i = dist; i < input.size(); ++i) {
            output[i] = input[i] ^ input[i - dist];
        }
    }

    void encode_second(ByteSpan input, std::vector<Byte>& output, size_t dist) const {
        // Need 2*dist bytes as prefix
        for (size_t i = 0; i < 2 * dist && i < input.size(); ++i) {
            output[i] = input[i];
        }

        // Second-order difference: x[i] - 2*x[i-dist] + x[i-2*dist]
        for (size_t i = 2 * dist; i < input.size(); ++i) {
            int diff = static_cast<int>(input[i])
                     - 2 * static_cast<int>(input[i - dist])
                     + static_cast<int>(input[i - 2 * dist]);
            output[i] = static_cast<Byte>(diff & 0xFF);
        }
    }

    DeltaConfig config_;
};

/**
 * Delta Decoder
 */
class DeltaDecoder {
public:
    explicit DeltaDecoder(const DeltaConfig& config = {})
        : config_(config)
    {}

    /**
     * Decode delta-encoded data
     */
    [[nodiscard]] std::vector<Byte> decode(ByteSpan input) const {
        if (input.empty()) {
            return {};
        }

        std::vector<Byte> output(input.size());
        const size_t dist = config_.distance;

        switch (config_.type) {
            case DeltaType::Subtract:
                decode_subtract(input, output, dist);
                break;
            case DeltaType::XOR:
                decode_xor(input, output, dist);
                break;
            case DeltaType::Second:
                decode_second(input, output, dist);
                break;
        }

        return output;
    }

    /**
     * Decode in-place
     */
    void decode_inplace(std::vector<Byte>& data) const {
        auto decoded = decode(data);
        data = std::move(decoded);
    }

private:
    void decode_subtract(ByteSpan input, std::vector<Byte>& output, size_t dist) const {
        for (size_t i = 0; i < dist && i < input.size(); ++i) {
            output[i] = input[i];
        }

        for (size_t i = dist; i < input.size(); ++i) {
            output[i] = static_cast<Byte>(input[i] + output[i - dist]);
        }
    }

    void decode_xor(ByteSpan input, std::vector<Byte>& output, size_t dist) const {
        for (size_t i = 0; i < dist && i < input.size(); ++i) {
            output[i] = input[i];
        }

        for (size_t i = dist; i < input.size(); ++i) {
            output[i] = input[i] ^ output[i - dist];
        }
    }

    void decode_second(ByteSpan input, std::vector<Byte>& output, size_t dist) const {
        for (size_t i = 0; i < 2 * dist && i < input.size(); ++i) {
            output[i] = input[i];
        }

        for (size_t i = 2 * dist; i < input.size(); ++i) {
            int val = static_cast<int>(input[i])
                    + 2 * static_cast<int>(output[i - dist])
                    - static_cast<int>(output[i - 2 * dist]);
            output[i] = static_cast<Byte>(val & 0xFF);
        }
    }

    DeltaConfig config_;
};

/**
 * Multi-channel delta for interleaved data (e.g., RGB images)
 */
class MultichannelDeltaEncoder {
public:
    explicit MultichannelDeltaEncoder(size_t num_channels = 3)
        : channels_(num_channels)
    {}

    [[nodiscard]] std::vector<Byte> encode(ByteSpan input) const {
        if (input.size() < channels_) {
            return {input.begin(), input.end()};
        }

        std::vector<Byte> output(input.size());

        // First pixel (all channels) is copied
        for (size_t i = 0; i < channels_; ++i) {
            output[i] = input[i];
        }

        // Each subsequent sample is differenced with the previous one
        // in the same channel
        for (size_t i = channels_; i < input.size(); ++i) {
            output[i] = static_cast<Byte>(input[i] - input[i - channels_]);
        }

        return output;
    }

    [[nodiscard]] std::vector<Byte> decode(ByteSpan input) const {
        if (input.size() < channels_) {
            return {input.begin(), input.end()};
        }

        std::vector<Byte> output(input.size());

        for (size_t i = 0; i < channels_; ++i) {
            output[i] = input[i];
        }

        for (size_t i = channels_; i < input.size(); ++i) {
            output[i] = static_cast<Byte>(input[i] + output[i - channels_]);
        }

        return output;
    }

private:
    size_t channels_;
};

} // namespace compressum::transform
