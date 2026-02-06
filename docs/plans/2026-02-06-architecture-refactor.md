# Architecture Refactor: Separation of Concerns

**Date:** 2026-02-06
**Status:** Approved

## Problem

Current `ConvertApp` violates single responsibility principle:
- Grid building logic
- Cell encoding logic
- Binary file writing (data.bin, shcoef.bin)
- Scattered writers (meta, attrs, env, index in SpatialGrid)

Additionally:
- `lod_files_.size()` repeated throughout instead of `num_lods` property
- Environment uses `vector<Splat>` instead of encoded format like other data

## Solution

### Architecture

```
┌─────────────────────────────────────────────────────────────────┐
│                        ConvertApp                                │
│  (Orchestrator: CLI parsing, progress reporting, workflow)       │
└─────────────────────────────────────────────────────────────────┘
                              │
         ┌────────────────────┼────────────────────┐
         ▼                    ▼                    ▼
┌─────────────────┐  ┌─────────────────┐  ┌─────────────────┐
│   SpatialGrid   │  │   GridEncoder   │  │   LccWriter     │
│                 │  │                 │  │                 │
│ - Build grid    │  │ - Encode cells  │  │ - Write all     │
│   from PLY      │  │ - Uses ranges   │  │   output files  │
│ - Cell lookup   │  │   from grid     │  │                 │
│ - Range compute │  │ - Return LccData│  │                 │
└─────────────────┘  └─────────────────┘  └─────────────────┘
         │                    │                    │
         └────────────────────┼────────────────────┘
                              ▼
                    ┌─────────────────┐
                    │     LccData     │
                    │                 │
                    │ - EncodedCells  │
                    │ - Environment   │
                    │ - Meta/Attrs    │
                    └─────────────────┘
```

### Class Responsibilities

| Class | Single Responsibility |
|-------|----------------------|
| `ConvertApp` | Orchestration, CLI, progress callbacks |
| `SpatialGrid` | Grid construction from PLY files, cell indexing, range computation |
| `GridEncoder` | Splat encoding using ranges from grid |
| `LccData` | Data container (encoded cells, env, meta) |
| `LccWriter` | File I/O for all LCC output files |

## Data Structures

```cpp
// Encoded data for one cell at one LOD level
struct EncodedCell {
    uint32_t cell_id;           // (cell_y << 16) | cell_x
    size_t lod;                 // LOD level index
    size_t count;               // Number of splats
    std::vector<uint8_t> data;  // Encoded splat data (32 bytes/splat)
    std::vector<uint8_t> shcoef; // SH coefficients (64 bytes/splat, optional)
};

// Environment data (encoded same format as cells)
struct EncodedEnvironment {
    size_t count;
    std::vector<uint8_t> data;
    std::vector<uint8_t> shcoef;
    EnvBounds bounds;
};

// Complete output data - passed from Encoder to Writer
struct LccData {
    std::vector<EncodedCell> cells;  // All cells, all LODs (sorted before write)
    EncodedEnvironment environment;  // Optional

    // Metadata
    size_t num_lods;
    size_t total_splats;
    std::vector<size_t> splats_per_lod;
    BBox bbox;
    AttributeRanges ranges;
    bool has_sh;
    float cell_size_x;
    float cell_size_y;
};
```

### Key Design Decisions

1. **Flat `vector<EncodedCell>`** instead of `map<cell_id, vector<EncodedCell>>`
   - Sorted by (cell_id, lod) before writing
   - Move semantics makes sorting O(n log n) with O(1) swaps
   - Better cache locality for sequential write

2. **Range computation in SpatialGrid** (not Encoder)
   - Ranges discovered during grid building pass
   - Encoder uses ranges for quantization

3. **Environment pre-encoded** as `EncodedEnvironment`
   - Consistent with cell encoding pattern
   - No more `vector<Splat>` intermediate

## Class Interfaces

### SpatialGrid

```cpp
class SpatialGrid {
public:
    static SpatialGrid from_files(const std::vector<std::string>& lod_files,
                                   float cell_size_x, float cell_size_y);

    const BBox& bbox() const;
    const AttributeRanges& ranges() const;
    size_t num_lods() const;
    bool has_sh() const;
    int sh_degree() const;
    const std::map<uint32_t, GridCell>& cells() const;

private:
    SpatialGrid(float cell_size_x, float cell_size_y, size_t num_lods);
    void build_from_files(const std::vector<std::string>& lod_files);
    uint32_t compute_cell_index(const Vec3f& pos) const;
};
```

### GridEncoder

```cpp
class GridEncoder {
public:
    using ProgressCallback = std::function<void(int percent, const std::string&)>;

    void set_progress_callback(ProgressCallback cb);
    LccData encode(const SpatialGrid& grid,
                   const std::vector<std::string>& lod_files);
    EncodedEnvironment encode_environment(const std::string& env_path, bool has_sh);
};
```

### LccWriter

```cpp
class LccWriter {
public:
    explicit LccWriter(const std::string& output_dir);
    void write(const LccData& data);

private:
    void write_data_bin(const std::vector<EncodedCell>& cells, bool has_sh);
    void write_shcoef_bin(const std::vector<EncodedCell>& cells);
    void write_index_bin(const std::vector<EncodedCell>& cells, size_t num_lods);
    void write_environment_bin(const EncodedEnvironment& env, bool has_sh);
    void write_meta_lcc(const LccData& data);
    void write_attrs_lcp();

    std::string output_dir_;
};
```

### Simplified ConvertApp

```cpp
void ConvertApp::run() {
    parse_args();
    find_ply_files();

    // Step 1: Build grid
    SpatialGrid grid = SpatialGrid::from_files(lod_files_,
                                                config_.cell_size_x,
                                                config_.cell_size_y);

    // Step 2: Encode all data
    GridEncoder encoder;
    encoder.set_progress_callback(progress_cb_);
    LccData data = encoder.encode(grid, lod_files_);

    // Step 3: Encode environment (if exists)
    if (!env_file_.empty()) {
        data.environment = encoder.encode_environment(env_file_, grid.has_sh());
    }

    // Step 4: Write output
    LccWriter writer(config_.output_dir);
    writer.write(data);
}
```

## File Structure

### New Organization

```
src/
├── convert_app.hpp/cpp      # Simplified orchestrator
├── spatial_grid.hpp/cpp     # Grid building + ranges (expanded)
├── grid_encoder.hpp/cpp     # NEW: encoding logic
├── lcc_writer.hpp/cpp       # NEW: all file I/O
├── lcc_types.hpp            # NEW: LccData, EncodedCell, etc.
├── splat_buffer.hpp/cpp     # Unchanged
├── compression.hpp/cpp      # Unchanged
├── ply_reader_mmap.hpp/cpp  # Unchanged
└── types.hpp                # Core types
```

### Files to Delete

- `meta_writer.hpp/cpp`
- `attrs_writer.hpp/cpp`
- `env_writer.hpp/cpp`
- `lcc_writer.hpp` (old, just structs)

## Migration Order

1. Create `lcc_types.hpp` with new data structures
2. Create `grid_encoder.hpp/cpp` (extract from convert_app)
3. Create `lcc_writer.hpp/cpp` (consolidate 3 writers + extract from convert_app/spatial_grid)
4. Expand `spatial_grid.hpp/cpp` with `from_files()` factory
5. Simplify `convert_app.cpp` to use new classes
6. Delete old writer files
7. Update CMakeLists.txt

## Testing Strategy

1. **Golden file comparison** - Generate reference output before refactor
2. **Verify checksums match** after each migration step
3. **Existing tests** must continue to pass
4. **New unit tests** for SpatialGrid, GridEncoder, LccWriter

### Verification Checklist

- [ ] Output files identical to before refactor
- [ ] All existing tests pass
- [ ] CLI works with same arguments
- [ ] GUI works unchanged
- [ ] Memory usage similar or better
- [ ] Performance similar or better
