# CompressUniversalMe (CompressUM)

A cross-platform CLI compression tool featuring an original hybrid compression algorithm implemented entirely from scratch in C++20 without external compression libraries.

## Features

- **Original CompressUM Algorithm** - Hybrid adaptive compression combining LZ77, Huffman, and rANS
- **Automatic Data Analysis** - Detects data type and selects optimal compression strategy
- **All Components From Scratch** - No external compression libraries (zlib, lz4, zstd, etc.)
- **High Performance** - SIMD optimizations (AVX2, SSE4.2, ARM NEON)
- **Multi-threaded** - Parallel block compression and decompression for large files
- **Cross-platform** - Linux, macOS (Intel/Apple Silicon), Windows
- **Single Binary** - No runtime dependencies

## Build Requirements

- C++20 compiler (GCC 11+, Clang 14+, MSVC 2022+)
- CMake 3.20+
- Ninja (recommended) or Make

## Quick Start

```bash
# Clone
git clone https://github.com/yourusername/CompressUniversalMe.git
cd CompressUniversalMe

# Build (Release)
cmake --preset release
cmake --build build/release -j$(nproc)

# Run
./build/release/cum --help
```

## Usage

### Compress a file
```bash
./cum compress input.txt -o output.cum
./cum compress input.txt                  # Creates input.txt.cum
```

### Decompress a file
```bash
./cum decompress output.cum -o restored.txt
./cum decompress output.cum               # Creates output (removes .cum extension)
```

### Analyze a file
```bash
./cum analyze input.txt
```

### Compression levels
```bash
./cum compress input.txt -l 1       # Fast compression (highest speed)
./cum compress input.txt -l 5       # Balanced (default)
./cum compress input.txt -l 9       # Best compression (smallest size)
```

### Multi-threaded compression
```bash
./cum compress largefile.bin -t 4   # Use 4 threads
./cum compress largefile.bin -t 0   # Auto-detect thread count
```

## Build Presets

```bash
cmake --preset debug          # Debug build with tests
cmake --preset release        # Optimized release build
cmake --preset release-static # Static binary (no dependencies)
```

## Running Tests

```bash
cmake --preset debug
cmake --build build/debug
cd build/debug && ctest --output-on-failure
```

## Algorithm Architecture

CompressUM uses a 5-phase hybrid compression pipeline:

```
Input Data
    │
    ▼
┌─────────────────────────────────────┐
│  Phase 1: Analysis                  │
│  • Byte frequency histogram         │
│  • Shannon entropy calculation      │
│  • Data type detection              │
└─────────────────────────────────────┘
    │
    ▼
┌─────────────────────────────────────┐
│  Phase 2: Preprocessing             │
│  • Delta encoding (sequential data) │
│  • BCJ filter (executables)         │
└─────────────────────────────────────┘
    │
    ▼
┌─────────────────────────────────────┐
│  Phase 3: Dictionary Compression    │
│  • LZ77 with hash chains            │
│  • Lazy matching optimization       │
│  • Adaptive window size (4KB-64KB)  │
└─────────────────────────────────────┘
    │
    ▼
┌─────────────────────────────────────┐
│  Phase 4: Context Modeling          │
│  • Order-0/1/2 statistics           │
│  • Probability estimation           │
└─────────────────────────────────────┘
    │
    ▼
┌─────────────────────────────────────┐
│  Phase 5: Entropy Coding (rANS)     │
│  • Asymmetric Numeral Systems       │
│  • Interleaved streams              │
└─────────────────────────────────────┘
    │
    ▼
Compressed Output (.cum)
```

### Level 9: context mixing (`entropy/cm.hpp`)

Levels 7-9 replace the LZ77 stage with an original bitwise context-mixing coder:

- 11 context models predict each bit: orders 0-4 and 6, current word, word pair,
  sparse contexts (bytes -2/-3, -2/-4, -4/-8) for tables and binary records
- A match model predicts the next bit from the longest earlier repeat
- Counters live in 64-byte, 2-way associative buckets with check tags,
  one bucket per context nibble
- Two logistic mixers with different weight selectors are averaged, then
  three SSE stages (order 0, 1, 2) refine the probability
- A 32-bit binary arithmetic coder writes the bits

## File Format (.cum)

```
Header (32 bytes):
┌──────────────────────────────────────┐
│ Magic: "CUM\x01"        (4 bytes)    │
│ Version (1, or 2 if CM) (2 bytes)    │
│ Flags                   (2 bytes)    │
│ Original size           (8 bytes)    │
│ Compressed size         (8 bytes)    │
│ CRC32                   (4 bytes)    │
│ Block count             (4 bytes)    │
└──────────────────────────────────────┘
Then per block: 8-byte header (24-bit compressed size, 24-bit original size,
preprocessor flags, method) followed by the block data
```

## Performance

Compressed size in bytes, Apple Silicon (ARM64), 2026-09-22. Lower is better.
Every `cum` result was verified to decompress byte-identical.

| File | Original | cum -l 5 | **cum -l 9** | gzip -9 | bzip2 -9 | xz -9e | zstd --ultra -22 | brotli -q 11 |
|------|---------:|---------:|------------:|--------:|---------:|-------:|-----------------:|-------------:|
| English word list | 2,493,885 | 838,270 | **410,195** | 754,299 | 857,578 | 637,488 | 661,859 | 649,944 |
| C/C++ headers | 8,000,000 | 1,425,988 | **670,078** | 1,274,843 | 1,020,259 | 878,776 | 918,568 | 878,215 |
| JSON log | 5,287,794 | 834,286 | **311,077** | 650,967 | 472,821 | 508,072 | 554,949 | 535,722 |
| Mach-O executable | 2,045,440 | 1,032,610 | **553,899** | 984,073 | 878,088 | 566,524 | 620,036 | 582,288 |
| 16-bit sensor samples | 2,000,000 | 837,400 | **366,517** | 819,713 | 390,907 | 541,800 | 649,736 | 598,623 |
| JPEG photo | 3,398,183 | 3,398,319 | **3,145,002** | 3,326,381 | 3,245,144 | 3,280,180 | 3,279,760 | - |

Level 9 (context mixing) runs at about 1-1.5 MB/s in both directions per thread;
blocks compress and decompress in parallel (29 MB file: 24 s on 1 thread, 8 s on 4)
and uses about 180 MB per 8 MB block (up to 4 blocks in parallel).
Levels 1-6 (LZ77 + Huffman) run at 100+ MB/s.

Reproduce on your own files:

```bash
CUM=build/release/cum scripts/bench.sh file1 file2 ...
```

## Core Components

| Component | File | Description |
|-----------|------|-------------|
| BitStream | `core/bitstream.hpp` | Bit-level I/O with buffering |
| CRC32-C | `core/crc32.hpp` | Hardware-accelerated checksum |
| Huffman | `entropy/huffman.hpp` | Canonical Huffman codes |
| Context mixing | `entropy/cm.hpp` | Level 9 bitwise CM coder (see below) |
| rANS | `entropy/rans.hpp` | Asymmetric Numeral Systems |
| LZ77 | `dictionary/lz77.hpp` | Hash chains, lazy matching |
| Legacy BWT | `transform/bwt.hpp` | Decoders for old v1 BWT blocks only |
| Delta | `transform/delta.hpp` | Delta encoding |
| BCJ | `transform/bcj.hpp` | Executable preprocessing |
| Deflate | `transform/deflate.hpp` | zlib-compatible inflate/deflate |

## Project Structure

```
compressum/
├── CMakeLists.txt
├── CMakePresets.json
├── include/
│   └── compressum/
│       ├── config.hpp
│       ├── types.hpp
│       ├── compressum.hpp
│       ├── core/
│       ├── entropy/
│       ├── dictionary/
│       ├── transform/
│       └── compressor/
├── src/
│   ├── main.cpp
│   ├── cli/
│   ├── core/
│   ├── entropy/
│   ├── dictionary/
│   ├── transform/
│   └── compressor/
└── tests/
    ├── test_*.cpp
    ├── benchmark.cpp
    └── fuzz/
```

## SIMD Support

- **x86-64**: SSE4.2 (CRC32, 16-byte compare), AVX2 (32-byte compare)
- **ARM64**: NEON (16-byte compare), CRC32 intrinsics

Detected automatically at compile time.

## License

MIT License - see [LICENSE](LICENSE) file.

## Contributing

1. Fork the repository
2. Create a feature branch
3. Make your changes
4. Run tests: `ctest --output-on-failure`
5. Submit a pull request

### Code Style

- C++20 standard
- RAII for resource management
- `std::span` over raw pointers
- `[[nodiscard]]` for value-returning functions
- PascalCase for classes, snake_case for functions
