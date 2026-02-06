#include "lcc_writer.hpp"
#include <fstream>
#include <sstream>
#include <iomanip>
#include <random>
#include <filesystem>
#include <algorithm>

namespace fs = std::filesystem;

namespace ply2lcc {

LccWriter::LccWriter(const std::string& output_dir)
    : output_dir_(output_dir) {
    fs::create_directories(output_dir_);
}

void LccWriter::write(const LccData& data) {
    write_data_bin(data);
    write_index_bin(data);
    write_meta_lcc(data);
    write_attrs_lcp();
}

void LccWriter::write_environment(const EncodedEnvironment& env, bool /*has_sh*/) {
    if (env.empty()) return;

    std::ofstream file(output_dir_ + "/environment.bin", std::ios::binary);
    if (!file) return;

    file.write(reinterpret_cast<const char*>(env.data.data()), env.data.size());
}

void LccWriter::write_data_bin(const LccData& data) {
    std::ofstream data_file(output_dir_ + "/data.bin", std::ios::binary);
    if (!data_file) {
        throw std::runtime_error("Failed to create data.bin");
    }

    std::ofstream sh_file;
    if (data.has_sh) {
        sh_file.open(output_dir_ + "/shcoef.bin", std::ios::binary);
        if (!sh_file) {
            throw std::runtime_error("Failed to create shcoef.bin");
        }
    }

    for (const auto& cell : data.cells) {
        if (cell.count == 0) continue;

        data_file.write(reinterpret_cast<const char*>(cell.data.data()), cell.data.size());

        if (data.has_sh && !cell.shcoef.empty()) {
            sh_file.write(reinterpret_cast<const char*>(cell.shcoef.data()), cell.shcoef.size());
        }
    }
}

void LccWriter::write_index_bin(const LccData& data) {
    std::ofstream file(output_dir_ + "/index.bin", std::ios::binary);
    if (!file) {
        throw std::runtime_error("Failed to create index.bin");
    }

    // Build units from sorted cells
    uint64_t data_offset = 0;
    uint64_t sh_offset = 0;
    auto units = data.build_index(data_offset, sh_offset);

    // Write each unit
    for (const auto& unit : units) {
        // Write unit index (4 bytes)
        file.write(reinterpret_cast<const char*>(&unit.index), 4);

        // Write LOD entries
        for (size_t lod = 0; lod < data.num_lods; ++lod) {
            const LccNodeInfo& node = unit.lods[lod];
            file.write(reinterpret_cast<const char*>(&node.splat_count), 4);
            file.write(reinterpret_cast<const char*>(&node.data_offset), 8);
            file.write(reinterpret_cast<const char*>(&node.data_size), 4);
        }
    }
}

std::string LccWriter::generate_guid() {
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<> dis(0, 15);

    std::stringstream ss;
    for (int i = 0; i < 32; ++i) {
        ss << std::hex << dis(gen);
    }
    return ss.str();
}

void LccWriter::write_meta_lcc(const LccData& data) {
    std::ofstream file(output_dir_ + "/meta.lcc");
    if (!file) {
        throw std::runtime_error("Failed to create meta.lcc");
    }

    bool has_environment = !data.environment.empty();
    std::string file_type = data.has_sh ? "Quality" : "Portable";

    file << std::setprecision(15);

    file << "{\n";
    file << "\t\"version\": \"5.0\",\n";
    file << "\t\"guid\": \"" << generate_guid() << "\",\n";
    file << "\t\"name\": \"XGrids Splats\",\n";
    file << "\t\"description\": \"Converted from PLY\",\n";
    file << "\t\"source\": \"ply\",\n";
    file << "\t\"dataType\": \"DIMENVUE\",\n";
    file << "\t\"totalSplats\": " << data.total_splats << ",\n";
    file << "\t\"totalLevel\": " << data.num_lods << ",\n";
    file << "\t\"cellLengthX\": " << data.cell_size_x << ",\n";
    file << "\t\"cellLengthY\": " << data.cell_size_y << ",\n";
    file << "\t\"indexDataSize\": " << (4 + 16 * data.num_lods) << ",\n";
    file << "\t\"offset\": [0, 0, 0],\n";
    file << "\t\"epsg\": 0,\n";
    file << "\t\"shift\": [0, 0, 0],\n";
    file << "\t\"scale\": [1, 1, 1],\n";

    // Splats per LOD
    file << "\t\"splats\": [";
    for (size_t i = 0; i < data.splats_per_lod.size(); ++i) {
        if (i > 0) file << ", ";
        file << data.splats_per_lod[i];
    }
    file << "],\n";

    // Bounding box
    file << "\t\"boundingBox\": {\n";
    file << "\t\t\"min\": [" << data.bbox.min.x << ", " << data.bbox.min.y << ", " << data.bbox.min.z << "],\n";
    file << "\t\t\"max\": [" << data.bbox.max.x << ", " << data.bbox.max.y << ", " << data.bbox.max.z << "]\n";
    file << "\t},\n";

    file << "\t\"encoding\": \"COMPRESS\",\n";
    file << "\t\"fileType\": \"" << file_type << "\",\n";

    // Attributes
    file << "\t\"attributes\": [\n";

    // Position - use environment bounds if available
    file << "\t\t{\n";
    file << "\t\t\t\"name\": \"position\",\n";
    if (has_environment) {
        file << "\t\t\t\"min\": [" << data.environment.bounds.pos_min.x << ", " << data.environment.bounds.pos_min.y << ", " << data.environment.bounds.pos_min.z << "],\n";
        file << "\t\t\t\"max\": [" << data.environment.bounds.pos_max.x << ", " << data.environment.bounds.pos_max.y << ", " << data.environment.bounds.pos_max.z << "]\n";
    } else {
        file << "\t\t\t\"min\": [" << data.bbox.min.x << ", " << data.bbox.min.y << ", " << data.bbox.min.z << "],\n";
        file << "\t\t\t\"max\": [" << data.bbox.max.x << ", " << data.bbox.max.y << ", " << data.bbox.max.z << "]\n";
    }
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
    if (file_type == "Portable") {
        file << "\t\t\t\"min\": [0, 0, 0],\n";
        file << "\t\t\t\"max\": [1, 1, 1]\n";
    } else {
        file << "\t\t\t\"min\": [" << data.ranges.sh_min.x << ", " << data.ranges.sh_min.y << ", " << data.ranges.sh_min.z << "],\n";
        file << "\t\t\t\"max\": [" << data.ranges.sh_max.x << ", " << data.ranges.sh_max.y << ", " << data.ranges.sh_max.z << "]\n";
    }
    file << "\t\t},\n";

    // Opacity
    file << "\t\t{\n";
    file << "\t\t\t\"name\": \"opacity\",\n";
    file << "\t\t\t\"min\": [" << data.ranges.opacity_min << "],\n";
    file << "\t\t\t\"max\": [" << data.ranges.opacity_max << "]\n";
    file << "\t\t},\n";

    // Scale
    file << "\t\t{\n";
    file << "\t\t\t\"name\": \"scale\",\n";
    file << "\t\t\t\"min\": [" << data.ranges.scale_min.x << ", " << data.ranges.scale_min.y << ", " << data.ranges.scale_min.z << "],\n";
    file << "\t\t\t\"max\": [" << data.ranges.scale_max.x << ", " << data.ranges.scale_max.y << ", " << data.ranges.scale_max.z << "]\n";
    file << "\t\t},\n";

    // Env placeholders
    file << "\t\t{\n";
    file << "\t\t\t\"name\": \"envnormal\",\n";
    file << "\t\t\t\"min\": [0, 0, 0],\n";
    file << "\t\t\t\"max\": [0, 0, 0]\n";
    file << "\t\t},\n";

    file << "\t\t{\n";
    file << "\t\t\t\"name\": \"envshcoef\",\n";
    if (file_type == "Portable") {
        file << "\t\t\t\"min\": [0, 0, 0],\n";
        file << "\t\t\t\"max\": [1, 1, 1]\n";
    } else if (has_environment) {
        file << "\t\t\t\"min\": [" << data.environment.bounds.sh_min.x << ", " << data.environment.bounds.sh_min.y << ", " << data.environment.bounds.sh_min.z << "],\n";
        file << "\t\t\t\"max\": [" << data.environment.bounds.sh_max.x << ", " << data.environment.bounds.sh_max.y << ", " << data.environment.bounds.sh_max.z << "]\n";
    } else {
        file << "\t\t\t\"min\": [" << data.ranges.sh_min.x << ", " << data.ranges.sh_min.y << ", " << data.ranges.sh_min.z << "],\n";
        file << "\t\t\t\"max\": [" << data.ranges.sh_max.x << ", " << data.ranges.sh_max.y << ", " << data.ranges.sh_max.z << "]\n";
    }
    file << "\t\t},\n";

    file << "\t\t{\n";
    file << "\t\t\t\"name\": \"envscale\",\n";
    if (has_environment) {
        file << "\t\t\t\"min\": [" << data.environment.bounds.scale_min.x << ", " << data.environment.bounds.scale_min.y << ", " << data.environment.bounds.scale_min.z << "],\n";
        file << "\t\t\t\"max\": [" << data.environment.bounds.scale_max.x << ", " << data.environment.bounds.scale_max.y << ", " << data.environment.bounds.scale_max.z << "]\n";
    } else {
        file << "\t\t\t\"min\": [" << data.ranges.scale_min.x << ", " << data.ranges.scale_min.y << ", " << data.ranges.scale_min.z << "],\n";
        file << "\t\t\t\"max\": [" << data.ranges.scale_max.x << ", " << data.ranges.scale_max.y << ", " << data.ranges.scale_max.z << "]\n";
    }
    file << "\t\t}\n";

    file << "\t]\n";
    file << "}\n";
}

void LccWriter::write_attrs_lcp() {
    std::ofstream file(output_dir_ + "/attrs.lcp");
    if (!file) {
        throw std::runtime_error("Failed to create attrs.lcp");
    }

    file << "{";
    file << "\"spawnPoint\":{";
    file << "\"position\":[0,0,0],";
    file << "\"rotation\":[0.7071068,0,0,0.7071068]";
    file << "},";
    file << "\"transform\":{";
    file << "\"position\":[0,0,0],";
    file << "\"rotation\":[0,0,0,1],";
    file << "\"scale\":[1,1,1]";
    file << "}";
    file << "}\n";
}

} // namespace ply2lcc
