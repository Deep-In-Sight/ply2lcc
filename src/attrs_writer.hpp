#ifndef PLY2LCC_ATTRS_WRITER_HPP
#define PLY2LCC_ATTRS_WRITER_HPP

#include <string>

namespace ply2lcc {

class AttrsWriter {
public:
    // Write attrs.lcp file with hardcoded spawnPoint and transform
    static bool write(const std::string& path);
};

} // namespace ply2lcc

#endif // PLY2LCC_ATTRS_WRITER_HPP
