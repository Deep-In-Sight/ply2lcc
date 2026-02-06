#ifndef PLY2LCC_PLATFORM_HPP
#define PLY2LCC_PLATFORM_HPP

#include <filesystem>
#include <fstream>
#include <cstdio>
#include <cstdint>
#include <cstddef>

#ifdef _WIN32
#include <windows.h>
#else
#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#endif

namespace platform {
namespace fs = std::filesystem;

/// File handle for memory mapping operations
struct FileHandle {
#ifdef _WIN32
    HANDLE file = INVALID_HANDLE_VALUE;
    HANDLE mapping = nullptr;
#else
    int fd = -1;
#endif
    std::size_t file_size = 0;

    bool valid() const {
#ifdef _WIN32
        return file != INVALID_HANDLE_VALUE;
#else
        return fd >= 0;
#endif
    }
};

/// Memory access pattern hints
enum class AccessHint { Sequential, Random, WillNeed, DontNeed };

/// Open file for memory mapping (read-only)
inline FileHandle file_open(const fs::path& path) {
    FileHandle h;
#ifdef _WIN32
    h.file = CreateFileW(path.wstring().c_str(), GENERIC_READ, FILE_SHARE_READ,
                         nullptr, OPEN_EXISTING,
                         FILE_ATTRIBUTE_NORMAL | FILE_FLAG_SEQUENTIAL_SCAN, nullptr);
    if (h.file != INVALID_HANDLE_VALUE) {
        LARGE_INTEGER size;
        if (GetFileSizeEx(h.file, &size)) {
            h.file_size = static_cast<std::size_t>(size.QuadPart);
        }
    }
#else
    h.fd = ::open(path.c_str(), O_RDONLY);
    if (h.fd >= 0) {
        struct stat st;
        if (::fstat(h.fd, &st) == 0) {
            h.file_size = static_cast<std::size_t>(st.st_size);
        }
    }
#endif
    return h;
}

/// Close file handle and release resources
inline void file_close(FileHandle& h) {
#ifdef _WIN32
    if (h.mapping) { CloseHandle(h.mapping); h.mapping = nullptr; }
    if (h.file != INVALID_HANDLE_VALUE) { CloseHandle(h.file); h.file = INVALID_HANDLE_VALUE; }
#else
    if (h.fd >= 0) { ::close(h.fd); h.fd = -1; }
#endif
    h.file_size = 0;
}

} // namespace platform

#endif // PLY2LCC_PLATFORM_HPP
