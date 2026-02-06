#ifndef PLY2LCC_GRID_ENCODER_HPP
#define PLY2LCC_GRID_ENCODER_HPP

#include "lcc_types.hpp"
#include "spatial_grid.hpp"
#include <string>
#include <vector>
#include <filesystem>
#include <functional>

namespace ply2lcc {

class GridEncoder {
public:
    using ProgressCallback = std::function<void(int percent, const std::string&)>;

    void set_progress_callback(ProgressCallback cb) { progress_cb_ = std::move(cb); }

    // Encode all cells from grid, returns complete LccData
    LccData encode(const SpatialGrid& grid,
                   const std::vector<std::filesystem::path>& lod_files);

    // Encode environment PLY file
    EncodedEnvironment encode_environment(const std::filesystem::path& env_path, bool has_sh);

private:
    void report_progress(int percent, const std::string& msg);

    ProgressCallback progress_cb_;
};

} // namespace ply2lcc

#endif // PLY2LCC_GRID_ENCODER_HPP
