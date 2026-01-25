cmake_minimum_required(VERSION 3.21...4.0)

project(CompressUM
    VERSION 1.0.0
    DESCRIPTION "Original compression algorithm implemented from scratch"
    LANGUAGES CXX
)

# ============================================================================
# C++20 Standard
# ============================================================================
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# ============================================================================
# Build Options
# ============================================================================
option(COMPRESSUM_BUILD_TESTS "Build unit tests" ON)
option(COMPRESSUM_BUILD_BENCHMARKS "Build benchmarks" ON)
option(COMPRESSUM_ENABLE_SIMD "Enable SIMD optimizations" ON)
option(COMPRESSUM_STATIC_BINARY "Build static binary" OFF)

# ============================================================================
# Compiler Detection and Flags
# ============================================================================
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    add_compile_options(
        -Wall -Wextra -Wpedantic
        -Wno-unused-parameter
        -fno-exceptions  # No exceptions in hot paths
    )

    # Detect architecture
    if(CMAKE_SYSTEM_PROCESSOR MATCHES "x86_64|AMD64|i686|i386")
        set(COMPRESSUM_ARCH_X86 TRUE)
        message(STATUS "Architecture: x86/x86_64")
    elseif(CMAKE_SYSTEM_PROCESSOR MATCHES "aarch64|arm64|ARM64|arm")
        set(COMPRESSUM_ARCH_ARM TRUE)
        message(STATUS "Architecture: ARM/ARM64")
    else()
        message(STATUS "Architecture: ${CMAKE_SYSTEM_PROCESSOR} (unknown)")
    endif()

    if(COMPRESSUM_ENABLE_SIMD AND COMPRESSUM_ARCH_X86)
        include(CheckCXXCompilerFlag)
        check_cxx_compiler_flag("-msse4.2" COMPILER_SUPPORTS_SSE42)
        check_cxx_compiler_flag("-mavx2" COMPILER_SUPPORTS_AVX2)

        if(COMPILER_SUPPORTS_SSE42)
            add_compile_options(-msse4.2)
            add_compile_definitions(COMPRESSUM_HAS_SSE42)
        endif()

        if(COMPILER_SUPPORTS_AVX2)
            add_compile_options(-mavx2)
            add_compile_definitions(COMPRESSUM_HAS_AVX2)
        endif()
    endif()

    # Release optimizations
    set(CMAKE_CXX_FLAGS_RELEASE "-O3 -DNDEBUG -march=native")

elseif(MSVC)
    add_compile_options(/W4 /permissive-)

    if(COMPRESSUM_ENABLE_SIMD)
        add_compile_options(/arch:AVX2)
        add_compile_definitions(COMPRESSUM_HAS_SSE42 COMPRESSUM_HAS_AVX2)
    endif()

    # Static CRT for static builds
    if(COMPRESSUM_STATIC_BINARY)
        set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
    endif()
endif()

# ============================================================================
# Static linking
# ============================================================================
if(COMPRESSUM_STATIC_BINARY)
    if(CMAKE_CXX_COMPILER_ID MATCHES "GNU")
        add_link_options(-static -static-libgcc -static-libstdc++)
    elseif(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
        add_link_options(-static)
    endif()
endif()

# ============================================================================
# Source Files
# ============================================================================
set(COMPRESSUM_CORE_SOURCES
    src/core/bitstream.cpp
    src/core/crc32.cpp
    src/core/mmap.cpp
    src/core/analyzer.cpp
)

set(COMPRESSUM_ENTROPY_SOURCES
    src/entropy/huffman.cpp
    src/entropy/arithmetic.cpp
    src/entropy/rans.cpp
)

set(COMPRESSUM_DICTIONARY_SOURCES
    src/dictionary/lz77.cpp
    src/dictionary/hashchain.cpp
    src/dictionary/match_finder.cpp
)

set(COMPRESSUM_TRANSFORM_SOURCES
    src/transform/bwt.cpp
    src/transform/delta.cpp
    src/transform/bcj.cpp
    src/transform/deflate.cpp
    src/transform/jpeg.cpp
    src/transform/png.cpp
)

set(COMPRESSUM_COMPRESSOR_SOURCES
    src/compressor/compressum.cpp
    src/compressor/block.cpp
    src/compressor/parallel.cpp
)

set(COMPRESSUM_CLI_SOURCES
    src/cli/parser.cpp
)

# ============================================================================
# Library Target
# ============================================================================
add_library(compressum_lib STATIC
    ${COMPRESSUM_CORE_SOURCES}
    ${COMPRESSUM_ENTROPY_SOURCES}
    ${COMPRESSUM_DICTIONARY_SOURCES}
    ${COMPRESSUM_TRANSFORM_SOURCES}
    ${COMPRESSUM_COMPRESSOR_SOURCES}
)

target_include_directories(compressum_lib
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_compile_features(compressum_lib PUBLIC cxx_std_20)

# Threading support
find_package(Threads REQUIRED)
target_link_libraries(compressum_lib PUBLIC Threads::Threads)

# ============================================================================
# Executable Target
# ============================================================================
add_executable(compressum
    src/main.cpp
    ${COMPRESSUM_CLI_SOURCES}
)

target_link_libraries(compressum PRIVATE compressum_lib)

# ============================================================================
# Tests
# ============================================================================
if(COMPRESSUM_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()

# ============================================================================
# Installation
# ============================================================================
install(TARGETS compressum
    RUNTIME DESTINATION bin
)

install(DIRECTORY include/compressum
    DESTINATION include
)

# ============================================================================
# Summary
# ============================================================================
message(STATUS "")
message(STATUS "CompressUM Configuration:")
message(STATUS "  Version:      ${PROJECT_VERSION}")
message(STATUS "  C++ Standard: ${CMAKE_CXX_STANDARD}")
message(STATUS "  Build Type:   ${CMAKE_BUILD_TYPE}")
message(STATUS "  SIMD:         ${COMPRESSUM_ENABLE_SIMD}")
message(STATUS "  Tests:        ${COMPRESSUM_BUILD_TESTS}")
message(STATUS "  Static:       ${COMPRESSUM_STATIC_BINARY}")
message(STATUS "")
