# CompressUM Tests

# Simple test framework (no external dependencies)
# Uses simple assert macros

# Core tests
add_executable(test_bitstream test_bitstream.cpp)
target_link_libraries(test_bitstream PRIVATE compressum_lib)
add_test(NAME bitstream COMMAND test_bitstream)

add_executable(test_crc32 test_crc32.cpp)
target_link_libraries(test_crc32 PRIVATE compressum_lib)
add_test(NAME crc32 COMMAND test_crc32)

add_executable(test_analyzer test_analyzer.cpp)
target_link_libraries(test_analyzer PRIVATE compressum_lib)
add_test(NAME analyzer COMMAND test_analyzer)

# Entropy coding tests
add_executable(test_huffman test_huffman.cpp)
target_link_libraries(test_huffman PRIVATE compressum_lib)
add_test(NAME huffman COMMAND test_huffman)

add_executable(test_rans test_rans.cpp)
target_link_libraries(test_rans PRIVATE compressum_lib)
add_test(NAME rans COMMAND test_rans)

# Dictionary compression tests
add_executable(test_lz77 test_lz77.cpp)
target_link_libraries(test_lz77 PRIVATE compressum_lib)
add_test(NAME lz77 COMMAND test_lz77)

# Transform tests (Phase 4)
add_executable(test_transforms test_transforms.cpp)
target_link_libraries(test_transforms PRIVATE compressum_lib)
add_test(NAME transforms COMMAND test_transforms)

# Integration tests (Phase 5)
add_executable(test_compressor test_compressor.cpp)
target_link_libraries(test_compressor PRIVATE compressum_lib)
add_test(NAME compressor COMMAND test_compressor)

# Deflate tests (Phase 6)
add_executable(test_deflate test_deflate.cpp)
target_link_libraries(test_deflate PRIVATE compressum_lib)
add_test(NAME deflate COMMAND test_deflate)

# JPEG optimizer tests (Phase 6)
add_executable(test_jpeg test_jpeg.cpp)
target_link_libraries(test_jpeg PRIVATE compressum_lib)
add_test(NAME jpeg COMMAND test_jpeg)

# PNG optimizer tests (Phase 6)
add_executable(test_png test_png.cpp)
target_link_libraries(test_png PRIVATE compressum_lib)
add_test(NAME png COMMAND test_png)

# Edge case tests (Phase 8)
add_executable(test_edge_cases test_edge_cases.cpp)
target_link_libraries(test_edge_cases PRIVATE compressum_lib)
add_test(NAME edge_cases COMMAND test_edge_cases)
set_tests_properties(edge_cases PROPERTIES TIMEOUT 120)

# Performance benchmarks (Phase 7)
add_executable(benchmark benchmark.cpp)
target_link_libraries(benchmark PRIVATE compressum_lib)
# Note: benchmark is not added as a test since it's for manual performance testing

# Fuzz tests (optional, requires Clang with libFuzzer)
option(COMPRESSUM_FUZZ "Build fuzz tests (requires Clang)" OFF)
if(COMPRESSUM_FUZZ)
    add_subdirectory(fuzz)
endif()
