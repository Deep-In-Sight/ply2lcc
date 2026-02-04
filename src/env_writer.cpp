#include "env_writer.hpp"
#include "ply_reader.hpp"
#include "compression.hpp"
#include <fstream>
#include <cmath>
#include <cstring>

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
        std::memcpy(buffer.data(), &s.pos.x, 4);
        std::memcpy(buffer.data() + 4, &s.pos.y, 4);
        std::memcpy(buffer.data() + 8, &s.pos.z, 4);

        // Color (4 bytes RGBA)
        uint32_t color = encode_color(s.f_dc, s.opacity);
        std::memcpy(buffer.data() + 12, &color, 4);

        // Scale (6 bytes)
        uint16_t scale_encoded[3];
        encode_scale(s.scale, bounds.scale_min, bounds.scale_max, scale_encoded);
        std::memcpy(buffer.data() + 16, scale_encoded, 6);

        // Rotation (4 bytes)
        uint32_t rot = encode_rotation(s.rot);
        std::memcpy(buffer.data() + 22, &rot, 4);

        // Normal (6 bytes) - zeros
        std::memset(buffer.data() + 26, 0, 6);

        // Next 64 bytes: SH coefficients (16 uint32 values)
        // Use the minimum of the per-channel mins and maximum of per-channel maxes
        // for scalar sh_min/sh_max required by encode_sh_coefficients
        float sh_min_scalar = std::min({bounds.sh_min.x, bounds.sh_min.y, bounds.sh_min.z});
        float sh_max_scalar = std::max({bounds.sh_max.x, bounds.sh_max.y, bounds.sh_max.z});
        uint32_t sh_encoded[16];
        encode_sh_coefficients(s.f_rest, sh_min_scalar, sh_max_scalar, sh_encoded);
        std::memcpy(buffer.data() + 32, sh_encoded, 64);

        file.write(reinterpret_cast<const char*>(buffer.data()), 96);
    }

    return true;
}

}  // namespace ply2lcc
