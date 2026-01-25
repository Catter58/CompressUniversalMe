# CompressUM Fuzz Tests
#
# Build with: cmake -DCMAKE_CXX_COMPILER=clang++ -DCOMPRESSUM_FUZZ=ON ..
#
# Run with: ./fuzz_decompress corpus/ -max_total_time=3600

if(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    set(FUZZ_COMPILE_FLAGS "-fsanitize=fuzzer,address,undefined -fno-omit-frame-pointer")
    set(FUZZ_LINK_FLAGS "-fsanitize=fuzzer,address,undefined")

    # Fuzz decompression
    add_executable(fuzz_decompress fuzz_decompress.cpp)
    target_link_libraries(fuzz_decompress PRIVATE compressum_lib)
    set_target_properties(fuzz_decompress PROPERTIES
        COMPILE_FLAGS "${FUZZ_COMPILE_FLAGS}"
        LINK_FLAGS "${FUZZ_LINK_FLAGS}")

    # Fuzz roundtrip
    add_executable(fuzz_roundtrip fuzz_roundtrip.cpp)
    target_link_libraries(fuzz_roundtrip PRIVATE compressum_lib)
    set_target_properties(fuzz_roundtrip PROPERTIES
        COMPILE_FLAGS "${FUZZ_COMPILE_FLAGS}"
        LINK_FLAGS "${FUZZ_LINK_FLAGS}")

    # Fuzz deflate
    add_executable(fuzz_deflate fuzz_deflate.cpp)
    target_link_libraries(fuzz_deflate PRIVATE compressum_lib)
    set_target_properties(fuzz_deflate PROPERTIES
        COMPILE_FLAGS "${FUZZ_COMPILE_FLAGS}"
        LINK_FLAGS "${FUZZ_LINK_FLAGS}")

    message(STATUS "Fuzz tests enabled (Clang with libFuzzer)")
else()
    message(WARNING "Fuzz tests require Clang compiler with libFuzzer support")
endif()
