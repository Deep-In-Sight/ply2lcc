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

    void expand_sh(float r, float g, float b) {
        sh_min.x = std::min(sh_min.x, r);
        sh_min.y = std::min(sh_min.y, g);
        sh_min.z = std::min(sh_min.z, b);
        sh_max.x = std::max(sh_max.x, r);
        sh_max.y = std::max(sh_max.y, g);
        sh_max.z = std::max(sh_max.z, b);
    }

    void expand_opacity(float sigmoid_opacity) {
        if (sigmoid_opacity < opacity_min) opacity_min = sigmoid_opacity;
        if (sigmoid_opacity > opacity_max) opacity_max = sigmoid_opacity;
    }
};

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
