# ply2lcc

A high-performance converter for 3D Gaussian Splatting (3DGS) PLY files to LCC format.

## Features

- **Zero-copy PLY reading**: Memory-mapped file access with SplatView for direct data access
- **Parallel grid building**: OpenMP-parallelized spatial partitioning with thread-local grids
- **Multi-LOD support**: Automatic detection and processing of LOD files (point_cloud_lod0.ply, etc.)
- **Environment support**: Separate processing of environment splats (point_cloud_env.ply)
- **SH coefficient encoding**: Full support for spherical harmonic coefficients (degree 3)

## Build

### CLI Only

```bash
mkdir build && cd build
cmake .. -DBUILD_TESTS=ON
make -j$(nproc)
```

### With GUI (requires Qt5 or Qt6)

```bash
mkdir build && cd build
cmake .. -DBUILD_GUI=ON
make -j$(nproc)
```

This builds both the CLI (`ply2lcc`) and GUI (`ply2lcc_gui`) executables.

## Usage

```bash
# Single PLY file
./ply2lcc -i /path/to/point_cloud.ply -o /path/to/output_dir

# LOD directory (auto-detects lod0, lod1, etc.)
./ply2lcc -i /path/to/iteration_30000 -o /path/to/output_dir

# Custom cell size
./ply2lcc -i input.ply -o output -x 50 -y 50

# Single LOD mode (no LOD hierarchy)
./ply2lcc -i input.ply -o output --single-lod
```

### Options

| Option | Description | Default |
|--------|-------------|---------|
| `-i, --input` | Input PLY file or directory | Required |
| `-o, --output` | Output LCC directory | Required |
| `-e <path>` | Path to environment.ply | Auto-detect in input dir |
| `-m <path>` | Path to collision.ply | Auto-detect in input dir |
| `-x, --cell-x` | Grid cell size X (meters) | 30.0 |
| `-y, --cell-y` | Grid cell size Y (meters) | 30.0 |
| `--single-lod` | Treat input as single LOD | false |

## GUI Usage

The GUI provides a user-friendly interface for users unfamiliar with command line tools.

```bash
./ply2lcc_gui
```

### Features

- **File pickers**: Browse for input PLY files and output directory
- **Input filter**: File picker filters for `point_cloud*.ply` files by default
- **Settings panel**:
  - Cell Size X/Y: Grid cell dimensions in meters
  - Single LOD mode: Disable LOD hierarchy
  - Include environment: File picker with path validation (red background if file not found)
  - Include collision: File picker with path validation (red background if file not found)
- **Progress bar**: Real-time conversion progress
- **Log display**: Timestamped conversion messages

When enabling environment or collision, the default path is set to the input directory. The path text box shows a red background if the file doesn't exist.

## Output Files

| File | Description |
|------|-------------|
| `data.bin` | Encoded splat data (32 bytes per splat) |
| `shcoef.bin` | SH coefficients (64 bytes per splat, Quality mode) |
| `index.bin` | Spatial index (cell-to-offset mapping) |
| `meta.lcc` | JSON metadata (bounds, attributes, settings) |
| `attrs.lcp` | Attribute metadata |
| `environment.bin` | Environment splats (if present) |

## Architecture

```
PLY Files (mmap)
     |
     v
SplatBuffer (zero-copy views)
     |
     v
Parallel Grid Building (OpenMP)
  - Thread-local grids
  - Parallel range computation
  - Sequential merge
     |
     v
Parallel Encoding (OpenMP)
  - Position, color, scale, rotation
  - SH coefficients (11-10-11 bit packing)
     |
     v
LCC Output
```

### Key Components

- **SplatBuffer/SplatView**: Zero-copy access to memory-mapped PLY data
- **SpatialGrid**: Spatial partitioning with cell-based indexing
- **ThreadLocalGrid**: Per-thread grid state for parallel building
- **Compression**: Encoding functions (color, scale, rotation, SH)
- **ConvertApp**: Main conversion pipeline orchestration

## Performance

- ~2.6x speedup from parallel grid building
- Memory efficient: No intermediate splat storage during grid building
- Scales with available CPU cores via OpenMP

## Testing

```bash
cd build
ctest --output-on-failure
```

## License

MIT
