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

} // namespace platform

#endif // PLY2LCC_PLATFORM_HPP
