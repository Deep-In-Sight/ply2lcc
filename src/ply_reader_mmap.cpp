#include "ply_reader_mmap.hpp"
#include <cstring>
#include <iostream>

namespace ply2lcc {

PLYReaderMmap::PLYReaderMmap(const char* filename)
    : miniply::PLYReader(filename)
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

#ifdef _WIN32
    // Use FILE_FLAG_SEQUENTIAL_SCAN for better read-ahead performance
    m_fileHandle = CreateFileA(m_filename.c_str(), GENERIC_READ, FILE_SHARE_READ,
                               nullptr, OPEN_EXISTING,
                               FILE_ATTRIBUTE_NORMAL | FILE_FLAG_SEQUENTIAL_SCAN, nullptr);
    if (m_fileHandle == INVALID_HANDLE_VALUE) {
        std::cerr << "PLYReaderMmap: Failed to open file for mapping\n";
        return nullptr;
    }

    LARGE_INTEGER fileSize;
    if (!GetFileSizeEx(m_fileHandle, &fileSize)) {
        CloseHandle(m_fileHandle);
        m_fileHandle = INVALID_HANDLE_VALUE;
        return nullptr;
    }

    m_mapHandle = CreateFileMappingA(m_fileHandle, nullptr, PAGE_READONLY, 0, 0, nullptr);
    if (m_mapHandle == nullptr) {
        CloseHandle(m_fileHandle);
        m_fileHandle = INVALID_HANDLE_VALUE;
        return nullptr;
    }

    m_mappedBase = static_cast<uint8_t*>(
        MapViewOfFile(m_mapHandle, FILE_MAP_READ, 0, 0, 0));
    if (m_mappedBase == nullptr) {
        CloseHandle(m_mapHandle);
        CloseHandle(m_fileHandle);
        m_mapHandle = nullptr;
        m_fileHandle = INVALID_HANDLE_VALUE;
        return nullptr;
    }

    m_mappedSize = static_cast<size_t>(fileSize.QuadPart);

    // Hint to Windows to prefetch the mapped memory for sequential access
    WIN32_MEMORY_RANGE_ENTRY range;
    range.VirtualAddress = m_mappedBase;
    range.NumberOfBytes = m_mappedSize;
    PrefetchVirtualMemory(GetCurrentProcess(), 1, &range, 0);
#else
    m_fd = open(m_filename.c_str(), O_RDONLY);
    if (m_fd < 0) {
        std::cerr << "PLYReaderMmap: Failed to open file for mapping\n";
        return nullptr;
    }

    struct stat st;
    if (fstat(m_fd, &st) < 0) {
        close(m_fd);
        m_fd = -1;
        return nullptr;
    }

    m_mappedSize = static_cast<size_t>(st.st_size);

    m_mappedBase = static_cast<uint8_t*>(
        mmap(nullptr, m_mappedSize, PROT_READ, MAP_PRIVATE, m_fd, 0));
    if (m_mappedBase == MAP_FAILED) {
        close(m_fd);
        m_fd = -1;
        m_mappedBase = nullptr;
        m_mappedSize = 0;
        return nullptr;
    }

    // Hint to kernel for sequential access pattern
    madvise(m_mappedBase, m_mappedSize, MADV_SEQUENTIAL);
#endif

    // Find "end_header\n" in the file
    const char* marker = "end_header\n";
    const size_t markerLen = 11;
    size_t headerEnd = 0;

    // Search in first 64KB (headers should be much smaller)
    size_t searchLimit = std::min(m_mappedSize, size_t(65536));
    for (size_t i = 0; i + markerLen <= searchLimit; ++i) {
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
    size_t dataOffset = headerEnd;

    // Find which element we're on and sum sizes of previous elements
    uint32_t numElems = num_elements();
    for (uint32_t i = 0; i < numElems; ++i) {
        miniply::PLYElement* e = const_cast<PLYReaderMmap*>(this)->get_element(i);
        if (e == elem) {
            break;  // Found current element
        }
        // Add this element's size to offset
        if (e->fixedSize) {
            dataOffset += static_cast<size_t>(e->rowStride) * e->count;
        } else {
            // Variable size element before us - can't compute offset
            std::cerr << "PLYReaderMmap: Variable-size element before current element\n";
            unmap_element();
            return nullptr;
        }
    }

    // Verify we have enough data
    size_t elementSize = static_cast<size_t>(elem->rowStride) * elem->count;
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
#ifdef _WIN32
    if (m_mappedBase != nullptr) {
        UnmapViewOfFile(m_mappedBase);
        m_mappedBase = nullptr;
    }
    if (m_mapHandle != nullptr) {
        CloseHandle(m_mapHandle);
        m_mapHandle = nullptr;
    }
    if (m_fileHandle != INVALID_HANDLE_VALUE) {
        CloseHandle(m_fileHandle);
        m_fileHandle = INVALID_HANDLE_VALUE;
    }
#else
    if (m_mappedBase != nullptr && m_mappedSize > 0) {
        munmap(m_mappedBase, m_mappedSize);
        m_mappedBase = nullptr;
    }
    if (m_fd >= 0) {
        close(m_fd);
        m_fd = -1;
    }
#endif

    m_mappedData = nullptr;
    m_mappedSize = 0;
}

} // namespace ply2lcc
