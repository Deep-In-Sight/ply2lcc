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

/// Map region of file into memory (read-only)
/// Returns nullptr on failure
inline void* mmap_read(FileHandle& h, std::size_t offset, std::size_t length) {
    if (!h.valid()) return nullptr;
#ifdef _WIN32
    if (!h.mapping) {
        h.mapping = CreateFileMappingW(h.file, nullptr, PAGE_READONLY, 0, 0, nullptr);
        if (!h.mapping) return nullptr;
    }
    DWORD offset_high = static_cast<DWORD>(offset >> 32);
    DWORD offset_low = static_cast<DWORD>(offset & 0xFFFFFFFF);
    return MapViewOfFile(h.mapping, FILE_MAP_READ, offset_high, offset_low, length);
#else
    void* addr = ::mmap(nullptr, length, PROT_READ, MAP_PRIVATE, h.fd, static_cast<off_t>(offset));
    return (addr == MAP_FAILED) ? nullptr : addr;
#endif
}

/// Unmap previously mapped region
inline void munmap(void* addr, std::size_t length) {
    if (!addr) return;
#ifdef _WIN32
    (void)length;
    UnmapViewOfFile(addr);
#else
    ::munmap(addr, length);
#endif
}

/// Advise kernel about memory access pattern
inline void madvise(void* addr, std::size_t length, AccessHint hint) {
    if (!addr || length == 0) return;
#ifdef _WIN32
    if (hint == AccessHint::Sequential || hint == AccessHint::WillNeed) {
        WIN32_MEMORY_RANGE_ENTRY range;
        range.VirtualAddress = addr;
        range.NumberOfBytes = length;
        PrefetchVirtualMemory(GetCurrentProcess(), 1, &range, 0);
    }
    // Random and DontNeed have no direct Windows equivalent
#else
    int advice = MADV_NORMAL;
    switch (hint) {
        case AccessHint::Sequential: advice = MADV_SEQUENTIAL; break;
        case AccessHint::Random:     advice = MADV_RANDOM; break;
        case AccessHint::WillNeed:   advice = MADV_WILLNEED; break;
        case AccessHint::DontNeed:   advice = MADV_DONTNEED; break;
    }
    ::madvise(addr, length, advice);
#endif
}

/// Open output file stream (takes fs::path only, preventing accidental std::string overload)
inline std::ofstream ofstream_open(const fs::path& path,
                                   std::ios::openmode mode = std::ios::binary) {
    return std::ofstream(path, mode);
}

/// Open input file stream (takes fs::path only, preventing accidental std::string overload)
inline std::ifstream ifstream_open(const fs::path& path,
                                   std::ios::openmode mode = std::ios::binary) {
    return std::ifstream(path, mode);
}

/// Open FILE* with Unicode path support
/// Caller responsible for fclose()
inline FILE* fopen(const fs::path& path, const char* mode) {
#ifdef _WIN32
    wchar_t wmode[8];
    std::mbstowcs(wmode, mode, 8);
    return _wfopen(path.wstring().c_str(), wmode);
#else
    return std::fopen(path.c_str(), mode);
#endif
}

} // namespace platform

#endif // PLY2LCC_PLATFORM_HPP
