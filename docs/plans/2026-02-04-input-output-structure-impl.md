# Input/Output Structure Implementation Plan

> **For Claude:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task.

**Goal:** Enforce standardized input/output directory structure with auto-detection of highest iteration folder.

**Architecture:** Add path resolution functions to validate input structure (`point_cloud/iteration_<N>/`) and auto-detect highest iteration. Output always goes to `output_dir/LCC_Results/`. Validation fails early with clear error messages.

**Tech Stack:** C++17 filesystem, regex, gtest

---

### Task 1: Add path resolution unit tests

**Files:**
- Create: `tests/test_path_resolution.cpp`
- Modify: `CMakeLists.txt:45-55` (add new test file)

**Step 1: Create test file with test cases**

Create `tests/test_path_resolution.cpp`:

```cpp
#include <gtest/gtest.h>
#include <filesystem>
#include "path_resolution.hpp"

namespace fs = std::filesystem;
using namespace ply2lcc;

class PathResolutionTest : public ::testing::Test {
protected:
    std::string temp_dir_;

    void SetUp() override {
        temp_dir_ = "/tmp/path_resolution_test_" + std::to_string(std::time(nullptr));
        fs::create_directories(temp_dir_);
    }

    void TearDown() override {
        fs::remove_all(temp_dir_);
    }

    void create_structure(const std::vector<std::string>& paths) {
        for (const auto& p : paths) {
            fs::create_directories(temp_dir_ + "/" + p);
        }
    }

    void create_file(const std::string& path) {
        std::ofstream f(temp_dir_ + "/" + path);
        f << "dummy";
    }
};

TEST_F(PathResolutionTest, FindHighestIteration) {
    create_structure({
        "point_cloud/iteration_100",
        "point_cloud/iteration_7000",
        "point_cloud/iteration_30000"
    });
    create_file("point_cloud/iteration_30000/point_cloud.ply");

    auto result = resolve_input_path(temp_dir_);
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->iteration_number, 30000);
    EXPECT_TRUE(result->path.find("iteration_30000") != std::string::npos);
}

TEST_F(PathResolutionTest, SingleIteration) {
    create_structure({"point_cloud/iteration_100"});
    create_file("point_cloud/iteration_100/point_cloud.ply");

    auto result = resolve_input_path(temp_dir_);
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->iteration_number, 100);
}

TEST_F(PathResolutionTest, MissingPointCloudDir) {
    // No point_cloud directory
    auto result = resolve_input_path(temp_dir_);
    EXPECT_FALSE(result.has_value());
    EXPECT_TRUE(result.error().find("point_cloud") != std::string::npos);
}

TEST_F(PathResolutionTest, NoIterationDirs) {
    create_structure({"point_cloud"});
    // point_cloud exists but no iteration_* inside

    auto result = resolve_input_path(temp_dir_);
    EXPECT_FALSE(result.has_value());
    EXPECT_TRUE(result.error().find("iteration") != std::string::npos);
}

TEST_F(PathResolutionTest, EmptyIteration) {
    create_structure({"point_cloud/iteration_100"});
    // iteration_100 exists but no PLY files

    auto result = resolve_input_path(temp_dir_);
    EXPECT_FALSE(result.has_value());
    EXPECT_TRUE(result.error().find("ply") != std::string::npos ||
                result.error().find("PLY") != std::string::npos);
}

TEST_F(PathResolutionTest, IgnoresNonIterationDirs) {
    create_structure({
        "point_cloud/iteration_100",
        "point_cloud/backup",
        "point_cloud/old_iteration_500"
    });
    create_file("point_cloud/iteration_100/point_cloud.ply");

    auto result = resolve_input_path(temp_dir_);
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->iteration_number, 100);
}
```

**Step 2: Add test to CMakeLists.txt**

Modify `CMakeLists.txt` to add the new test file to the test sources list. Find the `set(TEST_SOURCES ...)` block and add `tests/test_path_resolution.cpp`.

**Step 3: Run tests to verify they fail**

Run: `cd /home/linh/3dgs_ws/ply2lcc/build && cmake .. && make -j4 2>&1 | head -50`
Expected: FAIL with "path_resolution.hpp: No such file or directory"

**Step 4: Commit test file**

```bash
git add tests/test_path_resolution.cpp CMakeLists.txt
git commit -m "test: add path resolution unit tests (failing)"
```

---

### Task 2: Implement path resolution module

**Files:**
- Create: `src/path_resolution.hpp`
- Create: `src/path_resolution.cpp`
- Modify: `CMakeLists.txt` (add to lib sources)

**Step 1: Create path_resolution.hpp**

Create `src/path_resolution.hpp`:

```cpp
#ifndef PATH_RESOLUTION_HPP
#define PATH_RESOLUTION_HPP

#include <string>
#include <optional>
#include <variant>

namespace ply2lcc {

struct ResolvedPath {
    std::string path;           // Full path to iteration directory
    int iteration_number;       // The iteration number (e.g., 30000)
};

struct PathError {
    std::string message;
};

class PathResult {
public:
    PathResult(ResolvedPath path) : result_(std::move(path)) {}
    PathResult(PathError error) : result_(std::move(error)) {}

    bool has_value() const { return std::holds_alternative<ResolvedPath>(result_); }
    const ResolvedPath& operator*() const { return std::get<ResolvedPath>(result_); }
    const ResolvedPath* operator->() const { return &std::get<ResolvedPath>(result_); }
    std::string error() const { return std::get<PathError>(result_).message; }

private:
    std::variant<ResolvedPath, PathError> result_;
};

// Resolves input_dir to the actual iteration directory
// Returns the path to highest iteration_<N> directory containing PLY files
// Returns error if structure is invalid
PathResult resolve_input_path(const std::string& input_dir);

// Creates output directory structure: output_dir/LCC_Results/
// Returns the full path to LCC_Results directory
std::string resolve_output_path(const std::string& output_dir);

}  // namespace ply2lcc

#endif  // PATH_RESOLUTION_HPP
```

**Step 2: Create path_resolution.cpp**

Create `src/path_resolution.cpp`:

```cpp
#include "path_resolution.hpp"
#include <filesystem>
#include <regex>
#include <vector>
#include <algorithm>

namespace fs = std::filesystem;

namespace ply2lcc {

PathResult resolve_input_path(const std::string& input_dir) {
    // Check point_cloud directory exists
    std::string point_cloud_dir = input_dir + "/point_cloud";
    if (!fs::exists(point_cloud_dir) || !fs::is_directory(point_cloud_dir)) {
        return PathError{"point_cloud/ directory not found in " + input_dir};
    }

    // Find all iteration_* directories
    std::regex iter_pattern("iteration_(\\d+)");
    std::vector<std::pair<int, std::string>> iterations;

    for (const auto& entry : fs::directory_iterator(point_cloud_dir)) {
        if (!entry.is_directory()) continue;

        std::string dirname = entry.path().filename().string();
        std::smatch match;
        if (std::regex_match(dirname, match, iter_pattern)) {
            int num = std::stoi(match[1].str());
            iterations.emplace_back(num, entry.path().string());
        }
    }

    if (iterations.empty()) {
        return PathError{"No iteration_* directories found in " + point_cloud_dir};
    }

    // Sort by iteration number descending and find highest with PLY files
    std::sort(iterations.begin(), iterations.end(),
              [](const auto& a, const auto& b) { return a.first > b.first; });

    for (const auto& [iter_num, iter_path] : iterations) {
        // Check for point_cloud*.ply files
        bool has_ply = false;
        for (const auto& entry : fs::directory_iterator(iter_path)) {
            std::string filename = entry.path().filename().string();
            if (entry.path().extension() == ".ply" &&
                filename.find("point_cloud") == 0) {
                has_ply = true;
                break;
            }
        }

        if (has_ply) {
            return ResolvedPath{iter_path, iter_num};
        }
    }

    return PathError{"No point_cloud*.ply files found in any iteration directory"};
}

std::string resolve_output_path(const std::string& output_dir) {
    std::string lcc_results = output_dir + "/LCC_Results";
    fs::create_directories(lcc_results);
    return lcc_results;
}

}  // namespace ply2lcc
```

**Step 3: Add to CMakeLists.txt**

Add `src/path_resolution.cpp` to the library sources in CMakeLists.txt.

**Step 4: Build and run tests**

Run: `cd /home/linh/3dgs_ws/ply2lcc/build && cmake .. && make -j4 && ctest -R PathResolution -V`
Expected: All PathResolutionTest tests PASS

**Step 5: Commit implementation**

```bash
git add src/path_resolution.hpp src/path_resolution.cpp CMakeLists.txt
git commit -m "feat: implement path resolution for input/output structure"
```

---

### Task 3: Update main.cpp to use path resolution

**Files:**
- Modify: `src/main.cpp`

**Step 1: Update includes and usage message**

At top of `main.cpp`, add:
```cpp
#include "path_resolution.hpp"
```

Update `print_usage()`:
```cpp
void print_usage(const char* prog) {
    std::cerr << "Usage: " << prog << " <input_dir> -o <output_dir> [options]\n"
              << "\n"
              << "Expected input structure:\n"
              << "  input_dir/point_cloud/iteration_<N>/*.ply\n"
              << "\n"
              << "Output structure:\n"
              << "  output_dir/LCC_Results/{meta.lcc, Data.bin, Index.bin, ...}\n"
              << "\n"
              << "Options:\n"
              << "  --single-lod       Use only LOD0 (default: multi-LOD)\n"
              << "  --cell-size X,Y    Grid cell size in meters (default: 30,30)\n";
}
```

**Step 2: Replace path logic in main()**

After argument parsing (around line 84), replace the path validation and PLY finding with:

```cpp
    // Resolve input path
    auto input_result = resolve_input_path(config.input_dir);
    if (!input_result.has_value()) {
        std::cerr << "Error: " << input_result.error() << "\n";
        return 1;
    }

    std::string iteration_dir = input_result->path;
    std::cout << "Input: " << config.input_dir << "\n"
              << "  Using: point_cloud/iteration_" << input_result->iteration_number << "/\n";

    // Resolve output path
    std::string lcc_output_dir = resolve_output_path(config.output_dir);
    std::cout << "Output: " << lcc_output_dir << "\n"
              << "Mode: " << (config.single_lod ? "single-lod" : "multi-lod") << "\n"
              << "Cell size: " << config.cell_size_x << " x " << config.cell_size_y << "\n";

    // Find PLY files in resolved iteration directory
    auto ply_files = find_lod_files(iteration_dir, config.single_lod);
    if (ply_files.empty()) {
        std::cerr << "No point_cloud*.ply files found in " << iteration_dir << "\n";
        return 1;
    }
```

**Step 3: Update all output path references**

Replace all occurrences of `config.output_dir` with `lcc_output_dir`:

- Line ~173: `fs::create_directories(config.output_dir)` → remove (already created)
- Line ~175: `LCCWriter writer(config.output_dir, ...)` → `LCCWriter writer(lcc_output_dir, ...)`
- Line ~196: `grid.write_index_bin(config.output_dir + "/Index.bin", ...)` → `grid.write_index_bin(lcc_output_dir + "/Index.bin", ...)`
- Line ~213: `MetaWriter::write(config.output_dir + "/meta.lcc", ...)` → `MetaWriter::write(lcc_output_dir + "/meta.lcc", ...)`
- Line ~218: `config.output_dir` in output messages → `lcc_output_dir`

**Step 4: Update environment.ply path**

Change environment check to use iteration_dir:
```cpp
    std::string env_path = iteration_dir + "/environment.ply";
```

**Step 5: Build and run existing tests**

Run: `cd /home/linh/3dgs_ws/ply2lcc/build && make -j4 && ctest --output-on-failure`
Expected: Unit tests PASS, integration tests may fail (will fix in next task)

**Step 6: Commit changes**

```bash
git add src/main.cpp
git commit -m "feat: use path resolution in main.cpp"
```

---

### Task 4: Update integration tests for new structure

**Files:**
- Modify: `tests/test_integration.cpp`

**Step 1: Update SetUp() to use new structure**

The test data is already in the correct structure:
- `test_data/scene_ply/point_cloud/iteration_100/*.ply`
- `test_data/scene_lcc/LCC_Results/*.bin`

Update the `SetUp()` method to point to the parent directory:

```cpp
    void SetUp() override {
        std::vector<std::string> base_paths = {
            "../test_data",
            "test_data",
            "../../test_data"
        };

        for (const auto& base : base_paths) {
            if (fs::exists(base + "/scene_ply/point_cloud")) {
                test_data_ply_ = base + "/scene_ply";  // Parent dir, not iteration dir
                test_data_lcc_ = base + "/scene_lcc";
                break;
            }
        }

        if (test_data_ply_.empty() || !fs::exists(test_data_ply_ + "/point_cloud")) {
            GTEST_SKIP() << "Test data not available. Expected structure: "
                         << "test_data/scene_ply/point_cloud/iteration_*/";
        }
    }
```

**Step 2: Update getTestPlyPath() to use path resolution**

```cpp
    std::string getTestPlyPath() {
        auto result = resolve_input_path(test_data_ply_);
        if (!result.has_value()) {
            return "";
        }

        // Return path to point_cloud.ply in resolved iteration dir
        std::string ply_path = result->path + "/point_cloud.ply";
        if (fs::exists(ply_path)) {
            return ply_path;
        }

        // Fallback: any point_cloud*.ply
        for (const auto& entry : fs::directory_iterator(result->path)) {
            if (entry.path().extension() == ".ply" &&
                entry.path().filename().string().find("point_cloud") == 0) {
                return entry.path().string();
            }
        }
        return "";
    }

    std::string getIterationDir() {
        auto result = resolve_input_path(test_data_ply_);
        return result.has_value() ? result->path : "";
    }
```

**Step 3: Update getReferenceLccPath()**

```cpp
    std::string getReferenceLccPath() {
        std::string lcc_results = test_data_lcc_ + "/LCC_Results";
        if (fs::exists(lcc_results)) {
            return lcc_results;
        }
        return test_data_lcc_;
    }
```

**Step 4: Add include for path_resolution.hpp**

Add at top:
```cpp
#include "path_resolution.hpp"
```

**Step 5: Update FullConversionPipeline test**

The output should go to `/tmp/.../LCC_Results/`. Update the verification:

```cpp
TEST_F(IntegrationTest, FullConversionPipeline) {
    std::string iteration_dir = getIterationDir();
    ASSERT_FALSE(iteration_dir.empty());

    std::string ply_path = getTestPlyPath();
    ASSERT_FALSE(ply_path.empty());

    // Create temp output directory
    std::string output_base = "/tmp/ply2lcc_test_" + std::to_string(std::time(nullptr));
    std::string output_dir = output_base + "/LCC_Results";
    fs::create_directories(output_dir);

    // ... rest of test uses output_dir for file checks ...

    // Cleanup
    fs::remove_all(output_base);
}
```

**Step 6: Build and run all tests**

Run: `cd /home/linh/3dgs_ws/ply2lcc/build && make -j4 && ctest --output-on-failure`
Expected: All 32+ tests PASS

**Step 7: Commit changes**

```bash
git add tests/test_integration.cpp
git commit -m "test: update integration tests for new path structure"
```

---

### Task 5: Move misplaced test PLY file

**Files:**
- Bash commands only (move file)

There's a stray `point_cloud.ply` directly in `test_data/scene_ply/`. It should be removed or moved since the canonical location is now inside `point_cloud/iteration_100/`.

**Step 1: Check if file is duplicate**

```bash
ls -la /home/linh/3dgs_ws/ply2lcc/test_data/scene_ply/point_cloud.ply
ls -la /home/linh/3dgs_ws/ply2lcc/test_data/scene_ply/point_cloud/iteration_100/point_cloud.ply
```

**Step 2: Remove duplicate if same size**

If both exist and have same content (the iteration_100 one is canonical):
```bash
rm /home/linh/3dgs_ws/ply2lcc/test_data/scene_ply/point_cloud.ply
```

**Step 3: Verify tests still pass**

Run: `cd /home/linh/3dgs_ws/ply2lcc/build && ctest --output-on-failure`
Expected: All tests PASS

---

### Task 6: Final verification and cleanup

**Step 1: Run full test suite**

```bash
cd /home/linh/3dgs_ws/ply2lcc/build && cmake .. && make -j4 && ctest --output-on-failure
```
Expected: All tests PASS

**Step 2: Test CLI manually**

```bash
cd /home/linh/3dgs_ws/ply2lcc/build
./ply2lcc ../test_data/scene_ply -o /tmp/test_output --single-lod
ls -la /tmp/test_output/LCC_Results/
```

Expected output structure:
```
/tmp/test_output/LCC_Results/
├── meta.lcc
├── Data.bin
├── Index.bin
└── Shcoef.bin
```

**Step 3: Test error cases**

```bash
# Missing point_cloud dir
./ply2lcc /tmp -o /tmp/out
# Expected: Error: point_cloud/ directory not found

# Missing iteration dir
mkdir -p /tmp/test_bad/point_cloud
./ply2lcc /tmp/test_bad -o /tmp/out
# Expected: Error: No iteration_* directories found
```

**Step 4: Clean up test outputs**

```bash
rm -rf /tmp/test_output /tmp/test_bad
```
