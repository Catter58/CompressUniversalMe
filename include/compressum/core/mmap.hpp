#pragma once

/**
 * CompressUM - Original Compression Algorithm
 * Memory-mapped file I/O
 *
 * Cross-platform implementation using:
 * - POSIX: mmap/munmap
 * - Windows: CreateFileMapping/MapViewOfFile
 *
 * Provides zero-copy access to file contents for efficient
 * processing of large files.
 */

#include "../types.hpp"
#include <string>
#include <cstdio>

#ifdef _WIN32
    #define WIN32_LEAN_AND_MEAN
    #define NOMINMAX
    #include <windows.h>
#else
    #include <sys/mman.h>
    #include <sys/stat.h>
    #include <fcntl.h>
    #include <unistd.h>
#endif

namespace compressum::core {

/**
 * Memory-mapped file for reading
 */
class MappedFileReader {
public:
    MappedFileReader() = default;

    /**
     * Open and map a file for reading
     */
    explicit MappedFileReader(const std::string& path) {
        open(path);
    }

    ~MappedFileReader() {
        close();
    }

    // Non-copyable
    MappedFileReader(const MappedFileReader&) = delete;
    MappedFileReader& operator=(const MappedFileReader&) = delete;

    // Movable
    MappedFileReader(MappedFileReader&& other) noexcept {
        *this = std::move(other);
    }

    MappedFileReader& operator=(MappedFileReader&& other) noexcept {
        if (this != &other) {
            close();
            data_ = other.data_;
            size_ = other.size_;
            #ifdef _WIN32
                file_handle_ = other.file_handle_;
                mapping_handle_ = other.mapping_handle_;
                other.file_handle_ = INVALID_HANDLE_VALUE;
                other.mapping_handle_ = nullptr;
            #else
                fd_ = other.fd_;
                other.fd_ = -1;
            #endif
            other.data_ = nullptr;
            other.size_ = 0;
        }
        return *this;
    }

    /**
     * Open and map a file
     * @return ErrorCode::Ok on success
     */
    Result<void> open(const std::string& path) {
        close();

        #ifdef _WIN32
            // Windows implementation
            file_handle_ = CreateFileA(
                path.c_str(),
                GENERIC_READ,
                FILE_SHARE_READ,
                nullptr,
                OPEN_EXISTING,
                FILE_ATTRIBUTE_NORMAL,
                nullptr
            );

            if (file_handle_ == INVALID_HANDLE_VALUE) {
                return Result<void>::failure(ErrorCode::IoError);
            }

            LARGE_INTEGER file_size;
            if (!GetFileSizeEx(file_handle_, &file_size)) {
                CloseHandle(file_handle_);
                file_handle_ = INVALID_HANDLE_VALUE;
                return Result<void>::failure(ErrorCode::IoError);
            }

            size_ = static_cast<size_t>(file_size.QuadPart);

            if (size_ == 0) {
                // Empty file - no mapping needed
                return Result<void>::success();
            }

            mapping_handle_ = CreateFileMappingA(
                file_handle_,
                nullptr,
                PAGE_READONLY,
                0, 0,
                nullptr
            );

            if (mapping_handle_ == nullptr) {
                CloseHandle(file_handle_);
                file_handle_ = INVALID_HANDLE_VALUE;
                return Result<void>::failure(ErrorCode::IoError);
            }

            data_ = static_cast<const Byte*>(MapViewOfFile(
                mapping_handle_,
                FILE_MAP_READ,
                0, 0, 0
            ));

            if (data_ == nullptr) {
                CloseHandle(mapping_handle_);
                CloseHandle(file_handle_);
                mapping_handle_ = nullptr;
                file_handle_ = INVALID_HANDLE_VALUE;
                return Result<void>::failure(ErrorCode::IoError);
            }

        #else
            // POSIX implementation
            fd_ = ::open(path.c_str(), O_RDONLY);
            if (fd_ < 0) {
                return Result<void>::failure(ErrorCode::IoError);
            }

            struct stat st;
            if (fstat(fd_, &st) < 0) {
                ::close(fd_);
                fd_ = -1;
                return Result<void>::failure(ErrorCode::IoError);
            }

            size_ = static_cast<size_t>(st.st_size);

            if (size_ == 0) {
                // Empty file - no mapping needed
                return Result<void>::success();
            }

            void* mapped = mmap(nullptr, size_, PROT_READ, MAP_PRIVATE, fd_, 0);
            if (mapped == MAP_FAILED) {
                ::close(fd_);
                fd_ = -1;
                return Result<void>::failure(ErrorCode::IoError);
            }

            data_ = static_cast<const Byte*>(mapped);

            // Hint to kernel about access pattern
            madvise(const_cast<void*>(static_cast<const void*>(data_)), size_, MADV_SEQUENTIAL);
        #endif

        return Result<void>::success();
    }

    /**
     * Close the mapping
     */
    void close() {
        #ifdef _WIN32
            if (data_ != nullptr) {
                UnmapViewOfFile(data_);
                data_ = nullptr;
            }
            if (mapping_handle_ != nullptr) {
                CloseHandle(mapping_handle_);
                mapping_handle_ = nullptr;
            }
            if (file_handle_ != INVALID_HANDLE_VALUE) {
                CloseHandle(file_handle_);
                file_handle_ = INVALID_HANDLE_VALUE;
            }
        #else
            if (data_ != nullptr && size_ > 0) {
                munmap(const_cast<Byte*>(data_), size_);
                data_ = nullptr;
            }
            if (fd_ >= 0) {
                ::close(fd_);
                fd_ = -1;
            }
        #endif
        size_ = 0;
    }

    /**
     * Check if file is open
     */
    [[nodiscard]] bool is_open() const {
        #ifdef _WIN32
            return file_handle_ != INVALID_HANDLE_VALUE;
        #else
            return fd_ >= 0;
        #endif
    }

    /**
     * Get pointer to mapped data
     */
    [[nodiscard]] const Byte* data() const { return data_; }

    /**
     * Get file size
     */
    [[nodiscard]] size_t size() const { return size_; }

    /**
     * Get span of mapped data
     */
    [[nodiscard]] ByteSpan span() const {
        return ByteSpan(data_, size_);
    }

    /**
     * Access byte at index
     */
    [[nodiscard]] Byte operator[](size_t index) const {
        return data_[index];
    }

private:
    const Byte* data_ = nullptr;
    size_t size_ = 0;

    #ifdef _WIN32
        HANDLE file_handle_ = INVALID_HANDLE_VALUE;
        HANDLE mapping_handle_ = nullptr;
    #else
        int fd_ = -1;
    #endif
};

/**
 * Memory-mapped file for writing
 */
class MappedFileWriter {
public:
    MappedFileWriter() = default;

    ~MappedFileWriter() {
        close();
    }

    // Non-copyable, movable
    MappedFileWriter(const MappedFileWriter&) = delete;
    MappedFileWriter& operator=(const MappedFileWriter&) = delete;
    MappedFileWriter(MappedFileWriter&&) noexcept = default;
    MappedFileWriter& operator=(MappedFileWriter&&) noexcept = default;

    /**
     * Create and map a file for writing
     * @param path File path
     * @param size Initial size in bytes
     */
    Result<void> create(const std::string& path, size_t size) {
        close();
        size_ = size;

        #ifdef _WIN32
            file_handle_ = CreateFileA(
                path.c_str(),
                GENERIC_READ | GENERIC_WRITE,
                0,
                nullptr,
                CREATE_ALWAYS,
                FILE_ATTRIBUTE_NORMAL,
                nullptr
            );

            if (file_handle_ == INVALID_HANDLE_VALUE) {
                return Result<void>::failure(ErrorCode::IoError);
            }

            LARGE_INTEGER li;
            li.QuadPart = static_cast<LONGLONG>(size);
            if (!SetFilePointerEx(file_handle_, li, nullptr, FILE_BEGIN) ||
                !SetEndOfFile(file_handle_)) {
                CloseHandle(file_handle_);
                file_handle_ = INVALID_HANDLE_VALUE;
                return Result<void>::failure(ErrorCode::IoError);
            }

            mapping_handle_ = CreateFileMappingA(
                file_handle_,
                nullptr,
                PAGE_READWRITE,
                0, 0,
                nullptr
            );

            if (mapping_handle_ == nullptr) {
                CloseHandle(file_handle_);
                file_handle_ = INVALID_HANDLE_VALUE;
                return Result<void>::failure(ErrorCode::IoError);
            }

            data_ = static_cast<Byte*>(MapViewOfFile(
                mapping_handle_,
                FILE_MAP_ALL_ACCESS,
                0, 0, 0
            ));

            if (data_ == nullptr) {
                CloseHandle(mapping_handle_);
                CloseHandle(file_handle_);
                mapping_handle_ = nullptr;
                file_handle_ = INVALID_HANDLE_VALUE;
                return Result<void>::failure(ErrorCode::IoError);
            }

        #else
            fd_ = ::open(path.c_str(), O_RDWR | O_CREAT | O_TRUNC, 0644);
            if (fd_ < 0) {
                return Result<void>::failure(ErrorCode::IoError);
            }

            if (ftruncate(fd_, static_cast<off_t>(size)) < 0) {
                ::close(fd_);
                fd_ = -1;
                return Result<void>::failure(ErrorCode::IoError);
            }

            void* mapped = mmap(nullptr, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd_, 0);
            if (mapped == MAP_FAILED) {
                ::close(fd_);
                fd_ = -1;
                return Result<void>::failure(ErrorCode::IoError);
            }

            data_ = static_cast<Byte*>(mapped);
        #endif

        return Result<void>::success();
    }

    /**
     * Sync changes to disk
     */
    Result<void> sync() {
        if (data_ == nullptr) {
            return Result<void>::success();
        }

        #ifdef _WIN32
            if (!FlushViewOfFile(data_, 0)) {
                return Result<void>::failure(ErrorCode::IoError);
            }
        #else
            if (msync(data_, size_, MS_SYNC) < 0) {
                return Result<void>::failure(ErrorCode::IoError);
            }
        #endif

        return Result<void>::success();
    }

    /**
     * Truncate file to actual size and close
     */
    Result<void> truncate_and_close(size_t actual_size) {
        if (data_ == nullptr) {
            return Result<void>::success();
        }

        #ifdef _WIN32
            UnmapViewOfFile(data_);
            CloseHandle(mapping_handle_);

            LARGE_INTEGER li;
            li.QuadPart = static_cast<LONGLONG>(actual_size);
            if (!SetFilePointerEx(file_handle_, li, nullptr, FILE_BEGIN) ||
                !SetEndOfFile(file_handle_)) {
                CloseHandle(file_handle_);
                file_handle_ = INVALID_HANDLE_VALUE;
                data_ = nullptr;
                mapping_handle_ = nullptr;
                return Result<void>::failure(ErrorCode::IoError);
            }

            CloseHandle(file_handle_);
            file_handle_ = INVALID_HANDLE_VALUE;
            data_ = nullptr;
            mapping_handle_ = nullptr;

        #else
            munmap(data_, size_);
            data_ = nullptr;

            if (ftruncate(fd_, static_cast<off_t>(actual_size)) < 0) {
                ::close(fd_);
                fd_ = -1;
                return Result<void>::failure(ErrorCode::IoError);
            }

            ::close(fd_);
            fd_ = -1;
        #endif

        size_ = 0;
        return Result<void>::success();
    }

    void close() {
        #ifdef _WIN32
            if (data_ != nullptr) {
                UnmapViewOfFile(data_);
                data_ = nullptr;
            }
            if (mapping_handle_ != nullptr) {
                CloseHandle(mapping_handle_);
                mapping_handle_ = nullptr;
            }
            if (file_handle_ != INVALID_HANDLE_VALUE) {
                CloseHandle(file_handle_);
                file_handle_ = INVALID_HANDLE_VALUE;
            }
        #else
            if (data_ != nullptr && size_ > 0) {
                munmap(data_, size_);
                data_ = nullptr;
            }
            if (fd_ >= 0) {
                ::close(fd_);
                fd_ = -1;
            }
        #endif
        size_ = 0;
    }

    [[nodiscard]] bool is_open() const { return data_ != nullptr; }
    [[nodiscard]] Byte* data() { return data_; }
    [[nodiscard]] const Byte* data() const { return data_; }
    [[nodiscard]] size_t size() const { return size_; }
    [[nodiscard]] MutableByteSpan span() { return MutableByteSpan(data_, size_); }

private:
    Byte* data_ = nullptr;
    size_t size_ = 0;

    #ifdef _WIN32
        HANDLE file_handle_ = INVALID_HANDLE_VALUE;
        HANDLE mapping_handle_ = nullptr;
    #else
        int fd_ = -1;
    #endif
};

} // namespace compressum::core
