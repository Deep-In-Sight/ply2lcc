#include "grid_encoder.hpp"
#include "splat_buffer.hpp"
#include "compression.hpp"
#include <omp.h>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <stdexcept>

namespace ply2lcc {

void GridEncoder::report_progress(int percent, const std::string& msg) {
    if (progress_cb_) {
        progress_cb_(percent, msg);
    }
}

LccData GridEncoder::encode(const SpatialGrid& grid,
                             const std::vector<std::string>& lod_files) {
    LccData result;
    result.num_lods = grid.num_lods();
    result.bbox = grid.bbox();
    result.ranges = grid.ranges();
    result.has_sh = grid.has_sh();
    result.sh_degree = grid.sh_degree();
    result.cell_size_x = grid.cell_size_x();
    result.cell_size_y = grid.cell_size_y();
    result.splats_per_lod.resize(result.num_lods, 0);

    const auto& cells_map = grid.cells();

    // Prepare cells vector for parallel iteration
    std::vector<std::pair<uint32_t, const GridCell*>> cells_vec;
    cells_vec.reserve(cells_map.size());
    for (const auto& [idx, cell] : cells_map) {
        cells_vec.emplace_back(idx, &cell);
    }

    // Track progress across all LODs and cells
    size_t total_work = cells_vec.size() * result.num_lods;
    size_t processed = 0;

    // Thread-local storage for encoded cells
    int n_threads = omp_get_max_threads();
    std::vector<std::vector<EncodedCellData>> thread_cells(n_threads);

    for (size_t lod = 0; lod < result.num_lods; ++lod) {
        // Open SplatBuffer for this LOD
        SplatBuffer splats;
        if (!splats.initialize(lod_files[lod])) {
            throw std::runtime_error("Failed to read " + lod_files[lod] + ": " + splats.error());
        }

        result.splats_per_lod[lod] = splats.size();

        size_t report_interval = std::max(size_t(1), total_work / 100);
        const auto cells_count = static_cast<ptrdiff_t>(cells_vec.size());

        #pragma omp parallel
        {
            int tid = omp_get_thread_num();
            auto& local_cells = thread_cells[tid];

            #pragma omp for schedule(dynamic)
            for (ptrdiff_t i = 0; i < cells_count; ++i) {
                uint32_t cell_idx = cells_vec[static_cast<size_t>(i)].first;
                const GridCell* cell = cells_vec[static_cast<size_t>(i)].second;

                // Skip empty cells
                if (cell->splat_indices[lod].empty()) {
                    continue;
                }

                EncodedCellData enc(cell_idx, lod);
                enc.data.reserve(cell->splat_indices[lod].size() * 32);
                if (result.has_sh) {
                    enc.shcoef.reserve(cell->splat_indices[lod].size() * 64);
                }

                for (size_t idx : cell->splat_indices[lod]) {
                    SplatView sv = splats[idx];
                    encode_splat_view(sv, enc.data, enc.shcoef, result.ranges, result.has_sh);
                }
                enc.count = cell->splat_indices[lod].size();

                local_cells.push_back(std::move(enc));

                #pragma omp atomic
                processed++;

                // Report progress (only from thread 0 to avoid contention)
                if (tid == 0 && processed % report_interval == 0) {
                    int percent = static_cast<int>(processed * 75 / total_work);
                    report_progress(15 + percent, "Encoding cell " + std::to_string(processed) + "/" + std::to_string(total_work));
                }
            }
        }
    }

    // Merge thread-local cells into result
    size_t total_cells = 0;
    for (const auto& tc : thread_cells) {
        total_cells += tc.size();
    }
    result.cells.reserve(total_cells);
    for (auto& tc : thread_cells) {
        for (auto& cell : tc) {
            result.total_splats += cell.count;
            result.cells.push_back(std::move(cell));
        }
    }

    // Sort cells for sequential write
    result.sort_cells();

    return result;
}

EncodedEnvironment GridEncoder::encode_environment(const std::string& env_path, bool has_sh) {
    EncodedEnvironment result;

    SplatBuffer buffer;
    if (!buffer.initialize(env_path)) {
        return result;  // Empty result on failure
    }

    result.count = buffer.size();

    // Compute bounds from splats
    int num_f_rest = buffer.num_f_rest();
    int bands_per_channel = (num_f_rest > 0) ? num_f_rest / 3 : 0;

    for (size_t i = 0; i < buffer.size(); ++i) {
        SplatView sv = buffer[i];
        result.bounds.expand_pos(sv.pos());

        // Linear scale
        Vec3f linear_scale(std::exp(sv.scale().x), std::exp(sv.scale().y), std::exp(sv.scale().z));
        result.bounds.expand_scale(linear_scale);

        // SH coefficients
        for (int band = 0; band < bands_per_channel; ++band) {
            float r = sv.f_rest(band);
            float g = sv.f_rest(band + bands_per_channel);
            float b = sv.f_rest(band + 2 * bands_per_channel);
            result.bounds.expand_sh(r, g, b);
        }
    }

    // Encode splats
    // Quality mode: 96 bytes per splat (32 data + 64 SH)
    // Portable mode: 32 bytes per splat (data only)
    const size_t bytes_per_splat = has_sh ? 96 : 32;
    result.data.resize(buffer.size() * bytes_per_splat);

    uint8_t* out = result.data.data();

    for (size_t i = 0; i < buffer.size(); ++i) {
        SplatView sv = buffer[i];

        // Position (12 bytes)
        const Vec3f& pos = sv.pos();
        std::memcpy(out, &pos.x, 4);
        std::memcpy(out + 4, &pos.y, 4);
        std::memcpy(out + 8, &pos.z, 4);

        // Color (4 bytes RGBA)
        float f_dc[3] = {sv.f_dc().x, sv.f_dc().y, sv.f_dc().z};
        uint32_t color = encode_color(f_dc, sv.opacity());
        std::memcpy(out + 12, &color, 4);

        // Scale (6 bytes)
        uint16_t scale_encoded[3];
        encode_scale(sv.scale(), result.bounds.scale_min, result.bounds.scale_max, scale_encoded);
        std::memcpy(out + 16, scale_encoded, 6);

        // Rotation (4 bytes)
        float rot[4] = {sv.rot().w, sv.rot().x, sv.rot().y, sv.rot().z};
        uint32_t rot_encoded = encode_rotation(rot);
        std::memcpy(out + 22, &rot_encoded, 4);

        // Normal (6 bytes) - zeros
        std::memset(out + 26, 0, 6);

        // For Quality mode: add 64 bytes of SH coefficients
        if (has_sh) {
            float sh_min_scalar = std::min({result.bounds.sh_min.x, result.bounds.sh_min.y, result.bounds.sh_min.z});
            float sh_max_scalar = std::max({result.bounds.sh_max.x, result.bounds.sh_max.y, result.bounds.sh_max.z});

            float f_rest[45] = {0};
            for (int j = 0; j < num_f_rest && j < 45; ++j) {
                f_rest[j] = sv.f_rest(j);
            }

            uint32_t sh_encoded[16];
            encode_sh_coefficients(f_rest, sh_min_scalar, sh_max_scalar, sh_encoded);
            std::memcpy(out + 32, sh_encoded, 64);
        }

        out += bytes_per_splat;
    }

    return result;
}

} // namespace ply2lcc
