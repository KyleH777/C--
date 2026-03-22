#pragma once

#include <cstddef>
#include <filesystem>
#include <span>
#include <system_error>

/// RAII wrapper around OS memory-mapping primitives.
/// Maps the entire file read-only into the process address space.
/// The mapped region is valid for the lifetime of this object.
class MappedFile {
public:
    explicit MappedFile(const std::filesystem::path& path);
    ~MappedFile();

    MappedFile(const MappedFile&) = delete;
    MappedFile& operator=(const MappedFile&) = delete;
    MappedFile(MappedFile&& other) noexcept;
    MappedFile& operator=(MappedFile&& other) noexcept;

    [[nodiscard]] const char* data() const noexcept { return ptr_; }
    [[nodiscard]] std::size_t size() const noexcept { return size_; }
    [[nodiscard]] std::span<const char> span() const noexcept { return {ptr_, size_}; }

private:
    void close() noexcept;

    const char* ptr_  = nullptr;
    std::size_t size_ = 0;

#ifdef _WIN32
    void* file_handle_    = nullptr;
    void* mapping_handle_ = nullptr;
#else
    int fd_ = -1;
#endif
};
