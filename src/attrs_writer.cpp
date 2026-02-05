#include "attrs_writer.hpp"
#include <fstream>

namespace ply2lcc {

bool AttrsWriter::write(const std::string& path) {
    std::ofstream file(path);
    if (!file) {
        return false;
    }

    // Hardcoded attrs.lcp with spawnPoint and transform only
    file << "{";
    file << "\"spawnPoint\":{";
    file << "\"position\":[0,0,0],";
    file << "\"rotation\":[0.7071068,0,0,0.7071068]";  // euler(90,0,0)
    file << "},";
    file << "\"transform\":{";
    file << "\"position\":[0,0,0],";
    file << "\"rotation\":[0,0,0,1],";
    file << "\"scale\":[1,1,1]";
    file << "}";
    file << "}\n";

    return true;
}

} // namespace ply2lcc
