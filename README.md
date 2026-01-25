# CompressUniversalMe (CompressUM)

A cross-platform CLI compression tool featuring an original hybrid compression algorithm implemented entirely from scratch in C++20 without external compression libraries.

## Features

- **Original CompressUM Algorithm** - Hybrid adaptive compression combining LZ77, Huffman, and rANS
- **Automatic Data Analysis** - Detects data type and selects optimal compression strategy
- **All Components From Scratch** - No external compression libraries (zlib, lz4, zstd, etc.)
- **High Performance** - SIMD optimizations (AVX2, SSE4.2, ARM NEON)
- **Multi-threaded** - Parallel block compression for large files
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
./cum compress input.txt -l fast    # Fast compression (highest speed)
./cum compress input.txt -l normal  # Balanced (default)
./cum compress input.txt -l best    # Best compression (smallest size)
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
│  • BWT (text with high entropy)     │
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

## File Format (.cum)

```
Header (32 bytes):
┌──────────────────────────────────────┐
│ Magic: "CUM\x01"        (4 bytes)    │
│ Version                 (2 bytes)    │
│ Flags                   (2 bytes)    │
│ Original size           (8 bytes)    │
│ Compressed size         (8 bytes)    │
│ CRC32                   (4 bytes)    │
│ Block count             (4 bytes)    │
└──────────────────────────────────────┘
Block Table + Compressed Blocks...
```

## Performance

Benchmarks on Apple M1 (ARM64):

| Data Type | Compression Ratio | Compress Speed | Decompress Speed |
|-----------|-------------------|----------------|------------------|
| Text (English) | 2.75x | 335 MB/s | 183 MB/s |
| Binary (Structured) | 4.82x | 679 MB/s | 461 MB/s |
| Random | 1.00x | 1947 MB/s | 3813 MB/s |

CRC32-C throughput: 7.8 GB/s (ARM hardware acceleration)

## Core Components

| Component | File | Description |
|-----------|------|-------------|
| BitStream | `core/bitstream.hpp` | Bit-level I/O with buffering |
| CRC32-C | `core/crc32.hpp` | Hardware-accelerated checksum |
| Huffman | `entropy/huffman.hpp` | Canonical Huffman codes |
| rANS | `entropy/rans.hpp` | Asymmetric Numeral Systems |
| LZ77 | `dictionary/lz77.hpp` | Hash chains, lazy matching |
| BWT | `transform/bwt.hpp` | Burrows-Wheeler Transform |
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
