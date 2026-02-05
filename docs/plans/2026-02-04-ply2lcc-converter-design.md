# PLY to LCC Converter Design

## Overview

A C++ converter that transforms 3D Gaussian Splatting PLY files into the LCC (Lixel CyberColor) format for efficient streaming and rendering.

## Requirements

- **Input**: Binary PLY files from 3DGS (point_cloud.ply, point_cloud_1.ply, etc.)
- **Output**: LCC dataset (meta.lcc, Index.bin, Data.bin, Shcoef.bin, optionally Environment.bin)
- **Default mode**: Multi-LOD (auto-detect all point_cloud_N.ply files)
- **Optional**: Single-LOD mode (--single-lod flag, uses LOD0 only)
- **Grid partitioning**: Configurable cell size (default 30×30m)
- **Environment**: Auto-include environment.ply if present

## Architecture

```
ply2lcc/
├── src/
│   ├── main.cpp              # CLI entry point, argument parsing
│   ├── lcc_writer.hpp/cpp    # LCC output (meta, index, data, shcoef)
│   ├── spatial_grid.hpp/cpp  # Grid partitioning logic
│   ├── compression.hpp/cpp   # Scale/rotation/SH encoding
│   └── types.hpp             # Common data structures
├── external/
│   └── miniply/
│       └── miniply.h         # Header-only PLY parser
├── CMakeLists.txt
└── README.md
```

## CLI Interface

```bash
# Multi-LOD (default) - auto-detects point_cloud*.ply
./ply2lcc /path/to/iteration_100/ -o output/

# Single-LOD (LOD0 only)
./ply2lcc /path/to/iteration_100/ -o output/ --single-lod

# Custom cell size
./ply2lcc /path/to/iteration_100/ -o output/ --cell-size 50,50
```

## Data Structures

### PLY Input (62 properties per vertex)

| Property | Type | Count |
|----------|------|-------|
| x, y, z | float | 3 |
| nx, ny, nz | float | 3 |
| f_dc_0, f_dc_1, f_dc_2 | float | 3 |
| f_rest_0 ... f_rest_44 | float | 45 |
| opacity | float | 1 |
| scale_0, scale_1, scale_2 | float | 3 |
| rot_0, rot_1, rot_2, rot_3 | float | 4 (w, x, y, z) |

### In-Memory Splat

```cpp
struct Splat {
    float pos[3];
    float normal[3];
    float f_dc[3];
    float f_rest[45];
    float opacity;
    float scale[3];
    float rot[4];  // w, x, y, z
};
```

### LCC Data.bin (32 bytes/splat)

| Field | Bytes | Type | Encoding |
|-------|-------|------|----------|
| Position | 12 | float32×3 | Direct copy |
| Color | 4 | uint8×4 | RGBA from sigmoid(f_dc)×255, A from sigmoid(opacity)×255 |
| Scale | 6 | uint16×3 | Quantized: (exp(log_scale) - min) / (max - min) × 65535 |
| Rotation | 4 | uint32 | 10-10-10-2 bit quaternion encoding |
| Normal | 6 | uint16×3 | Zeros (unused in 3DGS) |

### LCC Shcoef.bin (64 bytes/splat)

15 SH coefficients × 3 channels = 45 floats → 16 uint32s using 11-10-11 bit packing.

```cpp
// Pack one RGB triplet into uint32
uint32_t pack_11_10_11(float r, float g, float b, float min, float max) {
    uint32_t r_enc = ((r - min) / (max - min)) * 2047;  // 11 bits
    uint32_t g_enc = ((g - min) / (max - min)) * 1023;  // 10 bits
    uint32_t b_enc = ((b - min) / (max - min)) * 2047;  // 11 bits
    return (b_enc << 21) | (g_enc << 11) | r_enc;
}
```

### Quaternion Encoding

```cpp
// LCC uses 10-10-10-2 encoding: xyz in 10 bits each, 2 bits for dropped component index
uint32_t encode_quaternion(float w, float x, float y, float z) {
    float q[4] = {x, y, z, w};  // reorder for encoding
    // Find largest component, drop it (reconstructed as sqrt(1 - sum of squares))
    int max_idx = 0;
    float max_val = 0;
    for (int i = 0; i < 4; i++) {
        if (fabs(q[i]) > max_val) {
            max_val = fabs(q[i]);
            max_idx = i;
        }
    }
    // Encode remaining 3 components in 10 bits each
    // ... (see LCC spec DecodeRotation for exact mapping)
}
```

## Spatial Grid Partitioning

### Grid Cell Structure

```cpp
struct GridCell {
    uint32_t index;  // (cell_y << 16) | cell_x
    std::vector<size_t> splat_indices[MAX_LODS];
};

// Assignment
int cell_x = (pos[0] - bbox.min[0]) / cell_length_x;
int cell_y = (pos[1] - bbox.min[1]) / cell_length_y;
uint32_t index = (cell_y << 16) | cell_x;
```

### Index.bin Layout

Per Unit (grid cell):
```
[index: u32]
[count_lod0: u32][offset_lod0: u64][size_lod0: u32]
[count_lod1: u32][offset_lod1: u64][size_lod1: u32]
...
```

`indexDataSize` = 4 + (totalLevel × 16) bytes

## Processing Pipeline

1. **Scan phase**: Read all PLY headers, count splats, compute global bounding box
2. **Min/max pass**: Stream through all splats to find attribute ranges (scale, SH, opacity)
3. **Grid assignment**: Assign each splat to grid cell based on XY position
4. **Write phase**: For each cell, compress and write splats to Data.bin/Shcoef.bin
5. **Index generation**: Write Index.bin with offsets/sizes per cell per LOD
6. **Metadata**: Generate meta.lcc JSON

## Optimization Phases

| Phase | Focus | Technique |
|-------|-------|-----------|
| Baseline | Correctness | Single-threaded, fstream I/O |
| Phase 1 | I/O speed | Memory-mapped PLY input |
| Phase 2 | CPU parallelism | Thread pool for compression |
| Phase 3 | SIMD | SSE/AVX for quaternion/SH encoding |
| Phase 4 | Write parallelism | Per-cell parallel compression |

## Verification

- Compare Data.bin structure against existing point_cloud_2_lccdata/
- Verify splat counts match between input PLY and output meta.lcc
- Byte-level inspection of compressed attributes

## Dependencies

- **miniply**: https://github.com/vilya/miniply (header-only PLY parser)
- **C++17**: filesystem, optional
- **CMake 3.16+**
