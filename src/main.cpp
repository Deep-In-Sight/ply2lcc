#include <iostream>
#include <string>
#include <filesystem>
#include <vector>
#include <algorithm>
#include <regex>

#include "types.hpp"
#include "ply_reader.hpp"
#include "lcc_writer.hpp"
#include "spatial_grid.hpp"
#include "meta_writer.hpp"
#include "path_resolution.hpp"

namespace fs = std::filesystem;
using namespace ply2lcc;

void print_usage(const char* prog) {
    std::cerr << "Usage: " << prog << " <input_dir> -o <output_dir> [options]\n"
              << "\n"
              << "Expected input structure:\n"
              << "  input_dir/point_cloud/iteration_<N>/*.ply\n"
              << "\n"
              << "Output structure:\n"
              << "  output_dir/LCC_Results/{meta.lcc, Data.bin, Index.bin, ...}\n"
              << "\n"
              << "Options:\n"
              << "  --single-lod       Use only LOD0 (default: multi-LOD)\n"
              << "  --cell-size X,Y    Grid cell size in meters (default: 30,30)\n";
}

std::vector<std::string> find_lod_files(const std::string& input_dir, bool single_lod) {
    std::vector<std::string> files;

    // LOD0 is point_cloud.ply
    std::string lod0 = input_dir + "/point_cloud.ply";
    if (fs::exists(lod0)) {
        files.push_back(lod0);
    }

    if (single_lod) {
        return files;
    }

    // Find point_cloud_N.ply files for LOD1+
    std::regex pattern("point_cloud_(\\d+)\\.ply");
    std::vector<std::pair<int, std::string>> numbered_files;

    for (const auto& entry : fs::directory_iterator(input_dir)) {
        std::string filename = entry.path().filename().string();
        std::smatch match;
        if (std::regex_match(filename, match, pattern)) {
            int num = std::stoi(match[1].str());
            numbered_files.emplace_back(num, entry.path().string());
        }
    }

    // Sort by number
    std::sort(numbered_files.begin(), numbered_files.end());

    for (const auto& [num, path] : numbered_files) {
        files.push_back(path);
    }

    return files;
}

int main(int argc, char* argv[]) {
    if (argc < 4) {
        print_usage(argv[0]);
        return 1;
    }

    ConvertConfig config;

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "-o" && i + 1 < argc) {
            config.output_dir = argv[++i];
        } else if (arg == "--single-lod") {
            config.single_lod = true;
        } else if (arg == "--cell-size" && i + 1 < argc) {
            if (sscanf(argv[++i], "%f,%f", &config.cell_size_x, &config.cell_size_y) != 2) {
                std::cerr << "Error: Invalid cell-size format. Use X,Y\n";
                return 1;
            }
        } else if (arg[0] != '-') {
            config.input_dir = arg;
        }
    }

    if (config.input_dir.empty() || config.output_dir.empty()) {
        print_usage(argv[0]);
        return 1;
    }

    // Resolve input path
    auto input_result = resolve_input_path(config.input_dir);
    if (!input_result.has_value()) {
        std::cerr << "Error: " << input_result.error() << "\n";
        return 1;
    }

    std::string iteration_dir = input_result->path;
    std::cout << "Input: " << config.input_dir << "\n"
              << "  Using: point_cloud/iteration_" << input_result->iteration_number << "/\n";

    // Resolve output path
    std::string lcc_output_dir = resolve_output_path(config.output_dir);
    std::cout << "Output: " << lcc_output_dir << "\n"
              << "Mode: " << (config.single_lod ? "single-lod" : "multi-lod") << "\n"
              << "Cell size: " << config.cell_size_x << " x " << config.cell_size_y << "\n";

    // Find PLY files in resolved iteration directory
    auto ply_files = find_lod_files(iteration_dir, config.single_lod);
    if (ply_files.empty()) {
        std::cerr << "No point_cloud*.ply files found in " << iteration_dir << "\n";
        return 1;
    }

    std::cout << "Found " << ply_files.size() << " LOD files\n";
    for (const auto& f : ply_files) {
        std::cout << "  " << f << "\n";
    }

    // Check for environment.ply
    std::string env_path = iteration_dir + "/environment.ply";
    bool has_env = fs::exists(env_path);
    if (has_env) {
        std::cout << "Found environment.ply\n";
    }

    // Phase 1: Read all PLYs and compute global bounds
    std::cout << "\nPhase 1: Computing bounds...\n";

    BBox global_bbox;
    AttributeRanges global_ranges;
    std::vector<std::vector<Splat>> all_splats(ply_files.size());
    std::vector<size_t> splats_per_lod;
    bool has_sh = true;

    for (size_t lod = 0; lod < ply_files.size(); ++lod) {
        PLYHeader header;
        std::cout << "  Reading LOD" << lod << ": " << ply_files[lod] << "\n";

        if (!PLYReader::read_splats(ply_files[lod], all_splats[lod], header)) {
            std::cerr << "Failed to read " << ply_files[lod] << "\n";
            return 1;
        }

        std::cout << "    " << all_splats[lod].size() << " splats\n";
        splats_per_lod.push_back(all_splats[lod].size());
        global_bbox.expand(header.bbox);

        if (lod == 0) {
            has_sh = header.has_sh;
        }

        // Compute attribute ranges
        for (const auto& s : all_splats[lod]) {
            Vec3f linear_scale(std::exp(s.scale.x), std::exp(s.scale.y), std::exp(s.scale.z));
            global_ranges.expand_scale(linear_scale);
            global_ranges.expand_opacity(sigmoid(s.opacity));

            if (has_sh) {
                for (int i = 0; i < 45; ++i) {
                    global_ranges.expand_sh(s.f_rest[i]);
                }
            }
        }
    }

    std::cout << "Global bbox: (" << global_bbox.min.x << ", " << global_bbox.min.y << ", " << global_bbox.min.z
              << ") - (" << global_bbox.max.x << ", " << global_bbox.max.y << ", " << global_bbox.max.z << ")\n";
    std::cout << "Has SH: " << (has_sh ? "yes" : "no") << "\n";

    // Phase 2: Build spatial grid
    std::cout << "\nPhase 2: Building spatial grid...\n";

    SpatialGrid grid(config.cell_size_x, config.cell_size_y, global_bbox, ply_files.size());

    for (size_t lod = 0; lod < ply_files.size(); ++lod) {
        for (size_t i = 0; i < all_splats[lod].size(); ++i) {
            grid.add_splat(lod, all_splats[lod][i].pos, i);
        }
    }

    std::cout << "Created " << grid.get_cells().size() << " grid cells\n";

    // Phase 3: Write LCC data
    std::cout << "\nPhase 3: Writing LCC data...\n";

    LCCWriter writer(lcc_output_dir, global_ranges, ply_files.size(), has_sh);

    for (const auto& [cell_index, cell] : grid.get_cells()) {
        for (size_t lod = 0; lod < ply_files.size(); ++lod) {
            if (cell.splat_indices[lod].empty()) continue;

            // Gather splats for this cell
            std::vector<Splat> cell_splats;
            cell_splats.reserve(cell.splat_indices[lod].size());
            for (size_t idx : cell.splat_indices[lod]) {
                cell_splats.push_back(all_splats[lod][idx]);
            }

            writer.write_splats(cell_index, lod, cell_splats);
        }
    }

    writer.finalize();

    // Phase 4: Write Index.bin
    std::cout << "\nPhase 4: Writing Index.bin...\n";
    grid.write_index_bin(lcc_output_dir + "/Index.bin", writer.get_units(), ply_files.size());

    // Phase 5: Write meta.lcc
    std::cout << "\nPhase 5: Writing meta.lcc...\n";

    MetaInfo meta;
    meta.guid = MetaWriter::generate_guid();
    meta.total_splats = writer.total_splats();
    meta.total_levels = ply_files.size();
    meta.cell_length_x = config.cell_size_x;
    meta.cell_length_y = config.cell_size_y;
    meta.index_data_size = 4 + 16 * ply_files.size();
    meta.splats_per_lod = splats_per_lod;
    meta.bounding_box = global_bbox;
    meta.file_type = has_sh ? "Quality" : "Portable";
    meta.attr_ranges = global_ranges;

    MetaWriter::write(lcc_output_dir + "/meta.lcc", meta);

    std::cout << "\nConversion complete!\n";
    std::cout << "Total splats: " << writer.total_splats() << "\n";
    std::cout << "Output files:\n";
    std::cout << "  " << lcc_output_dir << "/meta.lcc\n";
    std::cout << "  " << lcc_output_dir << "/Index.bin\n";
    std::cout << "  " << lcc_output_dir << "/Data.bin\n";
    if (has_sh) {
        std::cout << "  " << lcc_output_dir << "/Shcoef.bin\n";
    }

    return 0;
}
