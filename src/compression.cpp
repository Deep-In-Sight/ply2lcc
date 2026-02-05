#include "compression.hpp"
#include "splat_buffer.hpp"
#include <cmath>
#include <algorithm>
#include <cstring>

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
    // Input: rot[0]=w, rot[1]=x, rot[2]=y, rot[3]=z (PLY format)
    // LCC uses (x,y,z,w) order internally for index selection
    // idx indicates which component in (x,y,z,w) was dropped and computed

    float w = rot[0], x = rot[1], y = rot[2], z = rot[3];

    // Normalize quaternion
    float len = std::sqrt(w*w + x*x + y*y + z*z);
    if (len > 0) {
        w /= len; x /= len; y /= len; z /= len;
    }

    // Find largest absolute component in (w,x,y,z) order
    float abs_vals[4] = {std::fabs(w), std::fabs(x), std::fabs(y), std::fabs(z)};
    int max_idx_wxyz = 0;
    for (int i = 1; i < 4; ++i) {
        if (abs_vals[i] > abs_vals[max_idx_wxyz]) max_idx_wxyz = i;
    }

    // Ensure the dropped component is positive (negate quaternion if needed)
    float quat[4] = {w, x, y, z};
    if (quat[max_idx_wxyz] < 0) {
        w = -w; x = -x; y = -y; z = -z;
    }

    // LCC uses (x,y,z,w) order. Map from wxyz index to LCC index:
    // wxyz idx 0 (w largest) -> LCC idx 3 (w at position 3 in xyzw)
    // wxyz idx 1 (x largest) -> LCC idx 0 (x at position 0 in xyzw)
    // wxyz idx 2 (y largest) -> LCC idx 1 (y at position 1 in xyzw)
    // wxyz idx 3 (z largest) -> LCC idx 2 (z at position 2 in xyzw)
    static const int wxyz_to_lcc_idx[4] = {3, 0, 1, 2};
    int lcc_idx = wxyz_to_lcc_idx[max_idx_wxyz];

    // Encoding order derived from QLut decoding:
    // LCC idx 0: encode (y,z,w) = rot[2,3,0]
    // LCC idx 1: encode (x,z,w) = rot[1,3,0]
    // LCC idx 2: encode (x,y,w) = rot[1,2,0]
    // LCC idx 3: encode (x,y,z) = rot[1,2,3]
    static const int order[4][3] = {
        {2, 3, 0},  // LCC idx 0: encode y, z, w
        {1, 3, 0},  // LCC idx 1: encode x, z, w
        {1, 2, 0},  // LCC idx 2: encode x, y, w
        {1, 2, 3}   // LCC idx 3: encode x, y, z
    };

    float src[4] = {w, x, y, z};
    float enc[3];
    for (int i = 0; i < 3; ++i) {
        enc[i] = src[order[lcc_idx][i]];
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

    // Pack: bits 0-9 = p0, bits 10-19 = p1, bits 20-29 = p2, bits 30-31 = lcc_idx
    return p0 | (p1 << 10) | (p2 << 20) | (static_cast<uint32_t>(lcc_idx) << 30);
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

void encode_splat_view(const SplatView& sv,
                       std::vector<uint8_t>& data_buf,
                       std::vector<uint8_t>& sh_buf,
                       const AttributeRanges& ranges,
                       bool has_sh) {
    size_t data_offset = data_buf.size();
    data_buf.resize(data_offset + 32);
    uint8_t* data_ptr = data_buf.data() + data_offset;

    // Position (12 bytes)
    const Vec3f& pos = sv.pos();
    std::memcpy(data_ptr, &pos.x, 12);
    data_ptr += 12;

    // Color RGBA (4 bytes)
    const Vec3f& f_dc = sv.f_dc();
    float f_dc_arr[3] = {f_dc.x, f_dc.y, f_dc.z};
    uint32_t color = encode_color(f_dc_arr, sv.opacity());
    std::memcpy(data_ptr, &color, 4);
    data_ptr += 4;

    // Scale (6 bytes)
    uint16_t scale_enc[3];
    encode_scale(sv.scale(), ranges.scale_min, ranges.scale_max, scale_enc);
    std::memcpy(data_ptr, scale_enc, 6);
    data_ptr += 6;

    // Rotation (4 bytes)
    const Quat& rot = sv.rot();
    float rot_arr[4] = {rot.w, rot.x, rot.y, rot.z};
    uint32_t rot_enc = encode_rotation(rot_arr);
    std::memcpy(data_ptr, &rot_enc, 4);
    data_ptr += 4;

    // Normal (6 bytes) - zeros for 3DGS
    uint16_t normal_enc[3] = {0, 0, 0};
    std::memcpy(data_ptr, normal_enc, 6);

    // SH coefficients (64 bytes)
    if (has_sh) {
        size_t sh_offset = sh_buf.size();
        sh_buf.resize(sh_offset + 64);
        uint8_t* sh_ptr = sh_buf.data() + sh_offset;

        // Copy f_rest to array
        float f_rest[45];
        for (int i = 0; i < sv.num_f_rest() && i < 45; ++i) {
            f_rest[i] = sv.f_rest(i);
        }
        for (int i = sv.num_f_rest(); i < 45; ++i) {
            f_rest[i] = 0.0f;
        }

        uint32_t sh_enc[16];
        encode_sh_coefficients(f_rest, ranges.sh_min.x, ranges.sh_max.x, sh_enc);
        std::memcpy(sh_ptr, sh_enc, 64);
    }
}

} // namespace ply2lcc
