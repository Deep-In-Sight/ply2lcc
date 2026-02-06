#include "convert_app.hpp"
#include "splat_buffer.hpp"
#include "meta_writer.hpp"
#include "env_writer.hpp"
#include "attrs_writer.hpp"
#include "compression.hpp"

#include <iostream>
#include <filesystem>
#include <algorithm>
#include <regex>
#include <cmath>
#include <stdexcept>
#include <fstream>
#include <omp.h>

namespace fs = std::filesystem;

namespace ply2lcc {

ConvertApp::ConvertApp(int argc, char** argv)
    : argc_(argc), argv_(argv) {}

ConvertApp::ConvertApp(const ConvertConfig& config)
    : argc_(0), argv_(nullptr)
    , input_path_(config.input_path)
    , output_dir_(config.output_dir)
    , cell_size_x_(config.cell_size_x)
    , cell_size_y_(config.cell_size_y)
    , single_lod_(config.single_lod)
    , include_env_(config.include_env)
    , include_collision_(config.include_collision)
    , env_file_(config.env_path)
    , collision_file_(config.collision_path)
{
    // Derive input_dir_ and base_name_ from input_path_
    fs::path p(input_path_);
    if (fs::is_directory(p)) {
        input_dir_ = input_path_;
        base_name_ = "point_cloud";
    } else {
        input_dir_ = p.parent_path().string();
        base_name_ = p.stem().string();
    }
}

void ConvertApp::setProgressCallback(ProgressCallback cb) {
    progress_cb_ = std::move(cb);
}

void ConvertApp::setLogCallback(LogCallback cb) {
    log_cb_ = std::move(cb);
}

void ConvertApp::reportProgress(int percent, const std::string& msg) {
    if (progress_cb_) {
        progress_cb_(percent, msg);
    }
}

void ConvertApp::log(const std::string& msg) {
    if (log_cb_) {
        log_cb_(msg);
    } else {
        std::cout << msg;
    }
}

void ConvertApp::run() {
    reportProgress(0, "Starting conversion...");

    parseArgs();
    findPlyFiles();

    reportProgress(2, "Found " + std::to_string(lod_files_.size()) + " LOD files");

    validateOutput();

    reportProgress(5, "Building spatial grid...");
    buildSpatialGridParallel();

    reportProgress(15, "Encoding splats...");
    encodeAllLods();

    reportProgress(90, "Writing output files...");
    writeEncodedData();
    writeEnvironment();
    writeIndex();
    writeMeta();
    writeAttrs();

    reportProgress(100, "Conversion complete!");

    log("\nConversion complete!\n");
    log("Total splats: " + std::to_string(total_splats_) + "\n");
    log("Output: " + output_dir_ + "\n");
}

void ConvertApp::printUsage() {
    std::cerr << "Usage: " << argv_[0] << " -i <input.ply> -o <output_dir> [options]\n"
              << "\n"
              << "Options:\n"
              << "  -e <path>          Path to environment.ply (default: auto-detect in input dir)\n"
              << "  -m <path>          Path to collision.ply (default: auto-detect in input dir)\n"
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
        } else if (arg == "-e" && i + 1 < argc_) {
            env_file_ = argv_[++i];
            include_env_ = true;
        } else if (arg == "-m" && i + 1 < argc_) {
            collision_file_ = argv_[++i];
            include_collision_ = true;
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

    log("Input: " + input_path_ + "\n");
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
    log("Found " + std::to_string(lod_files_.size()) + " LOD level" +
        (lod_files_.size() > 1 ? "s" : "") + ":\n");

    for (size_t i = 0; i < lod_files_.size(); ++i) {
        std::string filename = fs::path(lod_files_[i]).filename().string();
        if (single_lod_ && i > 0) {
            log("  LOD" + std::to_string(i) + ": " + filename + " (skipped: --single-lod)\n");
        } else {
            log("  LOD" + std::to_string(i) + ": " + filename + "\n");
        }
    }

    // Apply single_lod filter
    if (single_lod_ && lod_files_.size() > 1) {
        lod_files_.resize(1);
    }

    // Check for environment.ply
    if (include_env_) {
        if (env_file_.empty()) {
            // Auto-detect in input directory
            env_file_ = input_dir_ + "/environment.ply";
        }
        if (fs::exists(env_file_)) {
            has_env_ = true;
            log("Environment: " + env_file_ + "\n");
        } else {
            log("Warning: environment.ply not found at " + env_file_ + "\n");
            env_file_.clear();
            has_env_ = false;
        }
    }

    // Check for collision.ply
    if (include_collision_) {
        if (collision_file_.empty()) {
            // Auto-detect in input directory
            collision_file_ = input_dir_ + "/collision.ply";
        }
        if (fs::exists(collision_file_)) {
            has_collision_ = true;
            log("Collision: " + collision_file_ + "\n");
        } else {
            log("Warning: collision.ply not found at " + collision_file_ + "\n");
            collision_file_.clear();
            has_collision_ = false;
        }
    }
}

void ConvertApp::validateOutput() {
    // Create output directory if needed
    fs::create_directories(output_dir_);

    // Warn if not empty
    if (!fs::is_empty(output_dir_)) {
        log("Warning: Output directory is not empty: " + output_dir_ + "\n");
    }

    log("Output: " + output_dir_ + "\n");
    log("Cell size: " + std::to_string(cell_size_x_) + " x " + std::to_string(cell_size_y_) + "\n");
}

void ConvertApp::buildSpatialGridParallel() {
    int n_threads = omp_get_max_threads();
    log("\nPhase 1: Building spatial grid (parallel, " + std::to_string(n_threads) + " threads)...\n");

    // First pass: compute global bbox (needed for grid cell calculation)
    for (size_t lod = 0; lod < lod_files_.size(); ++lod) {
        SplatBuffer buffer;
        if (!buffer.initialize(lod_files_[lod])) {
            throw std::runtime_error("Failed to read " + lod_files_[lod] + ": " + buffer.error());
        }
        global_bbox_.expand(buffer.compute_bbox());

        if (lod == 0) {
            has_sh_ = buffer.num_f_rest() > 0;
            sh_degree_ = buffer.sh_degree();
            num_f_rest_ = buffer.num_f_rest();
        }
    }

    log("Global bbox: (" + std::to_string(global_bbox_.min.x) + ", " + std::to_string(global_bbox_.min.y) + ", " + std::to_string(global_bbox_.min.z) +
        ") - (" + std::to_string(global_bbox_.max.x) + ", " + std::to_string(global_bbox_.max.y) + ", " + std::to_string(global_bbox_.max.z) + ")\n");

    // Create grid with known bbox
    grid_ = std::make_unique<SpatialGrid>(cell_size_x_, cell_size_y_, global_bbox_, lod_files_.size());

    // Second pass: parallel grid building per LOD
    for (size_t lod = 0; lod < lod_files_.size(); ++lod) {
        log("  Processing LOD" + std::to_string(lod) + ": " + fs::path(lod_files_[lod]).filename().string() + "\n");

        SplatBuffer splats;
        if (!splats.initialize(lod_files_[lod])) {
            throw std::runtime_error("Failed to read " + lod_files_[lod] + ": " + splats.error());
        }

        log("    " + std::to_string(splats.size()) + " splats\n");
        splats_per_lod_.push_back(splats.size());

        int n_threads = omp_get_max_threads();
        std::vector<ThreadLocalGrid> local_grids(n_threads);
        int bands_per_channel = (has_sh_ && num_f_rest_ > 0) ? num_f_rest_ / 3 : 0;

        #pragma omp parallel
        {
            int tid = omp_get_thread_num();
            const auto splat_count = static_cast<ptrdiff_t>(splats.size());

            #pragma omp for schedule(static)
            for (ptrdiff_t i = 0; i < splat_count; ++i) {
                SplatView sv = splats[static_cast<size_t>(i)];
                uint32_t cell_id = grid_->compute_cell_index(sv.pos());

                local_grids[tid].cell_indices[cell_id].push_back(static_cast<size_t>(i));

                // Expand ranges
                Vec3f linear_scale(std::exp(sv.scale().x), std::exp(sv.scale().y), std::exp(sv.scale().z));
                local_grids[tid].ranges.expand_scale(linear_scale);
                local_grids[tid].ranges.expand_opacity(sigmoid(sv.opacity()));

                if (bands_per_channel > 0) {
                    for (int band = 0; band < bands_per_channel; ++band) {
                        local_grids[tid].ranges.expand_sh(
                            sv.f_rest(band),
                            sv.f_rest(band + bands_per_channel),
                            sv.f_rest(band + 2 * bands_per_channel));
                    }
                }
            }
        }

        // Sequential merge
        for (int t = 0; t < n_threads; ++t) {
            grid_->merge(local_grids[t], lod);
            global_ranges_.merge(local_grids[t].ranges);
        }
    }

    log("Created " + std::to_string(grid_->get_cells().size()) + " grid cells\n");
    log("SH: " + (has_sh_ ? "degree " + std::to_string(sh_degree_) + " (" + std::to_string(num_f_rest_) + " coefficients)" : std::string("none")) + "\n");

    // Read environment if exists
    if (has_env_) {
        if (!EnvWriter::read_environment(env_file_, env_splats_, env_bounds_)) {
            log("Warning: Failed to read environment.ply\n");
            has_env_ = false;
        } else {
            log("  Environment: " + std::to_string(env_splats_.size()) + " splats\n");
        }
    }
}

void ConvertApp::encodeAllLods() {
    log("\nPhase 2: Encoding splats (parallel)...\n");

    // Prepare cells vector for parallel iteration
    const auto& cells_map = grid_->get_cells();
    std::vector<std::pair<uint32_t, const GridCell*>> cells_vec;
    cells_vec.reserve(cells_map.size());
    for (const auto& [idx, cell] : cells_map) {
        cells_vec.emplace_back(idx, &cell);
        encoded_cells_[idx].resize(lod_files_.size());
    }

    // Track progress across all LODs and cells
    size_t total_cells = cells_vec.size() * lod_files_.size();
    size_t processed = 0;

    for (size_t lod = 0; lod < lod_files_.size(); ++lod) {
        log("  Encoding LOD" + std::to_string(lod) + "...\n");

        // Reopen SplatBuffer for this LOD
        SplatBuffer splats;
        if (!splats.initialize(lod_files_[lod])) {
            throw std::runtime_error("Failed to reopen " + lod_files_[lod]);
        }

        // Calculate reporting frequency (every 1% progress or at least every cell if < 100 cells)
        size_t report_interval = (std::max)(size_t(1), total_cells / 100);
        const auto cells_count = static_cast<ptrdiff_t>(cells_vec.size());

        #pragma omp parallel for schedule(dynamic)
        for (ptrdiff_t i = 0; i < cells_count; ++i) {
            uint32_t cell_idx = cells_vec[static_cast<size_t>(i)].first;
            const GridCell* cell = cells_vec[static_cast<size_t>(i)].second;

            // Skip empty cells - no critical section needed
            if (cell->splat_indices[lod].empty()) {
                continue;
            }

            EncodedCell enc;
            enc.data.reserve(cell->splat_indices[lod].size() * 32);
            if (has_sh_) {
                enc.shcoef.reserve(cell->splat_indices[lod].size() * 64);
            }

            for (size_t idx : cell->splat_indices[lod]) {
                SplatView sv = splats[idx];
                encode_splat_view(sv, enc.data, enc.shcoef, global_ranges_, has_sh_);
            }
            enc.count = cell->splat_indices[lod].size();

            #pragma omp critical
            {
                encoded_cells_[cell_idx][lod] = std::move(enc);
                processed++;
                // Only report progress at intervals to reduce lock contention
                if (processed % report_interval == 0) {
                    int percent = 15 + static_cast<int>(processed * 75 / total_cells);
                    reportProgress(percent, "Encoding cell " + std::to_string(processed) + "/" + std::to_string(total_cells));
                }
            }
        }
    }

    log("  Encoding complete.\n");
}

void ConvertApp::writeEncodedData() {
    log("\nPhase 4: Writing LCC data...\n");

    fs::create_directories(output_dir_);

    std::ofstream data_file(output_dir_ + "/data.bin", std::ios::binary);
    if (!data_file) {
        throw std::runtime_error("Failed to create data.bin");
    }

    std::ofstream sh_file;
    if (has_sh_) {
        sh_file.open(output_dir_ + "/shcoef.bin", std::ios::binary);
        if (!sh_file) {
            throw std::runtime_error("Failed to create shcoef.bin");
        }
    }

    uint64_t data_offset = 0;
    uint64_t sh_offset = 0;

    // Iterate over cells in sorted order (required for index.bin)
    for (auto& [cell_idx, lod_data] : encoded_cells_) {
        LCCUnitInfo unit;
        unit.index = cell_idx;
        unit.lods.resize(lod_files_.size());

        for (size_t lod = 0; lod < lod_data.size(); ++lod) {
            auto& enc = lod_data[lod];

            if (enc.count == 0) continue;

            LCCNodeInfo& node = unit.lods[lod];
            node.splat_count = static_cast<uint32_t>(enc.count);
            node.data_offset = data_offset;
            node.data_size = static_cast<uint32_t>(enc.data.size());

            data_file.write(reinterpret_cast<char*>(enc.data.data()), enc.data.size());
            data_offset += enc.data.size();

            if (has_sh_) {
                node.sh_offset = sh_offset;
                node.sh_size = static_cast<uint32_t>(enc.shcoef.size());
                sh_file.write(reinterpret_cast<char*>(enc.shcoef.data()), enc.shcoef.size());
                sh_offset += enc.shcoef.size();
            }

            total_splats_ += enc.count;

            // Clear encoded data to free memory
            enc.data.clear();
            enc.data.shrink_to_fit();
            enc.shcoef.clear();
            enc.shcoef.shrink_to_fit();
        }

        units_.push_back(std::move(unit));
    }

    data_file.close();
    if (has_sh_) {
        sh_file.close();
    }

    log("  Written " + std::to_string(total_splats_) + " splats.\n");
}

void ConvertApp::writeEnvironment() {
    if (has_env_ && !env_splats_.empty()) {
        log("\nWriting environment.bin...\n");
        if (!EnvWriter::write_environment_bin(output_dir_ + "/environment.bin", env_splats_, env_bounds_, has_sh_)) {
            log("Warning: Failed to write environment.bin\n");
        }
    }
}

void ConvertApp::writeIndex() {
    log("\nPhase 5: Writing index.bin...\n");
    grid_->write_index_bin(output_dir_ + "/index.bin", units_, lod_files_.size());
}

void ConvertApp::writeMeta() {
    log("\nPhase 6: Writing meta.lcc...\n");

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
    log("\nPhase 7: Writing attrs.lcp...\n");
    AttrsWriter::write(output_dir_ + "/attrs.lcp");
}

} // namespace ply2lcc
