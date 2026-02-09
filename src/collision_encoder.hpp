#ifndef PLY2LCC_COLLISION_ENCODER_HPP
#define PLY2LCC_COLLISION_ENCODER_HPP

#include "lcc_types.hpp"
#include <filesystem>
#include <string>

namespace ply2lcc {

class CollisionEncoder {
public:
    CollisionEncoder() = default;

    // Encode collision mesh from PLY or OBJ file
    // scene_bbox: bounding box from splat cloud (used for cell partitioning)
    CollisionData encode(const std::filesystem::path& mesh_path,
                         float cell_size_x, float cell_size_y,
                         const BBox& scene_bbox);

    void set_log_callback(LogCallback cb) { log_cb_ = std::move(cb); }

private:
    void log(const std::string& msg);

    // Read mesh into vertices and faces (auto-detects format)
    bool read_mesh(const std::filesystem::path& path,
                   std::vector<Vec3f>& vertices,
                   std::vector<Triangle>& faces);

    bool read_ply(const std::filesystem::path& path,
                  std::vector<Vec3f>& vertices,
                  std::vector<Triangle>& faces);

    bool read_obj(const std::filesystem::path& path,
                  std::vector<Vec3f>& vertices,
                  std::vector<Triangle>& faces);

    // Partition mesh by grid cell, remapping vertices per cell
    // Uses scene_bbox for cell assignment (same as splat grid)
    void partition_by_cell(const std::vector<Vec3f>& vertices,
                           const std::vector<Triangle>& faces,
                           float cell_size_x, float cell_size_y,
                           const BBox& scene_bbox,
                           std::vector<CollisionCell>& cells);

    // Build BVH for a cell's triangles
    void build_bvh(CollisionCell& cell);

    LogCallback log_cb_;
};

} // namespace ply2lcc

#endif // PLY2LCC_COLLISION_ENCODER_HPP
