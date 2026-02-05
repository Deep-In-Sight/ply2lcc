#ifndef PLY2LCC_LCC_WRITER_HPP
#define PLY2LCC_LCC_WRITER_HPP

#include <cstdint>
#include <vector>

namespace ply2lcc {

// Per-LOD node information for index.bin
struct LCCNodeInfo {
    uint32_t splat_count = 0;
    uint64_t data_offset = 0;
    uint32_t data_size = 0;
    uint64_t sh_offset = 0;
    uint32_t sh_size = 0;
};

// Per-cell unit information for index.bin
struct LCCUnitInfo {
    uint32_t index;  // (cell_y << 16) | cell_x
    std::vector<LCCNodeInfo> lods;  // One per LOD level
};

} // namespace ply2lcc

#endif // PLY2LCC_LCC_WRITER_HPP
