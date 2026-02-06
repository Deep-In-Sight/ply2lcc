#include "spatial_grid.hpp"
#include "splat_buffer.hpp"
#include <cmath>
#include <algorithm>
#include <stdexcept>
#include <omp.h>

namespace ply2lcc {

SpatialGrid::SpatialGrid(float cell_size_x, float cell_size_y, size_t num_lods)
    : cell_size_x_(cell_size_x)
    , cell_size_y_(cell_size_y)
    , num_lods_(num_lods)
{
}

SpatialGrid SpatialGrid::from_files(const std::vector<std::string>& lod_files,
                                     float cell_size_x, float cell_size_y) {
    SpatialGrid grid(cell_size_x, cell_size_y, lod_files.size());

    // First pass: compute global bbox (needed for grid cell calculation)
    for (size_t lod = 0; lod < lod_files.size(); ++lod) {
        SplatBuffer buffer;
        if (!buffer.initialize(lod_files[lod])) {
            throw std::runtime_error("Failed to read " + lod_files[lod] + ": " + buffer.error());
        }
        grid.bbox_.expand(buffer.compute_bbox());

        if (lod == 0) {
            grid.has_sh_ = buffer.num_f_rest() > 0;
            grid.sh_degree_ = buffer.sh_degree();
            grid.num_f_rest_ = buffer.num_f_rest();
        }
    }

    // Second pass: parallel grid building per LOD
    int n_threads = omp_get_max_threads();
    int bands_per_channel = (grid.has_sh_ && grid.num_f_rest_ > 0) ? grid.num_f_rest_ / 3 : 0;

    for (size_t lod = 0; lod < lod_files.size(); ++lod) {
        SplatBuffer splats;
        if (!splats.initialize(lod_files[lod])) {
            throw std::runtime_error("Failed to read " + lod_files[lod] + ": " + splats.error());
        }

        std::vector<ThreadLocalGrid> local_grids(n_threads);

        #pragma omp parallel
        {
            int tid = omp_get_thread_num();
            const auto splat_count = static_cast<ptrdiff_t>(splats.size());

            #pragma omp for schedule(static)
            for (ptrdiff_t i = 0; i < splat_count; ++i) {
                SplatView sv = splats[static_cast<size_t>(i)];
                uint32_t cell_id = grid.compute_cell_index(sv.pos());

                local_grids[tid].cell_indices[cell_id].push_back(static_cast<size_t>(i));

                // Expand ranges
                Vec3f linear_scale(std::exp(sv.scale().x), std::exp(sv.scale().y), std::exp(sv.scale().z));
                local_grids[tid].ranges.expand_scale(linear_scale);
                local_grids[tid].ranges.expand_opacity(sigmoid(sv.opacity()));

                if (bands_per_channel > 0) {
                    for (int band = 0; band < bands_per_channel; ++band) {
                        local_grids[tid].ranges.expand_sh(
                            sv.f_rest(band),
                            sv.f_rest(band + bands_per_channel),
                            sv.f_rest(band + 2 * bands_per_channel));
                    }
                }
            }
        }

        // Sequential merge
        for (int t = 0; t < n_threads; ++t) {
            grid.merge(local_grids[t], lod);
            grid.ranges_.merge(local_grids[t].ranges);
        }
    }

    return grid;
}

uint32_t SpatialGrid::compute_cell_index(const Vec3f& pos) const {
    int cell_x = static_cast<int>(std::floor((pos.x - bbox_.min.x) / cell_size_x_));
    int cell_y = static_cast<int>(std::floor((pos.y - bbox_.min.y) / cell_size_y_));

    // Clamp to valid range (16-bit each)
    cell_x = std::max(0, std::min(cell_x, 65535));
    cell_y = std::max(0, std::min(cell_y, 65535));

    return (static_cast<uint32_t>(cell_y) << 16) | static_cast<uint32_t>(cell_x);
}

void SpatialGrid::merge(const ThreadLocalGrid& local, size_t lod) {
    for (const auto& [cell_id, indices] : local.cell_indices) {
        auto it = cells_.find(cell_id);
        if (it == cells_.end()) {
            auto result = cells_.emplace(cell_id, GridCell(cell_id, num_lods_));
            it = result.first;
        }
        auto& target = it->second.splat_indices[lod];
        target.insert(target.end(), indices.begin(), indices.end());
    }
}

} // namespace ply2lcc
