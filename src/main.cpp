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
