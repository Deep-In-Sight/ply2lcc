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

    // Add splat index to the grid
    void add_splat(size_t lod, const Vec3f& pos, size_t splat_idx);

    // Get all cells with their splat indices
    const std::map<uint32_t, GridCell>& get_cells() const { return cells_; }

    // Write Index.bin file
    bool write_index_bin(const std::string& path,
                         const std::vector<LCCUnitInfo>& units,
                         size_t num_lods) const;

    float cell_size_x() const { return cell_size_x_; }
    float cell_size_y() const { return cell_size_y_; }

private:
    float cell_size_x_;
    float cell_size_y_;
    BBox bbox_;
    size_t num_lods_;
    std::map<uint32_t, GridCell> cells_;
};

} // namespace ply2lcc

#endif // PLY2LCC_SPATIAL_GRID_HPP
