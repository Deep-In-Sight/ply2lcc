#ifndef PLY2LCC_LCC_TYPES_HPP
#define PLY2LCC_LCC_TYPES_HPP

#include "types.hpp"
#include <cstdint>
#include <vector>
#include <string>

namespace ply2lcc {

// Encoded data for one cell at one LOD level
struct EncodedCellData {
    uint32_t cell_id;               // (cell_y << 16) | cell_x
    size_t lod;                     // LOD level index
    size_t count;                   // Number of splats
    std::vector<uint8_t> data;      // Encoded splat data (32 bytes/splat)
    std::vector<uint8_t> shcoef;    // SH coefficients (64 bytes/splat, optional)

    EncodedCellData() : cell_id(0), lod(0), count(0) {}
    EncodedCellData(uint32_t id, size_t l) : cell_id(id), lod(l), count(0) {}
};

// Environment data (encoded same format as cells)
struct EncodedEnvironment {
    size_t count = 0;
    std::vector<uint8_t> data;
    std::vector<uint8_t> shcoef;
    EnvBounds bounds;

    bool empty() const { return count == 0; }
};

// Per-LOD node information for index.bin
struct LccNodeInfo {
    uint32_t splat_count = 0;
    uint64_t data_offset = 0;
    uint32_t data_size = 0;
    uint64_t sh_offset = 0;
    uint32_t sh_size = 0;
};

// Per-cell unit information for index.bin
struct LccUnitInfo {
    uint32_t index;                 // (cell_y << 16) | cell_x
    std::vector<LccNodeInfo> lods;  // One per LOD level
};

// Complete output data - passed from Encoder to Writer
struct LccData {
    std::vector<EncodedCellData> cells;     // All cells, all LODs
    EncodedEnvironment environment;          // Optional

    // Metadata (computed during encoding)
    size_t num_lods = 0;
    size_t total_splats = 0;
    std::vector<size_t> splats_per_lod;
    BBox bbox;
    AttributeRanges ranges;
    bool has_sh = false;
    int sh_degree = 0;

    // Config values for meta.lcc
    float cell_size_x = 30.0f;
    float cell_size_y = 30.0f;

    // Sort cells by (cell_id, lod) for sequential write
    void sort_cells();

    // Build index units from sorted cells
    std::vector<LccUnitInfo> build_index(uint64_t& data_offset, uint64_t& sh_offset) const;
};

} // namespace ply2lcc

#endif // PLY2LCC_LCC_TYPES_HPP
