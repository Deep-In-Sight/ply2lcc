#include "lcc_writer.hpp"
#include "platform.hpp"
#include <fstream>
#include <sstream>
#include <iomanip>
#include <random>
#include <filesystem>
#include <algorithm>

namespace fs = std::filesystem;

namespace ply2lcc {

LccWriter::LccWriter(const std::filesystem::path& output_dir)
    : output_dir_(output_dir) {
    fs::create_directories(output_dir_);
}

void LccWriter::write(const LccData& data) {
    write_data_bin(data);
    write_index_bin(data);
    write_meta_lcc(data);
    write_attrs_lcp(data);
    write_environment(data);
    write_collision(data);
    write_poses(data);
}

void LccWriter::write_environment(const LccData& data) {
    if (data.environment.empty()) return;

    auto file = platform::ofstream_open(output_dir_ / "environment.bin");
    if (!file) return;

    file.write(reinterpret_cast<const char*>(data.environment.data.data()),
               data.environment.data.size());
}

void LccWriter::write_collision(const LccData& data) {
    if (data.collision.empty()) return;

    auto file = platform::ofstream_open(output_dir_ / "collision.lci");
    if (!file) {
        throw std::runtime_error("Failed to create collision.lci");
    }

    constexpr uint32_t MAGIC = 0x6c6c6f63;  // "coll"
    constexpr uint32_t VERSION = 2;

    const auto& collision = data.collision;
    uint32_t mesh_num = static_cast<uint32_t>(collision.cells.size());
    uint32_t header_len = 48 + 40 * mesh_num;

    // Write main header
    file.write(reinterpret_cast<const char*>(&MAGIC), 4);
    file.write(reinterpret_cast<const char*>(&VERSION), 4);
    file.write(reinterpret_cast<const char*>(&header_len), 4);

    // Bounding box
    file.write(reinterpret_cast<const char*>(&collision.bbox.min.x), 4);
    file.write(reinterpret_cast<const char*>(&collision.bbox.min.y), 4);
    file.write(reinterpret_cast<const char*>(&collision.bbox.min.z), 4);
    file.write(reinterpret_cast<const char*>(&collision.bbox.max.x), 4);
    file.write(reinterpret_cast<const char*>(&collision.bbox.max.y), 4);
    file.write(reinterpret_cast<const char*>(&collision.bbox.max.z), 4);

    // Cell sizes
    file.write(reinterpret_cast<const char*>(&collision.cell_size_x), 4);
    file.write(reinterpret_cast<const char*>(&collision.cell_size_y), 4);

    // Mesh count
    file.write(reinterpret_cast<const char*>(&mesh_num), 4);

    // Calculate data offsets
    uint64_t current_offset = header_len;
    std::vector<uint64_t> offsets(mesh_num);
    std::vector<uint64_t> sizes(mesh_num);

    for (size_t i = 0; i < collision.cells.size(); ++i) {
        const auto& cell = collision.cells[i];
        offsets[i] = current_offset;

        uint64_t vertex_size = cell.vertices.size() * 3 * sizeof(float);
        uint64_t face_size = cell.faces.size() * 3 * sizeof(uint32_t);
        uint64_t bvh_size = cell.bvh_data.size();
        sizes[i] = vertex_size + face_size + bvh_size;

        current_offset += sizes[i];
    }

    // Write mesh headers
    for (size_t i = 0; i < collision.cells.size(); ++i) {
        const auto& cell = collision.cells[i];

        uint32_t index_x = cell.index & 0xFFFF;
        uint32_t index_y = (cell.index >> 16) & 0xFFFF;
        uint64_t offset = offsets[i];
        uint64_t bytes_size = sizes[i];
        uint32_t vertex_num = static_cast<uint32_t>(cell.vertices.size());
        uint32_t face_num = static_cast<uint32_t>(cell.faces.size());
        uint32_t bvh_size = static_cast<uint32_t>(cell.bvh_data.size());
        uint32_t reserved = 0;

        file.write(reinterpret_cast<const char*>(&index_x), 4);
        file.write(reinterpret_cast<const char*>(&index_y), 4);
        file.write(reinterpret_cast<const char*>(&offset), 8);
        file.write(reinterpret_cast<const char*>(&bytes_size), 8);
        file.write(reinterpret_cast<const char*>(&vertex_num), 4);
        file.write(reinterpret_cast<const char*>(&face_num), 4);
        file.write(reinterpret_cast<const char*>(&bvh_size), 4);
        file.write(reinterpret_cast<const char*>(&reserved), 4);
    }

    // Write mesh data
    for (const auto& cell : collision.cells) {
        // Vertices (float32 x 3 per vertex)
        for (const auto& v : cell.vertices) {
            file.write(reinterpret_cast<const char*>(&v.x), 4);
            file.write(reinterpret_cast<const char*>(&v.y), 4);
            file.write(reinterpret_cast<const char*>(&v.z), 4);
        }

        // Faces (uint32 x 3 per face)
        for (const auto& f : cell.faces) {
            file.write(reinterpret_cast<const char*>(&f.v0), 4);
            file.write(reinterpret_cast<const char*>(&f.v1), 4);
            file.write(reinterpret_cast<const char*>(&f.v2), 4);
        }

        // BVH data
        if (!cell.bvh_data.empty()) {
            file.write(reinterpret_cast<const char*>(cell.bvh_data.data()),
                       cell.bvh_data.size());
        }
    }
}

void LccWriter::write_data_bin(const LccData& data) {
    auto data_file = platform::ofstream_open(output_dir_ / "data.bin");
    if (!data_file) {
        throw std::runtime_error("Failed to create data.bin");
    }

    std::ofstream sh_file;
    if (data.has_sh) {
        sh_file = platform::ofstream_open(output_dir_ / "shcoef.bin");
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
    auto file = platform::ofstream_open(output_dir_ / "index.bin");
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
    auto file = platform::ofstream_open(output_dir_ / "meta.lcc", std::ios::out);
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
    file << "\t\"source\": \"lcc\",\n";
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

void LccWriter::write_poses(const LccData& data) {
    if (data.poses_path.empty()) return;

    // Create assets directory if needed
    auto assets_dir = output_dir_ / "assets";
    fs::create_directories(assets_dir);

    fs::copy_file(data.poses_path, assets_dir / "poses.json", fs::copy_options::overwrite_existing);
}

void LccWriter::write_attrs_lcp(const LccData& data) {
    auto file = platform::ofstream_open(output_dir_ / "attrs.lcp", std::ios::out);
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
    if (!data.collision.empty()) {
        file << ",\"collider\":{\"simpleMesh\":{\"type\":\"ply\",\"path\":\"collision.lci\"}}";
    }
    if (!data.poses_path.empty()) {
        file << ",\"poses\":{\"path\":\"assets/poses.json\"}";
    }
    file << "}\n";
}

} // namespace ply2lcc
