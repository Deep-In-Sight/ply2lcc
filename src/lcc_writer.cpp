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
