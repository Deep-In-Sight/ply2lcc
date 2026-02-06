# Platform Abstraction Layer Implementation Plan

> **For Claude:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task.

**Goal:** Create a unified cross-platform API for file operations with proper Unicode support.

**Architecture:** Single header `src/platform.hpp` containing `FileHandle`, mmap functions, stream factories, and `madvise`. Migrate existing platform-specific code in `PLYReaderMmap`, `LccWriter`, and `CollisionEncoder` to use this API.

**Tech Stack:** C++17, `std::filesystem::path`, POSIX/Win32 APIs

---

## Task 1: Create platform.hpp with Types and Forward Declarations

**Files:**
- Create: `src/platform.hpp`

**Step 1: Create the header with includes and types**

```cpp
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
```

**Step 2: Build to verify syntax**

Run: `cd /home/linh/3dgs_ws/ply2lcc/.worktrees/platform-abstraction/build && cmake --build . -j$(nproc) 2>&1 | tail -5`
Expected: Build succeeds (header not yet included anywhere)

**Step 3: Commit**

```bash
git add src/platform.hpp
git commit -m "feat(platform): add platform.hpp with FileHandle and AccessHint types"
```

---

## Task 2: Add file_open and file_close Functions

**Files:**
- Modify: `src/platform.hpp`

**Step 1: Add file_open implementation after FileHandle**

Add before the closing `} // namespace platform`:

```cpp
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
```

**Step 2: Build to verify**

Run: `cd /home/linh/3dgs_ws/ply2lcc/.worktrees/platform-abstraction/build && cmake --build . -j$(nproc) 2>&1 | tail -5`
Expected: Build succeeds

**Step 3: Commit**

```bash
git add src/platform.hpp
git commit -m "feat(platform): add file_open and file_close functions"
```

---

## Task 3: Add mmap_read and munmap Functions

**Files:**
- Modify: `src/platform.hpp`

**Step 1: Add mmap_read and munmap after file_close**

```cpp
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
```

**Step 2: Build to verify**

Run: `cd /home/linh/3dgs_ws/ply2lcc/.worktrees/platform-abstraction/build && cmake --build . -j$(nproc) 2>&1 | tail -5`
Expected: Build succeeds

**Step 3: Commit**

```bash
git add src/platform.hpp
git commit -m "feat(platform): add mmap_read and munmap functions"
```

---

## Task 4: Add madvise Function

**Files:**
- Modify: `src/platform.hpp`

**Step 1: Add madvise after munmap**

```cpp
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
```

**Step 2: Build to verify**

Run: `cd /home/linh/3dgs_ws/ply2lcc/.worktrees/platform-abstraction/build && cmake --build . -j$(nproc) 2>&1 | tail -5`
Expected: Build succeeds

**Step 3: Commit**

```bash
git add src/platform.hpp
git commit -m "feat(platform): add madvise function with AccessHint enum"
```

---

## Task 5: Add Stream Factory Functions

**Files:**
- Modify: `src/platform.hpp`

**Step 1: Add stream functions after madvise**

```cpp
/// Open output file stream with Unicode path support
inline std::ofstream ofstream_open(const fs::path& path,
                                   std::ios::openmode mode = std::ios::binary) {
#ifdef _WIN32
    return std::ofstream(path.wstring(), mode);
#else
    return std::ofstream(path, mode);
#endif
}

/// Open input file stream with Unicode path support
inline std::ifstream ifstream_open(const fs::path& path,
                                   std::ios::openmode mode = std::ios::binary) {
#ifdef _WIN32
    return std::ifstream(path.wstring(), mode);
#else
    return std::ifstream(path, mode);
#endif
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
```

**Step 2: Build to verify**

Run: `cd /home/linh/3dgs_ws/ply2lcc/.worktrees/platform-abstraction/build && cmake --build . -j$(nproc) 2>&1 | tail -5`
Expected: Build succeeds

**Step 3: Commit**

```bash
git add src/platform.hpp
git commit -m "feat(platform): add stream factory functions (ofstream_open, ifstream_open, fopen)"
```

---

## Task 6: Write Unit Tests for Platform API

**Files:**
- Create: `tests/test_platform.cpp`
- Modify: `CMakeLists.txt` (add test target)

**Step 1: Create test file**

```cpp
#include <gtest/gtest.h>
#include "platform.hpp"
#include <filesystem>
#include <fstream>

namespace fs = std::filesystem;

class PlatformTest : public ::testing::Test {
protected:
    fs::path test_file;

    void SetUp() override {
        test_file = fs::temp_directory_path() / "platform_test.txt";
        std::ofstream f(test_file);
        f << "Hello, World!";
    }

    void TearDown() override {
        fs::remove(test_file);
    }
};

TEST_F(PlatformTest, FileOpenValid) {
    auto handle = platform::file_open(test_file);
    EXPECT_TRUE(handle.valid());
    EXPECT_EQ(handle.file_size, 13);  // "Hello, World!" = 13 bytes
    platform::file_close(handle);
    EXPECT_FALSE(handle.valid());
}

TEST_F(PlatformTest, FileOpenInvalid) {
    auto handle = platform::file_open("/nonexistent/path/file.txt");
    EXPECT_FALSE(handle.valid());
}

TEST_F(PlatformTest, MmapRead) {
    auto handle = platform::file_open(test_file);
    ASSERT_TRUE(handle.valid());

    void* addr = platform::mmap_read(handle, 0, handle.file_size);
    ASSERT_NE(addr, nullptr);

    // Verify content
    std::string content(static_cast<char*>(addr), handle.file_size);
    EXPECT_EQ(content, "Hello, World!");

    platform::munmap(addr, handle.file_size);
    platform::file_close(handle);
}

TEST_F(PlatformTest, MadviseDoesNotCrash) {
    auto handle = platform::file_open(test_file);
    ASSERT_TRUE(handle.valid());

    void* addr = platform::mmap_read(handle, 0, handle.file_size);
    ASSERT_NE(addr, nullptr);

    // Should not crash
    platform::madvise(addr, handle.file_size, platform::AccessHint::Sequential);
    platform::madvise(addr, handle.file_size, platform::AccessHint::Random);
    platform::madvise(addr, handle.file_size, platform::AccessHint::WillNeed);
    platform::madvise(addr, handle.file_size, platform::AccessHint::DontNeed);

    platform::munmap(addr, handle.file_size);
    platform::file_close(handle);
}

TEST_F(PlatformTest, OfstreamOpen) {
    fs::path out_file = fs::temp_directory_path() / "platform_out.txt";
    {
        auto stream = platform::ofstream_open(out_file);
        EXPECT_TRUE(stream.is_open());
        stream << "Test output";
    }
    // Verify file was written
    std::ifstream in(out_file);
    std::string content;
    std::getline(in, content);
    EXPECT_EQ(content, "Test output");
    fs::remove(out_file);
}

TEST_F(PlatformTest, IfstreamOpen) {
    auto stream = platform::ifstream_open(test_file);
    EXPECT_TRUE(stream.is_open());
    std::string content;
    std::getline(stream, content);
    EXPECT_EQ(content, "Hello, World!");
}

TEST_F(PlatformTest, Fopen) {
    FILE* f = platform::fopen(test_file, "r");
    ASSERT_NE(f, nullptr);
    char buf[20];
    fgets(buf, sizeof(buf), f);
    EXPECT_STREQ(buf, "Hello, World!");
    fclose(f);
}
```

**Step 2: Add test target to CMakeLists.txt**

After line 90 (`target_link_libraries(test_integration ply2lcc_lib GTest::gtest_main)`), add:

```cmake
    add_executable(test_platform tests/test_platform.cpp)
    target_include_directories(test_platform PRIVATE ${CMAKE_SOURCE_DIR}/src)
    target_link_libraries(test_platform GTest::gtest_main)
    gtest_discover_tests(test_platform)
```

**Step 3: Build and run tests**

Run: `cd /home/linh/3dgs_ws/ply2lcc/.worktrees/platform-abstraction/build && cmake .. -DBUILD_TESTS=ON && cmake --build . -j$(nproc) && ctest -R test_platform --output-on-failure`
Expected: All platform tests pass

**Step 4: Commit**

```bash
git add tests/test_platform.cpp CMakeLists.txt
git commit -m "test(platform): add unit tests for platform API"
```

---

## Task 7: Migrate PLYReaderMmap to Use Platform API

**Files:**
- Modify: `src/ply_reader_mmap.hpp`
- Modify: `src/ply_reader_mmap.cpp`

**Step 1: Update ply_reader_mmap.hpp**

Replace entire file with:

```cpp
#ifndef PLY2LCC_PLY_READER_MMAP_HPP
#define PLY2LCC_PLY_READER_MMAP_HPP

#include "miniply/miniply.h"
#include "platform.hpp"
#include <cstdint>
#include <filesystem>

namespace ply2lcc {

/// Extended PLY reader with memory mapping support for direct element access.
/// Only supports binary little-endian PLY files with fixed-size elements.
class PLYReaderMmap : public miniply::PLYReader {
public:
    explicit PLYReaderMmap(const std::filesystem::path& filename);
    ~PLYReaderMmap();

    // Disable copy
    PLYReaderMmap(const PLYReaderMmap&) = delete;
    PLYReaderMmap& operator=(const PLYReaderMmap&) = delete;

    /// Memory-map the current element's data for direct access.
    /// Returns nullptr if:
    /// - The element has variable-size (list) properties
    /// - The file is not binary little-endian
    /// - Mapping fails
    /// @param[out] rowStride  Bytes per row in the mapped data
    /// @param[out] numRows    Number of rows in the element
    const uint8_t* map_element(uint32_t* rowStride, uint32_t* numRows);

    /// Unmap previously mapped element data.
    void unmap_element();

    /// Check if element is currently mapped
    bool is_mapped() const { return m_mappedData != nullptr; }

private:
    std::filesystem::path m_filename;
    platform::FileHandle m_handle;
    uint8_t* m_mappedBase = nullptr;  // Base address of mmap (for unmapping)
    uint8_t* m_mappedData = nullptr;  // Pointer to element data within mapping
    std::size_t m_mappedSize = 0;     // Total mapped size
};

} // namespace ply2lcc

#endif // PLY2LCC_PLY_READER_MMAP_HPP
```

**Step 2: Update ply_reader_mmap.cpp**

Replace entire file with:

```cpp
#include "ply_reader_mmap.hpp"
#include <cstring>
#include <iostream>
#include <algorithm>

namespace ply2lcc {

PLYReaderMmap::PLYReaderMmap(const std::filesystem::path& filename)
    : miniply::PLYReader(filename.string().c_str())
    , m_filename(filename)
{
}

PLYReaderMmap::~PLYReaderMmap()
{
    unmap_element();
}

const uint8_t* PLYReaderMmap::map_element(uint32_t* rowStride, uint32_t* numRows)
{
    // Clean up any previous mapping
    unmap_element();

    if (!valid() || !has_element()) {
        std::cerr << "PLYReaderMmap: No valid element to map\n";
        return nullptr;
    }

    // Only support binary little-endian
    if (file_type() != miniply::PLYFileType::Binary) {
        std::cerr << "PLYReaderMmap: Only binary little-endian format is supported\n";
        return nullptr;
    }

    const miniply::PLYElement* elem = element();
    if (!elem->fixedSize) {
        std::cerr << "PLYReaderMmap: Cannot map variable-size (list) elements\n";
        return nullptr;
    }

    // Open file for mapping
    m_handle = platform::file_open(m_filename);
    if (!m_handle.valid()) {
        std::cerr << "PLYReaderMmap: Failed to open file for mapping\n";
        return nullptr;
    }

    m_mappedSize = m_handle.file_size;

    // Map entire file
    m_mappedBase = static_cast<uint8_t*>(platform::mmap_read(m_handle, 0, m_mappedSize));
    if (m_mappedBase == nullptr) {
        platform::file_close(m_handle);
        m_mappedSize = 0;
        return nullptr;
    }

    // Hint for sequential access
    platform::madvise(m_mappedBase, m_mappedSize, platform::AccessHint::Sequential);

    // Find "end_header\n" in the file
    const char* marker = "end_header\n";
    const std::size_t markerLen = 11;
    std::size_t headerEnd = 0;

    // Search in first 64KB (headers should be much smaller)
    std::size_t searchLimit = std::min(m_mappedSize, std::size_t(65536));
    for (std::size_t i = 0; i + markerLen <= searchLimit; ++i) {
        if (memcmp(m_mappedBase + i, marker, markerLen) == 0) {
            headerEnd = i + markerLen;
            break;
        }
    }

    if (headerEnd == 0) {
        std::cerr << "PLYReaderMmap: Could not find end_header marker\n";
        unmap_element();
        return nullptr;
    }

    // Compute offset to current element by summing previous elements' sizes
    std::size_t dataOffset = headerEnd;

    // Find which element we're on and sum sizes of previous elements
    uint32_t numElems = num_elements();
    for (uint32_t i = 0; i < numElems; ++i) {
        miniply::PLYElement* e = const_cast<PLYReaderMmap*>(this)->get_element(i);
        if (e == elem) {
            break;  // Found current element
        }
        // Add this element's size to offset
        if (e->fixedSize) {
            dataOffset += static_cast<std::size_t>(e->rowStride) * e->count;
        } else {
            // Variable size element before us - can't compute offset
            std::cerr << "PLYReaderMmap: Variable-size element before current element\n";
            unmap_element();
            return nullptr;
        }
    }

    // Verify we have enough data
    std::size_t elementSize = static_cast<std::size_t>(elem->rowStride) * elem->count;
    if (dataOffset + elementSize > m_mappedSize) {
        std::cerr << "PLYReaderMmap: Element data extends beyond file\n";
        unmap_element();
        return nullptr;
    }

    m_mappedData = m_mappedBase + dataOffset;

    if (rowStride) *rowStride = elem->rowStride;
    if (numRows) *numRows = elem->count;

    return m_mappedData;
}

void PLYReaderMmap::unmap_element()
{
    if (m_mappedBase != nullptr && m_mappedSize > 0) {
        platform::munmap(m_mappedBase, m_mappedSize);
        m_mappedBase = nullptr;
    }

    platform::file_close(m_handle);

    m_mappedData = nullptr;
    m_mappedSize = 0;
}

} // namespace ply2lcc
```

**Step 3: Build and run all tests**

Run: `cd /home/linh/3dgs_ws/ply2lcc/.worktrees/platform-abstraction/build && cmake --build . -j$(nproc) && ctest --output-on-failure`
Expected: All tests pass

**Step 4: Commit**

```bash
git add src/ply_reader_mmap.hpp src/ply_reader_mmap.cpp
git commit -m "refactor(ply_reader_mmap): migrate to platform API"
```

---

## Task 8: Migrate LccWriter to Use Platform API

**Files:**
- Modify: `src/lcc_writer.cpp`

**Step 1: Update includes and replace std::ofstream calls**

At the top, add include:
```cpp
#include "platform.hpp"
```

Replace each `std::ofstream` construction with `platform::ofstream_open`:

- Line 30: `std::ofstream file(output_dir_ + "/environment.bin", std::ios::binary);`
  → `auto file = platform::ofstream_open(fs::path(output_dir_) / "environment.bin");`

- Line 40: `std::ofstream file(output_dir_ + "/collision.lci", std::ios::binary);`
  → `auto file = platform::ofstream_open(fs::path(output_dir_) / "collision.lci");`

- Line 137: `std::ofstream data_file(output_dir_ + "/data.bin", std::ios::binary);`
  → `auto data_file = platform::ofstream_open(fs::path(output_dir_) / "data.bin");`

- Line 144: `sh_file.open(output_dir_ + "/shcoef.bin", std::ios::binary);`
  → `sh_file = platform::ofstream_open(fs::path(output_dir_) / "shcoef.bin");`

- Line 162: `std::ofstream file(output_dir_ + "/index.bin", std::ios::binary);`
  → `auto file = platform::ofstream_open(fs::path(output_dir_) / "index.bin");`

- Line 200: `std::ofstream file(output_dir_ + "/meta.lcc");`
  → `auto file = platform::ofstream_open(fs::path(output_dir_) / "meta.lcc", std::ios::out);`

- Line 336: `std::ofstream file(output_dir_ + "/attrs.lcp");`
  → `auto file = platform::ofstream_open(fs::path(output_dir_) / "attrs.lcp", std::ios::out);`

**Step 2: Build and run tests**

Run: `cd /home/linh/3dgs_ws/ply2lcc/.worktrees/platform-abstraction/build && cmake --build . -j$(nproc) && ctest --output-on-failure`
Expected: All tests pass

**Step 3: Commit**

```bash
git add src/lcc_writer.cpp
git commit -m "refactor(lcc_writer): migrate to platform::ofstream_open for Unicode support"
```

---

## Task 9: Migrate CollisionEncoder to Use Platform API

**Files:**
- Modify: `src/collision_encoder.cpp`

**Step 1: Update includes and replace std::ifstream**

At the top, add include:
```cpp
#include "platform.hpp"
```

Line 48: Replace `std::ifstream file(path);`
With: `auto file = platform::ifstream_open(path, std::ios::in);`

**Step 2: Build and run tests**

Run: `cd /home/linh/3dgs_ws/ply2lcc/.worktrees/platform-abstraction/build && cmake --build . -j$(nproc) && ctest --output-on-failure`
Expected: All tests pass

**Step 3: Commit**

```bash
git add src/collision_encoder.cpp
git commit -m "refactor(collision_encoder): migrate to platform::ifstream_open for Unicode support"
```

---

## Task 10: Run Full Test Suite and Verify

**Step 1: Clean rebuild and run all tests**

Run: `cd /home/linh/3dgs_ws/ply2lcc/.worktrees/platform-abstraction/build && rm -rf * && cmake .. -DBUILD_TESTS=ON && cmake --build . -j$(nproc) && ctest --output-on-failure`
Expected: All tests pass (30+ tests)

**Step 2: Verify binary works**

Run: `./ply2lcc --help`
Expected: Shows usage information

**Step 3: Final commit if any cleanup needed**

If tests pass and no changes needed, proceed to next task.

---

## Task 11: Update convert_app.cpp to Accept fs::path

**Files:**
- Modify: `src/convert_app.hpp`
- Modify: `src/convert_app.cpp`

**Step 1: Check if convert_app needs updates**

Read convert_app.hpp and convert_app.cpp to see if they pass paths to PLYReaderMmap or other components. If they use `std::string` paths, update the interface to accept `std::filesystem::path`.

**Step 2: Update as needed and test**

Run: `cd /home/linh/3dgs_ws/ply2lcc/.worktrees/platform-abstraction/build && cmake --build . -j$(nproc) && ctest --output-on-failure`

**Step 3: Commit if changes made**

```bash
git add src/convert_app.hpp src/convert_app.cpp
git commit -m "refactor(convert_app): use std::filesystem::path for path arguments"
```

---

## Summary

After completing all tasks:
- `src/platform.hpp` provides unified cross-platform API
- All `#ifdef _WIN32` blocks consolidated into `platform.hpp`
- Unicode paths supported via `std::filesystem::path`
- All existing tests pass
- Code is cleaner and more maintainable
