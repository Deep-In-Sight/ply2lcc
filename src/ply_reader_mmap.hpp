#ifndef PLY2LCC_PLY_READER_MMAP_HPP
#define PLY2LCC_PLY_READER_MMAP_HPP

#include "miniply/miniply.h"
#include <cstdint>
#include <string>

#ifdef _WIN32
#include <windows.h>
#else
#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#endif

namespace ply2lcc {

/// Extended PLY reader with memory mapping support for direct element access.
/// Only supports binary little-endian PLY files with fixed-size elements.
class PLYReaderMmap : public miniply::PLYReader {
public:
    explicit PLYReaderMmap(const char* filename);
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
    std::string m_filename;
    uint8_t* m_mappedBase = nullptr;  // Base address of mmap (for unmapping)
    uint8_t* m_mappedData = nullptr;  // Pointer to element data within mapping
    size_t m_mappedSize = 0;          // Total mapped size

#ifdef _WIN32
    HANDLE m_fileHandle = INVALID_HANDLE_VALUE;
    HANDLE m_mapHandle = nullptr;
#else
    int m_fd = -1;
#endif
};

} // namespace ply2lcc

#endif // PLY2LCC_PLY_READER_MMAP_HPP
