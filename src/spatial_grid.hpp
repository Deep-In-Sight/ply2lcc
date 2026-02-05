#ifndef PLY2LCC_SPATIAL_GRID_HPP
#define PLY2LCC_SPATIAL_GRID_HPP

#include "types.hpp"
#include "lcc_writer.hpp"
#include <vector>
#include <map>

namespace ply2lcc {

class SpatialGrid {
public:
    SpatialGrid(float cell_size_x, float cell_size_y, const BBox& bbox, size_t num_lods);

    // Assign a splat to a grid cell
    uint32_t get_cell_index(const Vec3f& pos) const;

    // Compute cell index from position (thread-safe, no mutation)
    uint32_t compute_cell_index(const Vec3f& pos) const { return get_cell_index(pos); }

    // Merge a thread-local grid into this grid
    void merge(const ThreadLocalGrid& local, size_t lod);

    // Get all cells with their splat indices
    const std::map<uint32_t, GridCell>& get_cells() const { return cells_; }

    // Write Index.bin file
    bool write_index_bin(const std::string& path,
                         const std::vector<LCCUnitInfo>& units,
                         size_t num_lods) const;

private:
    float cell_size_x_;
    float cell_size_y_;
    BBox bbox_;
    size_t num_lods_;
    std::map<uint32_t, GridCell> cells_;
};

} // namespace ply2lcc

#endif // PLY2LCC_SPATIAL_GRID_HPP
