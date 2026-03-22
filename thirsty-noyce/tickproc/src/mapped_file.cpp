#include "mapped_file.h"

#ifdef _WIN32
// ═══════════════════════════════════════════════════════════════════════════
// Windows implementation: CreateFile → CreateFileMapping → MapViewOfFile
// ═══════════════════════════════════════════════════════════════════════════
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

MappedFile::MappedFile(const std::filesystem::path& path) {
    file_handle_ = CreateFileW(
        path.c_str(),
        GENERIC_READ,
        FILE_SHARE_READ,
        nullptr,
        OPEN_EXISTING,
        FILE_FLAG_SEQUENTIAL_SCAN,  // equivalent of madvise(SEQUENTIAL)
        nullptr);

    if (file_handle_ == INVALID_HANDLE_VALUE)
        throw std::system_error(
            static_cast<int>(GetLastError()),
            std::system_category(), "CreateFile");

    LARGE_INTEGER li;
    if (!GetFileSizeEx(file_handle_, &li)) {
        CloseHandle(file_handle_);
        throw std::system_error(
            static_cast<int>(GetLastError()),
            std::system_category(), "GetFileSizeEx");
    }
    size_ = static_cast<std::size_t>(li.QuadPart);

    if (size_ == 0) {
        CloseHandle(file_handle_);
        file_handle_ = nullptr;
        return;  // empty file — nothing to map
    }

    mapping_handle_ = CreateFileMappingW(
        file_handle_, nullptr, PAGE_READONLY, 0, 0, nullptr);
    if (!mapping_handle_) {
        CloseHandle(file_handle_);
        throw std::system_error(
            static_cast<int>(GetLastError()),
            std::system_category(), "CreateFileMapping");
    }

    ptr_ = static_cast<const char*>(
        MapViewOfFile(mapping_handle_, FILE_MAP_READ, 0, 0, 0));
    if (!ptr_) {
        CloseHandle(mapping_handle_);
        CloseHandle(file_handle_);
        throw std::system_error(
            static_cast<int>(GetLastError()),
            std::system_category(), "MapViewOfFile");
    }
}

void MappedFile::close() noexcept {
    if (ptr_)             { UnmapViewOfFile(ptr_);        ptr_ = nullptr; }
    if (mapping_handle_)  { CloseHandle(mapping_handle_); mapping_handle_ = nullptr; }
    if (file_handle_)     { CloseHandle(file_handle_);    file_handle_ = nullptr; }
    size_ = 0;
}

#else
// ═══════════════════════════════════════════════════════════════════════════
// POSIX implementation: open → fstat → mmap + madvise
// ═══════════════════════════════════════════════════════════════════════════
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

MappedFile::MappedFile(const std::filesystem::path& path) {
    fd_ = ::open(path.c_str(), O_RDONLY);
    if (fd_ == -1)
        throw std::system_error(errno, std::system_category(), "open");

    struct stat st{};
    if (::fstat(fd_, &st) == -1) {
        ::close(fd_);
        throw std::system_error(errno, std::system_category(), "fstat");
    }
    size_ = static_cast<std::size_t>(st.st_size);

    if (size_ == 0) {
        ::close(fd_);
        fd_ = -1;
        return;
    }

    void* addr = ::mmap(nullptr, size_, PROT_READ, MAP_PRIVATE, fd_, 0);
    if (addr == MAP_FAILED) {
        ::close(fd_);
        throw std::system_error(errno, std::system_category(), "mmap");
    }
    ptr_ = static_cast<const char*>(addr);

    // Advise the kernel that we will read sequentially — enables
    // aggressive read-ahead which is critical for multi-GB files.
    ::madvise(const_cast<void*>(static_cast<const void*>(ptr_)),
              size_, MADV_SEQUENTIAL);
}

void MappedFile::close() noexcept {
    if (ptr_)     { ::munmap(const_cast<char*>(ptr_), size_); ptr_ = nullptr; }
    if (fd_ != -1){ ::close(fd_);                             fd_ = -1; }
    size_ = 0;
}

#endif  // _WIN32

// ═══════════════════════════════════════════════════════════════════════════
// Common: move semantics + destructor
// ═══════════════════════════════════════════════════════════════════════════

MappedFile::~MappedFile() { close(); }

MappedFile::MappedFile(MappedFile&& other) noexcept
    : ptr_(other.ptr_), size_(other.size_)
#ifdef _WIN32
    , file_handle_(other.file_handle_), mapping_handle_(other.mapping_handle_)
#else
    , fd_(other.fd_)
#endif
{
    other.ptr_  = nullptr;
    other.size_ = 0;
#ifdef _WIN32
    other.file_handle_    = nullptr;
    other.mapping_handle_ = nullptr;
#else
    other.fd_ = -1;
#endif
}

MappedFile& MappedFile::operator=(MappedFile&& other) noexcept {
    if (this != &other) {
        close();
        ptr_  = other.ptr_;
        size_ = other.size_;
#ifdef _WIN32
        file_handle_    = other.file_handle_;
        mapping_handle_ = other.mapping_handle_;
        other.file_handle_    = nullptr;
        other.mapping_handle_ = nullptr;
#else
        fd_ = other.fd_;
        other.fd_ = -1;
#endif
        other.ptr_  = nullptr;
        other.size_ = 0;
    }
    return *this;
}
