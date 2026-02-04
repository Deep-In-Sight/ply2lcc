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
