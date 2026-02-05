#ifndef PLY2LCC_CONVERT_APP_HPP
#define PLY2LCC_CONVERT_APP_HPP

#include "types.hpp"
#include <string>
#include <vector>

namespace ply2lcc {

class ConvertApp {
public:
    ConvertApp(int argc, char** argv);
    void run();

private:
    void parseArgs();
    void findPlyFiles();
    void validateOutput();
    void computeBounds();
    void buildSpatialGrid();
    void writeLccData();
    void writeEnvironment();
    void writeIndex();
    void writeMeta();
    void writeAttrs();
    void printUsage();

    int argc_;
    char** argv_;

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
    std::string env_file_;
    bool has_env_ = false;

    // Conversion data
    std::vector<std::vector<Splat>> all_splats_;
    std::vector<size_t> splats_per_lod_;
    BBox global_bbox_;
    AttributeRanges global_ranges_;
    EnvBounds env_bounds_;
    std::vector<Splat> env_splats_;
    bool has_sh_ = false;
    int sh_degree_ = 0;
    int num_f_rest_ = 0;
    size_t total_splats_ = 0;
};

} // namespace ply2lcc

#endif // PLY2LCC_CONVERT_APP_HPP
