#ifndef PLY2LCC_LCC_WRITER_HPP
#define PLY2LCC_LCC_WRITER_HPP

#include "types.hpp"
#include <string>
#include <vector>
#include <fstream>
#include <map>

namespace ply2lcc {

struct LCCNodeInfo {
    uint32_t splat_count;
    uint64_t data_offset;
    uint32_t data_size;
    uint64_t sh_offset;
    uint32_t sh_size;
};

struct LCCUnitInfo {
    uint32_t index;  // (cell_y << 16) | cell_x
    std::vector<LCCNodeInfo> lods;  // One per LOD level
};

class LCCWriter {
public:
    LCCWriter(const std::string& output_dir,
              const AttributeRanges& ranges,
              size_t num_lods,
              bool has_sh);

    ~LCCWriter();

    // Write splats for a specific cell and LOD
    // Returns true on success
    bool write_splats(uint32_t cell_index,
                      size_t lod,
                      const std::vector<Splat>& splats);

    // Finalize and close all files
    bool finalize();

    // Get unit info for meta.lcc generation
    const std::vector<LCCUnitInfo>& get_units() const { return units_; }

    size_t total_splats() const { return total_splats_; }

private:
    std::string output_dir_;
    AttributeRanges ranges_;
    size_t num_lods_;
    bool has_sh_;

    std::ofstream data_file_;
    std::ofstream sh_file_;

    uint64_t data_offset_ = 0;
    uint64_t sh_offset_ = 0;
    size_t total_splats_ = 0;

    std::map<uint32_t, size_t> unit_index_map_;  // cell_index -> units_ index
    std::vector<LCCUnitInfo> units_;

    // Compression buffers (reused)
    std::vector<uint8_t> data_buffer_;
    std::vector<uint8_t> sh_buffer_;
};

} // namespace ply2lcc

#endif // PLY2LCC_LCC_WRITER_HPP
