# Platform Abstraction Layer Design

**Date**: 2026-02-06
**Status**: Approved

## Problem

The codebase has scattered `#ifdef _WIN32` checks and poor Unicode path handling:
- [ply_reader_mmap.hpp](../../src/ply_reader_mmap.hpp) / [ply_reader_mmap.cpp](../../src/ply_reader_mmap.cpp): Platform-specific mmap, file handles, prefetch hints
- [external/miniply/miniply.cpp](../../external/miniply/miniply.cpp): `fopen_s` vs `fopen`, `_fseeki64` vs `fseeko`
- [lcc_writer.cpp](../../src/lcc_writer.cpp), [collision_encoder.cpp](../../src/collision_encoder.cpp): `std::ofstream`/`std::ifstream` with `std::string` paths (fails on Windows for non-ASCII)

## Solution

A single header `src/platform.hpp` providing a platform-agnostic API under `namespace platform`.

## Design Decisions

| Decision | Choice | Rationale |
|----------|--------|-----------|
| File location | Single header `src/platform.hpp` | Small implementations, inline-friendly |
| Path type | `std::filesystem::path` | C++17 handles UTF-8 ↔ UTF-16 conversion automatically |
| mmap API | Separate handle + free functions | Explicit control, matches original request |
| Stream API | Factory functions returning std types | Simple, familiar, user manages cleanup |
| Prefetch hints | Separate `madvise()` function with enum | Explicit control over access patterns |

## API Reference

### Types

```cpp
namespace platform {
namespace fs = std::filesystem;

struct FileHandle {
#ifdef _WIN32
    HANDLE file = INVALID_HANDLE_VALUE;
    HANDLE mapping = nullptr;
#else
    int fd = -1;
#endif
    std::size_t file_size = 0;

    bool valid() const;
};

enum class AccessHint { Sequential, Random, WillNeed, DontNeed };
```

### Memory Mapping Functions

```cpp
// Open file for memory mapping (read-only)
FileHandle file_open(const fs::path& path);

// Close file handle and release resources
void file_close(FileHandle& handle);

// Map region of file into memory (read-only)
// Returns nullptr on failure
void* mmap_read(FileHandle& handle, std::size_t offset, std::size_t length);

// Unmap previously mapped region
void munmap(void* addr, std::size_t length);

// Advise kernel about memory access pattern
void madvise(void* addr, std::size_t length, AccessHint hint);
```

### Stream Factory Functions

```cpp
// Open output file stream with Unicode path support
std::ofstream ofstream_open(const fs::path& path,
                            std::ios::openmode mode = std::ios::binary);

// Open input file stream with Unicode path support
std::ifstream ifstream_open(const fs::path& path,
                            std::ios::openmode mode = std::ios::binary);

// Open FILE* with Unicode path support
// Caller responsible for fclose()
FILE* fopen(const fs::path& path, const char* mode);
```

## Implementation

### file_open / file_close

```cpp
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

inline void file_close(FileHandle& h) {
#ifdef _WIN32
    if (h.mapping) { CloseHandle(h.mapping); h.mapping = nullptr; }
    if (h.file != INVALID_HANDLE_VALUE) { CloseHandle(h.file); h.file = INVALID_HANDLE_VALUE; }
#else
    if (h.fd >= 0) { ::close(h.fd); h.fd = -1; }
#endif
    h.file_size = 0;
}
```

### mmap_read / munmap

```cpp
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
    void* addr = ::mmap(nullptr, length, PROT_READ, MAP_PRIVATE, h.fd, offset);
    return (addr == MAP_FAILED) ? nullptr : addr;
#endif
}

inline void munmap(void* addr, std::size_t length) {
    if (!addr) return;
#ifdef _WIN32
    (void)length;
    UnmapViewOfFile(addr);
#else
    ::munmap(addr, length);
#endif
}
```

### madvise

```cpp
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
```

### Stream Functions

```cpp
inline std::ofstream ofstream_open(const fs::path& path,
                                   std::ios::openmode mode = std::ios::binary) {
#ifdef _WIN32
    return std::ofstream(path.wstring(), mode);
#else
    return std::ofstream(path, mode);
#endif
}

inline std::ifstream ifstream_open(const fs::path& path,
                                   std::ios::openmode mode = std::ios::binary) {
#ifdef _WIN32
    return std::ifstream(path.wstring(), mode);
#else
    return std::ifstream(path, mode);
#endif
}

inline FILE* fopen(const fs::path& path, const char* mode) {
#ifdef _WIN32
    wchar_t wmode[8];
    std::mbstowcs(wmode, mode, 8);
    return _wfopen(path.wstring().c_str(), wmode);
#else
    return std::fopen(path.c_str(), mode);
#endif
}
```

## Migration Plan

1. Create `src/platform.hpp` with full implementation
2. Update `PLYReaderMmap` to use `platform::FileHandle`, `platform::mmap_read`, etc.
3. Update `LccWriter` to use `platform::ofstream_open`
4. Update `CollisionEncoder` to use `platform::ifstream_open`
5. Update miniply's `file_open` to use `platform::fopen` (or leave as-is since it's external)

## Notes

- Windows `PrefetchVirtualMemory` is best-effort, similar to POSIX `madvise`
- `AccessHint::Random` and `AccessHint::DontNeed` are no-ops on Windows (no equivalent)
- `mmap_read` is explicitly read-only; add `mmap_write` later if needed
