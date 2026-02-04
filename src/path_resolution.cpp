#include "path_resolution.hpp"
#include <filesystem>
#include <regex>
#include <vector>
#include <algorithm>

namespace fs = std::filesystem;

namespace ply2lcc {

PathResult resolve_input_path(const std::string& input_dir) {
    // Check point_cloud directory exists
    std::string point_cloud_dir = input_dir + "/point_cloud";
    if (!fs::exists(point_cloud_dir) || !fs::is_directory(point_cloud_dir)) {
        return PathError{"point_cloud/ directory not found in " + input_dir};
    }

    // Find all iteration_* directories
    std::regex iter_pattern("iteration_(\\d+)");
    std::vector<std::pair<int, std::string>> iterations;

    for (const auto& entry : fs::directory_iterator(point_cloud_dir)) {
        if (!entry.is_directory()) continue;

        std::string dirname = entry.path().filename().string();
        std::smatch match;
        if (std::regex_match(dirname, match, iter_pattern)) {
            int num = std::stoi(match[1].str());
            iterations.emplace_back(num, entry.path().string());
        }
    }

    if (iterations.empty()) {
        return PathError{"No iteration_* directories found in " + point_cloud_dir};
    }

    // Sort by iteration number descending and find highest with PLY files
    std::sort(iterations.begin(), iterations.end(),
              [](const auto& a, const auto& b) { return a.first > b.first; });

    for (const auto& [iter_num, iter_path] : iterations) {
        // Check for point_cloud*.ply files
        bool has_ply = false;
        for (const auto& entry : fs::directory_iterator(iter_path)) {
            std::string filename = entry.path().filename().string();
            if (entry.path().extension() == ".ply" &&
                filename.find("point_cloud") == 0) {
                has_ply = true;
                break;
            }
        }

        if (has_ply) {
            return ResolvedPath{iter_path, iter_num};
        }
    }

    return PathError{"No point_cloud*.ply files found in any iteration directory"};
}

std::string resolve_output_path(const std::string& output_dir) {
    std::string lcc_results = output_dir + "/LCC_Results";
    fs::create_directories(lcc_results);
    return lcc_results;
}

}  // namespace ply2lcc
