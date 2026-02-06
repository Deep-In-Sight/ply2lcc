#include "ply_reader_mmap.hpp"
#include <cstring>
#include <iostream>
#include <algorithm>

namespace ply2lcc {

PLYReaderMmap::PLYReaderMmap(const std::filesystem::path& filename)
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
