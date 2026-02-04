#include <iostream>
#include <string>
#include <filesystem>

#include "types.hpp"
#include "ply_reader.hpp"

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
}
