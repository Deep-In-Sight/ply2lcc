#ifndef PLY2LCC_CONVERT_APP_HPP
#define PLY2LCC_CONVERT_APP_HPP

#include "types.hpp"
#include <string>
#include <vector>

namespace ply2lcc {

class ConvertApp {
public:
    ConvertApp(int argc, char** argv);
    ConvertApp(const ConvertConfig& config);  // Constructor for GUI
    void setProgressCallback(ProgressCallback cb);
    void setLogCallback(LogCallback cb);
    void run();

private:
    void reportProgress(int percent, const std::string& msg);
    void log(const std::string& msg);
    void parseArgs();
    void findPlyFiles();
    void printUsage();

    int argc_;
    char** argv_;
    ProgressCallback progress_cb_;
    LogCallback log_cb_;

    // Config
    std::string input_path_;
    std::string input_dir_;
    std::string base_name_;
    std::string output_dir_;
    float cell_size_x_ = 30.0f;
    float cell_size_y_ = 30.0f;
    bool single_lod_ = false;

    // Discovered files
    std::vector<std::string> lod_files_;
    bool include_env_ = false;
    bool include_collision_ = false;
    std::string env_file_;
    std::string collision_file_;
};

} // namespace ply2lcc

#endif // PLY2LCC_CONVERT_APP_HPP
