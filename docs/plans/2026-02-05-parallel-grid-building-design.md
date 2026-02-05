# Parallel Grid Building Design

## Overview

Replace the current two-phase approach (materialize all splats → build grid) with a parallel single-pass design that:
- Builds grid in parallel using thread-local grids
- Computes attribute ranges during the same pass
- Avoids storing all splats in memory
- Re-opens SplatBuffer during encoding for splat access

## Data Flow

```
Phase 1: Parallel Grid Building (per LOD)
┌─────────────────────────────────────────────────────┐
│  SplatBuffer splats(lod_file)  // memory-mapped     │
│                                                     │
│  #pragma omp parallel                               │
│  ├─ thread 0: local_grid_0, local_ranges_0         │
│  ├─ thread 1: local_grid_1, local_ranges_1         │
│  └─ thread N: local_grid_N, local_ranges_N         │
│                                                     │
│  Sequential merge into global_grid, global_ranges   │
└─────────────────────────────────────────────────────┘

Phase 2: Parallel Encoding (per LOD)
┌─────────────────────────────────────────────────────┐
│  SplatBuffer splats(lod_file)  // reopen            │
│                                                     │
│  #pragma omp parallel for (cells)                   │
│  └─ encode_splat_view(splats[idx], global_ranges)  │
└─────────────────────────────────────────────────────┘

Phase 3: Sequential Write
┌─────────────────────────────────────────────────────┐
│  Write encoded_cells to data.bin, shcoef.bin        │
└─────────────────────────────────────────────────────┘
```

## Design Decisions

| Decision | Choice | Rationale |
|----------|--------|-----------|
| Attribute ranges | Compute during grid building | Single pass, merge thread-local ranges |
| Merge strategy | Simple append | Sequential merge is fast, complexity not worth it |
| Splat access for encoding | Re-open SplatBuffer | Clean separation, mmap is cached by OS |
| Parallelization | OpenMP auto (`schedule(static)`) | Already using OpenMP, handles division automatically |

## New Data Structures

### ThreadLocalGrid

Lightweight wrapper for per-thread accumulation:

```cpp
struct ThreadLocalGrid {
    std::map<uint32_t, std::vector<size_t>> cell_indices;  // cell_id -> splat indices
    AttributeRanges ranges;

    void add_splat(size_t lod, const Vec3f& pos, size_t idx,
                   const SplatView& sv, int bands_per_channel);
};
```

### Changes to SpatialGrid

```cpp
class SpatialGrid {
public:
    // Existing methods...

    // New: merge a thread-local grid
    void merge(const ThreadLocalGrid& local, size_t lod);

    // New: get cell index without adding (for thread-local use)
    uint32_t compute_cell_index(const Vec3f& pos) const;
};
```

### Changes to AttributeRanges

```cpp
struct AttributeRanges {
    // Existing fields...

    // New: merge another range
    void merge(const AttributeRanges& other);

    // New: expand from SplatView
    void expand_from_splat(const SplatView& sv, int bands_per_channel);
};
```

## Algorithm Implementation

### Phase 1: Parallel Grid Building

```cpp
void ConvertApp::buildSpatialGridParallel() {
    grid_ = std::make_unique<SpatialGrid>(cell_size_x_, cell_size_y_,
                                           global_bbox_, lod_files_.size());

    for (size_t lod = 0; lod < lod_files_.size(); ++lod) {
        SplatBuffer splats;
        splats.initialize(lod_files_[lod]);

        int n_threads = omp_get_max_threads();
        std::vector<ThreadLocalGrid> local_grids(n_threads);
        int bands_per_channel = has_sh_ ? num_f_rest_ / 3 : 0;

        #pragma omp parallel
        {
            int tid = omp_get_thread_num();

            #pragma omp for schedule(static)
            for (size_t i = 0; i < splats.size(); ++i) {
                SplatView sv = splats[i];
                uint32_t cell_id = grid_->compute_cell_index(sv.pos());

                local_grids[tid].cell_indices[cell_id].push_back(i);
                local_grids[tid].ranges.expand_from_splat(sv, bands_per_channel);
            }
        }

        // Sequential merge
        for (int t = 0; t < n_threads; ++t) {
            grid_->merge(local_grids[t], lod);
            global_ranges_.merge(local_grids[t].ranges);
        }

        splats_per_lod_.push_back(splats.size());
    }
}
```

### Phase 2: Parallel Encoding (Updated)

```cpp
void ConvertApp::encodeAllLods() {
    // Prepare cells vector for parallel iteration
    const auto& cells_map = grid_->get_cells();
    std::vector<std::pair<uint32_t, const GridCell*>> cells_vec;
    for (const auto& [idx, cell] : cells_map) {
        cells_vec.emplace_back(idx, &cell);
        encoded_cells_[idx].resize(lod_files_.size());
    }

    for (size_t lod = 0; lod < lod_files_.size(); ++lod) {
        // Reopen SplatBuffer for this LOD
        SplatBuffer splats;
        splats.initialize(lod_files_[lod]);

        #pragma omp parallel for schedule(dynamic)
        for (size_t i = 0; i < cells_vec.size(); ++i) {
            uint32_t cell_idx = cells_vec[i].first;
            const GridCell* cell = cells_vec[i].second;

            if (cell->splat_indices[lod].empty()) continue;

            EncodedCell enc;
            for (size_t idx : cell->splat_indices[lod]) {
                SplatView sv = splats[idx];  // Zero-copy access
                encode_splat_view(sv, enc.data, enc.shcoef,
                                  global_ranges_, has_sh_);
            }
            enc.count = cell->splat_indices[lod].size();

            #pragma omp critical
            encoded_cells_[cell_idx][lod] = std::move(enc);
        }
    }
}
```

## Helper Functions

### encode_splat_view

```cpp
void encode_splat_view(const SplatView& sv,
                       std::vector<uint8_t>& data_buf,
                       std::vector<uint8_t>& sh_buf,
                       const AttributeRanges& ranges,
                       bool has_sh);
```

### AttributeRanges::merge

```cpp
void AttributeRanges::merge(const AttributeRanges& other) {
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

### AttributeRanges::expand_from_splat

```cpp
void AttributeRanges::expand_from_splat(const SplatView& sv, int bands_per_channel) {
    Vec3f linear_scale(std::exp(sv.scale().x),
                       std::exp(sv.scale().y),
                       std::exp(sv.scale().z));
    expand_scale(linear_scale);
    expand_opacity(sigmoid(sv.opacity()));

    if (bands_per_channel > 0) {
        for (int band = 0; band < bands_per_channel; ++band) {
            expand_sh(sv.f_rest(band),
                      sv.f_rest(band + bands_per_channel),
                      sv.f_rest(band + 2 * bands_per_channel));
        }
    }
}
```

## Removals

- `all_splats_` member variable from `ConvertApp`
- `computeBounds()` function — merged into `buildSpatialGridParallel()`
- `encode_splat(const Splat&, ...)` — replaced by `encode_splat_view()`

## File Changes Summary

| File | Changes |
|------|---------|
| `src/types.hpp` | Add `ThreadLocalGrid`, `AttributeRanges::merge()`, `AttributeRanges::expand_from_splat()` |
| `src/spatial_grid.hpp/cpp` | Add `compute_cell_index()`, `merge(ThreadLocalGrid&, lod)` |
| `src/compression.hpp/cpp` | Replace `encode_splat()` with `encode_splat_view()` |
| `src/convert_app.hpp` | Remove `all_splats_`, remove `computeBounds()` declaration |
| `src/convert_app.cpp` | Replace `computeBounds()` + `buildSpatialGrid()` with `buildSpatialGridParallel()`, update `encodeAllLods()` |

## Expected Benefits

| Metric | Before | After |
|--------|--------|-------|
| Peak memory | ~15 GB (all splats) | ~500 MB (grid + encoded cells) |
| Grid building | Sequential | Parallel (Nx speedup) |
| File reads | 1x per LOD | 2x per LOD (but mmap cached) |
