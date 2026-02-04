#ifndef PLY2LCC_PLY_READER_HPP
#define PLY2LCC_PLY_READER_HPP

#include "types.hpp"
#include <string>
#include <vector>
#include <functional>

namespace ply2lcc {

struct PLYHeader {
    size_t vertex_count = 0;
    BBox bbox;
    Vec3f offset{0, 0, 0};
    Vec3f shift{0, 0, 0};
    Vec3f scale_transform{1, 1, 1};
    std::string source;
    int epsg = 0;
    bool has_sh = true;  // Has f_rest properties
};

class PLYReader {
public:
    // Read header only (for counting/bbox)
    static bool read_header(const std::string& path, PLYHeader& header);

    // Read all splats from a PLY file
    static bool read_splats(const std::string& path,
                           std::vector<Splat>& splats,
                           PLYHeader& header);

    // Stream splats with callback (memory efficient for large files)
    using SplatCallback = std::function<void(const Splat&, size_t idx)>;
    static bool stream_splats(const std::string& path,
                             const SplatCallback& callback,
                             PLYHeader& header);
};

} // namespace ply2lcc

#endif // PLY2LCC_PLY_READER_HPP
