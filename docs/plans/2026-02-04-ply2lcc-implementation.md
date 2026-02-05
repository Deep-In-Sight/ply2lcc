# PLY to LCC Converter Implementation Plan

> **For Claude:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task.

**Goal:** Build a C++ converter that transforms 3DGS PLY files into LCC format with grid-based spatial partitioning.

**Architecture:** Single-pass PLY reading with miniply, in-memory grid assignment, sequential compressed output. Baseline focuses on correctness; optimization phases follow.

**Tech Stack:** C++17, CMake, miniply (header-only PLY parser)

---

## Task 1: Project Scaffolding

**Files:**
- Create: `CMakeLists.txt`
- Create: `src/main.cpp`
- Create: `external/miniply/miniply.h`
- Create: `external/miniply/miniply.cpp`

**Step 1: Create CMakeLists.txt**

```cmake
cmake_minimum_required(VERSION 3.16)
project(ply2lcc VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

add_executable(ply2lcc
    src/main.cpp
    external/miniply/miniply.cpp
)

target_include_directories(ply2lcc PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/external
)

target_compile_options(ply2lcc PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall -Wextra -O2>
)
```

**Step 2: Create minimal main.cpp**

```cpp
#include <iostream>
#include <string>
#include <filesystem>

namespace fs = std::filesystem;

void print_usage(const char* prog) {
    std::cerr << "Usage: " << prog << " <input_dir> -o <output_dir> [options]\n"
              << "Options:\n"
              << "  --single-lod       Use only LOD0 (default: multi-LOD)\n"
              << "  --cell-size X,Y    Grid cell size in meters (default: 30,30)\n";
}

int main(int argc, char* argv[]) {
    if (argc < 4) {
        print_usage(argv[0]);
        return 1;
    }

    std::string input_dir;
    std::string output_dir;
    bool single_lod = false;
    float cell_size_x = 30.0f;
    float cell_size_y = 30.0f;

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "-o" && i + 1 < argc) {
            output_dir = argv[++i];
        } else if (arg == "--single-lod") {
            single_lod = true;
        } else if (arg == "--cell-size" && i + 1 < argc) {
            if (sscanf(argv[++i], "%f,%f", &cell_size_x, &cell_size_y) != 2) {
                std::cerr << "Error: Invalid cell-size format. Use X,Y\n";
                return 1;
            }
        } else if (arg[0] != '-') {
            input_dir = arg;
        }
    }

    if (input_dir.empty() || output_dir.empty()) {
        print_usage(argv[0]);
        return 1;
    }

    std::cout << "Input: " << input_dir << "\n"
              << "Output: " << output_dir << "\n"
              << "Mode: " << (single_lod ? "single-lod" : "multi-lod") << "\n"
              << "Cell size: " << cell_size_x << " x " << cell_size_y << "\n";

    return 0;
}
```

**Step 3: Download miniply**

Run:
```bash
mkdir -p external/miniply
curl -sL https://raw.githubusercontent.com/vilya/miniply/master/miniply.h -o external/miniply/miniply.h
curl -sL https://raw.githubusercontent.com/vilya/miniply/master/miniply.cpp -o external/miniply/miniply.cpp
```

**Step 4: Build and verify**

Run:
```bash
mkdir -p build && cd build && cmake .. && make
./ply2lcc /home/linh/3dgs_ws/ply2lcc/CheonanVillagePLY/point_cloud/iteration_100 -o /tmp/lcc_out
```

Expected output:
```
Input: /home/linh/3dgs_ws/ply2lcc/CheonanVillagePLY/point_cloud/iteration_100
Output: /tmp/lcc_out
Mode: multi-lod
Cell size: 30 x 30
```

**Step 5: Commit**

```bash
git init
git add CMakeLists.txt src/main.cpp external/
git commit -m "feat: project scaffolding with CLI argument parsing"
```

---

## Task 2: Types and Data Structures

**Files:**
- Create: `src/types.hpp`

**Step 1: Create types.hpp**

```cpp
#ifndef PLY2LCC_TYPES_HPP
#define PLY2LCC_TYPES_HPP

#include <cstdint>
#include <vector>
#include <array>
#include <string>
#include <limits>
#include <cmath>

namespace ply2lcc {

struct Vec3f {
    float x, y, z;

    Vec3f() : x(0), y(0), z(0) {}
    Vec3f(float x_, float y_, float z_) : x(x_), y(y_), z(z_) {}

    float& operator[](int i) { return (&x)[i]; }
    float operator[](int i) const { return (&x)[i]; }
};

struct BBox {
    Vec3f min{std::numeric_limits<float>::max(),
              std::numeric_limits<float>::max(),
              std::numeric_limits<float>::max()};
    Vec3f max{std::numeric_limits<float>::lowest(),
              std::numeric_limits<float>::lowest(),
              std::numeric_limits<float>::lowest()};

    void expand(const Vec3f& p) {
        for (int i = 0; i < 3; ++i) {
            if (p[i] < min[i]) min[i] = p[i];
            if (p[i] > max[i]) max[i] = p[i];
        }
    }

    void expand(const BBox& other) {
        for (int i = 0; i < 3; ++i) {
            if (other.min[i] < min[i]) min[i] = other.min[i];
            if (other.max[i] > max[i]) max[i] = other.max[i];
        }
    }
};

struct Splat {
    Vec3f pos;
    Vec3f normal;
    float f_dc[3];       // DC color coefficients
    float f_rest[45];    // SH coefficients (bands 1-3)
    float opacity;       // logit-space
    Vec3f scale;         // log-space
    float rot[4];        // quaternion (w, x, y, z)
};

struct AttributeRanges {
    Vec3f scale_min, scale_max;       // After exp() - linear space
    Vec3f sh_min, sh_max;             // SH coefficient range
    float opacity_min, opacity_max;   // After sigmoid() - [0,1] range

    AttributeRanges() {
        scale_min = Vec3f(std::numeric_limits<float>::max(),
                          std::numeric_limits<float>::max(),
                          std::numeric_limits<float>::max());
        scale_max = Vec3f(std::numeric_limits<float>::lowest(),
                          std::numeric_limits<float>::lowest(),
                          std::numeric_limits<float>::lowest());
        sh_min = scale_min;
        sh_max = scale_max;
        opacity_min = std::numeric_limits<float>::max();
        opacity_max = std::numeric_limits<float>::lowest();
    }

    void expand_scale(const Vec3f& linear_scale) {
        for (int i = 0; i < 3; ++i) {
            if (linear_scale[i] < scale_min[i]) scale_min[i] = linear_scale[i];
            if (linear_scale[i] > scale_max[i]) scale_max[i] = linear_scale[i];
        }
    }

    void expand_sh(float val) {
        // SH uses same min/max for all channels
        if (val < sh_min.x) sh_min = Vec3f(val, val, val);
        if (val > sh_max.x) sh_max = Vec3f(val, val, val);
    }

    void expand_opacity(float sigmoid_opacity) {
        if (sigmoid_opacity < opacity_min) opacity_min = sigmoid_opacity;
        if (sigmoid_opacity > opacity_max) opacity_max = sigmoid_opacity;
    }
};

struct GridCell {
    uint32_t index;  // (cell_y << 16) | cell_x
    std::vector<std::vector<size_t>> splat_indices;  // per-LOD

    GridCell(uint32_t idx, size_t num_lods)
        : index(idx), splat_indices(num_lods) {}
};

struct ConvertConfig {
    std::string input_dir;
    std::string output_dir;
    bool single_lod = false;
    float cell_size_x = 30.0f;
    float cell_size_y = 30.0f;
};

// Utility functions
inline float sigmoid(float x) {
    return 1.0f / (1.0f + std::exp(-x));
}

inline float clamp(float x, float lo, float hi) {
    return x < lo ? lo : (x > hi ? hi : x);
}

} // namespace ply2lcc

#endif // PLY2LCC_TYPES_HPP
```

**Step 2: Include in main.cpp and verify compilation**

Add to top of main.cpp:
```cpp
#include "types.hpp"
```

Run:
```bash
cd build && make
```

Expected: Compiles without errors.

**Step 3: Commit**

```bash
git add src/types.hpp src/main.cpp
git commit -m "feat: add core data structures (Splat, BBox, GridCell)"
```

---

## Task 3: PLY Reader

**Files:**
- Create: `src/ply_reader.hpp`
- Create: `src/ply_reader.cpp`
- Modify: `CMakeLists.txt`

**Step 1: Create ply_reader.hpp**

```cpp
#ifndef PLY2LCC_PLY_READER_HPP
#define PLY2LCC_PLY_READER_HPP

#include "types.hpp"
#include <string>
#include <vector>
#include <functional>

namespace ply2lcc {

struct PLYHeader {
    size_t vertex_count = 0;
    BBox bbox;
    Vec3f offset{0, 0, 0};
    Vec3f shift{0, 0, 0};
    Vec3f scale_transform{1, 1, 1};
    std::string source;
    int epsg = 0;
    bool has_sh = true;  // Has f_rest properties
};

class PLYReader {
public:
    // Read header only (for counting/bbox)
    static bool read_header(const std::string& path, PLYHeader& header);

    // Read all splats from a PLY file
    static bool read_splats(const std::string& path,
                           std::vector<Splat>& splats,
                           PLYHeader& header);

    // Stream splats with callback (memory efficient for large files)
    using SplatCallback = std::function<void(const Splat&, size_t idx)>;
    static bool stream_splats(const std::string& path,
                             const SplatCallback& callback,
                             PLYHeader& header);
};

} // namespace ply2lcc

#endif // PLY2LCC_PLY_READER_HPP
```

**Step 2: Create ply_reader.cpp**

```cpp
#include "ply_reader.hpp"
#include "miniply/miniply.h"
#include <cstring>
#include <iostream>

namespace ply2lcc {

static bool parse_comment_value(const std::string& comment, const char* key, float& value) {
    size_t pos = comment.find(key);
    if (pos == std::string::npos) return false;
    pos += strlen(key);
    while (pos < comment.size() && (comment[pos] == ' ' || comment[pos] == '\t')) pos++;
    value = std::stof(comment.substr(pos));
    return true;
}

static bool parse_comment_string(const std::string& comment, const char* key, std::string& value) {
    size_t pos = comment.find(key);
    if (pos == std::string::npos) return false;
    pos += strlen(key);
    while (pos < comment.size() && (comment[pos] == ' ' || comment[pos] == '\t')) pos++;
    size_t end = comment.find_first_of(" \t\r\n", pos);
    value = comment.substr(pos, end - pos);
    return true;
}

bool PLYReader::read_header(const std::string& path, PLYHeader& header) {
    miniply::PLYReader reader(path.c_str());
    if (!reader.valid()) {
        std::cerr << "Failed to open PLY file: " << path << "\n";
        return false;
    }

    // Find vertex element
    while (reader.has_element() && !reader.element_is(miniply::kPLYVertexElement)) {
        reader.next_element();
    }

    if (!reader.has_element()) {
        std::cerr << "No vertex element found in: " << path << "\n";
        return false;
    }

    header.vertex_count = reader.num_rows();

    // Check for SH coefficients
    header.has_sh = (reader.find_property("f_rest_0") != miniply::kInvalidIndex);

    return true;
}

bool PLYReader::read_splats(const std::string& path,
                           std::vector<Splat>& splats,
                           PLYHeader& header) {
    miniply::PLYReader reader(path.c_str());
    if (!reader.valid()) {
        std::cerr << "Failed to open PLY file: " << path << "\n";
        return false;
    }

    // Find vertex element
    while (reader.has_element() && !reader.element_is(miniply::kPLYVertexElement)) {
        reader.next_element();
    }

    if (!reader.has_element()) {
        std::cerr << "No vertex element found in: " << path << "\n";
        return false;
    }

    const uint32_t num_verts = reader.num_rows();
    header.vertex_count = num_verts;

    // Find property indices
    uint32_t pos_idx[3], normal_idx[3], f_dc_idx[3], opacity_idx, scale_idx[3], rot_idx[4];

    if (!reader.find_properties(pos_idx, 3, "x", "y", "z")) {
        std::cerr << "Missing position properties\n";
        return false;
    }

    bool has_normals = reader.find_properties(normal_idx, 3, "nx", "ny", "nz");

    if (!reader.find_properties(f_dc_idx, 3, "f_dc_0", "f_dc_1", "f_dc_2")) {
        std::cerr << "Missing f_dc properties\n";
        return false;
    }

    opacity_idx = reader.find_property("opacity");
    if (opacity_idx == miniply::kInvalidIndex) {
        std::cerr << "Missing opacity property\n";
        return false;
    }

    if (!reader.find_properties(scale_idx, 3, "scale_0", "scale_1", "scale_2")) {
        std::cerr << "Missing scale properties\n";
        return false;
    }

    if (!reader.find_properties(rot_idx, 4, "rot_0", "rot_1", "rot_2", "rot_3")) {
        std::cerr << "Missing rotation properties\n";
        return false;
    }

    // Find f_rest properties
    uint32_t f_rest_idx[45];
    header.has_sh = true;
    for (int i = 0; i < 45; ++i) {
        char name[16];
        snprintf(name, sizeof(name), "f_rest_%d", i);
        f_rest_idx[i] = reader.find_property(name);
        if (f_rest_idx[i] == miniply::kInvalidIndex) {
            header.has_sh = false;
            break;
        }
    }

    // Load element data
    if (!reader.load_element()) {
        std::cerr << "Failed to load vertex data\n";
        return false;
    }

    // Allocate temporary buffers
    std::vector<float> positions(num_verts * 3);
    std::vector<float> normals(num_verts * 3);
    std::vector<float> f_dc(num_verts * 3);
    std::vector<float> f_rest(header.has_sh ? num_verts * 45 : 0);
    std::vector<float> opacities(num_verts);
    std::vector<float> scales(num_verts * 3);
    std::vector<float> rotations(num_verts * 4);

    // Extract properties
    reader.extract_properties(pos_idx, 3, miniply::PLYPropertyType::Float, positions.data());

    if (has_normals) {
        reader.extract_properties(normal_idx, 3, miniply::PLYPropertyType::Float, normals.data());
    }

    reader.extract_properties(f_dc_idx, 3, miniply::PLYPropertyType::Float, f_dc.data());
    reader.extract_properties(&opacity_idx, 1, miniply::PLYPropertyType::Float, opacities.data());
    reader.extract_properties(scale_idx, 3, miniply::PLYPropertyType::Float, scales.data());
    reader.extract_properties(rot_idx, 4, miniply::PLYPropertyType::Float, rotations.data());

    if (header.has_sh) {
        for (int i = 0; i < 45; ++i) {
            std::vector<float> tmp(num_verts);
            reader.extract_properties(&f_rest_idx[i], 1, miniply::PLYPropertyType::Float, tmp.data());
            for (uint32_t v = 0; v < num_verts; ++v) {
                f_rest[v * 45 + i] = tmp[v];
            }
        }
    }

    // Convert to Splat structs
    splats.resize(num_verts);
    for (uint32_t i = 0; i < num_verts; ++i) {
        Splat& s = splats[i];
        s.pos = Vec3f(positions[i*3], positions[i*3+1], positions[i*3+2]);

        if (has_normals) {
            s.normal = Vec3f(normals[i*3], normals[i*3+1], normals[i*3+2]);
        } else {
            s.normal = Vec3f(0, 0, 0);
        }

        s.f_dc[0] = f_dc[i*3];
        s.f_dc[1] = f_dc[i*3+1];
        s.f_dc[2] = f_dc[i*3+2];

        if (header.has_sh) {
            for (int j = 0; j < 45; ++j) {
                s.f_rest[j] = f_rest[i*45 + j];
            }
        } else {
            memset(s.f_rest, 0, sizeof(s.f_rest));
        }

        s.opacity = opacities[i];
        s.scale = Vec3f(scales[i*3], scales[i*3+1], scales[i*3+2]);
        s.rot[0] = rotations[i*4];     // w
        s.rot[1] = rotations[i*4+1];   // x
        s.rot[2] = rotations[i*4+2];   // y
        s.rot[3] = rotations[i*4+3];   // z

        // Update bbox
        header.bbox.expand(s.pos);
    }

    return true;
}

bool PLYReader::stream_splats(const std::string& path,
                             const SplatCallback& callback,
                             PLYHeader& header) {
    std::vector<Splat> splats;
    if (!read_splats(path, splats, header)) {
        return false;
    }

    for (size_t i = 0; i < splats.size(); ++i) {
        callback(splats[i], i);
    }

    return true;
}

} // namespace ply2lcc
```

**Step 3: Update CMakeLists.txt**

Change the add_executable line:
```cmake
add_executable(ply2lcc
    src/main.cpp
    src/ply_reader.cpp
    external/miniply/miniply.cpp
)
```

**Step 4: Test PLY reading in main.cpp**

Replace the end of main() with:
```cpp
    // Test PLY reading
    std::string ply_path = input_dir + "/point_cloud.ply";
    if (!fs::exists(ply_path)) {
        ply_path = input_dir + "/point_cloud_2.ply";  // Fallback for testing
    }

    ply2lcc::PLYHeader header;
    std::vector<ply2lcc::Splat> splats;

    std::cout << "Reading: " << ply_path << "\n";
    if (!ply2lcc::PLYReader::read_splats(ply_path, splats, header)) {
        std::cerr << "Failed to read PLY file\n";
        return 1;
    }

    std::cout << "Loaded " << splats.size() << " splats\n";
    std::cout << "BBox: (" << header.bbox.min.x << ", " << header.bbox.min.y << ", " << header.bbox.min.z << ") - ("
              << header.bbox.max.x << ", " << header.bbox.max.y << ", " << header.bbox.max.z << ")\n";
    std::cout << "Has SH: " << (header.has_sh ? "yes" : "no") << "\n";

    return 0;
```

Add includes at top:
```cpp
#include "ply_reader.hpp"
```

**Step 5: Build and test with smaller PLY**

Run:
```bash
cd build && make -j4
./ply2lcc /home/linh/3dgs_ws/ply2lcc/CheonanVillagePLY/point_cloud/iteration_100 -o /tmp/lcc_out
```

Expected: Should load point_cloud_2.ply (7.2M splats) or whichever exists.

**Step 6: Commit**

```bash
git add src/ply_reader.hpp src/ply_reader.cpp CMakeLists.txt src/main.cpp
git commit -m "feat: add PLY reader with miniply"
```

---

## Task 4: Compression Functions

**Files:**
- Create: `src/compression.hpp`
- Create: `src/compression.cpp`
- Modify: `CMakeLists.txt`

**Step 1: Create compression.hpp**

```cpp
#ifndef PLY2LCC_COMPRESSION_HPP
#define PLY2LCC_COMPRESSION_HPP

#include "types.hpp"
#include <cstdint>

namespace ply2lcc {

// Encode RGBA color from f_dc and opacity
// f_dc: DC spherical harmonic coefficients (need sigmoid transform)
// opacity: logit-space opacity (need sigmoid transform)
uint32_t encode_color(const float f_dc[3], float opacity);

// Encode scale from log-space to quantized uint16
// log_scale: log-space scale values from PLY
// min/max: linear-space bounds for quantization
void encode_scale(const Vec3f& log_scale,
                  const Vec3f& scale_min, const Vec3f& scale_max,
                  uint16_t out[3]);

// Encode quaternion using 10-10-10-2 bit packing
// rot: quaternion (w, x, y, z)
uint32_t encode_rotation(const float rot[4]);

// Encode one SH triplet (RGB) using 11-10-11 bit packing
uint32_t encode_sh_triplet(float r, float g, float b, float sh_min, float sh_max);

// Encode all 15 SH bands (45 floats) into 16 uint32 values
// f_rest: 45 SH coefficients from PLY
// out: 16 uint32 output values
void encode_sh_coefficients(const float f_rest[45],
                           float sh_min, float sh_max,
                           uint32_t out[16]);

} // namespace ply2lcc

#endif // PLY2LCC_COMPRESSION_HPP
```

**Step 2: Create compression.cpp**

```cpp
#include "compression.hpp"
#include <cmath>
#include <algorithm>

namespace ply2lcc {

// SH coefficient C0 for converting DC to color
static constexpr float SH_C0 = 0.28209479177387814f;

uint32_t encode_color(const float f_dc[3], float opacity) {
    // Convert f_dc to RGB using SH formula: color = 0.5 + SH_C0 * f_dc
    // Then clamp to [0, 255]
    auto to_rgb = [](float dc) -> uint8_t {
        float color = 0.5f + SH_C0 * dc;
        color = clamp(color, 0.0f, 1.0f);
        return static_cast<uint8_t>(color * 255.0f + 0.5f);
    };

    uint8_t r = to_rgb(f_dc[0]);
    uint8_t g = to_rgb(f_dc[1]);
    uint8_t b = to_rgb(f_dc[2]);
    uint8_t a = static_cast<uint8_t>(clamp(sigmoid(opacity), 0.0f, 1.0f) * 255.0f + 0.5f);

    // RGBA packed as uint32 (little-endian: R in lowest byte)
    return (uint32_t(a) << 24) | (uint32_t(b) << 16) | (uint32_t(g) << 8) | uint32_t(r);
}

void encode_scale(const Vec3f& log_scale,
                  const Vec3f& scale_min, const Vec3f& scale_max,
                  uint16_t out[3]) {
    for (int i = 0; i < 3; ++i) {
        float linear = std::exp(log_scale[i]);
        float range = scale_max[i] - scale_min[i];
        float normalized = (range > 0) ? (linear - scale_min[i]) / range : 0.0f;
        normalized = clamp(normalized, 0.0f, 1.0f);
        out[i] = static_cast<uint16_t>(normalized * 65535.0f + 0.5f);
    }
}

uint32_t encode_rotation(const float rot[4]) {
    // Input: rot[0]=w, rot[1]=x, rot[2]=y, rot[3]=z
    // LCC encoding uses: find largest component, drop it, encode xyz in 10-10-10-2

    float w = rot[0], x = rot[1], y = rot[2], z = rot[3];

    // Normalize quaternion
    float len = std::sqrt(w*w + x*x + y*y + z*z);
    if (len > 0) {
        w /= len; x /= len; y /= len; z /= len;
    }

    // Find largest absolute component
    float abs_vals[4] = {std::fabs(w), std::fabs(x), std::fabs(y), std::fabs(z)};
    int max_idx = 0;
    for (int i = 1; i < 4; ++i) {
        if (abs_vals[i] > abs_vals[max_idx]) max_idx = i;
    }

    // Ensure the dropped component is positive (negate quaternion if needed)
    float quat[4] = {w, x, y, z};
    if (quat[max_idx] < 0) {
        w = -w; x = -x; y = -y; z = -z;
    }

    // Get the three components to encode (excluding max_idx)
    // Reorder to match LCC's QLut decoding
    float enc[3];
    static const int order[4][3] = {
        {1, 2, 3},  // drop w: encode x, y, z
        {0, 2, 3},  // drop x: encode w, y, z
        {0, 1, 3},  // drop y: encode w, x, z
        {0, 1, 2}   // drop z: encode w, x, y
    };

    float src[4] = {w, x, y, z};
    for (int i = 0; i < 3; ++i) {
        enc[i] = src[order[max_idx][i]];
    }

    // Scale from [-1/sqrt2, 1/sqrt2] to [0, 1]
    static const float rsqrt2 = 0.7071067811865475f;
    static const float sqrt2 = 1.414213562373095f;

    auto encode_component = [](float v) -> uint32_t {
        float normalized = (v + rsqrt2) / sqrt2;  // Map to [0, 1]
        normalized = clamp(normalized, 0.0f, 1.0f);
        return static_cast<uint32_t>(normalized * 1023.0f + 0.5f);
    };

    uint32_t p0 = encode_component(enc[0]);
    uint32_t p1 = encode_component(enc[1]);
    uint32_t p2 = encode_component(enc[2]);
    uint32_t idx = static_cast<uint32_t>(max_idx);

    // Pack: bits 0-9 = p0, bits 10-19 = p1, bits 20-29 = p2, bits 30-31 = idx
    return p0 | (p1 << 10) | (p2 << 20) | (idx << 30);
}

uint32_t encode_sh_triplet(float r, float g, float b, float sh_min, float sh_max) {
    float range = sh_max - sh_min;

    auto normalize = [sh_min, range](float v) -> float {
        if (range <= 0) return 0.5f;
        return clamp((v - sh_min) / range, 0.0f, 1.0f);
    };

    // 11-10-11 bit packing
    uint32_t r_enc = static_cast<uint32_t>(normalize(r) * 2047.0f + 0.5f);
    uint32_t g_enc = static_cast<uint32_t>(normalize(g) * 1023.0f + 0.5f);
    uint32_t b_enc = static_cast<uint32_t>(normalize(b) * 2047.0f + 0.5f);

    return r_enc | (g_enc << 11) | (b_enc << 21);
}

void encode_sh_coefficients(const float f_rest[45],
                           float sh_min, float sh_max,
                           uint32_t out[16]) {
    // f_rest layout: 45 floats for 15 SH bands, each band has 3 color channels
    // The PLY format stores them as: f_rest_0..f_rest_44
    // Grouped as: [R1,R2,...,R15, G1,G2,...,G15, B1,B2,...,B15]
    // We need to interleave as RGB triplets for encoding

    const float* r_coeffs = f_rest;        // f_rest[0..14]
    const float* g_coeffs = f_rest + 15;   // f_rest[15..29]
    const float* b_coeffs = f_rest + 30;   // f_rest[30..44]

    for (int i = 0; i < 15; ++i) {
        out[i] = encode_sh_triplet(r_coeffs[i], g_coeffs[i], b_coeffs[i], sh_min, sh_max);
    }

    // 16th uint32 is padding/unused
    out[15] = 0;
}

} // namespace ply2lcc
```

**Step 3: Update CMakeLists.txt**

```cmake
add_executable(ply2lcc
    src/main.cpp
    src/ply_reader.cpp
    src/compression.cpp
    external/miniply/miniply.cpp
)
```

**Step 4: Build and verify compilation**

Run:
```bash
cd build && make -j4
```

Expected: Compiles without errors.

**Step 5: Commit**

```bash
git add src/compression.hpp src/compression.cpp CMakeLists.txt
git commit -m "feat: add LCC compression functions (color, scale, rotation, SH)"
```

---

## Task 5: LCC Writer - Data.bin

**Files:**
- Create: `src/lcc_writer.hpp`
- Create: `src/lcc_writer.cpp`
- Modify: `CMakeLists.txt`

**Step 1: Create lcc_writer.hpp**

```cpp
#ifndef PLY2LCC_LCC_WRITER_HPP
#define PLY2LCC_LCC_WRITER_HPP

#include "types.hpp"
#include <string>
#include <vector>
#include <fstream>
#include <map>

namespace ply2lcc {

struct LCCNodeInfo {
    uint32_t splat_count;
    uint64_t data_offset;
    uint32_t data_size;
    uint64_t sh_offset;
    uint32_t sh_size;
};

struct LCCUnitInfo {
    uint32_t index;  // (cell_y << 16) | cell_x
    std::vector<LCCNodeInfo> lods;  // One per LOD level
};

class LCCWriter {
public:
    LCCWriter(const std::string& output_dir,
              const AttributeRanges& ranges,
              size_t num_lods,
              bool has_sh);

    ~LCCWriter();

    // Write splats for a specific cell and LOD
    // Returns true on success
    bool write_splats(uint32_t cell_index,
                      size_t lod,
                      const std::vector<Splat>& splats);

    // Finalize and close all files
    bool finalize();

    // Get unit info for meta.lcc generation
    const std::vector<LCCUnitInfo>& get_units() const { return units_; }

    size_t total_splats() const { return total_splats_; }

private:
    std::string output_dir_;
    AttributeRanges ranges_;
    size_t num_lods_;
    bool has_sh_;

    std::ofstream data_file_;
    std::ofstream sh_file_;

    uint64_t data_offset_ = 0;
    uint64_t sh_offset_ = 0;
    size_t total_splats_ = 0;

    std::map<uint32_t, size_t> unit_index_map_;  // cell_index -> units_ index
    std::vector<LCCUnitInfo> units_;

    // Compression buffers (reused)
    std::vector<uint8_t> data_buffer_;
    std::vector<uint8_t> sh_buffer_;
};

} // namespace ply2lcc

#endif // PLY2LCC_LCC_WRITER_HPP
```

**Step 2: Create lcc_writer.cpp**

```cpp
#include "lcc_writer.hpp"
#include "compression.hpp"
#include <filesystem>
#include <iostream>
#include <cstring>

namespace fs = std::filesystem;

namespace ply2lcc {

LCCWriter::LCCWriter(const std::string& output_dir,
                     const AttributeRanges& ranges,
                     size_t num_lods,
                     bool has_sh)
    : output_dir_(output_dir)
    , ranges_(ranges)
    , num_lods_(num_lods)
    , has_sh_(has_sh)
{
    fs::create_directories(output_dir);

    data_file_.open(output_dir + "/Data.bin", std::ios::binary);
    if (!data_file_) {
        throw std::runtime_error("Failed to create Data.bin");
    }

    if (has_sh) {
        sh_file_.open(output_dir + "/Shcoef.bin", std::ios::binary);
        if (!sh_file_) {
            throw std::runtime_error("Failed to create Shcoef.bin");
        }
    }
}

LCCWriter::~LCCWriter() {
    if (data_file_.is_open()) data_file_.close();
    if (sh_file_.is_open()) sh_file_.close();
}

bool LCCWriter::write_splats(uint32_t cell_index,
                             size_t lod,
                             const std::vector<Splat>& splats) {
    if (splats.empty()) return true;

    // Find or create unit
    size_t unit_idx;
    auto it = unit_index_map_.find(cell_index);
    if (it == unit_index_map_.end()) {
        unit_idx = units_.size();
        unit_index_map_[cell_index] = unit_idx;
        LCCUnitInfo unit;
        unit.index = cell_index;
        unit.lods.resize(num_lods_);
        units_.push_back(unit);
    } else {
        unit_idx = it->second;
    }

    LCCUnitInfo& unit = units_[unit_idx];
    LCCNodeInfo& node = unit.lods[lod];

    // Prepare data buffer (32 bytes per splat)
    const size_t data_bytes = splats.size() * 32;
    data_buffer_.resize(data_bytes);

    // Prepare SH buffer (64 bytes per splat)
    const size_t sh_bytes = has_sh_ ? splats.size() * 64 : 0;
    if (has_sh_) {
        sh_buffer_.resize(sh_bytes);
    }

    for (size_t i = 0; i < splats.size(); ++i) {
        const Splat& s = splats[i];
        uint8_t* data_ptr = data_buffer_.data() + i * 32;

        // Position (12 bytes)
        memcpy(data_ptr, &s.pos.x, 12);
        data_ptr += 12;

        // Color RGBA (4 bytes)
        uint32_t color = encode_color(s.f_dc, s.opacity);
        memcpy(data_ptr, &color, 4);
        data_ptr += 4;

        // Scale (6 bytes)
        uint16_t scale_enc[3];
        encode_scale(s.scale, ranges_.scale_min, ranges_.scale_max, scale_enc);
        memcpy(data_ptr, scale_enc, 6);
        data_ptr += 6;

        // Rotation (4 bytes)
        uint32_t rot_enc = encode_rotation(s.rot);
        memcpy(data_ptr, &rot_enc, 4);
        data_ptr += 4;

        // Normal (6 bytes) - zeros for 3DGS
        uint16_t normal_enc[3] = {0, 0, 0};
        memcpy(data_ptr, normal_enc, 6);

        // SH coefficients (64 bytes)
        if (has_sh_) {
            uint8_t* sh_ptr = sh_buffer_.data() + i * 64;
            uint32_t sh_enc[16];
            encode_sh_coefficients(s.f_rest, ranges_.sh_min.x, ranges_.sh_max.x, sh_enc);
            memcpy(sh_ptr, sh_enc, 64);
        }
    }

    // Write data
    node.splat_count = static_cast<uint32_t>(splats.size());
    node.data_offset = data_offset_;
    node.data_size = static_cast<uint32_t>(data_bytes);

    data_file_.write(reinterpret_cast<char*>(data_buffer_.data()), data_bytes);
    data_offset_ += data_bytes;

    if (has_sh_) {
        node.sh_offset = sh_offset_;
        node.sh_size = static_cast<uint32_t>(sh_bytes);
        sh_file_.write(reinterpret_cast<char*>(sh_buffer_.data()), sh_bytes);
        sh_offset_ += sh_bytes;
    }

    total_splats_ += splats.size();

    return true;
}

bool LCCWriter::finalize() {
    data_file_.close();
    if (has_sh_) {
        sh_file_.close();
    }
    return true;
}

} // namespace ply2lcc
```

**Step 3: Update CMakeLists.txt**

```cmake
add_executable(ply2lcc
    src/main.cpp
    src/ply_reader.cpp
    src/compression.cpp
    src/lcc_writer.cpp
    external/miniply/miniply.cpp
)
```

**Step 4: Build and verify**

Run:
```bash
cd build && make -j4
```

Expected: Compiles without errors.

**Step 5: Commit**

```bash
git add src/lcc_writer.hpp src/lcc_writer.cpp CMakeLists.txt
git commit -m "feat: add LCC writer for Data.bin and Shcoef.bin"
```

---

## Task 6: Spatial Grid and Index.bin

**Files:**
- Create: `src/spatial_grid.hpp`
- Create: `src/spatial_grid.cpp`
- Modify: `CMakeLists.txt`

**Step 1: Create spatial_grid.hpp**

```cpp
#ifndef PLY2LCC_SPATIAL_GRID_HPP
#define PLY2LCC_SPATIAL_GRID_HPP

#include "types.hpp"
#include "lcc_writer.hpp"
#include <vector>
#include <map>

namespace ply2lcc {

class SpatialGrid {
public:
    SpatialGrid(float cell_size_x, float cell_size_y, const BBox& bbox, size_t num_lods);

    // Assign a splat to a grid cell
    uint32_t get_cell_index(const Vec3f& pos) const;

    // Add splat index to the grid
    void add_splat(size_t lod, const Vec3f& pos, size_t splat_idx);

    // Get all cells with their splat indices
    const std::map<uint32_t, GridCell>& get_cells() const { return cells_; }

    // Write Index.bin file
    bool write_index_bin(const std::string& path,
                         const std::vector<LCCUnitInfo>& units,
                         size_t num_lods) const;

    float cell_size_x() const { return cell_size_x_; }
    float cell_size_y() const { return cell_size_y_; }

private:
    float cell_size_x_;
    float cell_size_y_;
    BBox bbox_;
    size_t num_lods_;
    std::map<uint32_t, GridCell> cells_;
};

} // namespace ply2lcc

#endif // PLY2LCC_SPATIAL_GRID_HPP
```

**Step 2: Create spatial_grid.cpp**

```cpp
#include "spatial_grid.hpp"
#include <fstream>
#include <iostream>
#include <cmath>

namespace ply2lcc {

SpatialGrid::SpatialGrid(float cell_size_x, float cell_size_y, const BBox& bbox, size_t num_lods)
    : cell_size_x_(cell_size_x)
    , cell_size_y_(cell_size_y)
    , bbox_(bbox)
    , num_lods_(num_lods)
{
}

uint32_t SpatialGrid::get_cell_index(const Vec3f& pos) const {
    int cell_x = static_cast<int>(std::floor((pos.x - bbox_.min.x) / cell_size_x_));
    int cell_y = static_cast<int>(std::floor((pos.y - bbox_.min.y) / cell_size_y_));

    // Clamp to valid range (16-bit each)
    cell_x = std::max(0, std::min(cell_x, 65535));
    cell_y = std::max(0, std::min(cell_y, 65535));

    return (static_cast<uint32_t>(cell_y) << 16) | static_cast<uint32_t>(cell_x);
}

void SpatialGrid::add_splat(size_t lod, const Vec3f& pos, size_t splat_idx) {
    uint32_t index = get_cell_index(pos);

    auto it = cells_.find(index);
    if (it == cells_.end()) {
        GridCell cell(index, num_lods_);
        cell.splat_indices[lod].push_back(splat_idx);
        cells_[index] = std::move(cell);
    } else {
        it->second.splat_indices[lod].push_back(splat_idx);
    }
}

bool SpatialGrid::write_index_bin(const std::string& path,
                                  const std::vector<LCCUnitInfo>& units,
                                  size_t num_lods) const {
    std::ofstream file(path, std::ios::binary);
    if (!file) {
        std::cerr << "Failed to create Index.bin\n";
        return false;
    }

    // Each unit entry: index(4) + [count(4) + offset(8) + size(4)] * num_lods
    // = 4 + 16 * num_lods bytes per unit

    for (const auto& unit : units) {
        // Write unit index
        file.write(reinterpret_cast<const char*>(&unit.index), 4);

        // Write LOD entries
        for (size_t lod = 0; lod < num_lods; ++lod) {
            const LCCNodeInfo& node = unit.lods[lod];
            file.write(reinterpret_cast<const char*>(&node.splat_count), 4);
            file.write(reinterpret_cast<const char*>(&node.data_offset), 8);
            file.write(reinterpret_cast<const char*>(&node.data_size), 4);
        }
    }

    return true;
}

} // namespace ply2lcc
```

**Step 3: Update CMakeLists.txt**

```cmake
add_executable(ply2lcc
    src/main.cpp
    src/ply_reader.cpp
    src/compression.cpp
    src/lcc_writer.cpp
    src/spatial_grid.cpp
    external/miniply/miniply.cpp
)
```

**Step 4: Build and verify**

Run:
```bash
cd build && make -j4
```

**Step 5: Commit**

```bash
git add src/spatial_grid.hpp src/spatial_grid.cpp CMakeLists.txt
git commit -m "feat: add spatial grid and Index.bin writer"
```

---

## Task 7: Meta.lcc Generation

**Files:**
- Create: `src/meta_writer.hpp`
- Create: `src/meta_writer.cpp`
- Modify: `CMakeLists.txt`

**Step 1: Create meta_writer.hpp**

```cpp
#ifndef PLY2LCC_META_WRITER_HPP
#define PLY2LCC_META_WRITER_HPP

#include "types.hpp"
#include <string>
#include <vector>

namespace ply2lcc {

struct MetaInfo {
    std::string version = "5.0";
    std::string guid;
    std::string name = "XGrids Splats";
    std::string description = "Converted from PLY";
    std::string source = "ply";
    std::string dataType = "DIMENVUE";

    size_t total_splats = 0;
    size_t total_levels = 1;
    float cell_length_x = 30.0f;
    float cell_length_y = 30.0f;
    size_t index_data_size = 0;  // Calculated: 4 + 16 * total_levels

    Vec3f offset{0, 0, 0};
    int epsg = 0;
    Vec3f shift{0, 0, 0};
    Vec3f scale_transform{1, 1, 1};

    std::vector<size_t> splats_per_lod;
    BBox bounding_box;

    std::string encoding = "COMPRESS";
    std::string file_type = "Quality";  // or "Portable"

    AttributeRanges attr_ranges;
};

class MetaWriter {
public:
    static bool write(const std::string& path, const MetaInfo& meta);
    static std::string generate_guid();
};

} // namespace ply2lcc

#endif // PLY2LCC_META_WRITER_HPP
```

**Step 2: Create meta_writer.cpp**

```cpp
#include "meta_writer.hpp"
#include <fstream>
#include <sstream>
#include <iomanip>
#include <random>
#include <chrono>

namespace ply2lcc {

std::string MetaWriter::generate_guid() {
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<> dis(0, 15);

    std::stringstream ss;
    for (int i = 0; i < 32; ++i) {
        ss << std::hex << dis(gen);
    }
    return ss.str();
}

bool MetaWriter::write(const std::string& path, const MetaInfo& meta) {
    std::ofstream file(path);
    if (!file) {
        return false;
    }

    file << std::setprecision(15);

    file << "{\n";
    file << "\t\"version\": \"" << meta.version << "\",\n";
    file << "\t\"guid\": \"" << meta.guid << "\",\n";
    file << "\t\"name\": \"" << meta.name << "\",\n";
    file << "\t\"description\": \"" << meta.description << "\",\n";
    file << "\t\"source\": \"" << meta.source << "\",\n";
    file << "\t\"dataType\": \"" << meta.dataType << "\",\n";
    file << "\t\"totalSplats\": " << meta.total_splats << ",\n";
    file << "\t\"totalLevel\": " << meta.total_levels << ",\n";
    file << "\t\"cellLengthX\": " << meta.cell_length_x << ",\n";
    file << "\t\"cellLengthY\": " << meta.cell_length_y << ",\n";
    file << "\t\"indexDataSize\": " << meta.index_data_size << ",\n";
    file << "\t\"offset\": [" << meta.offset.x << ", " << meta.offset.y << ", " << meta.offset.z << "],\n";
    file << "\t\"epsg\": " << meta.epsg << ",\n";
    file << "\t\"shift\": [" << meta.shift.x << ", " << meta.shift.y << ", " << meta.shift.z << "],\n";
    file << "\t\"scale\": [" << meta.scale_transform.x << ", " << meta.scale_transform.y << ", " << meta.scale_transform.z << "],\n";

    // Splats per LOD
    file << "\t\"splats\": [";
    for (size_t i = 0; i < meta.splats_per_lod.size(); ++i) {
        if (i > 0) file << ", ";
        file << meta.splats_per_lod[i];
    }
    file << "],\n";

    // Bounding box
    file << "\t\"boundingBox\": {\n";
    file << "\t\t\"min\": [" << meta.bounding_box.min.x << ", " << meta.bounding_box.min.y << ", " << meta.bounding_box.min.z << "],\n";
    file << "\t\t\"max\": [" << meta.bounding_box.max.x << ", " << meta.bounding_box.max.y << ", " << meta.bounding_box.max.z << "]\n";
    file << "\t},\n";

    file << "\t\"encoding\": \"" << meta.encoding << "\",\n";
    file << "\t\"fileType\": \"" << meta.file_type << "\",\n";

    // Attributes
    file << "\t\"attributes\": [\n";

    // Position (use 10x scale as in reference)
    file << "\t\t{\n";
    file << "\t\t\t\"name\": \"position\",\n";
    file << "\t\t\t\"min\": [" << meta.bounding_box.min.x * 10 << ", " << meta.bounding_box.min.y * 10 << ", " << meta.bounding_box.min.z * 10 << "],\n";
    file << "\t\t\t\"max\": [" << meta.bounding_box.max.x * 10 << ", " << meta.bounding_box.max.y * 10 << ", " << meta.bounding_box.max.z * 10 << "]\n";
    file << "\t\t},\n";

    // Normal
    file << "\t\t{\n";
    file << "\t\t\t\"name\": \"normal\",\n";
    file << "\t\t\t\"min\": [0, 0, 0],\n";
    file << "\t\t\t\"max\": [0, 0, 0]\n";
    file << "\t\t},\n";

    // Color
    file << "\t\t{\n";
    file << "\t\t\t\"name\": \"color\",\n";
    file << "\t\t\t\"min\": [0, 0, 0],\n";
    file << "\t\t\t\"max\": [1, 1, 1]\n";
    file << "\t\t},\n";

    // SH coefficients
    file << "\t\t{\n";
    file << "\t\t\t\"name\": \"shcoef\",\n";
    file << "\t\t\t\"min\": [" << meta.attr_ranges.sh_min.x << ", " << meta.attr_ranges.sh_min.y << ", " << meta.attr_ranges.sh_min.z << "],\n";
    file << "\t\t\t\"max\": [" << meta.attr_ranges.sh_max.x << ", " << meta.attr_ranges.sh_max.y << ", " << meta.attr_ranges.sh_max.z << "]\n";
    file << "\t\t},\n";

    // Opacity
    file << "\t\t{\n";
    file << "\t\t\t\"name\": \"opacity\",\n";
    file << "\t\t\t\"min\": [" << meta.attr_ranges.opacity_min << "],\n";
    file << "\t\t\t\"max\": [" << meta.attr_ranges.opacity_max << "]\n";
    file << "\t\t},\n";

    // Scale
    file << "\t\t{\n";
    file << "\t\t\t\"name\": \"scale\",\n";
    file << "\t\t\t\"min\": [" << meta.attr_ranges.scale_min.x << ", " << meta.attr_ranges.scale_min.y << ", " << meta.attr_ranges.scale_min.z << "],\n";
    file << "\t\t\t\"max\": [" << meta.attr_ranges.scale_max.x << ", " << meta.attr_ranges.scale_max.y << ", " << meta.attr_ranges.scale_max.z << "]\n";
    file << "\t\t},\n";

    // Env placeholders
    file << "\t\t{\n";
    file << "\t\t\t\"name\": \"envnormal\",\n";
    file << "\t\t\t\"min\": [0, 0, 0],\n";
    file << "\t\t\t\"max\": [0, 0, 0]\n";
    file << "\t\t},\n";

    file << "\t\t{\n";
    file << "\t\t\t\"name\": \"envshcoef\",\n";
    file << "\t\t\t\"min\": [" << meta.attr_ranges.sh_min.x << ", " << meta.attr_ranges.sh_min.y << ", " << meta.attr_ranges.sh_min.z << "],\n";
    file << "\t\t\t\"max\": [" << meta.attr_ranges.sh_max.x << ", " << meta.attr_ranges.sh_max.y << ", " << meta.attr_ranges.sh_max.z << "]\n";
    file << "\t\t},\n";

    file << "\t\t{\n";
    file << "\t\t\t\"name\": \"envscale\",\n";
    file << "\t\t\t\"min\": [" << meta.attr_ranges.scale_min.x << ", " << meta.attr_ranges.scale_min.y << ", " << meta.attr_ranges.scale_min.z << "],\n";
    file << "\t\t\t\"max\": [" << meta.attr_ranges.scale_max.x << ", " << meta.attr_ranges.scale_max.y << ", " << meta.attr_ranges.scale_max.z << "]\n";
    file << "\t\t}\n";

    file << "\t]\n";
    file << "}\n";

    return true;
}

} // namespace ply2lcc
```

**Step 3: Update CMakeLists.txt**

```cmake
add_executable(ply2lcc
    src/main.cpp
    src/ply_reader.cpp
    src/compression.cpp
    src/lcc_writer.cpp
    src/spatial_grid.cpp
    src/meta_writer.cpp
    external/miniply/miniply.cpp
)
```

**Step 4: Build and verify**

Run:
```bash
cd build && make -j4
```

**Step 5: Commit**

```bash
git add src/meta_writer.hpp src/meta_writer.cpp CMakeLists.txt
git commit -m "feat: add meta.lcc JSON writer"
```

---

## Task 8: Full Pipeline Integration

**Files:**
- Modify: `src/main.cpp`

**Step 1: Rewrite main.cpp with full pipeline**

```cpp
#include <iostream>
#include <string>
#include <filesystem>
#include <vector>
#include <algorithm>
#include <regex>

#include "types.hpp"
#include "ply_reader.hpp"
#include "lcc_writer.hpp"
#include "spatial_grid.hpp"
#include "meta_writer.hpp"

namespace fs = std::filesystem;
using namespace ply2lcc;

void print_usage(const char* prog) {
    std::cerr << "Usage: " << prog << " <input_dir> -o <output_dir> [options]\n"
              << "Options:\n"
              << "  --single-lod       Use only LOD0 (default: multi-LOD)\n"
              << "  --cell-size X,Y    Grid cell size in meters (default: 30,30)\n";
}

std::vector<std::string> find_lod_files(const std::string& input_dir, bool single_lod) {
    std::vector<std::string> files;

    // LOD0 is point_cloud.ply
    std::string lod0 = input_dir + "/point_cloud.ply";
    if (fs::exists(lod0)) {
        files.push_back(lod0);
    }

    if (single_lod) {
        return files;
    }

    // Find point_cloud_N.ply files for LOD1+
    std::regex pattern("point_cloud_(\\d+)\\.ply");
    std::vector<std::pair<int, std::string>> numbered_files;

    for (const auto& entry : fs::directory_iterator(input_dir)) {
        std::string filename = entry.path().filename().string();
        std::smatch match;
        if (std::regex_match(filename, match, pattern)) {
            int num = std::stoi(match[1].str());
            numbered_files.emplace_back(num, entry.path().string());
        }
    }

    // Sort by number
    std::sort(numbered_files.begin(), numbered_files.end());

    for (const auto& [num, path] : numbered_files) {
        files.push_back(path);
    }

    return files;
}

int main(int argc, char* argv[]) {
    if (argc < 4) {
        print_usage(argv[0]);
        return 1;
    }

    ConvertConfig config;

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "-o" && i + 1 < argc) {
            config.output_dir = argv[++i];
        } else if (arg == "--single-lod") {
            config.single_lod = true;
        } else if (arg == "--cell-size" && i + 1 < argc) {
            if (sscanf(argv[++i], "%f,%f", &config.cell_size_x, &config.cell_size_y) != 2) {
                std::cerr << "Error: Invalid cell-size format. Use X,Y\n";
                return 1;
            }
        } else if (arg[0] != '-') {
            config.input_dir = arg;
        }
    }

    if (config.input_dir.empty() || config.output_dir.empty()) {
        print_usage(argv[0]);
        return 1;
    }

    std::cout << "Input: " << config.input_dir << "\n"
              << "Output: " << config.output_dir << "\n"
              << "Mode: " << (config.single_lod ? "single-lod" : "multi-lod") << "\n"
              << "Cell size: " << config.cell_size_x << " x " << config.cell_size_y << "\n";

    // Find PLY files
    auto ply_files = find_lod_files(config.input_dir, config.single_lod);
    if (ply_files.empty()) {
        std::cerr << "No point_cloud*.ply files found in " << config.input_dir << "\n";
        return 1;
    }

    std::cout << "Found " << ply_files.size() << " LOD files\n";
    for (const auto& f : ply_files) {
        std::cout << "  " << f << "\n";
    }

    // Check for environment.ply
    std::string env_path = config.input_dir + "/environment.ply";
    bool has_env = fs::exists(env_path);
    if (has_env) {
        std::cout << "Found environment.ply\n";
    }

    // Phase 1: Read all PLYs and compute global bounds
    std::cout << "\nPhase 1: Computing bounds...\n";

    BBox global_bbox;
    AttributeRanges global_ranges;
    std::vector<std::vector<Splat>> all_splats(ply_files.size());
    std::vector<size_t> splats_per_lod;
    bool has_sh = true;

    for (size_t lod = 0; lod < ply_files.size(); ++lod) {
        PLYHeader header;
        std::cout << "  Reading LOD" << lod << ": " << ply_files[lod] << "\n";

        if (!PLYReader::read_splats(ply_files[lod], all_splats[lod], header)) {
            std::cerr << "Failed to read " << ply_files[lod] << "\n";
            return 1;
        }

        std::cout << "    " << all_splats[lod].size() << " splats\n";
        splats_per_lod.push_back(all_splats[lod].size());
        global_bbox.expand(header.bbox);

        if (lod == 0) {
            has_sh = header.has_sh;
        }

        // Compute attribute ranges
        for (const auto& s : all_splats[lod]) {
            Vec3f linear_scale(std::exp(s.scale.x), std::exp(s.scale.y), std::exp(s.scale.z));
            global_ranges.expand_scale(linear_scale);
            global_ranges.expand_opacity(sigmoid(s.opacity));

            if (has_sh) {
                for (int i = 0; i < 45; ++i) {
                    global_ranges.expand_sh(s.f_rest[i]);
                }
            }
        }
    }

    std::cout << "Global bbox: (" << global_bbox.min.x << ", " << global_bbox.min.y << ", " << global_bbox.min.z
              << ") - (" << global_bbox.max.x << ", " << global_bbox.max.y << ", " << global_bbox.max.z << ")\n";
    std::cout << "Has SH: " << (has_sh ? "yes" : "no") << "\n";

    // Phase 2: Build spatial grid
    std::cout << "\nPhase 2: Building spatial grid...\n";

    SpatialGrid grid(config.cell_size_x, config.cell_size_y, global_bbox, ply_files.size());

    for (size_t lod = 0; lod < ply_files.size(); ++lod) {
        for (size_t i = 0; i < all_splats[lod].size(); ++i) {
            grid.add_splat(lod, all_splats[lod][i].pos, i);
        }
    }

    std::cout << "Created " << grid.get_cells().size() << " grid cells\n";

    // Phase 3: Write LCC data
    std::cout << "\nPhase 3: Writing LCC data...\n";

    fs::create_directories(config.output_dir);

    LCCWriter writer(config.output_dir, global_ranges, ply_files.size(), has_sh);

    for (const auto& [cell_index, cell] : grid.get_cells()) {
        for (size_t lod = 0; lod < ply_files.size(); ++lod) {
            if (cell.splat_indices[lod].empty()) continue;

            // Gather splats for this cell
            std::vector<Splat> cell_splats;
            cell_splats.reserve(cell.splat_indices[lod].size());
            for (size_t idx : cell.splat_indices[lod]) {
                cell_splats.push_back(all_splats[lod][idx]);
            }

            writer.write_splats(cell_index, lod, cell_splats);
        }
    }

    writer.finalize();

    // Phase 4: Write Index.bin
    std::cout << "\nPhase 4: Writing Index.bin...\n";
    grid.write_index_bin(config.output_dir + "/Index.bin", writer.get_units(), ply_files.size());

    // Phase 5: Write meta.lcc
    std::cout << "\nPhase 5: Writing meta.lcc...\n";

    MetaInfo meta;
    meta.guid = MetaWriter::generate_guid();
    meta.total_splats = writer.total_splats();
    meta.total_levels = ply_files.size();
    meta.cell_length_x = config.cell_size_x;
    meta.cell_length_y = config.cell_size_y;
    meta.index_data_size = 4 + 16 * ply_files.size();
    meta.splats_per_lod = splats_per_lod;
    meta.bounding_box = global_bbox;
    meta.file_type = has_sh ? "Quality" : "Portable";
    meta.attr_ranges = global_ranges;

    MetaWriter::write(config.output_dir + "/meta.lcc", meta);

    std::cout << "\nConversion complete!\n";
    std::cout << "Total splats: " << writer.total_splats() << "\n";
    std::cout << "Output files:\n";
    std::cout << "  " << config.output_dir << "/meta.lcc\n";
    std::cout << "  " << config.output_dir << "/Index.bin\n";
    std::cout << "  " << config.output_dir << "/Data.bin\n";
    if (has_sh) {
        std::cout << "  " << config.output_dir << "/Shcoef.bin\n";
    }

    return 0;
}
```

**Step 2: Build and test**

Run:
```bash
cd build && make -j4
./ply2lcc /home/linh/3dgs_ws/ply2lcc/CheonanVillagePLY/point_cloud/iteration_100 -o /tmp/lcc_test --single-lod
```

Expected: Creates LCC output in /tmp/lcc_test with meta.lcc, Index.bin, Data.bin, Shcoef.bin

**Step 3: Verify output structure**

Run:
```bash
ls -la /tmp/lcc_test/
cat /tmp/lcc_test/meta.lcc
```

**Step 4: Commit**

```bash
git add src/main.cpp
git commit -m "feat: complete pipeline integration with multi-LOD support"
```

---

## Task 9: Verification Against Reference

**Files:**
- None (verification only)

**Step 1: Compare output sizes**

Run:
```bash
# Convert point_cloud_2.ply (same as reference)
./ply2lcc /home/linh/3dgs_ws/ply2lcc/CheonanVillagePLY/point_cloud/iteration_100 -o /tmp/lcc_verify --single-lod

# Compare file sizes with reference
echo "=== Reference ==="
ls -la /home/linh/3dgs_ws/ply2lcc/CheonanVillagePLY/point_cloud/iteration_100/point_cloud_2_lccdata/

echo "=== Our output ==="
ls -la /tmp/lcc_verify/
```

**Step 2: Compare splat counts in meta.lcc**

Run:
```bash
echo "Reference totalSplats:"
grep totalSplats /home/linh/3dgs_ws/ply2lcc/CheonanVillagePLY/point_cloud/iteration_100/point_cloud_2_lccdata/converted_from_ply.lcc

echo "Our totalSplats:"
grep totalSplats /tmp/lcc_verify/meta.lcc
```

**Step 3: Hex dump comparison of Data.bin structure**

Run:
```bash
echo "Reference Data.bin (first 128 bytes):"
xxd -l 128 /home/linh/3dgs_ws/ply2lcc/CheonanVillagePLY/point_cloud/iteration_100/point_cloud_2_lccdata/data.bin

echo "Our Data.bin (first 128 bytes):"
xxd -l 128 /tmp/lcc_verify/Data.bin
```

Expected: 32-byte splat structure visible, positions should be recognizable floats.

**Step 4: Commit verification notes**

```bash
git add -A
git commit -m "docs: verification against reference LCC data"
```

---

## Summary

Tasks 1-8 implement the baseline converter. Task 9 verifies correctness.

**Next optimization phases (future tasks):**
- Phase 1: Memory-mapped PLY input
- Phase 2: Multi-threaded splat compression
- Phase 3: SIMD for encoding
- Phase 4: Parallel chunk writing

---

Plan complete and saved to `docs/plans/2026-02-04-ply2lcc-implementation.md`. Two execution options:

**1. Subagent-Driven (this session)** - I dispatch fresh subagent per task, review between tasks, fast iteration

**2. Parallel Session (separate)** - Open new session with executing-plans, batch execution with checkpoints

Which approach?