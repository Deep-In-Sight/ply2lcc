# Input/Output Structure Design

## Overview

Modify the ply2lcc converter to enforce a standardized input/output directory structure with automatic path resolution and validation.

## Requirements

**Input structure:**
```
input_dir/
└── point_cloud/
    └── iteration_<N>/
        └── point_cloud.ply, point_cloud_1.ply, ...
```

**Output structure:**
```
output_dir/
└── LCC_Results/
    ├── meta.lcc
    ├── Data.bin
    ├── Index.bin
    └── Shcoef.bin (if SH present)
```

## Behavior

### Path Resolution

1. User passes top-level `input_dir` and `-o output_dir`
2. Tool validates `input_dir/point_cloud/` exists
3. Tool scans for `iteration_*` directories and selects highest iteration number
4. Tool verifies PLY files exist in selected iteration
5. Tool writes all output to `output_dir/LCC_Results/`

### Validation Errors

```
Error: input_dir/point_cloud/ directory not found
Error: No iteration_* directories found in input_dir/point_cloud/
Error: No point_cloud*.ply files found in input_dir/point_cloud/iteration_30000/
```

### Informational Output

```
Input: /path/to/scene_dir
  Using: point_cloud/iteration_30000/
Output: /path/to/output/LCC_Results/
```

## Code Changes

### src/main.cpp

- Add `resolve_input_path()` function to find `point_cloud/iteration_*/`
- Add `find_highest_iteration()` helper with regex for `iteration_(\d+)`
- Change output path from `config.output_dir` to `config.output_dir + "/LCC_Results"`
- Update usage message to reflect new structure expectation

### src/types.hpp

- Add `resolved_input_dir` field to `ConvertConfig` (stores the actual iteration path)

### tests/test_integration.cpp

- Update test paths to use `test_data/scene_ply/point_cloud/iteration_100/`
- Update `getReferenceLccPath()` to look in `test_data/scene_lcc/LCC_Results/`

### No changes needed

- `ply_reader.hpp/cpp` - Still receives direct path to PLY files
- `lcc_writer.hpp/cpp` - Still receives output directory path
- `compression.hpp/cpp` - Unchanged
- `spatial_grid.hpp/cpp` - Unchanged
- `meta_writer.hpp/cpp` - Unchanged

## New Unit Tests

- `PathResolutionTest.FindHighestIteration` - Verifies `iteration_30000` selected over `iteration_7000`
- `PathResolutionTest.MissingPointCloudDir` - Errors correctly when `point_cloud/` missing
- `PathResolutionTest.NoIterationDirs` - Errors when no `iteration_*` found
- `PathResolutionTest.EmptyIteration` - Errors when iteration dir has no PLY files

## Test Data Restructure

```
test_data/
├── scene_ply/
│   └── point_cloud/
│       └── iteration_100/
│           └── *.ply
└── scene_lcc/
    └── LCC_Results/
        ├── meta.lcc
        └── *.bin
```
