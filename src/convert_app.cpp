#include "convert_app.hpp"
#include "spatial_grid.hpp"
#include "grid_encoder.hpp"
#include "lcc_writer.hpp"
#include "collision_encoder.hpp"

#include <iostream>
#include <filesystem>
#include <algorithm>
#include <regex>
#include <stdexcept>

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

    // Create output directory
    fs::create_directories(output_dir_);
    log("Output: " + output_dir_ + "\n");
    log("Cell size: " + std::to_string(cell_size_x_) + " x " + std::to_string(cell_size_y_) + "\n");

    // Step 1: Build spatial grid
    reportProgress(5, "Building spatial grid...");
    log("\nPhase 1: Building spatial grid...\n");
    SpatialGrid grid = SpatialGrid::from_files(lod_files_, cell_size_x_, cell_size_y_);

    log("Global bbox: (" + std::to_string(grid.bbox().min.x) + ", " +
        std::to_string(grid.bbox().min.y) + ", " + std::to_string(grid.bbox().min.z) +
        ") - (" + std::to_string(grid.bbox().max.x) + ", " +
        std::to_string(grid.bbox().max.y) + ", " + std::to_string(grid.bbox().max.z) + ")\n");
    log("Created " + std::to_string(grid.cells().size()) + " grid cells\n");
    log("SH: " + (grid.has_sh() ? "degree " + std::to_string(grid.sh_degree()) +
        " (" + std::to_string(grid.num_f_rest()) + " coefficients)" : std::string("none")) + "\n");

    // Step 2: Encode all data
    reportProgress(15, "Encoding splats...");
    log("\nPhase 2: Encoding splats...\n");
    GridEncoder encoder;
    encoder.set_progress_callback([this](int pct, const std::string& msg) {
        reportProgress(15 + pct * 75 / 100, msg);
    });
    LccData data = encoder.encode(grid, lod_files_);

    // Step 3: Encode environment (if exists)
    if (!env_file_.empty() && fs::exists(env_file_)) {
        log("\nPhase 3: Encoding environment...\n");
        data.environment = encoder.encode_environment(env_file_, grid.has_sh());
        log("  Environment: " + std::to_string(data.environment.count) + " splats\n");
    }

    // Step 4: Encode collision mesh (if exists)
    if (!collision_file_.empty() && fs::exists(collision_file_)) {
        reportProgress(85, "Encoding collision mesh...");
        log("\nPhase 4: Encoding collision mesh...\n");
        CollisionEncoder collision_encoder;
        collision_encoder.set_log_callback([this](const std::string& msg) { log(msg); });
        data.collision = collision_encoder.encode(collision_file_, cell_size_x_, cell_size_y_);
        if (!data.collision.empty()) {
            log("  Collision: " + std::to_string(data.collision.total_triangles()) + " triangles, " +
                std::to_string(data.collision.cells.size()) + " cells\n");
        }
    }

    // Step 5: Write all output files
    reportProgress(90, "Writing output files...");
    log("\nPhase 5: Writing LCC data...\n");
    LccWriter writer(output_dir_);
    writer.write(data);

    reportProgress(100, "Conversion complete!");

    log("\nConversion complete!\n");
    log("Total splats: " + std::to_string(data.total_splats) + "\n");
    log("Output: " + output_dir_ + "\n");
}

void ConvertApp::printUsage() {
    std::cerr << "Usage: " << argv_[0] << " -i <input.ply> -o <output_dir> [options]\n"
              << "\n"
              << "Options:\n"
              << "  -e <path>          Path to environment.ply (default: auto-detect in input dir)\n"
              << "  -m <path>          Path to collision mesh (.ply or .obj, default: auto-detect)\n"
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
            env_file_ = input_dir_ + "/environment.ply";
        }
        if (fs::exists(env_file_)) {
            log("Environment: " + env_file_ + "\n");
        } else {
            log("Warning: environment.ply not found at " + env_file_ + "\n");
            env_file_.clear();
        }
    }

    // Check for collision mesh (supports .ply and .obj)
    if (include_collision_) {
        if (collision_file_.empty()) {
            // Try collision.ply first, then collision.obj
            std::string ply_path = input_dir_ + "/collision.ply";
            std::string obj_path = input_dir_ + "/collision.obj";
            if (fs::exists(ply_path)) {
                collision_file_ = ply_path;
            } else if (fs::exists(obj_path)) {
                collision_file_ = obj_path;
            }
        }
        if (!collision_file_.empty() && fs::exists(collision_file_)) {
            log("Collision: " + collision_file_ + "\n");
        } else {
            log("Warning: collision mesh not found (tried collision.ply, collision.obj)\n");
            collision_file_.clear();
        }
    }
}

} // namespace ply2lcc
