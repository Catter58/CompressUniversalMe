#pragma once

/**
 * CompressUM - Original Compression Algorithm
 * Parallel Compression Module
 *
 * Compresses blocks concurrently using multiple threads.
 * Each block is compressed independently, then assembled.
 */

#include "../config.hpp"
#include "../types.hpp"
#include "block.hpp"
#include <vector>
#include <thread>
#include <future>
#include <algorithm>

namespace compressum {

/**
 * Get the default number of threads based on hardware
 */
inline size_t default_thread_count() {
    size_t hw_threads = std::thread::hardware_concurrency();
    return hw_threads > 0 ? hw_threads : 4;
}

/**
 * Parallel block compressor
 */
class ParallelBlockCompressor {
public:
    explicit ParallelBlockCompressor(
        Level level = Level::Normal,
        size_t num_threads = 0
    )
        : level_(level)
        , num_threads_(num_threads > 0 ? num_threads : default_thread_count())
    {}

    /**
     * Compress multiple blocks in parallel
     * @param input Input data
     * @param block_size Size of each block
     * @return Vector of compressed blocks in order
     */
    [[nodiscard]] std::vector<CompressedBlock> compress_blocks(
        ByteSpan input,
        size_t block_size
    ) {
        if (input.empty()) {
            return {};
        }

        // Calculate number of blocks
        size_t num_blocks = (input.size() + block_size - 1) / block_size;

        // Limit threads to number of blocks
        size_t threads_to_use = std::min(num_threads_, num_blocks);

        if (threads_to_use <= 1 || num_blocks <= 1) {
            // Single-threaded fallback
            return compress_sequential(input, block_size, num_blocks);
        }

        // Prepare block ranges
        struct BlockRange {
            size_t start;
            size_t length;
            size_t index;
        };

        std::vector<BlockRange> ranges;
        ranges.reserve(num_blocks);

        for (size_t i = 0; i < num_blocks; ++i) {
            size_t start = i * block_size;
            size_t length = std::min(block_size, input.size() - start);
            ranges.push_back({start, length, i});
        }

        // Result storage
        std::vector<CompressedBlock> results(num_blocks);

        // Thread function
        auto compress_range = [&](size_t start_idx, size_t end_idx) {
            BlockCompressor compressor(level_);

            for (size_t i = start_idx; i < end_idx; ++i) {
                const auto& range = ranges[i];
                ByteSpan block_data(input.data() + range.start, range.length);
                results[range.index] = compressor.compress(block_data);
            }
        };

        // Distribute blocks among threads
        std::vector<std::thread> threads;
        threads.reserve(threads_to_use);

        size_t blocks_per_thread = num_blocks / threads_to_use;
        size_t extra_blocks = num_blocks % threads_to_use;

        size_t block_idx = 0;
        for (size_t t = 0; t < threads_to_use; ++t) {
            size_t count = blocks_per_thread + (t < extra_blocks ? 1 : 0);
            if (count > 0) {
                threads.emplace_back(compress_range, block_idx, block_idx + count);
                block_idx += count;
            }
        }

        // Wait for all threads
        for (auto& thread : threads) {
            thread.join();
        }

        return results;
    }

    /**
     * Compress using futures for better control
     */
    [[nodiscard]] std::vector<std::future<CompressedBlock>> compress_async(
        ByteSpan input,
        size_t block_size
    ) {
        std::vector<std::future<CompressedBlock>> futures;

        if (input.empty()) {
            return futures;
        }

        size_t num_blocks = (input.size() + block_size - 1) / block_size;
        futures.reserve(num_blocks);

        for (size_t i = 0; i < num_blocks; ++i) {
            size_t start = i * block_size;
            size_t length = std::min(block_size, input.size() - start);

            // Capture by value since input span might not outlive the async task
            std::vector<Byte> block_copy(input.data() + start, input.data() + start + length);

            futures.push_back(std::async(
                std::launch::async,
                [this, block_copy = std::move(block_copy)]() {
                    BlockCompressor compressor(level_);
                    return compressor.compress(ByteSpan(block_copy));
                }
            ));
        }

        return futures;
    }

    /**
     * Set number of threads
     */
    void set_num_threads(size_t n) {
        num_threads_ = n > 0 ? n : default_thread_count();
    }

    /**
     * Get number of threads
     */
    [[nodiscard]] size_t num_threads() const { return num_threads_; }

private:
    /**
     * Sequential compression fallback
     */
    [[nodiscard]] std::vector<CompressedBlock> compress_sequential(
        ByteSpan input,
        size_t block_size,
        size_t num_blocks
    ) {
        std::vector<CompressedBlock> results;
        results.reserve(num_blocks);

        BlockCompressor compressor(level_);

        for (size_t i = 0; i < num_blocks; ++i) {
            size_t start = i * block_size;
            size_t length = std::min(block_size, input.size() - start);
            ByteSpan block_data(input.data() + start, length);
            results.push_back(compressor.compress(block_data));
        }

        return results;
    }

    Level level_;
    size_t num_threads_;
};

} // namespace compressum
