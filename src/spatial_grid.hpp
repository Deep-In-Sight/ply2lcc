#ifndef PLY2LCC_SPATIAL_GRID_HPP
#define PLY2LCC_SPATIAL_GRID_HPP

#include "types.hpp"
#include <vector>
#include <map>
#include <string>

namespace ply2lcc {

class SpatialGrid {
public:
    // Factory: builds grid from PLY files, computes bbox and ranges
    static SpatialGrid from_files(const std::vector<std::string>& lod_files,
                                   float cell_size_x, float cell_size_y);

    // Accessors
    const BBox& bbox() const { return bbox_; }
    const AttributeRanges& ranges() const { return ranges_; }
    size_t num_lods() const { return num_lods_; }
    bool has_sh() const { return has_sh_; }
    int sh_degree() const { return sh_degree_; }
    int num_f_rest() const { return num_f_rest_; }
    float cell_size_x() const { return cell_size_x_; }
    float cell_size_y() const { return cell_size_y_; }

    // Cell data for encoding
    const std::map<uint32_t, GridCell>& cells() const { return cells_; }

    // Cell index computation (thread-safe, no mutation)
    uint32_t compute_cell_index(const Vec3f& pos) const;

    // Merge a thread-local grid into this grid
    void merge(const ThreadLocalGrid& local, size_t lod);

private:
    SpatialGrid(float cell_size_x, float cell_size_y, size_t num_lods);
    void set_bbox(const BBox& bbox) { bbox_ = bbox; }

    float cell_size_x_;
    float cell_size_y_;
    BBox bbox_;
    AttributeRanges ranges_;
    size_t num_lods_;
    bool has_sh_ = false;
    int sh_degree_ = 0;
    int num_f_rest_ = 0;
    std::map<uint32_t, GridCell> cells_;
};

} // namespace ply2lcc

#endif // PLY2LCC_SPATIAL_GRID_HPP
