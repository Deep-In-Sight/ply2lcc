#ifndef ENV_WRITER_HPP
#define ENV_WRITER_HPP

#include <string>
#include <vector>
#include "types.hpp"

namespace ply2lcc {

class EnvWriter {
public:
    // Read environment.ply and compute bounds
    static bool read_environment(const std::string& env_ply_path,
                                 std::vector<Splat>& splats,
                                 EnvBounds& bounds);

    // Write Environment.bin (combined data + SH, 96 bytes per splat)
    static bool write_environment_bin(const std::string& output_path,
                                      const std::vector<Splat>& splats,
                                      const EnvBounds& bounds);
};

}  // namespace ply2lcc

#endif  // ENV_WRITER_HPP
