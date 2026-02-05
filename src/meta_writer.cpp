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

    // SH coefficients - use placeholder for Portable mode
    file << "\t\t{\n";
    file << "\t\t\t\"name\": \"shcoef\",\n";
    if (meta.file_type == "Portable") {
        file << "\t\t\t\"min\": [0, 0, 0],\n";
        file << "\t\t\t\"max\": [1, 1, 1]\n";
    } else {
        file << "\t\t\t\"min\": [" << meta.attr_ranges.sh_min.x << ", " << meta.attr_ranges.sh_min.y << ", " << meta.attr_ranges.sh_min.z << "],\n";
        file << "\t\t\t\"max\": [" << meta.attr_ranges.sh_max.x << ", " << meta.attr_ranges.sh_max.y << ", " << meta.attr_ranges.sh_max.z << "]\n";
    }
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
    if (meta.file_type == "Portable") {
        // Portable mode: use placeholder values
        file << "\t\t\t\"min\": [0, 0, 0],\n";
        file << "\t\t\t\"max\": [1, 1, 1]\n";
    } else if (meta.has_environment) {
        file << "\t\t\t\"min\": [" << meta.env_bounds.sh_min.x << ", " << meta.env_bounds.sh_min.y << ", " << meta.env_bounds.sh_min.z << "],\n";
        file << "\t\t\t\"max\": [" << meta.env_bounds.sh_max.x << ", " << meta.env_bounds.sh_max.y << ", " << meta.env_bounds.sh_max.z << "]\n";
    } else {
        file << "\t\t\t\"min\": [" << meta.attr_ranges.sh_min.x << ", " << meta.attr_ranges.sh_min.y << ", " << meta.attr_ranges.sh_min.z << "],\n";
        file << "\t\t\t\"max\": [" << meta.attr_ranges.sh_max.x << ", " << meta.attr_ranges.sh_max.y << ", " << meta.attr_ranges.sh_max.z << "]\n";
    }
    file << "\t\t},\n";

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

    file << "\t]\n";
    file << "}\n";

    return true;
}

} // namespace ply2lcc
