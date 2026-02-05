# CLI Refactor Design

> **For Claude:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task.

**Goal:** Refactor ply2lcc to use flexible input/output paths and clean ConvertApp class structure.

**Architecture:** Extract all conversion logic from main() into ConvertApp class with clear phase methods. Simplify CLI to take direct PLY file path instead of directory with iteration folder logic.

**Tech Stack:** C++17, std::filesystem

---

## New CLI Interface

```bash
./ply2lcc -i /path/to/scene.ply -o /path/to/output/ [options]

Options:
  --single-lod       Use only LOD0 even if more LOD files exist
  --cell-size X,Y    Grid cell size in meters (default: 30,30)
```

## File Discovery

Given `-i /path/to/scene.ply`:
1. Extract directory: `/path/to/`
2. Extract base name: `scene`
3. Look for:
   - `scene.ply` (LOD0, required)
   - `scene_1.ply`, `scene_2.ply`, ... (sorted, stop at first gap)
   - `environment.ply` (optional)

## Output

Writes directly to output directory (no LCC_Results subfolder):
- `data.bin`
- `shcoef.bin`
- `index.bin`
- `environment.bin`
- `meta.lcc`
- `attrs.lcp`

## Logging

```
Input: /path/to/scene.ply
Found 3 LOD levels:
  LOD0: scene.ply
  LOD1: scene_1.ply
  LOD2: scene_2.ply
Found environment.ply
Warning: Output directory is not empty: /path/to/output/
```

With `--single-lod`:
```
Found 3 LOD levels:
  LOD0: scene.ply
  LOD1: scene_1.ply (skipped: --single-lod)
  LOD2: scene_2.ply (skipped: --single-lod)
```

No environment:
```
Warning: environment.ply not found
```

## ConvertApp Class

```cpp
// convert_app.hpp
class ConvertApp {
public:
    ConvertApp(int argc, char** argv);
    void run();

private:
    void parseArgs();
    void findPlyFiles();
    void validateOutput();
    void computeBounds();
    void buildSpatialGrid();
    void writeLccData();
    void writeEnvironment();
    void writeIndex();
    void writeMeta();
    void writeAttrs();

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

    // Conversion data
    std::vector<std::vector<Splat>> all_splats_;
    BBox global_bbox_;
    AttributeRanges global_ranges_;
    std::vector<size_t> splats_per_lod_;
    bool has_sh_ = false;
    int sh_degree_ = 0;
    int num_f_rest_ = 0;
};
```

## main.cpp

```cpp
int main(int argc, char** argv) {
    try {
        ConvertApp app(argc, argv);
        app.run();
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
}
```

## Error Handling

| Condition | Action |
|-----------|--------|
| Missing `-i` or `-o` | throw with usage |
| Input file doesn't exist | throw |
| Output dir not empty | warning, continue |
| No `scene_*.ply` files | fine, just use LOD0 |
| No environment.ply | warning, continue |

## File Changes

**New:**
- `src/convert_app.hpp`
- `src/convert_app.cpp`

**Modified:**
- `src/main.cpp` - minimal main() only
- `CMakeLists.txt` - add convert_app.cpp

**Removed:**
- `src/path_resolution.hpp`
- `src/path_resolution.cpp`
- `tests/test_path_resolution.cpp`
