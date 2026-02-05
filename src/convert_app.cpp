#include "convert_app.hpp"
#include "ply_reader.hpp"
#include "lcc_writer.hpp"
#include "spatial_grid.hpp"
#include "meta_writer.hpp"
#include "env_writer.hpp"
#include "attrs_writer.hpp"

#include <iostream>
#include <filesystem>
#include <algorithm>
#include <regex>
#include <cmath>
#include <stdexcept>

namespace fs = std::filesystem;

namespace ply2lcc {

ConvertApp::ConvertApp(int argc, char** argv)
    : argc_(argc), argv_(argv) {}

void ConvertApp::run() {
    parseArgs();
    findPlyFiles();
    validateOutput();
    computeBounds();
    buildSpatialGrid();
    writeLccData();
    writeEnvironment();
    writeIndex();
    writeMeta();
    writeAttrs();

    std::cout << "\nConversion complete!\n";
    std::cout << "Total splats: " << total_splats_ << "\n";
    std::cout << "Output: " << output_dir_ << "\n";
}

void ConvertApp::printUsage() {
    std::cerr << "Usage: " << argv_[0] << " -i <input.ply> -o <output_dir> [options]\n"
              << "\n"
              << "Options:\n"
              << "  --single-lod       Use only LOD0 even if more LOD files exist\n"
              << "  --cell-size X,Y    Grid cell size in meters (default: 30,30)\n";
}

void ConvertApp::parseArgs() {
    for (int i = 1; i < argc_; ++i) {
        std::string arg = argv_[i];
        if (arg == "-i" && i + 1 < argc_) {
            input_path_ = argv_[++i];
        } else if (arg == "-o" && i + 1 < argc_) {
            output_dir_ = argv_[++i];
        } else if (arg == "--single-lod") {
            single_lod_ = true;
        } else if (arg == "--cell-size" && i + 1 < argc_) {
            if (sscanf(argv_[++i], "%f,%f", &cell_size_x_, &cell_size_y_) != 2) {
                throw std::runtime_error("Invalid cell-size format. Use X,Y");
            }
        } else if (arg == "-h" || arg == "--help") {
            printUsage();
            std::exit(EXIT_SUCCESS);
        }
    }

    if (input_path_.empty() || output_dir_.empty()) {
        printUsage();
        throw std::runtime_error("Missing required arguments: -i and -o");
    }

    if (!fs::exists(input_path_)) {
        throw std::runtime_error("Input file not found: " + input_path_);
    }

    // Extract directory and base name
    fs::path p(input_path_);
    input_dir_ = p.parent_path().string();
    if (input_dir_.empty()) input_dir_ = ".";

    std::string filename = p.filename().string();
    if (filename.size() > 4 && filename.substr(filename.size() - 4) == ".ply") {
        base_name_ = filename.substr(0, filename.size() - 4);
    } else {
        throw std::runtime_error("Input file must have .ply extension");
    }

    std::cout << "Input: " << input_path_ << "\n";
}

void ConvertApp::findPlyFiles() {
    // LOD0 is the base file
    lod_files_.push_back(input_path_);

    // Find numbered LOD files: base_1.ply, base_2.ply, ...
    std::regex pattern(base_name_ + "_(\\d+)\\.ply");
    std::vector<std::pair<int, std::string>> numbered_files;

    for (const auto& entry : fs::directory_iterator(input_dir_)) {
        std::string filename = entry.path().filename().string();
        std::smatch match;
        if (std::regex_match(filename, match, pattern)) {
            int num = std::stoi(match[1].str());
            numbered_files.emplace_back(num, entry.path().string());
        }
    }

    // Sort by number
    std::sort(numbered_files.begin(), numbered_files.end());

    // Add files until first gap (must be continuous from 1)
    int expected = 1;
    for (const auto& [num, path] : numbered_files) {
        if (num != expected) break;
        lod_files_.push_back(path);
        expected++;
    }

    // Print LOD info
    std::cout << "Found " << lod_files_.size() << " LOD level"
              << (lod_files_.size() > 1 ? "s" : "") << ":\n";

    for (size_t i = 0; i < lod_files_.size(); ++i) {
        std::string filename = fs::path(lod_files_[i]).filename().string();
        if (single_lod_ && i > 0) {
            std::cout << "  LOD" << i << ": " << filename << " (skipped: --single-lod)\n";
        } else {
            std::cout << "  LOD" << i << ": " << filename << "\n";
        }
    }

    // Apply single_lod filter
    if (single_lod_ && lod_files_.size() > 1) {
        lod_files_.resize(1);
    }

    // Check for environment.ply
    env_file_ = input_dir_ + "/environment.ply";
    if (fs::exists(env_file_)) {
        has_env_ = true;
        std::cout << "Found environment.ply\n";
    } else {
        std::cerr << "Warning: environment.ply not found\n";
        env_file_.clear();
    }
}

void ConvertApp::validateOutput() {
    // Create output directory if needed
    fs::create_directories(output_dir_);

    // Warn if not empty
    if (!fs::is_empty(output_dir_)) {
        std::cerr << "Warning: Output directory is not empty: " << output_dir_ << "\n";
    }

    std::cout << "Output: " << output_dir_ << "\n";
    std::cout << "Cell size: " << cell_size_x_ << " x " << cell_size_y_ << "\n";
}

void ConvertApp::computeBounds() {
    std::cout << "\nPhase 1: Computing bounds...\n";

    all_splats_.resize(lod_files_.size());

    for (size_t lod = 0; lod < lod_files_.size(); ++lod) {
        PLYHeader header;
        std::cout << "  Reading LOD" << lod << ": " << fs::path(lod_files_[lod]).filename().string() << "\n";

        if (!PLYReader::read_splats(lod_files_[lod], all_splats_[lod], header)) {
            throw std::runtime_error("Failed to read " + lod_files_[lod]);
        }

        std::cout << "    " << all_splats_[lod].size() << " splats\n";
        splats_per_lod_.push_back(all_splats_[lod].size());
        global_bbox_.expand(header.bbox);

        if (lod == 0) {
            has_sh_ = header.has_sh;
            sh_degree_ = header.sh_degree;
            num_f_rest_ = header.num_f_rest;
        }

        // Compute attribute ranges
        int bands_per_channel = (has_sh_ && num_f_rest_ > 0) ? num_f_rest_ / 3 : 0;

        for (const auto& s : all_splats_[lod]) {
            Vec3f linear_scale(std::exp(s.scale.x), std::exp(s.scale.y), std::exp(s.scale.z));
            global_ranges_.expand_scale(linear_scale);
            global_ranges_.expand_opacity(sigmoid(s.opacity));

            if (has_sh_ && bands_per_channel > 0) {
                for (int band = 0; band < bands_per_channel; ++band) {
                    float r = s.f_rest[band];
                    float g = s.f_rest[band + bands_per_channel];
                    float b = s.f_rest[band + 2 * bands_per_channel];
                    global_ranges_.expand_sh(r, g, b);
                }
            }
        }
    }

    // Read environment if exists
    if (has_env_) {
        if (!EnvWriter::read_environment(env_file_, env_splats_, env_bounds_)) {
            std::cerr << "Warning: Failed to read environment.ply\n";
            has_env_ = false;
        } else {
            std::cout << "  Environment: " << env_splats_.size() << " splats\n";
        }
    }

    std::cout << "Global bbox: (" << global_bbox_.min.x << ", " << global_bbox_.min.y << ", " << global_bbox_.min.z
              << ") - (" << global_bbox_.max.x << ", " << global_bbox_.max.y << ", " << global_bbox_.max.z << ")\n";
    std::cout << "SH: " << (has_sh_ ? "degree " + std::to_string(sh_degree_) + " (" + std::to_string(num_f_rest_) + " coefficients)" : "none") << "\n";
}

void ConvertApp::buildSpatialGrid() {
    // This is handled in writeLccData for simplicity
}

void ConvertApp::writeLccData() {
    std::cout << "\nPhase 2: Building spatial grid...\n";

    SpatialGrid grid(cell_size_x_, cell_size_y_, global_bbox_, lod_files_.size());

    for (size_t lod = 0; lod < lod_files_.size(); ++lod) {
        for (size_t i = 0; i < all_splats_[lod].size(); ++i) {
            grid.add_splat(lod, all_splats_[lod][i].pos, i);
        }
    }

    std::cout << "Created " << grid.get_cells().size() << " grid cells\n";

    std::cout << "\nPhase 3: Writing LCC data...\n";

    LCCWriter writer(output_dir_, global_ranges_, lod_files_.size(), has_sh_);

    for (const auto& [cell_index, cell] : grid.get_cells()) {
        for (size_t lod = 0; lod < lod_files_.size(); ++lod) {
            if (cell.splat_indices[lod].empty()) continue;

            std::vector<Splat> cell_splats;
            cell_splats.reserve(cell.splat_indices[lod].size());
            for (size_t idx : cell.splat_indices[lod]) {
                cell_splats.push_back(all_splats_[lod][idx]);
            }

            writer.write_splats(cell_index, lod, cell_splats);
        }
    }

    writer.finalize();
    total_splats_ = writer.total_splats();

    // Write index
    std::cout << "\nPhase 4: Writing index.bin...\n";
    grid.write_index_bin(output_dir_ + "/index.bin", writer.get_units(), lod_files_.size());
}

void ConvertApp::writeEnvironment() {
    if (has_env_ && !env_splats_.empty()) {
        std::cout << "\nWriting environment.bin...\n";
        if (!EnvWriter::write_environment_bin(output_dir_ + "/environment.bin", env_splats_, env_bounds_, has_sh_)) {
            std::cerr << "Warning: Failed to write environment.bin\n";
        }
    }
}

void ConvertApp::writeIndex() {
    // Already done in writeLccData
}

void ConvertApp::writeMeta() {
    std::cout << "\nPhase 5: Writing meta.lcc...\n";

    MetaInfo meta;
    meta.guid = MetaWriter::generate_guid();
    meta.total_splats = total_splats_;
    meta.total_levels = lod_files_.size();
    meta.cell_length_x = cell_size_x_;
    meta.cell_length_y = cell_size_y_;
    meta.index_data_size = 4 + 16 * lod_files_.size();
    meta.splats_per_lod = splats_per_lod_;
    meta.bounding_box = global_bbox_;
    meta.file_type = has_sh_ ? "Quality" : "Portable";
    meta.attr_ranges = global_ranges_;

    meta.has_environment = has_env_ && !env_splats_.empty();
    if (meta.has_environment) {
        meta.env_bounds = env_bounds_;
    }

    MetaWriter::write(output_dir_ + "/meta.lcc", meta);
}

void ConvertApp::writeAttrs() {
    std::cout << "\nPhase 6: Writing attrs.lcp...\n";
    AttrsWriter::write(output_dir_ + "/attrs.lcp");
}

} // namespace ply2lcc
