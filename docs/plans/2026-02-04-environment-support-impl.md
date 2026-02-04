# Environment Support Implementation Plan

> **For Claude:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task.

**Goal:** Add proper environment.ply processing to generate Environment.bin and correct meta.lcc attributes.

**Architecture:** Read environment.ply, compute separate attribute ranges (envshcoef, envscale), write Environment.bin (combined 96 bytes/splat), update meta.lcc position attribute to use environment bounds.

**Tech Stack:** C++17, miniply, existing compression module

---

### Task 1: Add environment attribute ranges to types.hpp

**Files:**
- Modify: `src/types.hpp`

**Step 1: Add EnvBounds struct**

Add after the `AttributeRanges` struct:

```cpp
struct EnvBounds {
    Vec3f pos_min{std::numeric_limits<float>::max(), std::numeric_limits<float>::max(), std::numeric_limits<float>::max()};
    Vec3f pos_max{std::numeric_limits<float>::lowest(), std::numeric_limits<float>::lowest(), std::numeric_limits<float>::lowest()};
    Vec3f sh_min{std::numeric_limits<float>::max(), std::numeric_limits<float>::max(), std::numeric_limits<float>::max()};
    Vec3f sh_max{std::numeric_limits<float>::lowest(), std::numeric_limits<float>::lowest(), std::numeric_limits<float>::lowest()};
    Vec3f scale_min{std::numeric_limits<float>::max(), std::numeric_limits<float>::max(), std::numeric_limits<float>::max()};
    Vec3f scale_max{std::numeric_limits<float>::lowest(), std::numeric_limits<float>::lowest(), std::numeric_limits<float>::lowest()};

    void expand_pos(const Vec3f& p) {
        pos_min.x = std::min(pos_min.x, p.x);
        pos_min.y = std::min(pos_min.y, p.y);
        pos_min.z = std::min(pos_min.z, p.z);
        pos_max.x = std::max(pos_max.x, p.x);
        pos_max.y = std::max(pos_max.y, p.y);
        pos_max.z = std::max(pos_max.z, p.z);
    }

    void expand_sh(float r, float g, float b) {
        sh_min.x = std::min(sh_min.x, r);
        sh_min.y = std::min(sh_min.y, g);
        sh_min.z = std::min(sh_min.z, b);
        sh_max.x = std::max(sh_max.x, r);
        sh_max.y = std::max(sh_max.y, g);
        sh_max.z = std::max(sh_max.z, b);
    }

    void expand_scale(const Vec3f& s) {
        scale_min.x = std::min(scale_min.x, s.x);
        scale_min.y = std::min(scale_min.y, s.y);
        scale_min.z = std::min(scale_min.z, s.z);
        scale_max.x = std::max(scale_max.x, s.x);
        scale_max.y = std::max(scale_max.y, s.y);
        scale_max.z = std::max(scale_max.z, s.z);
    }
};
```

**Step 2: Build and verify**

Run: `cd /home/linh/3dgs_ws/ply2lcc/build && make -j4`
Expected: Build succeeds

**Step 3: Commit**

```bash
git add src/types.hpp
git commit -m "feat: add EnvBounds struct for environment attributes"
```

---

### Task 2: Create environment writer module

**Files:**
- Create: `src/env_writer.hpp`
- Create: `src/env_writer.cpp`
- Modify: `CMakeLists.txt` (add to LIB_SOURCES)

**Step 1: Create env_writer.hpp**

```cpp
#ifndef ENV_WRITER_HPP
#define ENV_WRITER_HPP

#include <string>
#include <vector>
#include "types.hpp"

namespace ply2lcc {

class EnvWriter {
public:
    // Read environment.ply and compute bounds
    // Returns true if environment.ply exists and was read successfully
    static bool read_environment(const std::string& env_ply_path,
                                 std::vector<Splat>& splats,
                                 EnvBounds& bounds);

    // Write Environment.bin (combined data + SH, 96 bytes per splat)
    static bool write_environment_bin(const std::string& output_path,
                                      const std::vector<Splat>& splats,
                                      const EnvBounds& bounds);
};

}  // namespace ply2lcc

#endif  // ENV_WRITER_HPP
```

**Step 2: Create env_writer.cpp**

```cpp
#include "env_writer.hpp"
#include "ply_reader.hpp"
#include "compression.hpp"
#include <fstream>
#include <cmath>

namespace ply2lcc {

bool EnvWriter::read_environment(const std::string& env_ply_path,
                                 std::vector<Splat>& splats,
                                 EnvBounds& bounds) {
    PLYHeader header;
    if (!PLYReader::read_splats(env_ply_path, splats, header)) {
        return false;
    }

    // Compute bounds from splats
    for (const auto& s : splats) {
        bounds.expand_pos(s.pos);

        // Linear scale
        Vec3f linear_scale(std::exp(s.scale.x), std::exp(s.scale.y), std::exp(s.scale.z));
        bounds.expand_scale(linear_scale);

        // SH coefficients - track per channel
        // f_rest has 45 coefficients: 15 bands × 3 channels (RGB interleaved)
        for (int band = 0; band < 15; ++band) {
            float r = s.f_rest[band * 3 + 0];
            float g = s.f_rest[band * 3 + 1];
            float b = s.f_rest[band * 3 + 2];
            bounds.expand_sh(r, g, b);
        }
    }

    return true;
}

bool EnvWriter::write_environment_bin(const std::string& output_path,
                                      const std::vector<Splat>& splats,
                                      const EnvBounds& bounds) {
    std::ofstream file(output_path, std::ios::binary);
    if (!file) {
        return false;
    }

    // Environment.bin: 96 bytes per splat (32 data + 64 SH)
    std::vector<uint8_t> buffer(96);

    for (const auto& s : splats) {
        // First 32 bytes: same as Data.bin format
        // Position (12 bytes)
        memcpy(buffer.data(), &s.pos.x, 4);
        memcpy(buffer.data() + 4, &s.pos.y, 4);
        memcpy(buffer.data() + 8, &s.pos.z, 4);

        // Color (4 bytes RGBA)
        uint32_t color = encode_color(s.f_dc, s.opacity);
        memcpy(buffer.data() + 12, &color, 4);

        // Scale (6 bytes)
        Vec3f linear_scale(std::exp(s.scale.x), std::exp(s.scale.y), std::exp(s.scale.z));
        encode_scale(linear_scale, bounds.scale_min, bounds.scale_max, buffer.data() + 16);

        // Rotation (4 bytes)
        uint32_t rot = encode_rotation(s.rot);
        memcpy(buffer.data() + 22, &rot, 4);

        // Normal (6 bytes) - zeros
        memset(buffer.data() + 26, 0, 6);

        // Next 64 bytes: SH coefficients
        encode_sh_coefficients(s.f_rest, bounds.sh_min, bounds.sh_max, buffer.data() + 32);

        file.write(reinterpret_cast<const char*>(buffer.data()), 96);
    }

    return true;
}

}  // namespace ply2lcc
```

**Step 3: Add to CMakeLists.txt**

Add `src/env_writer.cpp` to the library sources.

**Step 4: Build and verify**

Run: `cd /home/linh/3dgs_ws/ply2lcc/build && cmake .. && make -j4`
Expected: Build succeeds

**Step 5: Commit**

```bash
git add src/env_writer.hpp src/env_writer.cpp CMakeLists.txt
git commit -m "feat: add environment writer module"
```

---

### Task 3: Update meta_writer for environment attributes

**Files:**
- Modify: `src/meta_writer.hpp`
- Modify: `src/meta_writer.cpp`

**Step 1: Update MetaInfo struct in meta_writer.hpp**

Add environment bounds field:

```cpp
struct MetaInfo {
    // ... existing fields ...

    // Environment bounds (optional)
    bool has_environment = false;
    EnvBounds env_bounds;
};
```

**Step 2: Update meta_writer.cpp**

Change the position attribute to use environment bounds when available:

```cpp
// Position - use environment bounds if available, otherwise bbox
file << "\t\t{\n";
file << "\t\t\t\"name\": \"position\",\n";
if (meta.has_environment) {
    file << "\t\t\t\"min\": [" << meta.env_bounds.pos_min.x << ", " << meta.env_bounds.pos_min.y << ", " << meta.env_bounds.pos_min.z << "],\n";
    file << "\t\t\t\"max\": [" << meta.env_bounds.pos_max.x << ", " << meta.env_bounds.pos_max.y << ", " << meta.env_bounds.pos_max.z << "]\n";
} else {
    file << "\t\t\t\"min\": [" << meta.bounding_box.min.x << ", " << meta.bounding_box.min.y << ", " << meta.bounding_box.min.z << "],\n";
    file << "\t\t\t\"max\": [" << meta.bounding_box.max.x << ", " << meta.bounding_box.max.y << ", " << meta.bounding_box.max.z << "]\n";
}
file << "\t\t},\n";
```

Change envshcoef and envscale to use environment-specific ranges:

```cpp
// envshcoef
file << "\t\t{\n";
file << "\t\t\t\"name\": \"envshcoef\",\n";
if (meta.has_environment) {
    file << "\t\t\t\"min\": [" << meta.env_bounds.sh_min.x << ", " << meta.env_bounds.sh_min.y << ", " << meta.env_bounds.sh_min.z << "],\n";
    file << "\t\t\t\"max\": [" << meta.env_bounds.sh_max.x << ", " << meta.env_bounds.sh_max.y << ", " << meta.env_bounds.sh_max.z << "]\n";
} else {
    file << "\t\t\t\"min\": [" << meta.attr_ranges.sh_min.x << ", " << meta.attr_ranges.sh_min.y << ", " << meta.attr_ranges.sh_min.z << "],\n";
    file << "\t\t\t\"max\": [" << meta.attr_ranges.sh_max.x << ", " << meta.attr_ranges.sh_max.y << ", " << meta.attr_ranges.sh_max.z << "]\n";
}
file << "\t\t},\n";

// envscale
file << "\t\t{\n";
file << "\t\t\t\"name\": \"envscale\",\n";
if (meta.has_environment) {
    file << "\t\t\t\"min\": [" << meta.env_bounds.scale_min.x << ", " << meta.env_bounds.scale_min.y << ", " << meta.env_bounds.scale_min.z << "],\n";
    file << "\t\t\t\"max\": [" << meta.env_bounds.scale_max.x << ", " << meta.env_bounds.scale_max.y << ", " << meta.env_bounds.scale_max.z << "]\n";
} else {
    file << "\t\t\t\"min\": [" << meta.attr_ranges.scale_min.x << ", " << meta.attr_ranges.scale_min.y << ", " << meta.attr_ranges.scale_min.z << "],\n";
    file << "\t\t\t\"max\": [" << meta.attr_ranges.scale_max.x << ", " << meta.attr_ranges.scale_max.y << ", " << meta.attr_ranges.scale_max.z << "]\n";
}
file << "\t\t}\n";
```

**Step 3: Build and verify**

Run: `cd /home/linh/3dgs_ws/ply2lcc/build && make -j4`
Expected: Build succeeds

**Step 4: Commit**

```bash
git add src/meta_writer.hpp src/meta_writer.cpp
git commit -m "feat: update meta_writer for environment attributes"
```

---

### Task 4: Integrate environment processing into main.cpp

**Files:**
- Modify: `src/main.cpp`

**Step 1: Add include**

```cpp
#include "env_writer.hpp"
```

**Step 2: Add environment processing after environment.ply detection**

After the "Found environment.ply" message, add:

```cpp
    std::vector<Splat> env_splats;
    EnvBounds env_bounds;
    bool has_env = fs::exists(env_path);

    if (has_env) {
        std::cout << "Found environment.ply\n";
        if (!EnvWriter::read_environment(env_path, env_splats, env_bounds)) {
            std::cerr << "Warning: Failed to read environment.ply\n";
            has_env = false;
        } else {
            std::cout << "  " << env_splats.size() << " environment splats\n";
        }
    }
```

**Step 3: Write Environment.bin after Phase 3**

After `writer.finalize()`, add:

```cpp
    // Write Environment.bin if environment data exists
    if (has_env && !env_splats.empty()) {
        std::cout << "\nWriting Environment.bin...\n";
        if (!EnvWriter::write_environment_bin(lcc_output_dir + "/Environment.bin", env_splats, env_bounds)) {
            std::cerr << "Warning: Failed to write Environment.bin\n";
        }
    }
```

**Step 4: Update MetaInfo with environment data**

Before calling `MetaWriter::write()`, add:

```cpp
    meta.has_environment = has_env && !env_splats.empty();
    if (meta.has_environment) {
        meta.env_bounds = env_bounds;
    }
```

**Step 5: Update output files message**

Add Environment.bin to the output list:

```cpp
    if (has_env) {
        std::cout << "  " << lcc_output_dir << "/Environment.bin\n";
    }
```

**Step 6: Build and test**

Run: `cd /home/linh/3dgs_ws/ply2lcc/build && make -j4 && ctest --output-on-failure`
Expected: All tests pass

**Step 7: Commit**

```bash
git add src/main.cpp
git commit -m "feat: integrate environment processing into main"
```

---

### Task 5: Fix SH coefficient per-channel tracking

**Files:**
- Modify: `src/types.hpp`
- Modify: `src/main.cpp`

The current `AttributeRanges` tracks SH min/max globally. The reference tracks per-channel (R, G, B separately).

**Step 1: Update AttributeRanges in types.hpp**

Change `expand_sh` to track per-channel:

```cpp
void expand_sh(float r, float g, float b) {
    sh_min.x = std::min(sh_min.x, r);
    sh_min.y = std::min(sh_min.y, g);
    sh_min.z = std::min(sh_min.z, b);
    sh_max.x = std::max(sh_max.x, r);
    sh_max.y = std::max(sh_max.y, g);
    sh_max.z = std::max(sh_max.z, b);
}
```

**Step 2: Update main.cpp SH range computation**

Change the SH expansion loop to track per-channel:

```cpp
if (has_sh) {
    // f_rest has 45 coefficients: 15 bands × 3 channels (RGB interleaved)
    for (int band = 0; band < 15; ++band) {
        float r = s.f_rest[band * 3 + 0];
        float g = s.f_rest[band * 3 + 1];
        float b = s.f_rest[band * 3 + 2];
        global_ranges.expand_sh(r, g, b);
    }
}
```

**Step 3: Build and test**

Run: `cd /home/linh/3dgs_ws/ply2lcc/build && make -j4 && ctest --output-on-failure`
Expected: All tests pass

**Step 4: Commit**

```bash
git add src/types.hpp src/main.cpp
git commit -m "fix: track SH coefficients per-channel (R, G, B)"
```

---

### Task 6: Final verification against reference

**Step 1: Run full conversion**

```bash
cd /home/linh/3dgs_ws/ply2lcc/build
./ply2lcc ../test_data/scene_ply -o /tmp/lcc_test
```

**Step 2: Compare meta.lcc with reference**

```bash
cat /tmp/lcc_test/LCC_Results/meta.lcc
cat ../test_data/scene_lcc/LCC_Results/*.lcc
```

Verify:
- `position` min/max matches reference (from environment bounds)
- `envshcoef` has environment-specific values
- `envscale` has environment-specific values
- `shcoef` has per-channel min/max

**Step 3: Verify Environment.bin exists and has correct size**

```bash
ls -la /tmp/lcc_test/LCC_Results/Environment.bin
# Expected: 14810 splats × 96 bytes = 1,421,760 bytes
```

**Step 4: Clean up**

```bash
rm -rf /tmp/lcc_test
```
