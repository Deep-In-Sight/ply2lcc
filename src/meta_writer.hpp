#ifndef PLY2LCC_META_WRITER_HPP
#define PLY2LCC_META_WRITER_HPP

#include "types.hpp"
#include <string>
#include <vector>

namespace ply2lcc {

struct MetaInfo {
    std::string version = "5.0";
    std::string guid;
    std::string name = "XGrids Splats";
    std::string description = "Converted from PLY";
    std::string source = "ply";
    std::string dataType = "DIMENVUE";

    size_t total_splats = 0;
    size_t total_levels = 1;
    float cell_length_x = 30.0f;
    float cell_length_y = 30.0f;
    size_t index_data_size = 0;  // Calculated: 4 + 16 * total_levels

    Vec3f offset{0, 0, 0};
    int epsg = 0;
    Vec3f shift{0, 0, 0};
    Vec3f scale_transform{1, 1, 1};

    std::vector<size_t> splats_per_lod;
    BBox bounding_box;

    std::string encoding = "COMPRESS";
    std::string file_type = "Quality";  // or "Portable"

    AttributeRanges attr_ranges;
};

class MetaWriter {
public:
    static bool write(const std::string& path, const MetaInfo& meta);
    static std::string generate_guid();
};

} // namespace ply2lcc

#endif // PLY2LCC_META_WRITER_HPP
