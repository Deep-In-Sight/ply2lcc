#include "spatial_grid.hpp"
#include <fstream>
#include <iostream>
#include <cmath>
#include <algorithm>

namespace ply2lcc {

SpatialGrid::SpatialGrid(float cell_size_x, float cell_size_y, const BBox& bbox, size_t num_lods)
    : cell_size_x_(cell_size_x)
    , cell_size_y_(cell_size_y)
    , bbox_(bbox)
    , num_lods_(num_lods)
{
}

uint32_t SpatialGrid::get_cell_index(const Vec3f& pos) const {
    int cell_x = static_cast<int>(std::floor((pos.x - bbox_.min.x) / cell_size_x_));
    int cell_y = static_cast<int>(std::floor((pos.y - bbox_.min.y) / cell_size_y_));

    // Clamp to valid range (16-bit each)
    cell_x = std::max(0, std::min(cell_x, 65535));
    cell_y = std::max(0, std::min(cell_y, 65535));

    return (static_cast<uint32_t>(cell_y) << 16) | static_cast<uint32_t>(cell_x);
}

void SpatialGrid::add_splat(size_t lod, const Vec3f& pos, size_t splat_idx) {
    uint32_t index = get_cell_index(pos);

    auto it = cells_.find(index);
    if (it == cells_.end()) {
        auto result = cells_.emplace(index, GridCell(index, num_lods_));
        result.first->second.splat_indices[lod].push_back(splat_idx);
    } else {
        it->second.splat_indices[lod].push_back(splat_idx);
    }
}

bool SpatialGrid::write_index_bin(const std::string& path,
                                  const std::vector<LCCUnitInfo>& units,
                                  size_t num_lods) const {
    std::ofstream file(path, std::ios::binary);
    if (!file) {
        std::cerr << "Failed to create Index.bin\n";
        return false;
    }

    // Sort units by (cell_x, cell_y) - column first, then row
    // This matches the reference LCC format ordering
    std::vector<LCCUnitInfo> sorted_units = units;
    std::sort(sorted_units.begin(), sorted_units.end(),
              [](const LCCUnitInfo& a, const LCCUnitInfo& b) {
                  uint16_t ax = a.index & 0xFFFF;
                  uint16_t ay = (a.index >> 16) & 0xFFFF;
                  uint16_t bx = b.index & 0xFFFF;
                  uint16_t by = (b.index >> 16) & 0xFFFF;
                  if (ax != bx) return ax < bx;
                  return ay < by;
              });

    // Each unit entry: index(4) + [count(4) + offset(8) + size(4)] * num_lods
    // = 4 + 16 * num_lods bytes per unit

    for (const auto& unit : sorted_units) {
        // Write unit index
        file.write(reinterpret_cast<const char*>(&unit.index), 4);

        // Write LOD entries
        for (size_t lod = 0; lod < num_lods; ++lod) {
            const LCCNodeInfo& node = unit.lods[lod];
            file.write(reinterpret_cast<const char*>(&node.splat_count), 4);
            file.write(reinterpret_cast<const char*>(&node.data_offset), 8);
            file.write(reinterpret_cast<const char*>(&node.data_size), 4);
        }
    }

    return true;
}

} // namespace ply2lcc
