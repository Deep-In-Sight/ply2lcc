# Parallel Grid Building Implementation Plan

> **For Claude:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task.

**Goal:** Parallelize grid building with thread-local grids and eliminate the need to store all splats in memory.

**Architecture:** Process each LOD sequentially. Within each LOD, use OpenMP to build thread-local grids in parallel, then merge into global grid. Reopen SplatBuffer during encoding phase for splat access.

**Tech Stack:** C++17, OpenMP, memory-mapped I/O via SplatBuffer

---

### Task 1: Add AttributeRanges::merge() method

**Files:**
- Modify: `src/types.hpp:76-114`
- Test: `tests/test_types.cpp`

**Step 1: Write the failing test**

Add to `tests/test_types.cpp`:

```cpp
TEST(AttributeRangesTest, Merge) {
    AttributeRanges a, b;

    a.expand_scale(Vec3f(1.0f, 2.0f, 3.0f));
    a.expand_opacity(0.5f);
    a.expand_sh(0.1f, 0.2f, 0.3f);

    b.expand_scale(Vec3f(0.5f, 3.0f, 2.0f));
    b.expand_opacity(0.8f);
    b.expand_sh(-0.1f, 0.4f, 0.1f);

    a.merge(b);

    // Scale: min of mins, max of maxes
    EXPECT_FLOAT_EQ(a.scale_min.x, 0.5f);
    EXPECT_FLOAT_EQ(a.scale_min.y, 2.0f);
    EXPECT_FLOAT_EQ(a.scale_min.z, 2.0f);
    EXPECT_FLOAT_EQ(a.scale_max.x, 1.0f);
    EXPECT_FLOAT_EQ(a.scale_max.y, 3.0f);
    EXPECT_FLOAT_EQ(a.scale_max.z, 3.0f);

    // Opacity
    EXPECT_FLOAT_EQ(a.opacity_min, 0.5f);
    EXPECT_FLOAT_EQ(a.opacity_max, 0.8f);

    // SH per channel
    EXPECT_FLOAT_EQ(a.sh_min.x, -0.1f);
    EXPECT_FLOAT_EQ(a.sh_min.y, 0.2f);
    EXPECT_FLOAT_EQ(a.sh_min.z, 0.1f);
    EXPECT_FLOAT_EQ(a.sh_max.x, 0.1f);
    EXPECT_FLOAT_EQ(a.sh_max.y, 0.4f);
    EXPECT_FLOAT_EQ(a.sh_max.z, 0.3f);
}
```

**Step 2: Run test to verify it fails**

Run: `cd build && make test_types && ./test_types --gtest_filter=AttributeRangesTest.Merge`
Expected: FAIL - "no member named 'merge'"

**Step 3: Write minimal implementation**

Add to `AttributeRanges` struct in `src/types.hpp` after `expand_opacity()`:

```cpp
    void merge(const AttributeRanges& other) {
        for (int i = 0; i < 3; ++i) {
            scale_min[i] = std::min(scale_min[i], other.scale_min[i]);
            scale_max[i] = std::max(scale_max[i], other.scale_max[i]);
            sh_min[i] = std::min(sh_min[i], other.sh_min[i]);
            sh_max[i] = std::max(sh_max[i], other.sh_max[i]);
        }
        opacity_min = std::min(opacity_min, other.opacity_min);
        opacity_max = std::max(opacity_max, other.opacity_max);
    }
```

**Step 4: Run test to verify it passes**

Run: `cd build && make test_types && ./test_types --gtest_filter=AttributeRangesTest.Merge`
Expected: PASS

**Step 5: Commit**

```bash
git add src/types.hpp tests/test_types.cpp
git commit -m "feat(types): add AttributeRanges::merge() for thread-local range merging"
```

---

### Task 2: Add ThreadLocalGrid struct

**Files:**
- Modify: `src/types.hpp`
- Test: `tests/test_types.cpp`

**Step 1: Write the failing test**

Add to `tests/test_types.cpp`:

```cpp
TEST(ThreadLocalGridTest, AddAndAccess) {
    ThreadLocalGrid grid;

    grid.cell_indices[0x00010002].push_back(100);
    grid.cell_indices[0x00010002].push_back(200);
    grid.cell_indices[0x00030004].push_back(300);

    EXPECT_EQ(grid.cell_indices.size(), 2u);
    EXPECT_EQ(grid.cell_indices[0x00010002].size(), 2u);
    EXPECT_EQ(grid.cell_indices[0x00010002][0], 100u);
    EXPECT_EQ(grid.cell_indices[0x00030004].size(), 1u);
}
```

**Step 2: Run test to verify it fails**

Run: `cd build && make test_types && ./test_types --gtest_filter=ThreadLocalGridTest.AddAndAccess`
Expected: FAIL - "ThreadLocalGrid not declared"

**Step 3: Write minimal implementation**

Add to `src/types.hpp` after `EncodedCell` struct:

```cpp
struct ThreadLocalGrid {
    std::map<uint32_t, std::vector<size_t>> cell_indices;  // cell_id -> splat indices
    AttributeRanges ranges;
};
```

Add include at top of `src/types.hpp`:

```cpp
#include <map>
```

**Step 4: Run test to verify it passes**

Run: `cd build && make test_types && ./test_types --gtest_filter=ThreadLocalGridTest.AddAndAccess`
Expected: PASS

**Step 5: Commit**

```bash
git add src/types.hpp tests/test_types.cpp
git commit -m "feat(types): add ThreadLocalGrid for parallel grid building"
```

---

### Task 3: Add SpatialGrid::compute_cell_index() method

**Files:**
- Modify: `src/spatial_grid.hpp`
- Modify: `src/spatial_grid.cpp`

**Step 1: Refactor existing code**

The existing `get_cell_index()` already computes the cell index. Rename internal logic to `compute_cell_index()` (public, const) and have `get_cell_index()` call it.

In `src/spatial_grid.hpp`, the method already exists as `get_cell_index()`. Add alias:

```cpp
    // Compute cell index from position (thread-safe, no mutation)
    uint32_t compute_cell_index(const Vec3f& pos) const { return get_cell_index(pos); }
```

**Step 2: Build to verify no regression**

Run: `cd build && make -j$(nproc) && ctest --output-on-failure`
Expected: All tests pass

**Step 3: Commit**

```bash
git add src/spatial_grid.hpp
git commit -m "feat(spatial_grid): add compute_cell_index() alias for thread-safe access"
```

---

### Task 4: Add SpatialGrid::merge() method

**Files:**
- Modify: `src/spatial_grid.hpp`
- Modify: `src/spatial_grid.cpp`
- Test: manual verification via integration test

**Step 1: Add declaration to header**

In `src/spatial_grid.hpp`, add after `add_splat()`:

```cpp
    // Merge a thread-local grid into this grid
    void merge(const ThreadLocalGrid& local, size_t lod);
```

**Step 2: Write implementation**

In `src/spatial_grid.cpp`, add:

```cpp
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
```

**Step 3: Build to verify compilation**

Run: `cd build && make -j$(nproc)`
Expected: Build succeeds

**Step 4: Commit**

```bash
git add src/spatial_grid.hpp src/spatial_grid.cpp
git commit -m "feat(spatial_grid): add merge() for combining thread-local grids"
```

---

### Task 5: Add encode_splat_view() function

**Files:**
- Modify: `src/compression.hpp`
- Modify: `src/compression.cpp`

**Step 1: Add declaration to header**

In `src/compression.hpp`, add after existing `encode_splat()`:

```cpp
// Forward declaration
class SplatView;

// Encode a single splat from SplatView, appending to buffers
void encode_splat_view(const SplatView& sv,
                       std::vector<uint8_t>& data_buf,
                       std::vector<uint8_t>& sh_buf,
                       const AttributeRanges& ranges,
                       bool has_sh);
```

**Step 2: Write implementation**

In `src/compression.cpp`, add include and implementation:

```cpp
#include "splat_buffer.hpp"

void encode_splat_view(const SplatView& sv,
                       std::vector<uint8_t>& data_buf,
                       std::vector<uint8_t>& sh_buf,
                       const AttributeRanges& ranges,
                       bool has_sh) {
    size_t data_offset = data_buf.size();
    data_buf.resize(data_offset + 32);
    uint8_t* data_ptr = data_buf.data() + data_offset;

    // Position (12 bytes)
    const Vec3f& pos = sv.pos();
    std::memcpy(data_ptr, &pos.x, 12);
    data_ptr += 12;

    // Color RGBA (4 bytes)
    const Vec3f& f_dc = sv.f_dc();
    float f_dc_arr[3] = {f_dc.x, f_dc.y, f_dc.z};
    uint32_t color = encode_color(f_dc_arr, sv.opacity());
    std::memcpy(data_ptr, &color, 4);
    data_ptr += 4;

    // Scale (6 bytes)
    uint16_t scale_enc[3];
    encode_scale(sv.scale(), ranges.scale_min, ranges.scale_max, scale_enc);
    std::memcpy(data_ptr, scale_enc, 6);
    data_ptr += 6;

    // Rotation (4 bytes)
    const Quat& rot = sv.rot();
    float rot_arr[4] = {rot.w, rot.x, rot.y, rot.z};
    uint32_t rot_enc = encode_rotation(rot_arr);
    std::memcpy(data_ptr, &rot_enc, 4);
    data_ptr += 4;

    // Normal (6 bytes) - zeros for 3DGS
    uint16_t normal_enc[3] = {0, 0, 0};
    std::memcpy(data_ptr, normal_enc, 6);

    // SH coefficients (64 bytes)
    if (has_sh) {
        size_t sh_offset = sh_buf.size();
        sh_buf.resize(sh_offset + 64);
        uint8_t* sh_ptr = sh_buf.data() + sh_offset;

        // Copy f_rest to array
        float f_rest[45];
        for (int i = 0; i < sv.num_f_rest() && i < 45; ++i) {
            f_rest[i] = sv.f_rest(i);
        }
        for (int i = sv.num_f_rest(); i < 45; ++i) {
            f_rest[i] = 0.0f;
        }

        uint32_t sh_enc[16];
        encode_sh_coefficients(f_rest, ranges.sh_min.x, ranges.sh_max.x, sh_enc);
        std::memcpy(sh_ptr, sh_enc, 64);
    }
}
```

**Step 3: Build to verify compilation**

Run: `cd build && make -j$(nproc)`
Expected: Build succeeds

**Step 4: Commit**

```bash
git add src/compression.hpp src/compression.cpp
git commit -m "feat(compression): add encode_splat_view() for zero-copy encoding"
```

---

### Task 6: Update ConvertApp header for parallel grid building

**Files:**
- Modify: `src/convert_app.hpp`

**Step 1: Update header**

Replace the header contents with:

```cpp
#ifndef PLY2LCC_CONVERT_APP_HPP
#define PLY2LCC_CONVERT_APP_HPP

#include "types.hpp"
#include "lcc_writer.hpp"
#include "spatial_grid.hpp"
#include <string>
#include <vector>
#include <map>
#include <memory>

namespace ply2lcc {

class ConvertApp {
public:
    ConvertApp(int argc, char** argv);
    void run();

private:
    void parseArgs();
    void findPlyFiles();
    void validateOutput();
    void buildSpatialGridParallel();  // Replaces computeBounds + buildSpatialGrid
    void encodeAllLods();
    void writeEncodedData();
    void writeEnvironment();
    void writeIndex();
    void writeMeta();
    void writeAttrs();
    void printUsage();

    int argc_;
    char** argv_;

    // Config
    std::string input_path_;
    std::string input_dir_;
    std::string base_name_;
    std::string output_dir_;
    float cell_size_x_ = 30.0f;
    float cell_size_y_ = 30.0f;
    bool single_lod_ = false;

    // Discovered files
    std::vector<std::string> lod_files_;
    std::string env_file_;
    bool has_env_ = false;

    // Conversion data (no more all_splats_!)
    std::vector<size_t> splats_per_lod_;
    BBox global_bbox_;
    AttributeRanges global_ranges_;
    EnvBounds env_bounds_;
    std::vector<Splat> env_splats_;
    bool has_sh_ = false;
    int sh_degree_ = 0;
    int num_f_rest_ = 0;
    size_t total_splats_ = 0;

    // Spatial grid and parallel encoding state
    std::unique_ptr<SpatialGrid> grid_;
    std::map<uint32_t, std::vector<EncodedCell>> encoded_cells_;
    std::vector<LCCUnitInfo> units_;
};

} // namespace ply2lcc

#endif // PLY2LCC_CONVERT_APP_HPP
```

**Step 2: Build to check for errors (will fail until cpp updated)**

Run: `cd build && make -j$(nproc) 2>&1 | head -20`
Expected: Errors about missing/changed functions (expected)

**Step 3: Commit header change**

```bash
git add src/convert_app.hpp
git commit -m "refactor(convert_app): update header for parallel grid building"
```

---

### Task 7: Implement buildSpatialGridParallel()

**Files:**
- Modify: `src/convert_app.cpp`

**Step 1: Update includes**

At top of `src/convert_app.cpp`, ensure these includes:

```cpp
#include "convert_app.hpp"
#include "splat_buffer.hpp"
#include "meta_writer.hpp"
#include "env_writer.hpp"
#include "attrs_writer.hpp"
#include "compression.hpp"

#include <iostream>
#include <filesystem>
#include <algorithm>
#include <regex>
#include <cmath>
#include <stdexcept>
#include <fstream>
#include <omp.h>
```

**Step 2: Update run() method**

Replace `run()` with:

```cpp
void ConvertApp::run() {
    parseArgs();
    findPlyFiles();
    validateOutput();
    buildSpatialGridParallel();
    encodeAllLods();
    writeEncodedData();
    writeEnvironment();
    writeIndex();
    writeMeta();
    writeAttrs();

    std::cout << "\nConversion complete!\n";
    std::cout << "Total splats: " << total_splats_ << "\n";
    std::cout << "Output: " << output_dir_ << "\n";
}
```

**Step 3: Implement buildSpatialGridParallel()**

Replace `computeBounds()` and `buildSpatialGrid()` with:

```cpp
void ConvertApp::buildSpatialGridParallel() {
    std::cout << "\nPhase 1: Building spatial grid (parallel)...\n";

    // First pass: compute global bbox (needed for grid cell calculation)
    for (size_t lod = 0; lod < lod_files_.size(); ++lod) {
        SplatBuffer buffer;
        if (!buffer.initialize(lod_files_[lod])) {
            throw std::runtime_error("Failed to read " + lod_files_[lod] + ": " + buffer.error());
        }
        global_bbox_.expand(buffer.compute_bbox());

        if (lod == 0) {
            has_sh_ = buffer.num_f_rest() > 0;
            sh_degree_ = buffer.sh_degree();
            num_f_rest_ = buffer.num_f_rest();
        }
    }

    std::cout << "Global bbox: (" << global_bbox_.min.x << ", " << global_bbox_.min.y << ", " << global_bbox_.min.z
              << ") - (" << global_bbox_.max.x << ", " << global_bbox_.max.y << ", " << global_bbox_.max.z << ")\n";

    // Create grid with known bbox
    grid_ = std::make_unique<SpatialGrid>(cell_size_x_, cell_size_y_, global_bbox_, lod_files_.size());

    // Second pass: parallel grid building per LOD
    for (size_t lod = 0; lod < lod_files_.size(); ++lod) {
        std::cout << "  Processing LOD" << lod << ": " << fs::path(lod_files_[lod]).filename().string() << "\n";

        SplatBuffer splats;
        if (!splats.initialize(lod_files_[lod])) {
            throw std::runtime_error("Failed to read " + lod_files_[lod] + ": " + splats.error());
        }

        std::cout << "    " << splats.size() << " splats\n";
        splats_per_lod_.push_back(splats.size());

        int n_threads = omp_get_max_threads();
        std::vector<ThreadLocalGrid> local_grids(n_threads);
        int bands_per_channel = (has_sh_ && num_f_rest_ > 0) ? num_f_rest_ / 3 : 0;

        #pragma omp parallel
        {
            int tid = omp_get_thread_num();

            #pragma omp for schedule(static)
            for (size_t i = 0; i < splats.size(); ++i) {
                SplatView sv = splats[i];
                uint32_t cell_id = grid_->compute_cell_index(sv.pos());

                local_grids[tid].cell_indices[cell_id].push_back(i);

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
            grid_->merge(local_grids[t], lod);
            global_ranges_.merge(local_grids[t].ranges);
        }
    }

    std::cout << "Created " << grid_->get_cells().size() << " grid cells\n";
    std::cout << "SH: " << (has_sh_ ? "degree " + std::to_string(sh_degree_) + " (" + std::to_string(num_f_rest_) + " coefficients)" : "none") << "\n";

    // Read environment if exists
    if (has_env_) {
        if (!EnvWriter::read_environment(env_file_, env_splats_, env_bounds_)) {
            std::cerr << "Warning: Failed to read environment.ply\n";
            has_env_ = false;
        } else {
            std::cout << "  Environment: " << env_splats_.size() << " splats\n";
        }
    }
}
```

**Step 4: Delete old computeBounds() and buildSpatialGrid() functions**

Remove these functions entirely from `src/convert_app.cpp`.

**Step 5: Build to verify compilation**

Run: `cd build && make -j$(nproc)`
Expected: Build succeeds (may have warnings)

**Step 6: Commit**

```bash
git add src/convert_app.cpp
git commit -m "feat(convert_app): implement parallel grid building with thread-local grids"
```

---

### Task 8: Update encodeAllLods() to use SplatBuffer

**Files:**
- Modify: `src/convert_app.cpp`

**Step 1: Replace encodeAllLods()**

```cpp
void ConvertApp::encodeAllLods() {
    std::cout << "\nPhase 2: Encoding splats (parallel)...\n";

    // Prepare cells vector for parallel iteration
    const auto& cells_map = grid_->get_cells();
    std::vector<std::pair<uint32_t, const GridCell*>> cells_vec;
    cells_vec.reserve(cells_map.size());
    for (const auto& [idx, cell] : cells_map) {
        cells_vec.emplace_back(idx, &cell);
        encoded_cells_[idx].resize(lod_files_.size());
    }

    for (size_t lod = 0; lod < lod_files_.size(); ++lod) {
        std::cout << "  Encoding LOD" << lod << "...\n";

        // Reopen SplatBuffer for this LOD
        SplatBuffer splats;
        if (!splats.initialize(lod_files_[lod])) {
            throw std::runtime_error("Failed to reopen " + lod_files_[lod]);
        }

        #pragma omp parallel for schedule(dynamic)
        for (size_t i = 0; i < cells_vec.size(); ++i) {
            uint32_t cell_idx = cells_vec[i].first;
            const GridCell* cell = cells_vec[i].second;

            if (cell->splat_indices[lod].empty()) continue;

            EncodedCell enc;
            enc.data.reserve(cell->splat_indices[lod].size() * 32);
            if (has_sh_) {
                enc.shcoef.reserve(cell->splat_indices[lod].size() * 64);
            }

            for (size_t idx : cell->splat_indices[lod]) {
                SplatView sv = splats[idx];
                encode_splat_view(sv, enc.data, enc.shcoef, global_ranges_, has_sh_);
            }
            enc.count = cell->splat_indices[lod].size();

            #pragma omp critical
            encoded_cells_[cell_idx][lod] = std::move(enc);
        }
    }

    std::cout << "  Encoding complete.\n";
}
```

**Step 2: Build and run full conversion test**

Run: `cd build && make -j$(nproc) && ./ply2lcc -i ../test_data/cheonan/ply/point_cloud/iteration_100/point_cloud.ply -o /tmp/lcc_test`
Expected: Conversion completes successfully

**Step 3: Verify output**

Run: `ls -la /tmp/lcc_test/`
Expected: data.bin, shcoef.bin, index.bin, meta.lcc, attrs.lcp, environment.bin

**Step 4: Commit**

```bash
git add src/convert_app.cpp
git commit -m "feat(convert_app): use SplatBuffer in encoding phase for zero-copy access"
```

---

### Task 9: Run full test suite and verify

**Step 1: Run all tests**

Run: `cd build && ctest --output-on-failure`
Expected: All tests pass

**Step 2: Run conversion and compare output sizes**

Run:
```bash
rm -rf /tmp/lcc_parallel_test
time ./ply2lcc -i ../test_data/cheonan/ply/point_cloud/iteration_100/point_cloud.ply -o /tmp/lcc_parallel_test
echo "---"
ls -la /tmp/lcc_parallel_test/
```
Expected: Similar output sizes to before, potentially faster execution

**Step 3: Commit if any final fixes needed**

```bash
git add -A
git commit -m "test: verify parallel grid building implementation"
```

---

### Task 10: Final cleanup and commit

**Step 1: Remove any dead code**

Check for and remove:
- Any remaining references to `all_splats_`
- Old `computeBounds()` function remnants
- Unused includes

**Step 2: Run final build and test**

Run: `cd build && make -j$(nproc) && ctest --output-on-failure`
Expected: Clean build, all tests pass

**Step 3: Final commit**

```bash
git add -A
git commit -m "refactor: clean up after parallel grid building implementation"
```
