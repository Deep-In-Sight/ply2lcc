#include "lcc_types.hpp"
#include <algorithm>

namespace ply2lcc {

void LccData::sort_cells() {
    std::sort(cells.begin(), cells.end(), [](const EncodedCellData& a, const EncodedCellData& b) {
        // Sort by cell_x first (column), then cell_y (row), then LOD
        uint16_t ax = a.cell_id & 0xFFFF;
        uint16_t ay = (a.cell_id >> 16) & 0xFFFF;
        uint16_t bx = b.cell_id & 0xFFFF;
        uint16_t by = (b.cell_id >> 16) & 0xFFFF;
        if (ax != bx) return ax < bx;
        if (ay != by) return ay < by;
        return a.lod < b.lod;
    });
}

std::vector<LccUnitInfo> LccData::build_index(uint64_t& data_offset, uint64_t& sh_offset) const {
    std::vector<LccUnitInfo> units;

    uint32_t current_cell_id = UINT32_MAX;
    LccUnitInfo* current_unit = nullptr;

    for (const auto& cell : cells) {
        if (cell.count == 0) continue;

        if (cell.cell_id != current_cell_id) {
            units.emplace_back();
            current_unit = &units.back();
            current_unit->index = cell.cell_id;
            current_unit->lods.resize(num_lods);
            current_cell_id = cell.cell_id;
        }

        LccNodeInfo& node = current_unit->lods[cell.lod];
        node.splat_count = static_cast<uint32_t>(cell.count);
        node.data_offset = data_offset;
        node.data_size = static_cast<uint32_t>(cell.data.size());
        data_offset += cell.data.size();

        if (has_sh && !cell.shcoef.empty()) {
            node.sh_offset = sh_offset;
            node.sh_size = static_cast<uint32_t>(cell.shcoef.size());
            sh_offset += cell.shcoef.size();
        }
    }

    return units;
}

} // namespace ply2lcc
